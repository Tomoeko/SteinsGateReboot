#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/render/sprite_render_records.hpp"
#include "sgre/scene/motion_alternate_program.hpp"
#include "sgre/scene/motion_texture_binding.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

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

	throw std::runtime_error("unsupported live mask operation was accepted");
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 8;
	result.Height = 8;
	result.MaxDraws = 32;
	result.MaxVertices = 128;
	result.MaxIndices = 128;
	result.MaxTextures = 16;
	result.MaxPipelines = 16;
	result.MaxTextureBytes = 64 * 1024;

	return result;
}

static constexpr std::array<float, 16> kPixelProjection{.25f, 0, 0, -1, 0, -.25f, 0, 1, 0, 0, 0, .5f, 0, 0, 0, 1};
static constexpr std::array<std::uint32_t, 4> kStrip{0, 2, 1, 3};

static std::array<sgre::scene::MotionSpriteVertex, 4> Quad(float left, float top, float right, float bottom, std::uint32_t rgba = 0x10203040) {
	std::array<sgre::scene::MotionSpriteVertex, 4> result;
	result[0].Position = {left, top};
	result[1].Position = {right, top};
	result[2].Position = {left, bottom};
	result[3].Position = {right, bottom};
	result[0].Uv = {0, 0};
	result[1].Uv = {1, 0};
	result[2].Uv = {0, 1};
	result[3].Uv = {1, 1};
	for (auto& vertex : result) {
		vertex.Color = rgba;
	}

	return result;
}

static sgre::render::SpriteTextureHandle Solid(sgre::render::SpritePassRenderer& renderer, std::array<std::uint8_t, 4> rgba, sgre::resource::PixelFormat format = sgre::resource::PixelFormat::Bc7) {
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels.assign(rgba.begin(), rgba.end());

	return renderer.UploadTexture(format, image);
}

static sgre::scene::SpritePipeline Pipeline(std::uint32_t blend = 0, std::uint32_t attributes = 4, sgre::resource::PixelFormat format = sgre::resource::PixelFormat::Bc7) {
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = format;
	material.TextureAttributes = attributes;
	material.BlendMode = blend;
	sgre::scene::SpritePipelineOptions options;
	options.Sampling = sgre::scene::TextureSampling::Nearest;
	options.AddressU = sgre::scene::TextureAddress::Clamp;
	options.AddressV = sgre::scene::TextureAddress::Clamp;

	return sgre::scene::SelectSpritePipeline(material, options);
}

static void Draw(sgre::render::SpriteFrameEncoder& encoder, sgre::render::SpriteTextureHandle texture, sgre::render::SpritePipelineHandle pipeline, float left, float top, float right, float bottom, std::uint32_t color = 0x10203040) {
	const auto vertices = Quad(left, top, right, bottom, color);
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = kStrip;
	encoder.Append(draw, kPixelProjection);
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::size_t x, std::size_t y, const std::array<int, 4>& expected, int tolerance = 1) {
	Require(pixels.Width == 8 && pixels.Height == 8 && pixels.Rgba.size() == 256, "live mask pixel capture extent differs");
	for (std::size_t channel = 0; channel < 4; ++channel) {
		const auto actual = static_cast<int>(pixels.Rgba[(y * pixels.Width + x) * 4 + channel]);
		if (std::abs(actual - expected[channel]) > tolerance) {
			throw std::runtime_error("live mask pixel differs at " + std::to_string(x) + "," + std::to_string(y) + " channel " + std::to_string(channel) + " expected " + std::to_string(expected[channel]) + " actual " + std::to_string(actual));
		}
	}
}

static void TestUnionAndRestore(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	const auto half = Solid(renderer, {17, 29, 71, 128});
	const auto red = Solid(renderer, {255, 0, 0, 255});
	const auto green = Solid(renderer, {0, 255, 0, 255});
	const auto pipeline = renderer.PreparePipeline(Pipeline());
	sgre::render::SpriteFrameEncoder encoder(renderer);
	encoder.PrepareTextureMask();
	encoder.Begin({0, 0, 1, 1}, sgre::render::SpritePassLoadAction::Clear);
	encoder.SetScissor({1, 1, 7, 7});
	encoder.ConfigureTextureMask(0, 0, 0);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Begin);
	Require(encoder.ActivePixelShader(pipeline) == 2048, "actual RGBA phase3 shader is not native runtime2048");
	Draw(encoder, half, pipeline, 0, 0, 4, 8);
	Draw(encoder, half, pipeline, 0, 0, 4, 8);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Resolve);
	const auto restored_scissor = encoder.SubmittedDeviceScissor();
	Require(restored_scissor.MinimumX == 1 && restored_scissor.MinimumY == 1 && restored_scissor.MaximumX == 7 && restored_scissor.MaximumY == 7 && encoder.ActivePixelShader(pipeline) == 195, "mask Resolve lost the real saved scissor or ordinary auxiliary shader");
	Draw(encoder, red, pipeline, 0, 0, 8, 8);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Finish);
	Draw(encoder, green, pipeline, 6, 0, 8, 8);
	const auto pixels = renderer.Capture(encoder.End());
	RequirePixel(pixels, 2, 2, {192, 0, 63, 255});
	RequirePixel(pixels, 4, 2, {0, 0, 255, 255});
	RequirePixel(pixels, 6, 2, {0, 255, 0, 255});
	RequirePixel(pixels, 7, 2, {0, 0, 255, 255});
	RequirePixel(pixels, 0, 0, {0, 0, 255, 255});
}

static void TestCombineSubtractAndColorOnly(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	const auto half = Solid(renderer, {90, 30, 60, 128});
	const auto red = Solid(renderer, {255, 0, 0, 255});
	const auto pipeline = renderer.PreparePipeline(Pipeline());
	sgre::render::SpriteFrameEncoder encoder(renderer);
	encoder.PrepareTextureMask();
	encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Begin);
	Draw(encoder, red, pipeline, 0, 0, 6, 8);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Begin);
	Require(encoder.ActivePixelShader(pipeline) == 2049, "actual RGBA phase4 shader is not native runtime2049");
	Draw(encoder, half, pipeline, 0, 0, 8, 4);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::End);
	Require(encoder.ActivePixelShader(pipeline) == 2050, "actual RGBA phase5 shader is not native runtime2050");
	Draw(encoder, half, pipeline, 0, 0, 3, 4);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Resolve);
	Draw(encoder, red, pipeline, 0, 0, 8, 8);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Finish);
	const auto pixels = renderer.Capture(encoder.End());
	RequirePixel(pixels, 1, 1, {64, 0, 0, 255});
	RequirePixel(pixels, 4, 1, {128, 0, 0, 255});
	RequirePixel(pixels, 6, 1, {0, 0, 0, 255});
	RequirePixel(pixels, 4, 5, {0, 0, 0, 255});

	encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::End);
	encoder.ConfigureTextureMask(0x80, 2, -.5f);
	Require(encoder.ActivePixelShader(pipeline, false) == 2047, "nonzero operation byte or color-only phase5 selector differs");
	Draw(encoder, {}, pipeline, 0, 0, 4, 8, 0x10203080);
	encoder.ConfigureTextureMask(0, 1, 0);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Resolve);
	Draw(encoder, red, pipeline, 0, 0, 8, 8);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Finish);
	const auto end_first = renderer.Capture(encoder.End());
	RequirePixel(end_first, 1, 1, {126, 0, 0, 255});
	RequirePixel(end_first, 6, 1, {255, 0, 0, 255});
}

static std::array<std::uint8_t, 4> BackgroundPixel(std::size_t x, std::size_t y) {
	return {static_cast<std::uint8_t>(20 + x * 25), static_cast<std::uint8_t>(30 + y * 23), static_cast<std::uint8_t>(210 - x * 17), 255};
}

static sgre::render::SpriteTextureHandle Background(sgre::render::SpritePassRenderer& renderer) {
	sgre::resource::RgbaImage image;
	image.Width = 8;
	image.Height = 8;
	for (std::size_t y = 0; y < 8; ++y) {
		for (std::size_t x = 0; x < 8; ++x) {
			const auto pixel = BackgroundPixel(x, y);
			image.Pixels.insert(image.Pixels.end(), pixel.begin(), pixel.end());
		}
	}

	return renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
}

static sgre::scene::MutableTextureSpec CopiedSpec() {
	sgre::scene::MutableTextureSpec spec;
	spec.Storage = sgre::scene::MutableTextureStorage::RenderTarget;
	spec.Description.Width = 4;
	spec.Description.Height = 4;
	spec.Description.OriginalFormat = 28;

	return spec;
}

static std::array<int, 4> CopiedExpected(std::size_t x, std::size_t y, bool overlay, bool double_color = false) {
	const auto destination = BackgroundPixel(x, y);
	const std::array<double, 3> source{128.0 / 255, 64.0 / 255, 192.0 / 255};
	const double opacity = (153.0 / 255) * (128.0 / 255) * (double_color ? 128.0 / 255 : 1.0);
	std::array<int, 4> result{0, 0, 0, 255};
	for (std::size_t channel = 0; channel < 3; ++channel) {
		const auto dest = static_cast<double>(destination[channel]) / 255;
		const std::array<double, 3> factors{32.0 / 255, 64.0 / 255, 96.0 / 255};
		const double value = source[channel] * (double_color ? factors[channel] * 2 : 1.0);
		const auto combined = overlay ? (dest < .5 ? (dest * value) * 2 : 1 - ((1 - dest) * (1 - value)) * 2) : std::min(dest + value, 1.0);
		result[channel] = static_cast<int>(std::round((opacity * (combined - dest) + dest) * 255));
	}

	return result;
}

static void TestMaskedCopiedRegion(const std::filesystem::path& shader, bool double_color = false) {
	for (const bool overlay : {false, true}) {
		sgre::render::SpritePassRenderer renderer(Description(), shader);
		const auto background = Background(renderer);
		const auto half = Solid(renderer, {60, 70, 80, 128});
		const auto source = Solid(renderer, {128, 64, 192, 153});
		const auto ordinary = renderer.PreparePipeline(Pipeline());
		const auto copied_pipeline = renderer.PreparePipeline(Pipeline(overlay ? 0x112 : 0x110, double_color ? 0 : 4));
		auto backend = renderer.CreateMutableTextureBackend();
		sgre::scene::MutableTextureOwner copied(*backend, CopiedSpec());
		copied.SetAddress(0, 0);
		copied.Bind(2, sgre::scene::MutableTextureStage::Pixel);
		sgre::render::SpriteFrameEncoder encoder(renderer);
		encoder.PrepareTextureMask();
		encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
		Draw(encoder, background, ordinary, 0, 0, 8, 8);
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Begin);
		// Alternate copied flags are deliberately present here: phase3 uses the mask shader without t2.
		Draw(encoder, half, copied_pipeline, 0, 0, 4, 8);
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Resolve);
		Require(encoder.ActivePixelShader(copied_pipeline) == (overlay ? (double_color ? 1302u : 1150u) : (double_color ? 1300u : 1148u)), "phase6 did not select the actual masked copied runtime program");
		const auto destination = encoder.CopyCurrentTarget(copied, {2, 2, 6, 6});
		const auto vertices = Quad(2, 2, 6, 6, double_color ? 0x20406080 : 0x10203040);
		sgre::render::SpritePassDraw draw;
		draw.Texture = source;
		draw.Pipeline = copied_pipeline;
		draw.Vertices = vertices;
		draw.Indices = kStrip;
		draw.CopiedDestination = destination;
		draw.CopiedUvAffine = {.25f, .25f, -.5f, -.5f};
		encoder.Append(draw, kPixelProjection);
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Finish);
		const auto pixels = renderer.Capture(encoder.End());
		RequirePixel(pixels, 2, 2, CopiedExpected(2, 2, overlay, double_color));
		RequirePixel(pixels, 3, 4, CopiedExpected(3, 4, overlay, double_color));
		const auto unchanged = BackgroundPixel(5, 3);
		RequirePixel(pixels, 5, 3, {unchanged[0], unchanged[1], unchanged[2], unchanged[3]});
		const auto outside = BackgroundPixel(1, 1);
		RequirePixel(pixels, 1, 1, {outside[0], outside[1], outside[2], outside[3]});
	}
}

static std::array<int, 4> MaskedDarkenExpected(std::size_t x, std::size_t y, bool ignore) {
	const auto destination = BackgroundPixel(x, y);
	const std::array<double, 3> source{128.0 / 255, 64.0 / 255, 192.0 / 255};
	const std::array<double, 3> color{32.0 / 255, 64.0 / 255, 96.0 / 255};
	const double opacity = (153.0 / 255) * (ignore ? 1.0 : 128.0 / 255) * (128.0 / 255);
	std::array<int, 4> result{0, 0, 0, 255};
	for (std::size_t channel = 0; channel < 3; ++channel) {
		const double dest = static_cast<double>(destination[channel]) / 255;
		const double value = source[channel] * (ignore ? 1.0 : color[channel] * 2.0);
		result[channel] = static_cast<int>(std::lround((dest + opacity * (std::min(value, dest) - dest)) * 255));
	}

	return result;
}

static void TestMaskedDarken(const std::filesystem::path& shader) {
	for (const bool ignore : {false, true}) {
		sgre::render::SpritePassRenderer renderer(Description(), shader);
		const auto background = Background(renderer);
		const auto half = Solid(renderer, {60, 70, 80, 128});
		const auto source = Solid(renderer, {128, 64, 192, 153});
		const auto ordinary = renderer.PreparePipeline(Pipeline());
		const auto copied_pipeline = renderer.PreparePipeline(Pipeline(0x118, ignore ? 4 : 0));
		auto backend = renderer.CreateMutableTextureBackend();
		sgre::scene::MutableTextureOwner copied(*backend, CopiedSpec());
		copied.SetAddress(0, 0);
		copied.Bind(2, sgre::scene::MutableTextureStage::Pixel);
		sgre::render::SpriteFrameEncoder encoder(renderer);
		encoder.PrepareTextureMask();
		encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
		Draw(encoder, background, ordinary, 0, 0, 8, 8);
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Begin);
		Draw(encoder, half, ordinary, 0, 0, 4, 8);
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Resolve);
		Require(encoder.ActivePixelShader(copied_pipeline) == (ignore ? 1156u : 1308u), "darken Resolve must select its exact masked runtime program");
		const auto destination = encoder.CopyCurrentTarget(copied, {2, 2, 6, 6});
		const auto vertices = Quad(2, 2, 6, 6, 0x20406080);
		sgre::render::SpritePassDraw draw;
		draw.Texture = source;
		draw.Pipeline = copied_pipeline;
		draw.Vertices = vertices;
		draw.Indices = kStrip;
		draw.CopiedDestination = destination;
		draw.CopiedUvAffine = {.25f, .25f, -.5f, -.5f};
		encoder.Append(draw, kPixelProjection);
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Finish);
		const auto pixels = renderer.Capture(encoder.End());
		for (std::size_t y = 0; y < 8; ++y) {
			for (std::size_t x = 0; x < 8; ++x) {
				const auto base = BackgroundPixel(x, y);
				const std::array<int, 4> unchanged{base[0], base[1], base[2], base[3]};
				RequirePixel(pixels, x, y, x >= 2 && x < 4 && y >= 2 && y < 6 ? MaskedDarkenExpected(x, y, ignore) : unchanged);
			}
		}
	}
}

static void TestRecordsAndActualKeyBits(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(2);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	const auto half = Solid(*renderer, {0, 0, 0, 128});
	const auto red = Solid(*renderer, {255, 0, 0, 255});
	const auto ordinary = renderer->PreparePipeline(Pipeline());
	const auto copied_pipeline = renderer->PreparePipeline(Pipeline(0x112, 5));
	auto backend = graphics.CreateMutableTextureBackend();
	sgre::scene::MutableTextureOwner copied(*backend, CopiedSpec());
	copied.SetAddress(0, 0);
	copied.Bind(2, sgre::scene::MutableTextureStage::Pixel);
	records.PrepareTextureMask();
	sgre::render::FrameHostHeap heap(2);
	heap.Begin();
	const auto mask_vertices = Quad(-4, -4, 0, 4);
	const auto drawn_vertices = Quad(-2, -2, 2, 2);
	sgre::render::SpriteHostInputs mask_inputs(heap, mask_vertices, kStrip);
	sgre::render::SpriteHostInputs drawn_inputs(heap, drawn_vertices, kStrip);
	records.BeginFrame({.25f, .75f, .5f, 1}, sgre::render::SpritePassLoadAction::Clear);
	records.BeginRecord();
	records.SetFilterColor(0xffffffff);
	records.SetBlend(0, 0);
	records.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
	records.ApplyTextureMask(sgre::scene::MotionStencilAction::Begin);
	records.Append(half, ordinary, mask_inputs, 4);
	records.ApplyTextureMask(sgre::scene::MotionStencilAction::Resolve);
	const auto destination = records.CopyCurrentTarget(copied, {2, 2, 6, 6});
	records.SetCopiedDestination(destination, {.25f, .25f, -.5f, -.5f});
	records.SetBlend(0x112, 0);
	records.Append(red, copied_pipeline, drawn_inputs, 5);
	records.ClearCopiedDestination();
	records.ApplyTextureMask(sgre::scene::MotionStencilAction::Finish);
	records.EndRecord();
	const auto submitted = records.EndFrame();
	heap.End();
	const auto pixels = renderer->Capture(submitted);
	RequirePixel(pixels, 3, 3, {96, 159, 64, 255}, 2);
	RequirePixel(pixels, 5, 3, {64, 191, 128, 255}, 1);
	Require(statistics.Current().DrawOperations == 2 && statistics.Current().SubmittedElements == 8, "mask record/copied draw did not consume the real indexed inputs and counters");
}

static void TestAbortAndGuardBoundaries(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	const auto red = Solid(renderer, {255, 0, 0, 255});
	const auto pipeline = renderer.PreparePipeline(Pipeline());
	sgre::render::SpriteFrameEncoder encoder(renderer);
	encoder.Begin({0, 0, 1, 1}, sgre::render::SpritePassLoadAction::Clear);
	const auto prior = encoder.End();
	const auto before = renderer.Capture(prior);
	encoder.Begin({1, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	RequireError([&] {
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
	},
	    "idle-prepared");
	encoder.Abort();
	encoder.PrepareTextureMask();
	encoder.Begin({1, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	RequireError([&] {
		encoder.PrepareTextureMask();
	},
	    "quiescent");
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Begin);
	Draw(encoder, red, pipeline, 0, 0, 8, 8);
	encoder.Abort();
	Require(renderer.Capture(prior).Rgba == before.Rgba, "aborted mask render committed its clear or reused-mask writes");
	encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Begin);
	Draw(encoder, red, pipeline, 0, 0, 2, 8);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Resolve);
	Draw(encoder, red, pipeline, 0, 0, 8, 8);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Finish);
	const auto recovered = renderer.Capture(encoder.End());
	RequirePixel(recovered, 1, 1, {255, 0, 0, 255});
	RequirePixel(recovered, 5, 1, {0, 0, 0, 255});

	encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Prepare);
	RequireError([&] {
		encoder.ApplyTextureMask(sgre::scene::MotionStencilAction::Resolve);
	},
	    "initialized mask");
	encoder.Abort();
	encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	std::exception_ptr wrong_thread;
	std::thread other([&] {
		try {
			encoder.ConfigureTextureMask(0, 1, 0);
		} catch (...) {
			wrong_thread = std::current_exception();
		}
	});
	other.join();
	Require(wrong_thread != nullptr, "mask mutation from another owner thread was accepted");
	encoder.Abort();
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("usage: texture_mask_live_frame_tests sprite_pass.metal");
		}
		@autoreleasepool {
			TestUnionAndRestore(argv[1]);
			TestCombineSubtractAndColorOnly(argv[1]);
			TestMaskedCopiedRegion(argv[1]);
			TestMaskedCopiedRegion(argv[1], true);
			TestMaskedDarken(argv[1]);
			TestRecordsAndActualKeyBits(argv[1]);
			TestAbortAndGuardBoundaries(argv[1]);
		}
		std::cout << "actual mode1 R8 alpha targets/same-command-buffer union/combine/subtract/resolve, copied1148/1150 region pixels, real records/key5, clip restore and abort replay passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
