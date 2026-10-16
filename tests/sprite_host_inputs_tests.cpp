#include "sgre/render/sprite_host_inputs.hpp"
#include "sgre/render/sprite_pass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const auto& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("expired or malformed real host input was accepted");
}

static std::array<sgre::scene::MotionSpriteVertex, 4> Vertices(std::uint32_t color) {
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-2, -2};
	vertices[1].Position = {2, -2};
	vertices[2].Position = {-2, 2};
	vertices[3].Position = {2, 2};
	for (auto& vertex : vertices) {
		vertex.Color = color;
		vertex.Uv = {.5f, .5f};
	}

	return vertices;
}

static std::vector<std::uint32_t> Indices(std::size_t count) {
	const std::array<std::uint32_t, 4> quad{0, 2, 1, 3};
	std::vector<std::uint32_t> indices(count);
	for (std::size_t index = 0; index < count; ++index) {
		indices[index] = quad[index % quad.size()];
	}

	return indices;
}

static std::uint64_t ReadInteger(std::istream& input, std::size_t width) {
	std::uint64_t result = 0;
	for (std::size_t index = 0; index < width; ++index) {
		const auto value = input.get();
		Require(value != std::char_traits<char>::eof(), "short bounded native paged input fixture");
		result |= static_cast<std::uint64_t>(static_cast<unsigned char>(value)) << (index * 8);
	}

	return result;
}

static void CompareBytes(std::istream& input, std::span<const std::byte> bytes) {
	for (const auto value : bytes) {
		Require(ReadInteger(input, 1) == std::to_integer<std::uint8_t>(value), "actual host input differs from native CreateBuffer's initial bytes");
	}
}

static void Replay(const std::filesystem::path& fixture) {
	std::ifstream input(fixture, std::ios::binary);
	Require(input.is_open() && ReadInteger(input, 4) == 0x31495350 && ReadInteger(input, 4) == 4, "wrong bounded paged input native fixture");
	sgre::render::FrameHostHeap heap(2);
	const auto vertices = Vertices(0xff00ffffu);
	const std::array<std::size_t, 4> counts{4, 7, 32767, 32768};
	std::size_t compared_bytes = 0;
	for (std::size_t case_index = 0; case_index < counts.size(); ++case_index) {
		Require(ReadInteger(input, 4) == case_index && ReadInteger(input, 4) == 4 && ReadInteger(input, 4) == counts[case_index], "native paged fixture count/order changed");
		const auto width = ReadInteger(input, 4);
		const auto entry_byte = ReadInteger(input, 4);
		Require(ReadInteger(input, 4) == 80, "native format4 vertex stride changed");
		const auto used = ReadInteger(input, 8);
		heap.Begin();
		const auto indices = Indices(counts[case_index]);
		sgre::render::SpriteHostInputs actual(heap, vertices, indices);
		if ((case_index & 1u) != 0) {
			heap.End();
		}
		Require(actual.VertexCount() == 4 && actual.IndexCount() == counts[case_index] && actual.IndexWordBytes() == width && heap.ResourceEntryByte() == entry_byte && heap.CaptureUsedBytes() == used, "actual allocation/count/entry retention differs from native");
		CompareBytes(input, actual.VertexBytes());
		CompareBytes(input, actual.IndexBytes());
		compared_bytes += actual.VertexBytes().size() + actual.IndexBytes().size();
	}
	Require(input.peek() == std::char_traits<char>::eof() && compared_bytes == 196948, "native paged input fixture boundary changed");
}

static void TestBoundsAndLifetime() {
	const auto vertices = Vertices(0xff00ffffu);
	const auto indices = Indices(4);
	sgre::render::FrameHostHeap heap(1);
	const auto before = heap.CaptureUsedBytes();
	const std::array<std::uint32_t, 3> invalid{0, 1, 4};
	RequireFailure([&] {
		sgre::render::SpriteHostInputs input(heap, vertices, invalid);
	});
	Require(heap.CaptureUsedBytes() == before, "bad index changed host allocation before rejection");
	auto nonfinite = vertices;
	nonfinite[0].Position.X = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		sgre::render::SpriteHostInputs input(heap, nonfinite, indices);
	});
	Require(heap.CaptureUsedBytes() == before, "bad float changed host allocation before rejection");
	sgre::render::SpriteHostInputs input(heap, vertices, indices);
	heap.End();
	Require(input.VertexCount() == 4 && heap.ResourceEntryByte() == 1, "End invented a host-input readiness rejection");
	heap.Begin();
	RequireFailure([&] {
		static_cast<void>(input.IndexBytes());
	});
	sgre::render::SpriteHostInputs replacement(heap, vertices, indices);
	heap.Close();
	RequireFailure([&] {
		static_cast<void>(replacement.VertexCount());
	});
}

static void TestMetalLifetime(const std::filesystem::path& shader) {
	sgre::render::SpritePassDescription description;
	description.Width = 4;
	description.Height = 4;
	description.MaxVertices = 8;
	description.MaxIndices = 65536;
	description.MaxDraws = 2;
	description.MaxTextures = 1;
	description.MaxPipelines = 1;
	description.MaxTextureBytes = 4;
	sgre::render::SpritePassRenderer renderer(description, shader);
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 255, 255, 255};
	const auto texture = renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = renderer.PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	sgre::render::FrameHostHeap heap(2);
	heap.Begin();
	const auto vertices = Vertices(0x3f7fff80u);
	const auto narrow_indices = Indices(7);
	const auto wide_indices = Indices(32768);
	sgre::render::SpriteHostInputs narrow(heap, vertices, narrow_indices);
	sgre::render::SpriteHostInputs wide(heap, vertices, wide_indices);
	std::array<sgre::render::SpritePassDraw, 2> draws;
	for (auto& draw : draws) {
		draw.Texture = texture;
		draw.Pipeline = pipeline;
	}
	draws[0].HostInputs = &narrow;
	draws[1].HostInputs = &wide;
	const std::array<float, 16> projection{.5f, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	// The existing caller-array path supplies an independent measured shader output for this color.
	sgre::render::SpritePassDraw baseline;
	baseline.Texture = texture;
	baseline.Pipeline = pipeline;
	baseline.Vertices = vertices;
	baseline.Indices = wide_indices;
	const auto baseline_submission = renderer.Submit(std::span(&baseline, 1), projection);
	const auto baseline_pixels = renderer.Capture(baseline_submission);
	Require(baseline_pixels.Rgba.size() == 64 && baseline_pixels.Rgba[2] != 0 && baseline_pixels.Rgba[3] != 0, "actual colored baseline did not produce target pixels");
	const auto submission = renderer.Submit(draws, projection);
	heap.Begin();
	heap.Begin();
	RequireFailure([&] {
		static_cast<void>(narrow.VertexBytes());
	});
	RequireFailure([&] {
		static_cast<void>(renderer.Submit(draws, projection));
	});
	const auto pixels = renderer.Capture(submission);
	Require(pixels.Width == 4 && pixels.Height == 4 && pixels.Rgba.size() == 64, "wrong actual Metal target capture");
	Require(pixels.Rgba == baseline_pixels.Rgba, "queued Metal input copy changed after actual host page retirement");
	const auto current_vertices = Vertices(0xffffffffu);
	const auto current_indices = Indices(4);
	sgre::render::SpriteHostInputs current(heap, current_vertices, current_indices);
	draws[0].HostInputs = &current;
	draws[0].Vertices = current_vertices;
	RequireFailure([&] {
		static_cast<void>(renderer.Submit(std::span(draws).first(1), projection));
	});
	heap.Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "usage: sprite_host_inputs_tests native_fixture shader");
		Replay(argv[1]);
		TestBoundsAndLifetime();
		TestMetalLifetime(argv[2]);
		std::cout << "four native paged input cases and real mixed-index Metal retirement pixels pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
