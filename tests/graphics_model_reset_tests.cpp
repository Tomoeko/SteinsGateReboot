#include "sgre/scene/graphics_model_stack.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::array<unsigned char, 4> bytes{};
	input.read(reinterpret_cast<char*>(bytes.data()), 4);
	Require(input.good(), "model reset fixture is truncated");

	return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static_assert(!std::is_copy_constructible_v<sgre::scene::GraphicsModelStack>);
static_assert(!std::is_move_constructible_v<sgre::scene::GraphicsModelStack>);
static_assert(!std::is_copy_assignable_v<sgre::scene::GraphicsModelStack>);
static_assert(!std::is_move_assignable_v<sgre::scene::GraphicsModelStack>);

static void Replay(std::ifstream& input) {
	Require(ReadWord(input) == 0x31524d47 && ReadWord(input) == 64, "wrong original model reset fixture");
	for (std::size_t case_index = 0; case_index < 64; ++case_index) {
		const auto old_depth = ReadWord(input);
		Require(old_depth <= 32, "native model depth exceeds fixture bound");
		sgre::scene::GraphicsModelStack stack(33);
		for (std::size_t index = 0; index < old_depth; ++index) {
			stack.Push();
			sgre::scene::MotionScissorMatrix model;
			for (auto& value : model.Coefficients) {
				value = std::bit_cast<float>(ReadWord(input));
			}
			stack.SetCurrent(model);
		}
		std::array<std::uint32_t, 12> expected;
		for (auto& value : expected) {
			value = ReadWord(input);
		}

		stack.Reset();
		Require(stack.Depth() == 1, "native reset did not replace prior model slots");
		for (std::size_t word = 0; word < expected.size(); ++word) {
			Require(std::bit_cast<std::uint32_t>(stack.Current().Coefficients[word]) == expected[word], "reset model differs from original instruction words");
		}
		stack.Push();
		stack.Pop();
		Require(stack.Depth() == 1, "reset owner cannot resume normal virtual-screen push/pop");
		stack.Reset();
		Require(stack.Depth() == 1, "repeated reset accumulated model slots");
	}
	Require(input.peek() == std::char_traits<char>::eof(), "original reset fixture has trailing bytes");
	sgre::scene::GraphicsModelStack minimum(1);
	minimum.Reset();
	minimum.Pop();
	minimum.Reset();
	Require(minimum.Depth() == 1, "minimum capacity reset failed");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: model_reset_tests native_fixture");
		std::ifstream input(argv[1], std::ios::binary);
		Require(input.good(), "cannot open model reset fixture");
		Replay(input);
		std::cout << "64 actual original model resets and resumed virtual-screen stack operations pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
