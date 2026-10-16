#include "sgre/render/graphics_frame_cycle.hpp"

#include "sgre/platform/host_performance_counter.hpp"

#include "sprite_presentation_fixture.hpp"
#include "text_glyph_drawing_fixture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 32;
	result.Height = 16;
	result.MaxVertices = 256;
	result.MaxIndices = 512;
	result.MaxDraws = 32;
	result.MaxTextures = 32;
	result.MaxPipelines = 256;
	// The actual default BGRA atlas and its frame upload slots share this real backend budget.
	result.MaxTextureBytes = 32 * 1024 * 1024;
	return result;
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::uint32_t x, std::uint32_t y, const std::array<std::uint8_t, 4>& expected) {
	const auto first = (static_cast<std::size_t>(y) * pixels.Width + x) * 4;
	for (std::size_t channel = 0; channel < expected.size(); ++channel) {
		// Selected before GPU execution: one byte for the RGBA8 conversion of native normalized colors/blending.
		const auto difference = static_cast<int>(pixels.Rgba[first + channel]) - static_cast<int>(expected[channel]);
		if (difference < -1 || difference > 1) {
			throw std::runtime_error("Text glyph pixel x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(channel) + " expected=" + std::to_string(expected[channel]) + " actual=" + std::to_string(pixels.Rgba[first + channel]));
		}
	}
}

static void ReadActualGlyphCoverage(sgre::text::TextGlyphAtlasOwner& atlas, const sgre::text::VectorGlyphDrawRectangle& rectangle, std::array<std::uint8_t, 64>& coverage) {
	const auto& source = rectangle.SourcePixels;
	Require(source.Right - source.Left == 8 && source.Bottom - source.Top == 8, "authored eight-pixel CFF rectangle must retain its actual cached dimensions");
	Require(source.Left >= 1 && source.Top >= 1 && source.Right <= 1024 && source.Bottom <= 1024 && std::floor(source.Left) == source.Left && std::floor(source.Top) == source.Top, "actual fixed-atlas source coordinates must remain integral and bounded before CPU mapping");
	auto& texture = atlas.Texture();
	const auto mapped = texture.Map(true);
	Require(mapped.Succeeded, "actual CPU-shadow glyph atlas must permit its preserve mapping");
	try {
		const auto pitch = texture.RowPitch();
		const auto left = static_cast<std::size_t>(source.Left);
		const auto top = static_cast<std::size_t>(source.Top);
		for (std::size_t row = 0; row < 8; ++row) {
			for (std::size_t column = 0; column < 8; ++column) {
				const auto first = (top + row) * pitch + (left + column) * 4;
				Require(first <= mapped.Bytes.size() && 4 <= mapped.Bytes.size() - first, "authored actual atlas region exceeds its owner mapping");
				const auto alpha = std::to_integer<std::uint8_t>(mapped.Bytes[first + 3]);
				for (std::size_t channel = 0; channel < 3; ++channel) {
					Require(std::to_integer<std::uint8_t>(mapped.Bytes[first + channel]) == alpha, "native CFF white BGRA atlas retains coverage in every channel");
				}
				Require(alpha != 0, "authored CFF rectangle must cover every glyph pixel");
				coverage[row * 8 + column] = alpha;
			}
		}
		const auto range = std::minmax_element(coverage.begin(), coverage.end());
		std::cout << "Actual CFF8 CPU atlas coverage min=" << static_cast<std::uint32_t>(*range.first) << " max=" << static_cast<std::uint32_t>(*range.second) << '\n';
	} catch (...) {
		texture.Unmap();
		throw;
	}
	texture.Unmap();
}

static void CompositeExpectedGlyph(std::array<std::uint8_t, 4>& destination, const std::array<std::uint8_t, 64>& coverage, std::uint32_t x, std::uint32_t y, std::uint32_t left, std::uint32_t top, const std::array<std::uint8_t, 4>& color, std::uint32_t sample_step = 1) {
	const auto extent = 8 / sample_step;
	if (x < left || y < top || x - left >= extent || y - top >= extent) {
		return;
	}
	const auto sample_x = (x - left) * sample_step + sample_step / 2;
	const auto sample_y = (y - top) * sample_step + sample_step / 2;
	const auto amount = static_cast<double>(coverage[sample_y * 8 + sample_x]) / 255;
	const auto source_alpha = amount * static_cast<double>(color[3]) / 255;
	for (std::size_t channel = 0; channel < 4; ++channel) {
		// Original PS240 multiplies premultiplied-white coverage by BGRA-decoded color; native blend row0
		// then applies source alpha to RGB, and source-one/inverse-source-alpha to alpha.
		const auto source = amount * color[channel];
		const auto result = source * (channel == 3 ? 1 : source_alpha) + destination[channel] * (1 - source_alpha);
		destination[channel] = static_cast<std::uint8_t>(std::round(std::clamp(result, 0.0, 255.0)));
	}
}

static void TestActualTextAtlasFrame(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto primary = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	sgre::scene::TextureCache cache([&](auto format, const auto& pixels) {
		return primary->UploadTexture(format, pixels);
	},
	    [&](auto texture) {
		    primary->ReleaseTexture(texture);
	    });
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	cycle.PrepareTextGlyphs();
	cycle.PrepareFillRectangle();
	std::size_t callbacks = 0;
	bool injected_failure = false;
	bool coverage_ready = false;
	std::array<std::uint8_t, 64> coverage{};
	text_drawing_test::Fixture fixture(cache, cycle.RawTextureBackend(), [&](auto& atlas, auto rectangles, const auto& clip) {
		++callbacks;
		if (!coverage_ready) {
			Require(!rectangles.empty(), "actual Text queue must contain its real retained glyphs");
			ReadActualGlyphCoverage(atlas, rectangles.front(), coverage);
			coverage_ready = true;
		}
		cycle.DrawTextGlyphs(atlas, rectangles, clip);
		if (injected_failure) {
			throw std::runtime_error("Text glyph containing draw failure");
		}
	},
	    &cycle.CameraScope());
	fixture.Call("setFont", {fixture.Properties({{"color", std::bit_cast<std::int32_t>(std::uint32_t{0xff40a0ff})}})});
	fixture.Render();
	const auto position = [&] {
		fixture.Call("setCoord", {-12.0f, -4.0f});
	};
	position();
	const auto begin = [&] {
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
	};
	const auto draw = [&] {
		fixture.Draw();
		const auto& state = cycle.FrameState();
		Require(state.RawScissor.MinimumX == 0 && state.RawScissor.MinimumY == 0 && state.RawScissor.MaximumX == 32 && state.RawScissor.MaximumY == 16 && cycle.CameraScope().Depth() == 1 && !cycle.CameraScope().IsDrawing(), "Text glyph receiving must restore actual raw clip/model/camera scope");
	};
	const auto check = [&](const auto& expected) {
		cycle.PostFrame();
		const auto pixels = primary->Capture(cycle.LastSubmission()->Identity);
		for (std::uint32_t y = 0; y < pixels.Height; ++y) {
			for (std::uint32_t x = 0; x < pixels.Width; ++x) {
				RequirePixel(pixels, x, y, expected(x, y));
			}
		}
	};
	// Script/native stored ARGB ff40a0ff →queueffa040ff →packedff40a0ff →BGRA input RGBA64/160/255/255.
	const std::array<std::uint8_t, 4> tint{64, 160, 255, 255};
	const std::array<std::uint8_t, 4> clear{0, 0, 0, 255};
	try {
		begin();
		draw();
		check([&](auto x, auto y) {
			auto pixel = clear;
			CompositeExpectedGlyph(pixel, coverage, x, y, 4, 4, tint);
			CompositeExpectedGlyph(pixel, coverage, x, y, 12, 4, tint);
			return pixel;
		});
		Require(cycle.CurrentCounters().DrawOperations == 1 && cycle.CurrentCounters().SubmittedElements == 12 && !fixture.Text.Context(fixture.Instance).Atlas().State().Texture->Dirty, "two actual CFF glyphs must batch as native format4 and upload their real BGRA atlas");

		fixture.Call("setShowCount", {std::int32_t{1}});
		begin();
		draw();
		check([&](auto x, auto y) {
			auto pixel = clear;
			CompositeExpectedGlyph(pixel, coverage, x, y, 4, 4, tint);
			return pixel;
		});

		fixture.Call("setShowCount", {std::int32_t{2}});
		fixture.Call("setClipMode", {std::int32_t{2}});
		fixture.Call("setClipOffset", {8.0f, 0.0f});
		begin();
		draw();
		check([&](auto x, auto y) {
			auto pixel = clear;
			CompositeExpectedGlyph(pixel, coverage, x, y, 12, 4, tint);
			return pixel;
		});
		fixture.Call("setClipMode", {std::int32_t{0}});
		fixture.Call("setClipOffset", {0.0f, 0.0f});

		fixture.Call("setCoord", {-8.0f, -4.0f});
		fixture.Call("setScale", {0.5f, 0.5f});
		begin();
		draw();
		check([&](auto x, auto y) {
			auto pixel = clear;
			CompositeExpectedGlyph(pixel, coverage, x, y, 12, 6, tint, 2);
			CompositeExpectedGlyph(pixel, coverage, x, y, 16, 6, tint, 2);
			return pixel;
		});
		fixture.Call("setScale", {1.0f, 1.0f});

		fixture.Call("clear");
		fixture.Call("setFont", {fixture.Properties({{"shadow", true}, {"shadowdiff", std::int32_t{1}}, {"color", std::bit_cast<std::int32_t>(std::uint32_t{0xff40a0ff})}})});
		fixture.Render();
		position();
		begin();
		sgre::scene::FillRectangleDrawPacket background;
		background.Vertices = {{{-16, -8}, {-16, 8}, {16, -8}, {16, 8}}};
		background.Color = 0xffffffff;
		cycle.DrawFillRectangle(background);
		draw();
		check([&](auto x, auto y) {
			std::array<std::uint8_t, 4> pixel{255, 255, 255, 255};
			const std::array<std::uint8_t, 4> shadow{0, 0, 0, 255};
			CompositeExpectedGlyph(pixel, coverage, x, y, 5, 5, shadow);
			CompositeExpectedGlyph(pixel, coverage, x, y, 4, 4, tint);
			CompositeExpectedGlyph(pixel, coverage, x, y, 13, 5, shadow);
			CompositeExpectedGlyph(pixel, coverage, x, y, 12, 4, tint);
			return pixel;
		});
		Require(cycle.CurrentCounters().DrawOperations == 2 && cycle.CurrentCounters().SubmittedElements == 28, "real white FillRect background must reveal the actual shadow queue beside main glyphs");

		fixture.Call("clear");
		fixture.Call("setFont", {fixture.Properties({{"shadow", false}, {"color", std::bit_cast<std::int32_t>(std::uint32_t{0x8040a0ff})}})});
		fixture.Render();
		position();
		injected_failure = true;
		begin();
		try {
			fixture.Draw();
			throw std::runtime_error("Text glyph fixture expected containing failure");
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()) == "Text glyph containing draw failure", error.what());
			cycle.AbortFrame();
		}
		injected_failure = false;
		begin();
		draw();
		check([&](auto x, auto y) {
			auto pixel = clear;
			const std::array<std::uint8_t, 4> translucent{64, 160, 255, 128};
			CompositeExpectedGlyph(pixel, coverage, x, y, 4, 4, translucent);
			CompositeExpectedGlyph(pixel, coverage, x, y, 12, 4, translucent);
			return pixel;
		});
		Require(callbacks == 7, "every successful and aborted real Text glyph occurrence must be consumed exactly once");
	} catch (...) {
		cycle.AbortFrame();
		throw;
	}
	fixture.Layers.ReleaseInstance(fixture.Layer);
	fixture.Vm.Close();
	cycle.Close();
	cache.Close();
	workers.Close();
	const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observation.WindowVisible && !observation.WindowKey && !observation.Presenting && observation.DrawableRequests == 0, "Text glyph fixture requested a surface/drawable");
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "usage: text_glyph_frame_tests sprite.metal sprite_present.metal");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				TestActualTextAtlasFrame(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "Text glyph fixture changed foreground/activation or retained a window");
			}
			std::cout << "PASS actual Text/CFF/atlas/format4 tint, shown count, clipping, camera, shadow, alpha and abort/replay pixels\n";
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* error) {
		std::cerr << "Text glyph Objective-C failure: " << error.reason.UTF8String << '\n';
		return 1;
	}
}
