#include "sgre/scene/motion_bezier_lattice.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<char, 4> bytes{};
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(input), "native degree conversion vector is truncated");
	return static_cast<std::uint8_t>(bytes[0]) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static sgre::resource::MotionBezierLattice ReadLattice(std::istream& input, std::uint32_t degree, std::uint32_t count) {
	Require(degree <= 255 && count == ((degree & 15) + 1) * ((degree >> 4) + 1) && count <= 49, "native conversion input exceeds exact bounded degree cardinality");
	sgre::resource::MotionBezierLattice lattice;
	lattice.EncodedDegree = static_cast<std::uint8_t>(degree);
	lattice.Controls.resize(count);
	for (auto& control : lattice.Controls) {
		control.X = std::bit_cast<float>(ReadWord(input));
		control.Y = std::bit_cast<float>(ReadWord(input));
	}
	return lattice;
}

static void CheckLattice(std::istream& input, const sgre::resource::MotionBezierLattice& lattice, std::uint32_t degree, std::uint32_t count, std::string_view operation, std::uint32_t record) {
	Require(lattice.EncodedDegree == degree && lattice.Controls.size() == count, "native conversion changed target degree/cardinality");
	for (std::size_t index = 0; index < lattice.Controls.size(); ++index) {
		const auto x = ReadWord(input);
		const auto y = ReadWord(input);
		const auto actual_x = std::bit_cast<std::uint32_t>(lattice.Controls[index].X);
		const auto actual_y = std::bit_cast<std::uint32_t>(lattice.Controls[index].Y);
		if (actual_x != x || actual_y != y) {
			throw std::runtime_error(std::string(operation) + " differs in native record " + std::to_string(record) + " point " + std::to_string(index) + " X=" + std::to_string(actual_x) + "/" + std::to_string(x) + " Y=" + std::to_string(actual_y) + "/" + std::to_string(y));
		}
	}
}

static void TestNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open() && ReadWord(input) == 0x3143424d, "native conversion input requires MBC1 header");
	const auto records = ReadWord(input);
	Require(records == 32, "native conversion case inventory changed");
	std::size_t compared = 0;
	for (std::uint32_t record = 0; record < records; ++record) {
		const auto source_degree = ReadWord(input);
		const auto target_degree = ReadWord(input);
		const auto source_count = ReadWord(input);
		const auto target_count = ReadWord(input);
		const auto combined_degree = ReadWord(input);
		const auto combined_count = ReadWord(input);
		const auto source = ReadLattice(input, source_degree, source_count);
		const auto incoming = ReadLattice(input, target_degree, target_count);
		const auto converted = sgre::scene::ConvertMotionBezierLatticeDegree(source, static_cast<std::uint8_t>(target_degree));
		CheckLattice(input, converted, target_degree, target_count, "degree conversion", record);
		const auto combined = sgre::scene::CombineMotionBezierLattices(source, incoming);
		CheckLattice(input, combined, combined_degree, combined_count, "cross-axis combination", record);
		compared += (target_count + combined_count) * 2;
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native conversion input has trailing bytes");
	std::cout << "Native degree conversion: 32 actual554400/32 actual554dd0 cases/" << compared << " exact control words\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_lattice_degree_conversion_tests native-vectors.bin");
		TestNative(argv[1]);
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
