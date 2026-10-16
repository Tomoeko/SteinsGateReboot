#include "sgre/scene/picture_regions.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestCropAndAppend() {
	std::array<sgre::scene::PictureTileBounds, 2> tiles{};
	tiles[0].Bounds = {0, 0, 10, 8};
	tiles[0].Source = 11;
	tiles[1].Bounds = {10, 0, 15, 8};
	tiles[1].Source = 22;
	sgre::scene::PictureRegionSet regions(tiles, 4);
	regions.Append({4, 2, 12, 7}, {1.5f, 2.5f});
	Require(regions.Width() == 9 && regions.Height() == 7, "fractional range extent truncates after destination offset");
	Require(regions.Regions().size() == 2, "range spans source tiles in original order");
	Require(regions.Regions()[0].Destination == std::array<float, 4>{1.5f, 2.5f, 7.5f, 7.5f}, "first piece destination uses cropped range origin");
	Require(regions.Regions()[0].SourceBounds == std::array<float, 4>{4, 2, 10, 7}, "first piece source remains tile-local");
	Require(regions.Regions()[1].SourceBounds == std::array<float, 4>{0, 2, 2, 7}, "second piece source subtracts its own tile origin");
	Require(regions.Regions()[0].Source == 11 && regions.Regions()[1].Source == 22, "source identity follows actual source order");
	regions.Append({0, 0, 1, 1}, {20, 30});
	Require(regions.Regions().size() == 3 && regions.Width() == 21 && regions.Height() == 31, "additional range appends and expands extent");
	regions.Append({100, 100, 200, 300}, {0, 0});
	Require(regions.Regions().size() == 3 && regions.Width() == 100 && regions.Height() == 200, "nonintersecting range still changes logical extent");
	regions.Clear();
	Require(regions.Regions().empty() && regions.Width() == 0 && regions.Height() == 0, "clear retires prepared ranges and dimensions");
}

static void TestContainsAndFailurePreflight() {
	std::array<sgre::scene::PictureTileBounds, 1> tiles{};
	tiles[0].Bounds = {0, 0, 4, 4};
	sgre::scene::PictureRegionSet regions(tiles, 1);
	regions.Append({0, 0, 4, 4});
	Require(regions.Contains({5, 6}, {5, 6}), "left/top bounds are inclusive after native coordinate translation");
	Require(!regions.Contains({9, 6}, {5, 6}) && !regions.Contains({5, 10}, {5, 6}), "right/bottom bounds are exclusive");
	bool capacity = false;
	try {
		regions.Append({0, 0, 4, 4}, {100, 200});
	} catch (const std::runtime_error&) {
		capacity = true;
	}
	Require(capacity && regions.Width() == 4 && regions.Height() == 4 && regions.Regions().size() == 1, "authored capacity failure precedes any geometry mutation");
	bool nonfinite = false;
	try {
		regions.Append({0, 0, std::numeric_limits<float>::infinity(), 4});
	} catch (const std::runtime_error&) {
		nonfinite = true;
	}
	Require(nonfinite && regions.Width() == 4, "unmeasured nonfinite extent rejects explicitly");
	sgre::scene::PictureRegionSet empty({}, 1);
	empty.Append({0, 0, 7.75f, 3.25f});
	Require(empty.Width() == 7 && empty.Height() == 3 && empty.Regions().empty(), "zero tile model still publishes native range extent");
}

class NativePictureReader {
public:
	explicit NativePictureReader(const char* filename) : m_Input(filename, std::ios::binary) {
		if (!m_Input) {
			throw std::runtime_error("native picture projection could not be opened");
		}
	}

	std::uint32_t Word() {
		std::array<char, 4> bytes{};
		m_Input.read(bytes.data(), bytes.size());
		if (!m_Input) {
			throw std::runtime_error("native picture projection is truncated");
		}

		return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24;
	}

	template <std::size_t Count>
	std::array<float, Count> Floats() {
		std::array<float, Count> result{};
		for (auto& value : result) {
			value = std::bit_cast<float>(Word());
		}

		return result;
	}

	void Finish() {
		Require(m_Input.peek() == std::char_traits<char>::eof(), "native picture projection has trailing bytes");
	}

private:
	std::ifstream m_Input;
};

static void TestNativeProjection(const char* filename) {
	NativePictureReader input(filename);
	const auto count = input.Word();
	Require(count == 1000, "native picture corpus retains its exact bounded case count");
	for (std::uint32_t case_id = 0; case_id < count; ++case_id) {
		const auto width = input.Word();
		const auto height = input.Word();
		const auto tile_count = input.Word();
		const auto operation_count = input.Word();
		Require(width <= 4096 && height <= 4096 && tile_count <= 4 && operation_count == 8, "native picture case exceeds retained corpus bounds");
		std::vector<sgre::scene::PictureTileBounds> tiles(tile_count);
		for (std::uint32_t index = 0; index < tile_count; ++index) {
			tiles[index].Bounds = input.Floats<4>();
			tiles[index].Source = index;
		}

		sgre::scene::PictureRegionSet regions(tiles, 64);
		for (std::uint32_t operation = 0; operation < operation_count; ++operation) {
			const auto clear = input.Word();
			const auto rectangle = input.Floats<4>();
			const auto destination = input.Floats<2>();
			const auto output_words = input.Word();
			if (clear != 0) {
				regions.Clear();
			} else {
				regions.Append(rectangle, destination);
			}

			Require(output_words == 3 + regions.Regions().size() * 9, "native picture region count differs at case " + std::to_string(case_id));
			Require(input.Word() == regions.Width() && input.Word() == regions.Height() && input.Word() == regions.Regions().size(), "native picture logical extent differs at case " + std::to_string(case_id));
			for (const auto& region : regions.Regions()) {
				for (const auto value : region.Destination) {
					Require(input.Word() == std::bit_cast<std::uint32_t>(value), "native picture destination float differs");
				}
				for (const auto value : region.SourceBounds) {
					Require(input.Word() == std::bit_cast<std::uint32_t>(value), "native picture source float differs");
				}
				Require(input.Word() == region.Source, "native picture source identity differs");
			}

			const auto position = input.Floats<2>();
			for (std::uint32_t query = 0; query < 2; ++query) {
				const auto point = input.Floats<2>();
				Require(input.Word() == static_cast<std::uint32_t>(regions.Contains(point, position)), "native picture hit-test differs");
			}
		}
	}
	input.Finish();
}

int main(int argc, char** argv) {
	try {
		TestCropAndAppend();
		TestContainsAndFailurePreflight();
		if (argc == 2) {
			TestNativeProjection(argv[1]);
		}
		std::cout << "picture region tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
