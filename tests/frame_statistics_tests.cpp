#include "sgre/render/frame_statistics.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<unsigned char, 4> bytes;
	Require(static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), 4)), "original statistics fixture is truncated");

	return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static std::array<std::uint32_t, 4> ReadCounters(std::istream& input) {
	std::array<std::uint32_t, 4> words;
	for (auto& word : words) {
		word = ReadWord(input);
	}

	return words;
}

static void TestNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(ReadWord(input) == 0x31545346 && ReadWord(input) == 128, "original statistics fixture header differs");
	for (std::size_t case_index = 0; case_index < 128; ++case_index) {
		const auto initial = ReadCounters(input);
		sgre::render::RenderFrameStatistics statistics({initial[0], initial[2]}, {initial[1], initial[3]});
		for (std::size_t phase = 0; phase < 4; ++phase) {
			const auto begin_frame = ReadWord(input);
			const auto elements = ReadWord(input);
			const auto expected = ReadCounters(input);
			Require(begin_frame <= 1, "original statistics fixture contains an invalid phase");
			if (begin_frame != 0) {
				statistics.BeginFrame();
			} else {
				statistics.RecordDraw(elements);
			}
			const auto current = statistics.Current();
			const auto previous = statistics.Previous();
			Require(current.DrawOperations == expected[0] && previous.DrawOperations == expected[1] && current.SubmittedElements == expected[2] && previous.SubmittedElements == expected[3], "current/previous logical draw counters differ from original instructions");
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "original statistics fixture contains trailing bytes");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: frame_statistics_tests original_fixture");
		TestNative(argv[1]);
		std::cout << "Compared 128 initialized native counter cases and 512 wrapping/reset operations\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
