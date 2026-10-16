#include "sgre/render/sprite_render_records.hpp"

#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, const char* fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find(fragment) != std::string::npos, error.what());

		return;
	}

	throw std::runtime_error("missing Picture record rejection");
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 8;
	result.Height = 8;
	result.MaxVertices = 64;
	result.MaxIndices = 64;
	result.MaxDraws = 16;
	result.MaxTextures = 8;
	result.MaxPipelines = 64;
	result.MaxTextureBytes = 4096;

	return result;
}

static sgre::scene::RawTextureDrawPacket Packet(std::uint32_t opacity) {
	sgre::scene::RawTextureDrawPacket packet;
	packet.Vertices[0].Position = {-4, -4};
	packet.Vertices[1].Position = {-4, 4};
	packet.Vertices[2].Position = {4, -4};
	packet.Vertices[3].Position = {4, 4};
	packet.Vertices[0].Uv = {0, 0};
	packet.Vertices[1].Uv = {0, 1};
	packet.Vertices[2].Uv = {1, 0};
	packet.Vertices[3].Uv = {1, 1};
	packet.Color = ((opacity & 255u) << 24) | 0x00ffffffu;
	packet.TextureAttributes = opacity == 255 ? 4u : 0u;

	return packet;
}

static sgre::scene::SpritePipeline Pipeline(sgre::resource::PixelFormat format, std::uint32_t opacity, std::uint32_t filter) {
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = format;
	material.TextureAttributes = opacity == 255 ? 4u : 0u;
	material.BlendMode = 0;
	sgre::scene::SpritePipelineOptions options;
	options.Sampling = filter == 21 ? sgre::scene::TextureSampling::Linear : sgre::scene::TextureSampling::Nearest;
	options.AddressU = sgre::scene::TextureAddress::Mirror;
	options.AddressV = sgre::scene::TextureAddress::Mirror;
	auto result = sgre::scene::SelectSpritePipeline(material, options);
	result.NativeVertexShaderIndex = 4;
	if (filter == 4) {
		result.Sampler.Magnification = sgre::scene::TextureSampling::Linear;
		result.Sampler.NativeFilterCode = 4;
	}

	return result;
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::size_t x, std::size_t y, const std::array<int, 4>& expected) {
	const auto start = (y * pixels.Width + x) * 4;
	for (std::size_t channel = 0; channel < expected.size(); ++channel) {
		if (std::abs(static_cast<int>(pixels.Rgba[start + channel]) - expected[channel]) > 1) {
			throw std::runtime_error("Picture pixel channel " + std::to_string(channel) + " expected " + std::to_string(expected[channel]) + " received " + std::to_string(pixels.Rgba[start + channel]));
		}
	}
}

static void TestActualRecords(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(8);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {64, 128, 192, 128};
	const std::array<sgre::resource::PixelFormat, 3> formats{sgre::resource::PixelFormat::Bc7, sgre::resource::PixelFormat::Alpha8, sgre::resource::PixelFormat::Luminance8};
	const std::array<std::array<int, 4>, 3> opaque{{{32, 64, 96, 255}, {128, 128, 128, 255}, {64, 64, 64, 255}}};
	const std::array<std::array<int, 4>, 3> translucent{{{16, 32, 48, 255}, {64, 64, 64, 255}, {32, 32, 32, 255}}};
	for (std::size_t format = 0; format < formats.size(); ++format) {
		const auto texture = renderer->UploadTexture(formats[format], image);
		for (const auto filter : {0u, 4u, 21u}) {
			for (const auto opacity : {255u, 128u, 0x1ffu}) {
				const auto pipeline = renderer->PreparePipeline(Pipeline(formats[format], opacity, filter));
				const auto packet = Packet(opacity);
				records.BeginFrame({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
				records.BeginRecord();
				records.SetPictureBlend(0);
				records.AppendPicture(texture, pipeline, packet, opacity);
				const auto state = records.CurrentRecord();
				Require(state.TextureAttributes == packet.TextureAttributes && state.UniformArgb == packet.Color, "Picture DWORD-opacity attributes/uniform changed in the actual record");
				records.EndRecord();
				const auto pixels = renderer->Capture(records.EndFrame());
				RequirePixel(pixels, 3, 3, opacity == 128 ? translucent[format] : opaque[format]);
				Require(statistics.Current().DrawOperations == 1 && statistics.Current().SubmittedElements == 4, "Picture must submit exactly one genuine four-vertex nonindexed strip");
			}
		}
		renderer->ReleaseTexture(texture);
	}

	image.Width = 2;
	image.Pixels = {0, 0, 0, 255, 255, 255, 255, 255};
	const auto checker = renderer->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	std::array<sgre::render::SpritePipelineHandle, 3> filters;
	std::size_t index = 0;
	for (const auto filter : {0u, 4u, 21u}) {
		filters[index++] = renderer->PreparePipeline(Pipeline(sgre::resource::PixelFormat::Bc7, 255, filter));
	}
	Require(filters[0] != filters[1] && filters[1] != filters[2] && filters[0] != filters[2], "Picture mixed sampler aliased the prepared nearest/linear pipeline key");
	for (std::size_t filter = 0; filter < filters.size(); ++filter) {
		records.BeginFrame({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
		records.BeginRecord();
		records.SetPictureBlend(0);
		records.AppendPicture(checker, filters[filter], Packet(255), 255);
		records.EndRecord();
		const auto pixels = renderer->Capture(records.EndFrame());
		const auto expected = filter == 0 ? 0 : 96;
		RequirePixel(pixels, 3, 3, {expected, expected, expected, 255});
	}

	records.BeginFrame({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	records.BeginRecord();
	RequireFailure([&] {
		records.SetBlend(6, 0);
	},
	    "measured blend");
	RequireFailure([&] {
		records.SetPictureBlend(11);
	},
	    "rows0..10");
	records.SetPictureBlend(6);
	RequireFailure([&] {
		records.AppendRaw(checker, filters[0], Packet(255));
	},
	    "measured blend");
	records.SetPictureBlend(0);
	RequireFailure([&] {
		records.AppendRaw(checker, filters[0], Packet(255));
	},
	    "mutable RGBA");
	RequireFailure([&] {
		records.AppendMovieRaw(checker, filters[0], Packet(255));
	},
	    "attributes differ");
	RequireFailure([&] {
		records.AppendPicture(checker, filters[0], Packet(255), 0x1ff);
	},
	    "DWORD opacity");
	auto invalid = Packet(255);
	invalid.Vertices[0].Position.X = std::numeric_limits<float>::quiet_NaN();
	RequireFailure([&] {
		records.AppendPicture(checker, filters[0], invalid, 255);
	},
	    "finite actual XY/UV");
	records.AppendPicture(checker, filters[0], Packet(255), 255);
	records.AbortFrame();
	Require(!records.CompletedSubmission(), "aborted Picture frame published replacement pixels");
	records.BeginFrame({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	records.BeginRecord();
	records.SetPictureBlend(0);
	records.AppendPicture(checker, filters[0], Packet(255), 255);
	records.EndRecord();
	static_cast<void>(records.EndFrame());
	renderer->ReleaseTexture(checker);
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("Picture records require the exact Metal shader path");
		}
		TestActualRecords(argv[1]);
		std::cout << "Picture: native format2 Image channels, opacity, distinct mixed samplers, rejection and abort recovery passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
