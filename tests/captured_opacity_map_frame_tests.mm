#include "sgre/render/graphics_frame_cycle.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/scene/captured_task.hpp"
#include "sgre/scene/image_assets.hpp"
#include "sgre/scene/parallel_task_set.hpp"

#include "sprite_presentation_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("expected Captured opacity-map rejection: " + std::string(expected));
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

struct MapImage {
	std::shared_ptr<const sgre::scene::LoadedImageAsset> Asset;
	std::unique_ptr<sgre::scene::ImageTextureOwner> Texture;
};

static MapImage PrepareImage(sgre::scene::TextureCache& cache, sgre::scene::ImageAssetCatalog& catalog, std::string format, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> bytes) {
	Require(bytes.size() == static_cast<std::size_t>(width) * height, "authored map image must supply its actual raw one-channel extent");
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->SerializedSize = bytes.size();
	document->Chunks = {{0, static_cast<std::uint32_t>(bytes.size())}};
	const auto pixel = Atom(sgre::resource::PsbChunkReference{});
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::move(format))}, {"width", Atom(static_cast<std::int64_t>(width))}, {"height", Atom(static_cast<std::int64_t>(height))}, {"truncated_width", Atom(static_cast<std::int64_t>(width))}, {"truncated_height", Atom(static_cast<std::int64_t>(height))}, {"pixel", pixel}});
	texture.SerializedOffset = 64;
	const auto tile = Atom(sgre::resource::PsbObject{{"left", Atom(std::int64_t{0})}, {"top", Atom(std::int64_t{0})}, {"width", Atom(static_cast<std::int64_t>(width))}, {"height", Atom(static_cast<std::int64_t>(height))}, {"image", texture}});
	const auto entry = Atom(sgre::resource::PsbObject{{"label", Atom(std::string("map"))}, {"width", Atom(static_cast<std::int64_t>(width))}, {"height", Atom(static_cast<std::int64_t>(height))}, {"texture", Atom(sgre::resource::PsbArray{tile})}});
	document->Root = Atom(sgre::resource::PsbObject{{"imageList", Atom(sgre::resource::PsbArray{entry})}});
	MapImage result;
	result.Asset = catalog.Prepare(std::move(document), std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes)));
	result.Texture = std::make_unique<sgre::scene::ImageTextureOwner>(cache, result.Asset);
	return result;
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 8;
	result.Height = 8;
	result.MaxVertices = 256;
	result.MaxIndices = 512;
	result.MaxDraws = 32;
	result.MaxTextures = 32;
	result.MaxPipelines = 256;
	result.MaxTextureBytes = 65536;
	return result;
}

static sgre::scene::FillRectangleDrawPacket Rectangle(float left, float top, float right, float bottom, std::uint32_t color) {
	sgre::scene::FillRectangleDrawPacket packet;
	packet.Vertices = {{{left, top}, {left, bottom}, {right, top}, {right, bottom}}};
	packet.Color = color;
	return packet;
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::uint32_t x, std::uint32_t y, const std::array<std::uint8_t, 4>& expected) {
	Require(x < pixels.Width && y < pixels.Height, "opacity-map pixel exceeds actual target");
	const auto start = (static_cast<std::size_t>(y) * pixels.Width + x) * 4;
	for (std::size_t channel = 0; channel < expected.size(); ++channel) {
		// The fixed one-byte bound covers RGBA8 render conversion, selected before GPU execution.
		const auto difference = static_cast<int>(pixels.Rgba[start + channel]) - static_cast<int>(expected[channel]);
		if (difference < -1 || difference > 1) {
			throw std::runtime_error("opacity-map pixel x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(channel) + " expected=" + std::to_string(expected[channel]) + " actual=" + std::to_string(pixels.Rgba[start + channel]));
		}
	}
}

static void TestActualMapFrame(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto primary = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	std::size_t uploaded = 0;
	std::size_t released = 0;
	sgre::scene::TextureCache cache([&](auto format, const auto& pixels) {
		++uploaded;
		return primary->UploadTexture(format, pixels);
	},
	    [&](auto handle) {
		    primary->ReleaseTexture(handle);
		    ++released;
	    });
	sgre::scene::ImageAssetCatalog catalog(cache);
	auto horizontal = PrepareImage(cache, catalog, "L8", 2, 1, {0, 255});
	auto vertical = PrepareImage(cache, catalog, "L8", 1, 2, {0, 255});
	auto gradient = PrepareImage(cache, catalog, "L8", 4, 1, {0, 85, 170, 255});
	auto white = PrepareImage(cache, catalog, "L8", 1, 1, {255});
	auto alpha = PrepareImage(cache, catalog, "A8", 1, 1, {255});
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	static_cast<void>(cycle.CaptureBackend());
	cycle.PrepareFillRectangle();
	cycle.PrepareCaptured();
	auto source = std::make_shared<sgre::scene::CaptureTask>(4, 4);
	source->SetOffset({4, 4});
	sgre::scene::CapturedTask consumer(source);
	consumer.SetOpacity(127.5f);
	consumer.SetOpacityMap(*horizontal.Asset, horizontal.Texture->Source(0), 255);
	consumer.SetOpacityMapRepeat({2, 2});
	std::uint32_t address_u = 0;
	std::uint32_t address_v = 0;
	bool authored_sampler = false;
	bool half_vertex_alpha = false;
	std::size_t received = 0;
	sgre::scene::CapturedOpacityMapDrawPacket last_packet;
	sgre::scene::ParallelTaskSet children(1);
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto) {
		consumer.Draw([&](auto& texture, const auto& packet) {
			cycle.DrawCaptured(texture, packet);
		},
		    [&](auto& texture, auto image, const auto& packet) {
			    Require(&texture == &source->Texture() && cycle.CameraScope().Depth() == 2, "map callback must use the exact Capture source under its real camera child");
			    last_packet = packet;
			    if (authored_sampler) {
				    // These typed packet cases exercise all native sampler pairs independently of the script's paired smoothing setter.
				    image.SetAddress(address_u, address_v);
				    last_packet.AddressU = address_u;
				    last_packet.AddressV = address_v;
			    }
			    if (half_vertex_alpha) {
				    for (auto& vertex : last_packet.Vertices) {
					    vertex.Color = 0x8000ff00;
				    }
			    }
			    cycle.DrawCapturedOpacityMap(texture, image, last_packet);
			    ++received;
		    });
	};
	static_cast<void>(children.Append(std::move(child)));
	const auto begin = [&] {
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
	};
	const auto fill = [&](std::uint32_t color) {
		cycle.DrawFillRectangle(Rectangle(-4, -4, 4, 4, color));
	};
	const auto capture_pattern = [&] {
		fill(0xffff0000);
		cycle.DrawFillRectangle(Rectangle(-2, -4, 0, 0, 0xff0000ff));
		cycle.DrawCapture(source);
		fill(0xff00ff00);
	};
	const auto draw = [&] {
		cycle.CameraScope().Draw(sgre::scene::LayerCameraParameters{}, children, 0);
	};
	try {
		authored_sampler = true;
		for (address_v = 0; address_v != 3; ++address_v) {
			for (address_u = 0; address_u != 3; ++address_u) {
				begin();
				capture_pattern();
				draw();
				cycle.PostFrame();
				const auto pixels = primary->Capture(cycle.LastSubmission()->Identity);
				const std::array<std::array<std::uint8_t, 4>, 3> coverage{{{{0, 255, 255, 255}}, {{0, 255, 0, 255}}, {{0, 255, 255, 0}}}};
				for (std::uint32_t y = 4; y != 8; ++y) {
					for (std::uint32_t x = 4; x != 8; ++x) {
						const auto selected = coverage[address_u][x - 4];
						RequirePixel(pixels, x, y, x < 6 ? std::array<std::uint8_t, 4>{selected, static_cast<std::uint8_t>(255 - selected), 0, 255} : std::array<std::uint8_t, 4>{0, static_cast<std::uint8_t>(255 - selected), selected, 255});
					}
				}
				Require(cycle.CurrentCounters().DrawOperations == 4 && cycle.CurrentCounters().SubmittedElements == 16, "map adds one native indexed four-element strip beside actual source/background draws");
			}
		}
		authored_sampler = false;
		consumer.SetOpacityMapRepeat({1, 1});
		begin();
		capture_pattern();
		draw();
		cycle.PostFrame();
		const auto linear = primary->Capture(cycle.LastSubmission()->Identity);
		RequirePixel(linear, 5, 5, {64, 191, 0, 255});
		RequirePixel(linear, 6, 5, {0, 64, 191, 255});
		// At these non-texel-center coordinates, nearest sampling would produce0/255 instead of64/191.
		consumer.SetOpacityMap(*horizontal.Asset, horizontal.Texture->Source(0), 85);
		consumer.SetOpacity(63.75f);
		begin();
		capture_pattern();
		draw();
		cycle.PostFrame();
		const auto narrowed = primary->Capture(cycle.LastSubmission()->Identity);
		RequirePixel(narrowed, 5, 5, {0, 255, 0, 255});
		RequirePixel(narrowed, 6, 5, {0, 191, 64, 255});
		RequirePixel(narrowed, 7, 5, {0, 0, 255, 255});
		consumer.SetOpacity(127.5f);
		consumer.SetOpacityMapRepeat({2, 2});
		authored_sampler = true;
		// A vertical map discriminates TEXCOORD1.y and every independent V sampler.
		consumer.SetOpacityMap(*vertical.Asset, vertical.Texture->Source(0), 255);
		for (address_v = 0; address_v != 3; ++address_v) {
			address_u = 0;
			begin();
			capture_pattern();
			draw();
			cycle.PostFrame();
			const auto pixels = primary->Capture(cycle.LastSubmission()->Identity);
			const std::array<std::array<std::uint8_t, 4>, 3> coverage{{{{0, 255, 255, 255}}, {{0, 255, 0, 255}}, {{0, 255, 255, 0}}}};
			for (std::uint32_t y = 4; y != 8; ++y) {
				const auto selected = coverage[address_v][y - 4];
				RequirePixel(pixels, 5, y, {selected, static_cast<std::uint8_t>(255 - selected), 0, 255});
			}
		}
		authored_sampler = false;
		consumer.SetOpacityMap(*gradient.Asset, gradient.Texture->Source(0), 255);
		consumer.SetOpacityMapRepeat({1, 1});
		begin();
		capture_pattern();
		draw();
		cycle.PostFrame();
		const auto pixels = primary->Capture(cycle.LastSubmission()->Identity);
		for (std::uint32_t y = 4; y != 8; ++y) {
			for (std::uint32_t x = 4; x != 8; ++x) {
				const auto selected = static_cast<std::uint8_t>((x - 4) * 85);
				RequirePixel(pixels, x, y, x < 6 ? std::array<std::uint8_t, 4>{selected, static_cast<std::uint8_t>(255 - selected), 0, 255} : std::array<std::uint8_t, 4>{0, static_cast<std::uint8_t>(255 - selected), selected, 255});
			}
		}
		consumer.SetOpacityMap(*white.Asset, white.Texture->Source(0), 255);
		half_vertex_alpha = true;
		begin();
		capture_pattern();
		draw();
		cycle.PostFrame();
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 5, 5, {128, 127, 0, 255});
		half_vertex_alpha = false;
		consumer.SetOpacityMap(*alpha.Asset, alpha.Texture->Source(0), 255);
		begin();
		capture_pattern();
		draw();
		cycle.PostFrame();
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 5, 5, {0, 255, 0, 255});
		// A8 raw red is zero even with full alpha; ordinary white-alpha interpretation would wrongly reveal the source.
		consumer.SetOpacityMap(*white.Asset, white.Texture->Source(0), 255);
		begin();
		capture_pattern();
		draw();
		RequireFailure([&] {
			cycle.DrawCapturedOpacityMap(source->Texture(), {}, last_packet);
		},
		    "live Capture/Image owners");
		Require(!cycle.LastSubmission() && !source->State().Captured && !consumer.State().Ready, "later map failure aborts the entire same-buffer Capture and invalidates its uncommitted readiness");
		begin();
		capture_pattern();
		draw();
		cycle.PostFrame();
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 5, 5, {255, 0, 0, 255});
		consumer.ResetOpacityMap();
		consumer.SetOpacity(255);
		begin();
		fill(0xff00ff00);
		draw();
		cycle.PostFrame();
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 7, 5, {0, 0, 255, 255});
		Require(received == 19, "every actual mapped draw and aborted/replayed occurrence was consumed exactly once");
	} catch (...) {
		cycle.AbortFrame();
		children.Clear();
		consumer.Close();
		source->Close();
		throw;
	}
	children.Clear();
	consumer.Close();
	source->Close();
	source.reset();
	cycle.Close();
	alpha.Texture.reset();
	white.Texture.reset();
	gradient.Texture.reset();
	vertical.Texture.reset();
	horizontal.Texture.reset();
	cache.Close();
	workers.Close();
	Require(uploaded == 5 && released == uploaded, "real decoded Image source owners retire their actual backend textures exactly once");
	const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observation.WindowVisible && !observation.WindowKey && !observation.Presenting && observation.DrawableRequests == 0, "opacity-map physical fixture requested a surface/drawable");
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "usage: captured_opacity_map_frame_tests sprite.metal sprite_present.metal");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				TestActualMapFrame(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "opacity-map fixture changed foreground/activation or retained a window");
			}
			std::cout << "PASS actual Capture/Image dual-UV opacity-map frame, samplers, raw-red A8/L8, alpha, abort/replay and ordinary recovery\n";
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* error) {
		std::cerr << "opacity-map Objective-C failure: " << error.reason.UTF8String << '\n';
		return 1;
	}
}
