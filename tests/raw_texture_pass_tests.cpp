#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/raw_texture_task.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}
static void RequireError(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("unsupported RawTex Metal operation accepted");
}
static sgre::render::SpritePassDescription Description(std::uint32_t dimension = 2) {
	sgre::render::SpritePassDescription description;
	description.Width = dimension;
	description.Height = dimension;
	description.MaxVertices = 16;
	description.MaxIndices = 16;
	description.MaxDraws = 4;
	description.MaxTextures = 4;
	description.MaxPipelines = 4;
	description.MaxTextureBytes = 4096;
	return description;
}
static sgre::scene::MutableTextureSpec Spec(std::uint32_t format) {
	sgre::scene::MutableTextureSpec spec;
	spec.Description.Width = 2;
	spec.Description.Height = 2;
	spec.Description.OriginalFormat = format;
	return spec;
}
static constexpr std::array<float, 16> kProjection{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0.5f, 0, 0, 0, 1};
static constexpr std::array<std::array<std::uint8_t, 4>, 4> kPixels{{{97, 157, 211, 173}, {31, 83, 229, 127}, {191, 47, 113, 223}, {71, 181, 59, 67}}};
static void Fill(sgre::scene::MutableTextureOwner& texture, std::uint32_t format, bool white = false) {
	const auto mapped = texture.Map();
	Require(mapped.Succeeded && mapped.Bytes.size() == 16, "RawTex Metal shadow mapping changed");
	for (std::size_t pixel = 0; pixel < 4; ++pixel) {
		for (std::size_t channel = 0; channel < 4; ++channel) {
			const auto source = format != 28 && channel != 1 && channel != 3 ? 2 - channel : channel;
			mapped.Bytes[pixel * 4 + channel] = static_cast<std::byte>(white ? 255 : kPixels[pixel][source]);
		}
	}
	texture.Unmap();
	texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
}
static sgre::render::SpritePipelineHandle RawPipeline(sgre::render::SpritePassRenderer& renderer, const sgre::scene::RawTextureDrawPacket& packet, std::uint32_t format) {
	sgre::scene::MotionShaderProgramInput input;
	input.Layout = sgre::scene::MotionVertexLayout::XyUv;
	input.Texture = sgre::scene::ClassifyMotionShaderTexture(true, format);
	input.TextureAttributes = packet.TextureAttributes;
	const auto selected = sgre::scene::SelectOrdinaryMotionShaderProgram(input);
	Require(selected.VertexIndex == 4, "original RawTex selector lost VS4");
	sgre::scene::SpritePipeline pipeline;
	pipeline.NativeVertexShaderIndex = selected.VertexIndex;
	pipeline.NativePixelShaderIndex = selected.PixelIndex;
	pipeline.VertexColor = (packet.TextureAttributes & 4) != 0 ? sgre::scene::SpriteVertexColor::Ignore : sgre::scene::SpriteVertexColor::Multiply;
	pipeline.Blend.Enabled = false;
	return renderer.PreparePipeline(pipeline);
}
static sgre::render::SpritePassDraw RawDraw(sgre::render::SpritePassRenderer& renderer, const sgre::scene::RawTextureDrawPacket& packet, std::uint32_t format) {
	sgre::render::SpritePassDraw draw;
	draw.Texture = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	draw.Pipeline = RawPipeline(renderer, packet, format);
	draw.RawTextureVertices = packet.Vertices;
	draw.RawTextureColor = packet.Color;
	return draw;
}
static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::size_t pixel, std::array<std::uint8_t, 4> expected, int tolerance = 0) {
	Require(pixel < pixels.Rgba.size() / 4, "RawTex capture pixel exceeds its bounds");
	for (std::size_t channel = 0; channel < 4; ++channel) {
		const auto actual = pixels.Rgba[pixel * 4 + channel];
		Require(std::abs(static_cast<int>(actual) - expected[channel]) <= tolerance, "RawTex Metal pixel/channel differs: " + std::to_string(pixel) + "/" + std::to_string(channel) + " got " + std::to_string(actual));
	}
}
static void TestPacketChannelsAndUniform(const std::filesystem::path& shader) {
	for (const std::uint32_t format : std::array<std::uint32_t, 3>{28, 87, 88}) {
		sgre::render::SpritePassRenderer renderer(Description(), shader);
		auto backend = renderer.CreateMutableTextureBackend();
		sgre::scene::MutableTextureOwner texture(*backend, Spec(format));
		Fill(texture, format);
		for (const std::uint32_t color : std::array<std::uint32_t, 2>{0xffffffff, 0x80123456}) {
			const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, color);
			const auto draw = RawDraw(renderer, packet, format);
			const auto pixels = renderer.Capture(renderer.Submit(std::span(&draw, 1), kProjection));
			const std::array<std::uint32_t, 4> factors{(color >> 16) & 255, (color >> 8) & 255, color & 255, color >> 24};
			for (std::size_t pixel = 0; pixel < 4; ++pixel) {
				auto expected = kPixels[pixel];
				if (format == 88) {
					expected[3] = 255;
				}
				for (std::size_t channel = 0; channel < 4; ++channel) {
					expected[channel] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(expected[channel]) * factors[channel] + 127) / 255);
				}
				RequirePixel(pixels, pixel, expected, color == 0xffffffff ? 0 : 1);
			}
		}
	}
}
static void TestMatrixAndCoverage(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(4), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::scene::MutableTextureOwner texture(*backend, Spec(28));
	Fill(texture, 28);
	const auto packet = sgre::scene::PrepareRawTextureDrawPacket({0, 0}, {1, 1}, 0xffffffff);
	const auto draw = RawDraw(renderer, packet, 28);
	auto matrix = kProjection;
	matrix[3] = -0.5f;
	matrix[7] = 0.5f;
	const auto pixels = renderer.Capture(renderer.Submit(std::span(&draw, 1), matrix, {1, 0, 1, 1}));
	for (std::size_t y = 0; y < 4; ++y) {
		for (std::size_t x = 0; x < 4; ++x) {
			if (x >= 1 && x <= 2 && y >= 1 && y <= 2) {
				RequirePixel(pixels, y * 4 + x, kPixels[(y - 1) * 2 + x - 1]);
			} else {
				RequirePixel(pixels, y * 4 + x, {255, 0, 255, 255});
			}
		}
	}
}
static void TestMixedLayoutsAndColorSnapshots(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::scene::MutableTextureOwner texture(*backend, Spec(28));
	Fill(texture, 28, true);
	const auto left = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {1, 2}, 0xffff0000);
	const auto top_right = sgre::scene::PrepareRawTextureDrawPacket({0, -1}, {1, 1}, 0xff00ff00);
	std::array<sgre::scene::MotionSpriteVertex, 4> ordinary;
	ordinary[0].Position = {0, -1};
	ordinary[1].Position = {0, 1};
	ordinary[2].Position = {1, -1};
	ordinary[3].Position = {1, 1};
	for (auto& vertex : ordinary) {
		vertex.Color = 0x0000ffff;
		vertex.Uv = {0.5f, 0.5f};
	}
	const std::array<std::uint32_t, 4> indices{0, 1, 2, 3};
	sgre::scene::SpritePipeline ordinary_pipeline;
	ordinary_pipeline.VertexColor = sgre::scene::SpriteVertexColor::Multiply;
	ordinary_pipeline.NativePixelShaderIndex = 240;
	ordinary_pipeline.Blend.Enabled = false;
	sgre::render::SpritePassDraw ordinary_draw;
	ordinary_draw.Texture = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	ordinary_draw.Pipeline = renderer.PreparePipeline(ordinary_pipeline);
	ordinary_draw.Vertices = ordinary;
	ordinary_draw.Indices = indices;
	const std::array<sgre::render::SpritePassDraw, 3> draws{RawDraw(renderer, left, 28), ordinary_draw, RawDraw(renderer, top_right, 28)};
	Require(draws[0].Pipeline.Index != draws[1].Pipeline.Index, "RawTex and ordinary vertex interfaces shared an incompatible pipeline cache entry");
	const auto pixels = renderer.Capture(renderer.Submit(draws, kProjection));
	RequirePixel(pixels, 0, {255, 0, 0, 255});
	RequirePixel(pixels, 1, {0, 255, 0, 255});
	RequirePixel(pixels, 2, {255, 0, 0, 255});
	RequirePixel(pixels, 3, {0, 0, 255, 255});
}
static void TestLayoutGuards(const std::filesystem::path& shader, const std::filesystem::path& ordinary_shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::scene::MutableTextureOwner texture(*backend, Spec(28));
	Fill(texture, 28, true);
	const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, 0xffffffff);
	auto draw = RawDraw(renderer, packet, 28);
	const std::array<std::uint32_t, 4> indices{0, 1, 2, 3};
	draw.Indices = indices;
	RequireError([&]() {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "nonindexed");
	draw.Indices = {};
	draw.RawTextureVertices = std::span(packet.Vertices).first(3);
	RequireError([&]() {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "four XYUV");
	draw.RawTextureVertices = packet.Vertices;
	draw.Topology = sgre::scene::MotionDrawTopology::TriangleList;
	RequireError([&]() {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "nonindexed");
	sgre::render::SpritePassRenderer old_library(Description(), ordinary_shader);
	RequireError([&]() {
		static_cast<void>(RawPipeline(old_library, packet, 28));
	},
	    "vertex shader is unavailable");
}
class ShaderFixture {
public:
	explicit ShaderFixture(std::filesystem::path path) : m_Path(std::move(path)) {
		Require(!std::filesystem::exists(m_Path), "RawTex negative shader fixture must not overwrite an existing file");
	}
	ShaderFixture(const ShaderFixture&) = delete;
	ShaderFixture& operator=(const ShaderFixture&) = delete;
	~ShaderFixture() {
		if (m_Created) {
			std::error_code error;
			std::filesystem::remove(m_Path, error);
		}
	}
	void WriteWithoutRawVertex(const std::filesystem::path& source) {
		std::ifstream input(source, std::ios::binary | std::ios::ate);
		const auto byte_count = input.tellg();
		Require(input && byte_count > 0 && byte_count <= 64 * 1024, "RawTex fixture shader exceeds its checked length");
		std::string text(static_cast<std::size_t>(byte_count), '\0');
		input.seekg(0);
		Require(static_cast<bool>(input.read(text.data(), static_cast<std::streamsize>(text.size()))), "RawTex fixture shader read failed");
		const auto start = text.find("struct RawTextureVertexInput");
		const auto end = text.find("static metal::float4 InterpretTexture", start);
		Require(start != std::string::npos && end != std::string::npos && end > start, "RawTex fixture shader lacks the authored vertex block");
		text.erase(start, end - start);
		std::ofstream output(m_Path, std::ios::binary);
		Require(static_cast<bool>(output), "RawTex fixture shader creation failed");
		m_Created = true;
		Require(static_cast<bool>(output.write(text.data(), static_cast<std::streamsize>(text.size()))), "RawTex fixture shader write failed");
	}

private:
	std::filesystem::path m_Path;
	bool m_Created = false;
};
static void TestComposedRawTask(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::scene::RawTextureTask task(*backend, {2, 2});
	std::array<std::uint8_t, 12> rgb;
	for (std::size_t pixel = 0; pixel < 4; ++pixel) {
		for (std::size_t channel = 0; channel < 3; ++channel) {
			rgb[pixel * 3 + channel] = kPixels[pixel][channel];
		}
	}
	task.RestoreRgb(rgb);
	task.SetCoordinate({-1, -1});
	task.SetColor({0.5f, 0.25f, 1});
	task.SetOpacity(128);
	task.SetDrawPaused(false);
	task.Texture().Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto packet = task.PrepareDrawPacket();
	Require(packet.Color == 0x807f3fff, "composed RawTex task lost native color truncation/low-byte opacity");
	const auto draw = RawDraw(renderer, packet, task.Texture().State().Description.OriginalFormat);
	const auto submission = renderer.Submit(std::span(&draw, 1), kProjection);
	task.Close();
	RequireError([&]() {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "stale");
	const auto pixels = renderer.Capture(submission);
	for (std::size_t pixel = 0; pixel < 4; ++pixel) {
		const std::array<std::uint8_t, 4> expected{static_cast<std::uint8_t>((std::uint32_t{kPixels[pixel][0]} * 127 + 127) / 255), static_cast<std::uint8_t>((std::uint32_t{kPixels[pixel][1]} * 63 + 127) / 255), kPixels[pixel][2], 128};
		RequirePixel(pixels, pixel, expected, 1);
	}
}
int main(int argc, char** argv) {
	try {
		if (argc != 3) {
			throw std::runtime_error("expected readable Metal shader and nonexistent ignored fixture output paths");
		}
		const std::filesystem::path shader(argv[1]);
		const std::filesystem::path fixture_path(argv[2]);
		ShaderFixture fixture(fixture_path);
		fixture.WriteWithoutRawVertex(shader);
		TestPacketChannelsAndUniform(shader);
		TestMatrixAndCoverage(shader);
		TestMixedLayoutsAndColorSnapshots(shader);
		TestLayoutGuards(shader, fixture_path);
		TestComposedRawTask(shader);
		std::cout << "RawTex VS4 XYUV/nonindexed strip, CB0 color/matrix, mixed interfaces and layout guards passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
