#include "sgre/scene/graphics_model_stack.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::array<std::uint8_t, 4> bytes{};
	for (auto& byte : bytes) {
		const auto value = input.get();
		Require(value != std::char_traits<char>::eof(), "model-stack native fixture is truncated");
		byte = static_cast<std::uint8_t>(value);
	}

	return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static sgre::scene::MotionScissorMatrix ReadModel(std::ifstream& input) {
	sgre::scene::MotionScissorMatrix model;
	for (auto& value : model.Coefficients) {
		value = std::bit_cast<float>(ReadWord(input));
	}

	return model;
}

static void RequireFailure(const auto& operation) {
	bool failed = false;
	try {
		operation();
	} catch (const std::runtime_error&) {
		failed = true;
	}

	Require(failed, "invalid model-stack operation must reject before mutation");
}

static void TestAdmission() {
	for (const auto capacity : {std::size_t{0}, std::size_t{1025}}) {
		RequireFailure([&] {
			sgre::scene::GraphicsModelStack stack(capacity);
		});
	}

	sgre::scene::GraphicsModelStack stack(2);
	RequireFailure([&] {
		stack.Pop();
	});
	RequireFailure([&] {
		static_cast<void>(stack.Current());
	});
	RequireFailure([&] {
		stack.SetCurrent({});
	});
	stack.Push();
	const auto* parent = &stack.Current();
	stack.Push();
	RequireFailure([&] {
		stack.Push();
	});
	Require(stack.Depth() == 2, "capacity rejection must preserve the existing depth");
	stack.Pop();
	Require(&stack.Current() == parent, "preallocated push must preserve a live parent model borrow");
	stack.SetCurrent(stack.Current());
	stack.Pop();
	Require(stack.Depth() == 0, "actual parent retirement must empty the owner");
}

static void Replay(const std::filesystem::path& path) {
	std::ifstream input(path, std::ios::binary);
	Require(static_cast<bool>(input), "cannot open bounded model-stack native fixture");
	Require(ReadWord(input) == 0x31534d47, "wrong model-stack native fixture magic");
	const auto families = ReadWord(input);
	Require(families > 0 && families <= 64, "model-stack native fixture exceeds its family bound");
	std::uint32_t comparisons = 0;
	for (std::uint32_t family = 0; family < families; ++family) {
		sgre::scene::GraphicsModelStack stack(32);
		const auto count = ReadWord(input);
		Require(count > 0 && count <= 256, "model-stack native fixture exceeds its phase bound");
		for (std::uint32_t phase = 0; phase < count; ++phase) {
			const auto kind = ReadWord(input);
			switch (kind) {
			case 1:
				stack.Push();
				break;
			case 2:
				stack.SetCurrent(ReadModel(input));
				break;
			case 3:
				stack.Pop();
				break;
			default:
				throw std::runtime_error("model-stack native fixture has an unknown operation");
			}

			const auto expected_depth = ReadWord(input);
			Require(expected_depth == stack.Depth(), "model-stack depth differs from native at family " + std::to_string(family));
			if (expected_depth != 0) {
				const auto expected = ReadModel(input);
				const auto& actual = stack.Current();
				for (std::size_t index = 0; index < expected.Coefficients.size(); ++index) {
					Require(std::bit_cast<std::uint32_t>(actual.Coefficients[index]) == std::bit_cast<std::uint32_t>(expected.Coefficients[index]), "model-stack word differs from native at family " + std::to_string(family) + " phase " + std::to_string(phase));
				}
			}
			++comparisons;
		}
		Require(stack.Depth() == 0, "native family must retire every nested model slot");
	}
	Require(input.peek() == std::char_traits<char>::eof(), "model-stack native fixture has trailing bytes");
	std::cout << comparisons << " native model-stack snapshots matched\n";
}

int main(int argc, char** argv) {
	try {
		TestAdmission();
		if (argc == 2) {
			Replay(argv[1]);
		} else {
			Require(argc == 1, "usage: graphics_model_stack_tests [native_fixture]");
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
