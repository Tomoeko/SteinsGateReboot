#include "sgre/render/sprite_graphics_targets.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::uint32_t ReadWord(std::istream& input) {
	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		const auto byte = input.get();
		Require(byte != std::char_traits<char>::eof(), "truncated original mask-control fixture");
		result |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << (index * 8);
	}

	return result;
}

static void TestOriginalWords(const std::filesystem::path& fixture, const std::filesystem::path& shader) {
	std::ifstream input(fixture, std::ios::binary);
	Require(input.good(), "cannot open original mask-control fixture");
	std::array<char, 4> signature;
	input.read(signature.data(), static_cast<std::streamsize>(signature.size()));
	Require(signature == std::array<char, 4>{'G', 'M', 'C', '1'} && ReadWord(input) == 16, "wrong bounded original mask-control fixture");
	sgre::render::SpritePassDescription description;
	description.Width = 2;
	description.Height = 2;
	description.MaxVertices = 4;
	description.MaxIndices = 4;
	description.MaxDraws = 1;
	description.MaxTextures = 1;
	description.MaxPipelines = 1;
	description.MaxTextureBytes = 64;
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 1);
	const auto initial = graphics.MaskControl();
	Require(initial.Operation == 0 && std::bit_cast<std::uint32_t>(initial.ThresholdMinimum) == 0 && std::bit_cast<std::uint32_t>(initial.ThresholdMaximum) == 0, "actual graphics owner fabricated nonzero constructor mask fields");
	for (std::size_t index = 0; index < 16; ++index) {
		const auto minimum = ReadWord(input);
		const auto maximum = ReadWord(input);
		const auto expected_operation = ReadWord(input);
		const auto expected_minimum = ReadWord(input);
		const auto expected_maximum = ReadWord(input);
		graphics.ConfigureOrdinaryMaskControl(std::bit_cast<float>(minimum), std::bit_cast<float>(maximum));
		const auto actual = graphics.MaskControl();
		Require(actual.Operation == expected_operation && std::bit_cast<std::uint32_t>(actual.ThresholdMinimum) == expected_minimum && std::bit_cast<std::uint32_t>(actual.ThresholdMaximum) == expected_maximum, "actual containing graphics scalar words differ from unchanged original MOVSS stores");
	}
	Require(input.peek() == std::char_traits<char>::eof(), "extra original mask-control fixture bytes");
	const auto before = graphics.MaskControl();
	std::exception_ptr thread_error;
	std::thread foreign([&] {
		try {
			try {
				graphics.ConfigureOrdinaryMaskControl(1, 0);
			} catch (const std::runtime_error& error) {
				Require(std::string(error.what()).find("owner thread") != std::string::npos, error.what());

				return;
			}
			throw std::runtime_error("foreign thread mutated real graphics mask control");
		} catch (...) {
			thread_error = std::current_exception();
		}
	});
	foreign.join();
	if (thread_error) {
		std::rethrow_exception(thread_error);
	}
	const auto after = graphics.MaskControl();
	Require(before.Operation == after.Operation && std::bit_cast<std::uint32_t>(before.ThresholdMinimum) == std::bit_cast<std::uint32_t>(after.ThresholdMinimum) && std::bit_cast<std::uint32_t>(before.ThresholdMaximum) == std::bit_cast<std::uint32_t>(after.ThresholdMaximum), "rejected foreign control changed actual stored scalar words");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "mask-control proof requires original fixture and actual shader source");
		TestOriginalWords(argv[1], argv[2]);
		std::cout << "PASS16 original graphics mask-control word pairs and actual owner/thread guards; zero submitted frames\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
