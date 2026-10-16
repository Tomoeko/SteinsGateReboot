#include "sgre/render/frame_host_heap.hpp"
#include "sgre/render/frame_statistics.hpp"
#include "sgre/render/graphics_default_state.hpp"
#include "sgre/render/sprite_graphics_camera.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/render/sprite_host_inputs.hpp"
#include "sgre/resource/image.hpp"
#include "sgre/scene/graphics_model_stack.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::uint32_t Word(std::istream& input) {
	std::uint32_t value = 0;
	for (std::uint32_t byte = 0; byte < 4; ++byte) {
		const auto character = input.get();
		Require(character != std::char_traits<char>::eof(), "short native graphics default-state fixture");
		value |= static_cast<std::uint32_t>(static_cast<unsigned char>(character)) << (byte * 8);
	}

	return value;
}

static std::uint32_t NativeBlendFactor(sgre::scene::SpriteBlendFactor value) {
	switch (value) {
	case sgre::scene::SpriteBlendFactor::Zero:
		return 1;
	case sgre::scene::SpriteBlendFactor::One:
		return 2;
	default:
		throw std::runtime_error("default-state fixture encountered an unobserved blend factor");
	}
}

static void ReplayStates(const std::filesystem::path& fixture) {
	std::ifstream input(fixture, std::ios::binary);
	Require(input.is_open() && Word(input) == 0x31535244 && Word(input) == 16, "wrong bounded native graphics state fixture");
	for (std::uint32_t case_index = 0; case_index < 16; ++case_index) {
		const auto failure_mask = Word(input);
		const auto positive_success = Word(input);
		const auto event_count = Word(input);
		Require(failure_mask < 8 && positive_success <= 1 && event_count <= 9, "native default-state case exceeds its observed event bound");
		const auto state = sgre::render::PrepareDefaultGraphicsRenderState();
		Require(Word(input) == static_cast<std::uint32_t>(state.AlphaToCoverageEnabled) && Word(input) == static_cast<std::uint32_t>(state.IndependentBlendEnabled), "default blend header differs from original");
		Require(Word(input) == static_cast<std::uint32_t>(state.Blend.Enabled), "default blend enable differs from original");
		Require(Word(input) == NativeBlendFactor(state.Blend.SourceRgb) && Word(input) == NativeBlendFactor(state.Blend.DestinationRgb), "default RGB blend factors differ from original");
		Require(state.Blend.RgbOperation == sgre::scene::SpriteBlendOperation::Add && Word(input) == 1, "default RGB blend operation differs from original");
		Require(Word(input) == NativeBlendFactor(state.Blend.SourceAlpha) && Word(input) == NativeBlendFactor(state.Blend.DestinationAlpha), "default alpha blend factors differ from original");
		Require(state.Blend.AlphaOperation == sgre::scene::SpriteBlendOperation::Add && Word(input) == 1, "default alpha blend operation differs from original");
		Require(input.get() == state.Blend.WriteMask, "default color-write mask differs from original");
		for (std::size_t unused = 37; unused < 264; ++unused) {
			Require(input.get() == 0, "unused independent-blend records or padding changed");
		}
		Require(Word(input) == static_cast<std::uint32_t>(state.Depth.DepthEnabled) && Word(input) == static_cast<std::uint32_t>(state.Depth.DepthWriteEnabled) && Word(input) == state.Depth.Comparison && Word(input) == static_cast<std::uint32_t>(state.Depth.StencilEnabled), "default depth state differs from original");
		Require(input.get() == state.Depth.StencilReadMask && input.get() == state.Depth.StencilWriteMask && input.get() == 0 && input.get() == 0, "default stencil masks or padding differ from original");
		for (const auto face_word : state.Depth.StencilFaceWords) {
			Require(Word(input) == face_word, "disabled default stencil face words differ from original");
		}
		Require(state.Raster.Fill == sgre::render::GraphicsRasterFill::Solid && Word(input) == 3 && state.Raster.Cull == sgre::render::GraphicsRasterCull::None && Word(input) == 1, "default raster fill/cull differs from original");
		Require(Word(input) == static_cast<std::uint32_t>(state.Raster.FrontCounterClockwise) && Word(input) == static_cast<std::uint32_t>(state.Raster.DepthBias), "default winding/depth bias differs from original");
		Require(Word(input) == std::bit_cast<std::uint32_t>(state.Raster.DepthBiasClamp) && Word(input) == std::bit_cast<std::uint32_t>(state.Raster.SlopeScaledDepthBias), "default depth-bias float words differ from original");
		Require(Word(input) == static_cast<std::uint32_t>(state.Raster.DepthClipEnabled) && Word(input) == static_cast<std::uint32_t>(state.Raster.ScissorEnabled) && Word(input) == static_cast<std::uint32_t>(state.Raster.MultisampleEnabled) && Word(input) == static_cast<std::uint32_t>(state.Raster.AntialiasedLineEnabled), "default raster flags differ from original");
		Require(state.BlendFactors == std::array<float, 4>{} && state.SampleMask == 0xffffffff && state.Depth.StencilReference == 0, "default bound blend factors/sample mask/stencil reference differ from native observer arguments");
		std::uint32_t observed_events = 0;
		for (std::uint32_t kind = 0; kind < 3; ++kind) {
			Require(Word(input) == 1 && Word(input) == kind && Word(input) == static_cast<std::uint32_t>((failure_mask & (1u << kind)) != 0), "native default state did not preserve creation order");
			++observed_events;
			if ((failure_mask & (1u << kind)) == 0) {
				Require(Word(input) == 2 && Word(input) == kind && Word(input) == 0, "native state bind did not depend on its own successful creation");
				++observed_events;
			}
		}
		for (std::uint32_t offset = 0; offset < 3; ++offset) {
			const auto kind = 2 - offset;
			if ((failure_mask & (1u << kind)) == 0) {
				Require(Word(input) == 3 && Word(input) == kind && Word(input) == 0, "native local-state release order changed");
				++observed_events;
			}
		}
		Require(observed_events == event_count, "native default-state event payload changed");
	}
	Require(input.peek() == std::char_traits<char>::eof(), "extra native default-state fixture bytes");
}

static std::uint32_t CachedWord(const std::array<std::uint8_t, 96>& bytes, std::size_t offset) {
	Require(offset <= bytes.size() && 4 <= bytes.size() - offset, "native pipeline cache word exceeds its bounded row");
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return value;
}

static void ReplayCounters(const std::filesystem::path& fixture) {
	std::ifstream input(fixture, std::ios::binary);
	Require(input.is_open() && Word(input) == 0x31435047 && Word(input) == 128, "wrong bounded pipeline reset fixture");
	for (std::size_t index = 0; index < 128; ++index) {
		std::array<std::uint8_t, 96> before;
		std::array<std::uint8_t, 96> after;
		for (auto& byte : before) {
			const auto value = input.get();
			Require(value != std::char_traits<char>::eof(), "short initial pipeline cache observation");
			byte = static_cast<std::uint8_t>(value);
		}
		for (auto& byte : after) {
			const auto value = input.get();
			Require(value != std::char_traits<char>::eof(), "short final pipeline cache observation");
			byte = static_cast<std::uint8_t>(value);
		}
		sgre::render::RenderFrameCounters current;
		current.DrawOperations = CachedWord(before, 0x4c);
		current.SubmittedElements = CachedWord(before, 0x54);
		sgre::render::RenderFrameCounters previous;
		previous.DrawOperations = CachedWord(before, 0x50);
		previous.SubmittedElements = CachedWord(before, 0x58);
		sgre::render::RenderFrameStatistics statistics(current, previous);
		statistics.BeginFrame();
		Require(statistics.Current().DrawOperations == CachedWord(after, 0x4c) && statistics.Current().SubmittedElements == CachedWord(after, 0x54) && statistics.Previous().DrawOperations == CachedWord(after, 0x50) && statistics.Previous().SubmittedElements == CachedWord(after, 0x58), "actual statistics owner differs from native pipeline reset");
	}
	Require(input.peek() == std::char_traits<char>::eof(), "extra native pipeline reset fixture bytes");
}

static std::array<sgre::scene::MotionSpriteVertex, 4> Vertices(std::uint32_t color) {
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-2, -2};
	vertices[1].Position = {2, -2};
	vertices[2].Position = {-2, 2};
	vertices[3].Position = {2, 2};
	for (auto& vertex : vertices) {
		vertex.Color = color;
		vertex.Uv = {0, 0};
	}

	return vertices;
}

static void TestActualRaster(const std::filesystem::path& shader) {
	sgre::render::SpritePassDescription description;
	description.Width = 4;
	description.Height = 4;
	description.MaxVertices = 8;
	description.MaxIndices = 12;
	description.MaxDraws = 2;
	description.MaxTextures = 1;
	description.MaxPipelines = 1;
	description.MaxTextureBytes = 4;
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 2);
	sgre::scene::GraphicsModelStack models(4);
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	sgre::render::FrameHostHeap heap(2);
	heap.Begin();
	static_cast<void>(graphics.SelectTarget(nullptr));
	const auto target = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(target));
	models.Reset();
	camera.ResetDefaultCamera();
	const auto projection = camera.OrdinaryShaderRegisters();
	Require(projection[11] < 0, "actual default camera no longer supplies the discriminating negative depth");
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 255, 255, 255};
	const auto texture = target->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	auto pipeline_value = sgre::scene::SelectSpritePipeline(material, options);
	pipeline_value.Blend = sgre::render::PrepareDefaultGraphicsRenderState().Blend;
	const auto pipeline = target->PreparePipeline(pipeline_value);
	const auto red = Vertices(0xff0000ff);
	const auto blue = Vertices(0x0000ffff);
	const std::array<std::uint32_t, 6> clockwise{0, 1, 2, 2, 1, 3};
	const std::array<std::uint32_t, 6> counterclockwise{2, 1, 0, 3, 1, 2};
	sgre::render::SpriteHostInputs red_inputs(heap, red, clockwise);
	sgre::render::SpriteHostInputs blue_inputs(heap, blue, counterclockwise);
	std::array<sgre::render::SpritePassDraw, 2> draws;
	for (auto& draw : draws) {
		draw.Texture = texture;
		draw.Pipeline = pipeline;
		draw.Topology = sgre::scene::MotionDrawTopology::TriangleList;
	}
	draws[0].HostInputs = &red_inputs;
	draws[0].Scissor = sgre::render::SpritePassScissor{0, 0, 2, 4};
	draws[1].HostInputs = &blue_inputs;
	draws[1].Scissor = sgre::render::SpritePassScissor{2, 0, 2, 4};
	const auto submission = graphics.Submit(draws, projection);
	Require(submission.Owner == target.get(), "actual raster draw submitted through a foreign target owner");
	heap.End();
	const auto pixels = target->Capture(submission.Identity);
	Require(pixels.Width == 4 && pixels.Height == 4 && pixels.Rgba.size() == 64, "wrong actual raster verification target");
	for (std::size_t y = 0; y < 4; ++y) {
		for (std::size_t x = 0; x < 4; ++x) {
			const auto offset = (y * 4 + x) * 4;
			Require(pixels.Rgba[offset] == (x < 2 ? 255 : 0) && pixels.Rgba[offset + 1] == 0 && pixels.Rgba[offset + 2] == (x < 2 ? 0 : 255) && pixels.Rgba[offset + 3] == 255, "negative-depth/opposite-winding/scissored default raster output differs");
		}
	}
	heap.Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 4, "usage: graphics_default_state_tests state_fixture cache_fixture shader");
		ReplayStates(argv[1]);
		ReplayCounters(argv[2]);
		TestActualRaster(argv[3]);
		std::cout << "PASS16 original default states/128 resets and16 actual negative-depth winding/scissor pixels\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
