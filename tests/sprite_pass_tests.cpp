#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_grid_draw.hpp"
#include "sgre/scene/motion_textures.hpp"
#include "sgre/scene/texture_cache.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected rejected sprite input: " + std::string(message));
}

static std::array<sgre::scene::MotionSpriteVertex, 4> Quad(float u = 0.5f, float v = 0.5f) {
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-1, 1};
	vertices[1].Position = {1, 1};
	vertices[2].Position = {-1, -1};
	vertices[3].Position = {1, -1};
	for (auto& vertex : vertices) {
		vertex.Uv = {u, v};
		vertex.Color = 0x4080c080;
	}

	return vertices;
}

static constexpr std::array<std::uint32_t, 4> kStrip = {0, 2, 1, 3};
// Four original constant-buffer registers, with XY in .xy, translation in .w, and Z/W constant.
static constexpr std::array<float, 16> kProjection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5f, 0, 0, 0, 1};

static sgre::resource::RgbaImage Image(std::array<std::uint8_t, 4> pixel) {
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels.assign(pixel.begin(), pixel.end());

	return image;
}

static std::array<float, 4> ExpectedFragment(sgre::resource::PixelFormat format, unsigned color_mode, unsigned operation) {
	std::array<float, 4> value = {96.0f / 255, 160.0f / 255, 224.0f / 255, 128.0f / 255};
	if (format == sgre::resource::PixelFormat::Alpha8) {
		value = {1, 1, 1, 128.0f / 255};
	} else if (format == sgre::resource::PixelFormat::Luminance8) {
		value = {96.0f / 255, 96.0f / 255, 96.0f / 255, 1};
	}

	if (color_mode != 0) {
		const std::array<float, 4> color = {64.0f / 255, 128.0f / 255, 192.0f / 255, 128.0f / 255};
		for (std::size_t lane = 0; lane < 4; ++lane) {
			value[lane] *= color[lane];
			if (color_mode == 2 && lane < 3) {
				value[lane] *= 2;
			}
		}
	}

	for (std::size_t lane = 0; lane < 3; ++lane) {
		if (operation == 1) {
			value[lane] = 1 - value[lane];
		} else if (operation == 2) {
			value[lane] *= value[3];
		}
	}

	return value;
}

static void RequirePixels(const sgre::render::ColorPassPixels& pixels, const std::array<float, 4>& expected, int tolerance = 1) {
	Require(pixels.Rgba.size() == static_cast<std::size_t>(pixels.Width) * pixels.Height * 4, "sprite capture row packing is invalid");
	for (std::size_t offset = 0; offset < pixels.Rgba.size(); offset += 4) {
		for (std::size_t lane = 0; lane < 4; ++lane) {
			const auto target = static_cast<int>(std::lround(std::clamp(expected[lane], 0.0f, 1.0f) * 255));
			if (std::abs(static_cast<int>(pixels.Rgba[offset + lane]) - target) > tolerance) {
				throw std::runtime_error("sprite pixel " + std::to_string(offset / 4) + " lane " + std::to_string(lane) + " was " + std::to_string(pixels.Rgba[offset + lane]) + ", expected " + std::to_string(target));
			}
		}
	}
}

static sgre::render::ColorPassPixels Draw(sgre::render::SpritePassRenderer& renderer, sgre::render::SpriteTextureHandle texture, const sgre::scene::SpritePipeline& pipeline, std::span<const sgre::scene::MotionSpriteVertex> vertices, const std::array<float, 4>& clear = {}) {
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = renderer.PreparePipeline(pipeline);
	draw.Vertices = vertices;
	draw.Indices = kStrip;
	const auto submission = renderer.Submit(std::span(&draw, 1), kProjection, clear);

	return renderer.Capture(submission);
}

static void TestShaderOperations(sgre::render::SpritePassRenderer& renderer) {
	const std::array<sgre::resource::PixelFormat, 3> formats = {sgre::resource::PixelFormat::Bc7, sgre::resource::PixelFormat::Alpha8, sgre::resource::PixelFormat::Luminance8};
	const auto vertices = Quad();
	for (const auto format : formats) {
		const auto texture = renderer.UploadTexture(format, Image({96, 160, 224, 128}));
		for (unsigned color_mode = 0; color_mode < 3; ++color_mode) {
			for (unsigned operation = 0; operation < 3; ++operation) {
				sgre::scene::MotionSpriteCommand command;
				command.TextureFormat = format;
				command.TextureAttributes = color_mode == 0 ? 4 : 0;
				command.BlendMode = (color_mode == 2 ? 0x10u : 0u) | (operation == 1 ? 5u : operation == 2 ? 3u
				                                                                                           : 0u);
				sgre::scene::SpritePipelineOptions options;
				options.EnableBlending = false;
				const auto pipeline = sgre::scene::SelectSpritePipeline(command, options);
				RequirePixels(Draw(renderer, texture, pipeline, vertices), ExpectedFragment(format, color_mode, operation));
			}
		}

		renderer.ReleaseTexture(texture);
	}
}

static float BlendFactor(sgre::scene::SpriteBlendFactor factor, float source_alpha, float destination) {
	switch (factor) {
	case sgre::scene::SpriteBlendFactor::Zero:
		return 0;
	case sgre::scene::SpriteBlendFactor::One:
		return 1;
	case sgre::scene::SpriteBlendFactor::SourceAlpha:
		return source_alpha;
	case sgre::scene::SpriteBlendFactor::InverseSourceAlpha:
		return 1 - source_alpha;
	case sgre::scene::SpriteBlendFactor::DestinationColor:
		return destination;
	case sgre::scene::SpriteBlendFactor::InverseDestinationColor:
		return 1 - destination;
	}

	throw std::runtime_error("unknown blend fixture factor");
}

static void TestBlending(sgre::render::SpritePassRenderer& renderer) {
	const auto texture = renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, Image({96, 160, 224, 128}));
	const auto vertices = Quad();
	const std::array<float, 4> clear = {32.0f / 255, 64.0f / 255, 96.0f / 255, 160.0f / 255};
	for (std::uint32_t mode = 0; mode < 12; ++mode) {
		sgre::scene::MotionSpriteCommand command;
		command.BlendMode = mode;
		command.TextureAttributes = 4;
		sgre::scene::SpritePipelineOptions options;
		const auto pipeline = sgre::scene::SelectSpritePipeline(command, options);
		const auto source = ExpectedFragment(sgre::resource::PixelFormat::Bc7, 0, mode == 5 ? 1 : mode == 3 || mode == 4 ? 2
		                                                                                                                 : 0);
		auto expected = source;
		if (pipeline.Blend.Enabled) {
			for (std::size_t lane = 0; lane < 4; ++lane) {
				const bool alpha = lane == 3;
				const auto source_factor = BlendFactor(alpha ? pipeline.Blend.SourceAlpha : pipeline.Blend.SourceRgb, source[3], clear[lane]);
				const auto destination_factor = BlendFactor(alpha ? pipeline.Blend.DestinationAlpha : pipeline.Blend.DestinationRgb, source[3], clear[lane]);
				const auto source_term = source[lane] * source_factor;
				const auto destination_term = clear[lane] * destination_factor;
				const auto operation = alpha ? pipeline.Blend.AlphaOperation : pipeline.Blend.RgbOperation;
				expected[lane] = operation == sgre::scene::SpriteBlendOperation::ReverseSubtract ? destination_term - source_term : source_term + destination_term;
			}
		}

		RequirePixels(Draw(renderer, texture, pipeline, vertices, clear), expected);
		options.WriteColor = false;
		RequirePixels(Draw(renderer, texture, sgre::scene::SelectSpritePipeline(command, options), vertices, clear), clear, 0);
	}

	renderer.ReleaseTexture(texture);
}

static void TestSharedTextureCache(sgre::render::SpritePassRenderer& renderer) {
	sgre::scene::TextureCache cache([&](const auto format, const auto& image) {
		return renderer.UploadTexture(format, image);
	},
	    [&](const auto texture) {
		    renderer.ReleaseTexture(texture);
	    });
	sgre::resource::ImageTexture description;
	description.SerializedValueOffset = 100;
	description.Format = sgre::resource::PixelFormat::Alpha8;
	description.Width = 1;
	description.Height = 1;
	description.TruncatedWidth = 1;
	description.TruncatedHeight = 1;
	description.PixelRange.Size = 1;
	const std::array<std::uint8_t, 1> bytes{128};
	const auto first = cache.Acquire(11, description, bytes);
	const auto duplicate = cache.Acquire(11, description, {});
	const auto texture = cache.Get(first).Backend;
	Require(first == duplicate && cache.Get(first).References == 2, "native document/value reuse must share its actual uploaded Metal texture");
	sgre::scene::MotionSpriteCommand command;
	command.TextureFormat = description.Format;
	command.TextureAttributes = 4;
	command.BlendMode = 0;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = sgre::scene::SelectSpritePipeline(command, options);
	const auto vertices = Quad();
	RequirePixels(Draw(renderer, texture, pipeline, vertices), {1, 1, 1, 128.0f / 255}, 0);
	cache.ReleaseTexture(first);
	RequirePixels(Draw(renderer, cache.Get(duplicate).Backend, pipeline, vertices), {1, 1, 1, 128.0f / 255}, 0);
	cache.ReleaseTexture(duplicate);
	RequireError([&] {
		static_cast<void>(Draw(renderer, texture, pipeline, vertices));
	},
	    "stale");
	const auto replacement = cache.Acquire(11, description, bytes);
	Require(cache.Get(replacement).Backend.Index == texture.Index && cache.Get(replacement).Backend.Generation != texture.Generation, "final shared release must permit actual Metal texture slot reuse with a new generation");
	cache.Close();
}

static void TestMotionTextureSlots(sgre::render::SpritePassRenderer& renderer) {
	sgre::scene::TextureCache cache([&](const auto format, const auto& image) {
		return renderer.UploadTexture(format, image);
	},
	    [&](const auto texture) {
		    renderer.ReleaseTexture(texture);
	    });
	sgre::scene::MotionTextureSlots first(cache);
	sgre::scene::MotionTextureSlots second(cache);
	sgre::resource::ImageTexture description;
	description.SerializedValueOffset = 200;
	description.Format = sgre::resource::PixelFormat::Alpha8;
	description.Width = 1;
	description.Height = 1;
	description.TruncatedWidth = 1;
	description.TruncatedHeight = 1;
	description.PixelRange.Size = 1;
	const std::array<std::uint8_t, 1> bytes{128};
	const auto a = first.Import(22, description, bytes);
	const auto b = second.Import(22, description, {});
	const auto cached = first.Get(a).Texture;
	const auto backend = cache.Get(cached).Backend;
	Require(second.Get(b).Texture == cached && cache.Get(cached).References == 2, "two motion managers must share the actual uploaded Metal texture");

	sgre::scene::MotionSpriteCommand command;
	command.TextureFormat = description.Format;
	command.TextureAttributes = 4;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = sgre::scene::SelectSpritePipeline(command, options);
	RequirePixels(Draw(renderer, backend, pipeline, Quad()), {1, 1, 1, 128.0f / 255}, 0);
	first.Close();
	RequirePixels(Draw(renderer, cache.Get(second.Get(b).Texture).Backend, pipeline, Quad()), {1, 1, 1, 128.0f / 255}, 0);
	second.Release(b);
	RequireError([&] {
		static_cast<void>(Draw(renderer, backend, pipeline, Quad()));
	},
	    "stale");
}

static void TestSamplersAndBounds(sgre::render::SpritePassRenderer& renderer) {
	auto image = Image({255, 0, 0, 255});
	image.Width = 2;
	image.Pixels.insert(image.Pixels.end(), {0, 0, 255, 255});
	const auto texture = renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteCommand command;
	command.TextureAttributes = 4;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	options.Sampling = sgre::scene::TextureSampling::Linear;
	RequirePixels(Draw(renderer, texture, sgre::scene::SelectSpritePipeline(command, options), Quad()), {0.5f, 0, 0.5f, 1});
	options.Sampling = sgre::scene::TextureSampling::Nearest;
	RequirePixels(Draw(renderer, texture, sgre::scene::SelectSpritePipeline(command, options), Quad()), {0, 0, 1, 1}, 0);
	for (const auto address : {sgre::scene::TextureAddress::Clamp, sgre::scene::TextureAddress::Repeat, sgre::scene::TextureAddress::Mirror}) {
		options.AddressU = address;
		const auto expected = address == sgre::scene::TextureAddress::Repeat ? std::array<float, 4>{1, 0, 0, 1} : std::array<float, 4>{0, 0, 1, 1};
		RequirePixels(Draw(renderer, texture, sgre::scene::SelectSpritePipeline(command, options), Quad(1.25f)), expected, 0);
	}

	const auto pipeline = sgre::scene::SelectSpritePipeline(command, options);
	const auto first = renderer.PreparePipeline(pipeline);
	Require(first == renderer.PreparePipeline(pipeline), "identical sprite pipeline was compiled twice");
	auto invalid = pipeline;
	invalid.ColorFilter = sgre::scene::SpriteColorFilter::Add;
	RequireError([&] {
		static_cast<void>(renderer.PreparePipeline(invalid));
	},
	    "ordinary shader");
	auto vertices = Quad();
	auto shifted_projection = kProjection;
	shifted_projection[0] = 0.5f;
	shifted_projection[3] = 0.25f;
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = first;
	draw.Vertices = vertices;
	draw.Indices = kStrip;
	const auto submission = renderer.Submit(std::span(&draw, 1), shifted_projection, {0, 1, 0, 1});
	const auto pixels = renderer.Capture(submission);
	for (std::size_t row = 0; row < pixels.Height; ++row) {
		for (std::size_t column = 0; column < pixels.Width; ++column) {
			const auto offset = (row * pixels.Width + column) * 4;
			const bool covered = column >= 24 && column < 56;
			Require(pixels.Rgba[offset] == 0 && pixels.Rgba[offset + 1] == (covered ? 0 : 255) && pixels.Rgba[offset + 2] == (covered ? 255 : 0) && pixels.Rgba[offset + 3] == 255, "ordinary sprite raw projection registers or clip coverage differ");
		}
	}

	const std::array<std::uint32_t, 4> bad_indices = {0, 2, 4, 3};
	draw.Indices = bad_indices;
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "index");
	draw.Indices = kStrip;
	const std::array<std::uint32_t, 6> triangles = {0, 2, 1, 1, 2, 3};
	draw.Indices = triangles;
	draw.Topology = sgre::scene::MotionDrawTopology::TriangleList;
	RequirePixels(renderer.Capture(renderer.Submit(std::span(&draw, 1), kProjection)), {0, 0, 1, 1}, 0);
	draw.Indices = kStrip;
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "topology");
	draw.Topology = sgre::scene::MotionDrawTopology::TriangleStrip;
	draw.Pipeline.Owner = nullptr;
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "another renderer");
	draw.Pipeline = first;
	renderer.ReleaseTexture(texture);
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "stale");
	const auto replacement = renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	Require(replacement.Index == texture.Index && replacement.Generation != texture.Generation, "reused texture slot did not invalidate its old handle");
	renderer.ReleaseTexture(replacement);
}

static void TestRecoveredGridSubmission(sgre::render::SpritePassRenderer& renderer) {
	sgre::resource::MotionSource source;
	source.Name = "SyntheticAtlas";
	source.Texture.Format = sgre::resource::PixelFormat::Bc7;
	source.Texture.Width = 2;
	source.Texture.Height = 2;
	sgre::resource::MotionIcon icon;
	icon.Name = "SyntheticIcon";
	icon.Width = 2;
	icon.Height = 2;
	sgre::scene::MotionHierarchyState state;
	state.Attributes.Hidden = false;
	state.Attributes.Source = source.Name;
	state.Attributes.Icon = icon.Name;
	state.Attributes.Colors.fill(0x808080ff);
	state.Attributes.Deformation = sgre::scene::IdentityMotionBezierPatch();
	sgre::scene::PreparedMotionLayer layer;
	layer.MeshTransform = 1;
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 2;
	subdivision.Y = 2;
	std::array<sgre::resource::MotionPoint, 9> positions;
	std::array<sgre::scene::MotionSpriteVertex, 9> vertices;
	std::array<std::uint32_t, 24> indices;
	sgre::scene::MotionGridBuffers buffers;
	buffers.Positions = positions;
	buffers.Vertices = vertices;
	buffers.Indices = indices;
	const auto command = sgre::scene::BuildOwnMotionGridCommand(layer, state, source, icon, subdivision, buffers, 0, false);
	Require(command.has_value(), "visible local deformation did not produce grid geometry");
	auto image = Image({96, 160, 224, 128});
	image.Width = 2;
	image.Height = 2;
	image.Pixels = {96, 160, 224, 128, 96, 160, 224, 128, 96, 160, 224, 128, 96, 160, 224, 128};
	const auto texture = renderer.UploadTexture(source.Texture.Format, image);
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = renderer.PreparePipeline(sgre::scene::SelectSpritePipeline(command->Material, options));
	draw.Vertices = command->Vertices;
	draw.Indices = command->Indices;
	draw.Topology = command->Topology;
	auto projection = kProjection;
	projection[3] = -1;
	projection[5] = -1;
	projection[7] = 1;
	RequirePixels(renderer.Capture(renderer.Submit(std::span(&draw, 1), projection)), ExpectedFragment(source.Texture.Format, 0, 0), 0);
	renderer.ReleaseTexture(texture);
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("sprite_pass_tests SHADER_SOURCE");
		}

		sgre::render::SpritePassDescription description;
		description.Width = 64;
		description.Height = 32;
		description.MaxVertices = 16;
		description.MaxIndices = 64;
		description.MaxDraws = 4;
		description.MaxTextures = 4;
		description.MaxPipelines = 96;
		description.MaxTextureBytes = 64;
		sgre::render::SpritePassRenderer renderer(description, argv[1]);
		TestShaderOperations(renderer);
		TestBlending(renderer);
		TestSharedTextureCache(renderer);
		TestMotionTextureSlots(renderer);
		TestSamplersAndBounds(renderer);
		TestRecoveredGridSubmission(renderer);
		std::cout << "Metal sprite texture/color operations, twelve blend modes, samplers, register layout, and resource bounds passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
