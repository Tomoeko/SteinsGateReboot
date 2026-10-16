#include "sgre/platform/host_time.hpp"
#include "sgre/render/color_pass.hpp"

#include <array>
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

static void TestReferenceShaders(const std::filesystem::path& shader) {
	sgre::render::ColorPassDescription description;
	description.Width = 64;
	description.Height = 32;
	description.MaxVertices = 4;
	description.MaxIndices = 4;
	sgre::render::ColorPassRenderer renderer(description, shader);
	std::array<sgre::render::ColorPassVertex, 4> vertices;
	vertices[0].Position = {-1, 1, 0.5f};
	vertices[1].Position = {1, 1, 0.5f};
	vertices[2].Position = {-1, -1, 0.5f};
	vertices[3].Position = {1, -1, 0.5f};
	for (auto& vertex : vertices) {
		vertex.Color = {1, 0.5f, 0, 1};
	}

	const std::array<std::uint16_t, 4> indices{0, 2, 1, 3};
	std::array<float, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const auto first = renderer.Submit(vertices, indices, matrix);
	const auto full = renderer.Capture(first);
	Require(full.Width == 64 && full.Height == 32 && full.Rgba.size() == 64 * 32 * 4, "Metal capture dimensions/row packing differ");
	for (std::size_t offset = 0; offset < full.Rgba.size(); offset += 4) {
		Require(full.Rgba[offset] == 255 && full.Rgba[offset + 1] == 128 && full.Rgba[offset + 2] == 0 && full.Rgba[offset + 3] == 255, "passthrough shader or RGBA8 conversion changed a uniform pixel");
	}

	// Column zero contains the row-vector translation; using matrix * position would alter W instead.
	matrix[0] = 0.5f;
	matrix[3] = 0.25f;
	for (auto& vertex : vertices) {
		vertex.Color = {0, 0, 1, 1};
	}

	const auto second = renderer.Submit(vertices, indices, matrix, {1, 0, 0, 1});
	const auto shifted = renderer.Capture(second);
	for (std::size_t row = 0; row < 32; ++row) {
		for (std::size_t column = 0; column < 64; ++column) {
			const auto offset = (row * 64 + column) * 4;
			const bool covered = column >= 24 && column < 56;
			Require(shifted.Rgba[offset] == (covered ? 0 : 255) && shifted.Rgba[offset + 1] == 0 && shifted.Rgba[offset + 2] == (covered ? 255 : 0) && shifted.Rgba[offset + 3] == 255, "matrix columns, viewport convention, or strip coverage differ");
		}
	}

	try {
		static_cast<void>(renderer.Capture(first));
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("latest submission") != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("old capture identity read back a newer target");
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("color_pass_tests SHADER_SOURCE");
		}

		TestReferenceShaders(argv[1]);
		const auto time = sgre::platform::ReadHostLocalDateTime();
		Require(time.Month >= 1 && time.Month <= 12 && time.MonthDay >= 1 && time.MonthDay <= 31 && time.Hour >= 0 && time.Hour <= 23 && time.Minute >= 0 && time.Minute <= 59 && time.Second >= 0 && time.Second <= 60 && time.WeekDay >= 0 && time.WeekDay <= 6 && time.YearDay >= 0 && time.YearDay <= 365, "native local calendar service returned invalid ranges");
		std::cout << "Metal reference color-pass fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
