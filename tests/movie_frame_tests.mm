#include "sgre/render/graphics_frame_cycle.hpp"

#include "sgre/platform/host_performance_counter.hpp"

#include "sprite_presentation_fixture.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 8;
	result.Height = 8;
	result.MaxVertices = 128;
	result.MaxIndices = 256;
	result.MaxDraws = 16;
	result.MaxTextures = 8;
	result.MaxPipelines = 256;
	result.MaxTextureBytes = 65536;

	return result;
}

static std::shared_ptr<sgre::media::MovieRgb32Frame> PrepareFrame(bool green) {
	auto frame = std::make_shared<sgre::media::MovieRgb32Frame>();
	frame->Width = 16;
	frame->Height = 16;
	frame->PaddedWidth = 16;
	frame->PaddedHeight = 16;
	frame->RowBytes = 64;
	frame->Bytes.resize(1024);
	for (std::size_t y = 0; y < 16; ++y) {
		for (std::size_t x = 0; x < 16; ++x) {
			const auto offset = y * 64 + x * 4;
			frame->Bytes[offset] = green ? 0 : 50;
			frame->Bytes[offset + 1] = green ? 255 : 100;
			frame->Bytes[offset + 2] = y == 0 ? 128 : green ? 0
			                                                : 200;
			frame->Bytes[offset + 3] = 17;
		}
	}

	return frame;
}

static void RequireCenter(const sgre::render::ColorPassPixels& pixels, const std::array<int, 4>& expected) {
	const auto start = (std::size_t{4} * pixels.Width + 4) * 4;
	for (std::size_t channel = 0; channel < expected.size(); ++channel) {
		const auto difference = static_cast<int>(pixels.Rgba[start + channel]) - expected[channel];
		if (difference < -1 || difference > 1) {
			throw std::runtime_error("movie pixel channel " + std::to_string(channel) + " expected " + std::to_string(expected[channel]) + " received " + std::to_string(pixels.Rgba[start + channel]));
		}
	}
}

static void TestActualFrame(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto primary = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		return primary->UploadTexture(format, image);
	},
	    [&](auto handle) {
		    primary->ReleaseTexture(handle);
	    });
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	cycle.PrepareMovies();
	std::uint32_t identity = 1;
	sgre::media::MovieDrawFrame frame;
	frame.OwnerIdentity = &identity;
	frame.FrameSerial = 1;
	frame.Sample.Rgb32 = PrepareFrame(false);
	frame.Position = {-4, -4};
	frame.Extent = {8, 8};
	sgre::scene::ParallelTaskSet children(1);
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto) {
		Require(!cycle.CanRetireMovie(&identity), "movie owner retired inside its active actual frame");
		cycle.DrawMovie(frame);
	};
	static_cast<void>(children.Append(std::move(child)));
	try {
		for (std::size_t phase = 0; phase < 4; ++phase) {
			if (phase == 1) {
				frame.Opacity = 128;
			} else if (phase == 2) {
				frame.UseAlpha = true;
			} else if (phase == 3) {
				frame.UseAlpha = false;
				frame.Opacity = 255;
				frame.FrameSerial = 2;
				frame.Sample.Rgb32 = PrepareFrame(true);
			}
			cycle.PreFrame();
			cycle.BeginDrawing();
			cycle.ResetRootCamera();
			cycle.CameraScope().Draw(sgre::scene::LayerCameraParameters{}, children, 0);
			cycle.PostFrame();
			const auto pixels = primary->Capture(cycle.LastSubmission()->Identity);
			const std::array<std::array<int, 4>, 4> expected{{{200, 100, 50, 255}, {100, 50, 25, 255}, {50, 25, 13, 255}, {0, 255, 0, 255}}};
			RequireCenter(pixels, expected[phase]);
		}
		Require(cycle.CanRetireMovie(&identity), "movie owner must retire after actual outer-frame encoding");
		cycle.RetireMovie(&identity);
		cycle.Close();
	} catch (...) {
		cycle.AbortFrame();
		cycle.RetireMovie(&identity);
		cycle.Close();
		throw;
	}
	workers.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("movie frame fixture requires exact sprite/presentation shader paths");
		}
		TestActualFrame(argv[1], argv[2]);
		std::cout << "actual movie RGB32/packed-alpha/update/retirement frames passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
