#include "sgre/render/bitmap_font_record_receiver.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/frame_host_pool.hpp"
#include "sgre/render/graphics_frame_cycle.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/scene/bitmap_console.hpp"
#include "sgre/scene/indicator_placement.hpp"

#include <AppKit/AppKit.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool value, std::string_view message) {
	if (!value) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireError(const std::function<void()>& operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());
		return;
	}

	throw std::runtime_error("invalid bitmap glyph owner operation was accepted");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static std::shared_ptr<sgre::resource::PsbDocument> FontDocument() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	PsbArray sources;
	for (std::uint32_t index = 0; index < 2; ++index) {
		sgre::resource::PsbChunkReference chunk;
		chunk.Index = index;
		auto source = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{1})}, {"height", Atom(std::int64_t{1})}, {"truncated_width", Atom(std::int64_t{1})}, {"truncated_height", Atom(std::int64_t{1})}, {"pixel", Atom(chunk)}});
		source.SerializedOffset = 64 + index * 32;
		sources.push_back(std::move(source));
	}
	const auto glyph = [](std::int64_t source) {
		return Atom(PsbObject{{"id", Atom(source)}, {"x", Atom(std::int64_t{0})}, {"y", Atom(std::int64_t{0})}, {"width", Atom(std::int64_t{2})}, {"height", Atom(std::int64_t{2})}, {"w", Atom(std::int64_t{1})}, {"h", Atom(std::int64_t{1})}, {"a", Atom(std::int64_t{0})}, {"b", Atom(std::int64_t{0})}, {"d", Atom(std::int64_t{0})}});
	};
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 130;
	document->Chunks = {{128, 1}, {129, 1}};
	document->Root = Atom(PsbObject{{"id", Atom(std::string("font"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(1.0800000429153442)}, {"label", Atom(std::string("Synthetic actual glyph atlas"))}, {"minWidth", Atom(std::int64_t{2})}, {"minHeight", Atom(std::int64_t{2})}, {"maxWidth", Atom(std::int64_t{2})}, {"maxHeight", Atom(std::int64_t{2})}, {"source", Atom(std::move(sources))}, {"code", Atom(PsbObject{{"A", glyph(0)}, {"B", glyph(1)}})}});
	document->Root.SerializedOffset = document->Header.RootOffset;

	return document;
}

static std::shared_ptr<std::vector<std::uint8_t>> FontBacking() {
	auto bytes = std::make_shared<std::vector<std::uint8_t>>(130);
	bytes->at(128) = 255;
	bytes->at(129) = 128;

	return bytes;
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 16;
	result.Height = 8;
	result.MaxVertices = 64;
	result.MaxIndices = 96;
	result.MaxDraws = 8;
	result.MaxTextures = 16;
	result.MaxPipelines = 16;
	result.MaxTextureBytes = 64 * 1024;

	return result;
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::size_t x, std::size_t y, const std::array<int, 4>& expected) {
	Require(pixels.Width == 16 && pixels.Height == 8 && pixels.Rgba.size() == 512, "actual bitmap glyph target/capture dimensions differ");
	for (std::size_t channel = 0; channel < 4; ++channel) {
		const auto actual = static_cast<int>(pixels.Rgba[(y * 16 + x) * 4 + channel]);
		if (actual < expected[channel] - 1 || actual > expected[channel] + 1) {
			throw std::runtime_error("actual bitmap glyph pixel differs at " + std::to_string(x) + "," + std::to_string(y) + " channel " + std::to_string(channel) + " expected " + std::to_string(expected[channel]) + " actual " + std::to_string(actual));
		}
	}
}

struct GlyphFrameCleanup {
	sgre::render::SpriteRenderRecords& Records;
	sgre::render::BitmapFontRecordReceiver& Receiver;
	sgre::render::FrameHostPool& Pool;
	~GlyphFrameCleanup() noexcept {
		try {
			Records.AbortFrame();
			Receiver.ReleaseFrameInputs();
			Pool.Close();
		} catch (...) {
			std::terminate();
		}
	}
};

static void PrepareConsole(sgre::scene::BitmapConsole& console, std::uint32_t color, std::uint32_t opacity) {
	console.SetFontScale(2);
	console.SetFontColor(color);
	console.SetOpacity(opacity);
	console.Print("AABA");
	console.Update(1);
	Require(console.Displayed().size() == 4 && console.PendingCount() == 0 && console.State().Cursor.Width == 16, "real Console update failed to populate captured-style glyph elements");
}

static void TestRealGlyphAtlasAndFrameInputs(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(2);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	sgre::render::SpritePassPresentation surface(*renderer, transfer);
	// Real workers stay idle; glyph inputs are produced by the actual serialized draw receiver.
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&counter] {
		return counter.ReadRawCounter();
	},
	    1, 2);
	sgre::render::FrameHostPool pool(surface, workers, records);
	std::size_t uploaded = 0;
	std::size_t released = 0;
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && image.Width == 1 && image.Height == 1, "glyph atlas must come from its actual decoded A8 source");
		++uploaded;
		return renderer->UploadTexture(format, image);
	},
	    [&](auto handle) {
		    renderer->ReleaseTexture(handle);
		    ++released;
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	const auto asset = catalog.Prepare(FontDocument(), FontBacking());
	sgre::scene::BitmapFontTextureOwner font(cache, asset);
	sgre::scene::BitmapConsole red(asset->Model, {16, 8});
	sgre::scene::BitmapConsole blue(asset->Model, {16, 8});
	PrepareConsole(red, 0xff0000ff, 128);
	PrepareConsole(blue, 0x0000ffff, 255);
	sgre::scene::IndicatorPlacement placement;
	placement.UpdateMeasuredSize({16, 8});
	placement.SetCoordinate({-8, -4});
	placement.SetVisible(true);
	sgre::render::BitmapFontRecordReceiver receiver(records, *renderer, cache, pool);
	GlyphFrameCleanup cleanup{records, receiver, pool};
	Require(uploaded == 2 && cache.Count() == 2, "actual font provider did not upload both real source wrappers");

	records.ResetFrameState();
	pool.Begin();
	const auto first_frame = pool.FrameCounter();
	records.BeginPreparedFrame({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	records.SetScissor({0, 0, 10, 8});
	receiver.Draw(red, placement, font, 73);
	Require(records.RecordDepth() == 0 && pool.RetainedInputs() == 3, "AABA source transitions did not retain three exact physical input cells");
	RequireError([&] {
		receiver.ReleaseFrameInputs();
	},
	    "completed or aborted");
	RequireError([&] {
		pool.Begin();
	},
	    "active");
	const auto accepted = records.EndFrame();
	pool.End();
	RequireError([&] {
		pool.Begin();
	},
	    "input");
	const auto pixels = renderer->Capture(accepted);
	RequirePixel(pixels, 0, 0, {128, 0, 0, 255});
	RequirePixel(pixels, 1, 1, {128, 0, 0, 255});
	RequirePixel(pixels, 4, 0, {128, 0, 0, 255});
	RequirePixel(pixels, 5, 1, {128, 0, 0, 255});
	RequirePixel(pixels, 8, 0, {64, 0, 0, 255});
	RequirePixel(pixels, 9, 1, {64, 0, 0, 255});
	RequirePixel(pixels, 2, 0, {0, 0, 0, 255});
	RequirePixel(pixels, 3, 1, {0, 0, 0, 255});
	RequirePixel(pixels, 12, 0, {0, 0, 0, 255});
	Require(statistics.Current().DrawOperations == 3 && statistics.Current().SubmittedElements == 24, "glyph list counts differ from two A glyphs plus B/A source batches");
	receiver.ReleaseFrameInputs();
	Require(pool.RetainedInputs() == 0 && pool.FrameCounter() == first_frame, "releasing actual glyph borrows advanced or retained their physical frame");

	records.ResetFrameState();
	pool.Begin();
	records.BeginPreparedFrame({0, 1, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	receiver.Draw(blue, placement, font, 74);
	Require(pool.RetainedInputs() == 3, "abort fixture failed to create real blue glyph inputs");
	records.AbortFrame();
	pool.End();
	Require(renderer->Capture(accepted).Rgba == pixels.Rgba, "aborted glyph commands committed their clear or color changes");
	RequireError([&] {
		pool.Begin();
	},
	    "input");
	receiver.ReleaseFrameInputs();

	records.ResetFrameState();
	pool.Begin();
	records.BeginPreparedFrame({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	receiver.Draw(blue, placement, font, 75);
	const auto recovered = records.EndFrame();
	pool.End();
	const auto replayed = renderer->Capture(recovered);
	RequirePixel(replayed, 0, 0, {0, 0, 255, 255});
	RequirePixel(replayed, 9, 1, {0, 0, 128, 255});
	RequirePixel(replayed, 12, 0, {0, 0, 255, 255});
	receiver.ReleaseFrameInputs();
	font.Close();
	Require(released == 2 && cache.Count() == 0 && pool.RetainedInputs() == 0 && pool.BoundWriters() == 0, "actual glyph source retirement left a GPU wrapper or frame borrow");
	pool.Close();
	workers.Close();
}

static void TestContainedGraphicsCycle(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	auto description = Description();
	description.MaxPipelines = 128;
	description.MaxTextures = 32;
	sgre::render::SpriteGraphicsTargets graphics(description, shader);
	const auto renderer = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*renderer, transfer);
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&counter] {
		return counter.ReadRawCounter();
	},
	    1, 2);
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		return renderer->UploadTexture(format, image);
	},
	    [&](auto handle) {
		    renderer->ReleaseTexture(handle);
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	const auto asset = catalog.Prepare(FontDocument(), FontBacking());
	sgre::scene::BitmapFontTextureOwner font(cache, asset);
	sgre::scene::BitmapConsole console(asset->Model, {16, 8});
	PrepareConsole(console, 0xff0000ff, 255);
	sgre::scene::IndicatorPlacement placement;
	placement.UpdateMeasuredSize({16, 8});
	placement.SetCoordinate({-8, -4});
	placement.SetVisible(true);
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	RequireError([&] {
		cycle.DrawBitmapConsole(console, placement, font, 1);
	},
	    "active primary");
	cycle.PreFrame();
	cycle.BeginDrawing();
	cycle.DrawBitmapConsole(console, placement, font, 2);
	cycle.PostFrame();
	const auto accepted = cycle.LastSubmission();
	Require(accepted.has_value() && cycle.CurrentCounters().DrawOperations == 3 && cycle.CurrentCounters().SubmittedElements == 24, "contained cycle lost the actual bitmap source batches");
	const auto original = renderer->Capture(accepted->Identity);
	RequirePixel(original, 0, 0, {255, 0, 0, 255});
	RequirePixel(original, 9, 1, {128, 0, 0, 255});

	console.ClearDisplayed();
	PrepareConsole(console, 0x0000ffff, 255);
	cycle.PreFrame();
	cycle.BeginDrawing();
	cycle.DrawBitmapConsole(console, placement, font, 3);
	cycle.AbortFrame();
	Require(!cycle.LastSubmission() && renderer->Capture(accepted->Identity).Rgba == original.Rgba, "contained glyph abort committed an unaccepted frame");
	cycle.PreFrame();
	cycle.BeginDrawing();
	cycle.DrawBitmapConsole(console, placement, font, 4);
	cycle.PostFrame();
	RequirePixel(renderer->Capture(cycle.LastSubmission()->Identity), 0, 0, {0, 0, 255, 255});
	font.Close();
	Require(cache.Count() == 0, "contained cycle retained a font input after its completed frame");
	cycle.Close();
	workers.Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "bitmap glyph receiver requires actual sprite and transfer Metal source paths");
		@autoreleasepool {
			const bool active_before = NSRunningApplication.currentApplication.active;
			Require(NSApp.windows.count == 0, "glyph fixture requires no preexisting application window");
			@autoreleasepool {
				TestRealGlyphAtlasAndFrameInputs(argv[1], argv[2]);
				TestContainedGraphicsCycle(argv[1], argv[2]);
			}
			Require(NSApp.windows.count == 0 && NSRunningApplication.currentApplication.active == active_before, "glyph fixture left a hidden window or changed application activation");
		}
		std::cout << "actual A8 bitmap glyph triangles/source-order batches/color-opacity/scissor pixels, real pool fences, abort replay and hidden cleanup passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
