#include "sgre/scene/render_target_layout.hpp"

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
	std::array<unsigned char, 4> bytes{};
	for (auto& byte : bytes) {
		const auto value = input.get();
		Require(value != std::char_traits<char>::eof(), "render-target native fixture is truncated");
		byte = static_cast<unsigned char>(value);
	}

	return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static void RequireWord(std::ifstream& input, float actual, std::uint32_t index) {
	Require(ReadWord(input) == std::bit_cast<std::uint32_t>(actual), "render-target native scalar differs at case " + std::to_string(index));
}

static void TestAdmissionAndLayout() {
	for (const auto dimensions : {std::array<std::uint32_t, 2>{0, 1}, std::array<std::uint32_t, 2>{1, 0}, std::array<std::uint32_t, 2>{16385, 1}, std::array<std::uint32_t, 2>{1, 0xffffffff}}) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::scene::PrepareRenderTargetLayout(dimensions[0], dimensions[1], 0, 0));
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected, "invalid allocated target extent must reject");
	}

	const auto first = sgre::scene::PrepareRenderTargetLayout(3, 5, 2, 0);
	const auto second = sgre::scene::PrepareRenderTargetLayout(3, 5, 2, 1);
	const auto other_eye = sgre::scene::PrepareRenderTargetLayout(3, 5, 2, 2);
	const auto vertical = sgre::scene::PrepareRenderTargetLayout(3, 5, 3, 1);
	Require(first.Viewport.X == 0 && first.Viewport.Width == 1.5f && second.Viewport.X == 1.5f && second.ViewportBounds.MaximumX == 3, "odd physical horizontal split must preserve half pixels");
	Require(other_eye.Viewport.X == 0 && vertical.Viewport.Y == 2.5f && vertical.Viewport.Height == 2.5f, "only native eye one offsets its split");
	Require(first.FullScissor.MaximumX == 3 && first.FullScissor.MaximumY == 5 && first.Projection.Coefficients == second.Projection.Coefficients, "split selection must preserve full physical scissor/projection");
	Require(std::bit_cast<std::uint32_t>(first.Projection.Coefficients[3]) == 0x80000000 && std::bit_cast<std::uint32_t>(first.Projection.Coefficients[7]) == 0x80000000, "projection must retain native negative-zero translations");
}

static void Replay(const std::filesystem::path& path) {
	std::ifstream input(path, std::ios::binary);
	Require(static_cast<bool>(input), "cannot open bounded render-target native fixture");
	Require(ReadWord(input) == 0x314c5452, "wrong render-target native fixture magic");
	const auto count = ReadWord(input);
	Require(count > 0 && count <= 10000, "render-target native fixture case count exceeds bound");
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto width = ReadWord(input);
		const auto height = ReadWord(input);
		const auto type = ReadWord(input);
		const auto eye = ReadWord(input);
		const auto actual = sgre::scene::PrepareRenderTargetLayout(width, height, type, eye);
		for (const auto value : {actual.Viewport.X, actual.Viewport.Y, actual.Viewport.Width, actual.Viewport.Height, actual.Viewport.MinimumDepth, actual.Viewport.MaximumDepth}) {
			RequireWord(input, value, index);
		}
		for (const auto value : {actual.ViewportBounds.MinimumX, actual.ViewportBounds.MinimumY, actual.ViewportBounds.MaximumX, actual.ViewportBounds.MaximumY, actual.FullScissor.MinimumX, actual.FullScissor.MinimumY, actual.FullScissor.MaximumX, actual.FullScissor.MaximumY}) {
			RequireWord(input, value, index);
		}
		for (const auto value : actual.Projection.Coefficients) {
			RequireWord(input, value, index);
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "render-target native fixture has trailing bytes");
	std::cout << count << " original target viewport/projection cases match\n";
}

static void ReplayClear(const std::filesystem::path& path) {
	std::ifstream input(path, std::ios::binary);
	Require(static_cast<bool>(input), "cannot open bounded native clear-color fixture");
	Require(ReadWord(input) == 0x31434352, "wrong native clear-color fixture magic");
	const auto count = ReadWord(input);
	Require(count > 0 && count <= 10000, "native clear-color case count exceeds bound");
	for (std::uint32_t index = 0; index < count; ++index) {
		std::array<std::int32_t, 4> words{};
		for (auto& word : words) {
			word = std::bit_cast<std::int32_t>(ReadWord(input));
		}
		const auto actual = sgre::scene::PrepareRenderTargetClearColor(words);
		for (const auto value : actual) {
			RequireWord(input, value, index);
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native clear-color fixture has trailing bytes");
	std::cout << count << " original color-clear word conversions match\n";
}

static void TestClearWords() {
	const auto clear = sgre::scene::PrepareRenderTargetClearColor({0, 255, -1, 256});
	Require(clear[0] == 0 && clear[1] == 1 && clear[2] > 16000000 && clear[3] > 1, "raw color words must zero-extend and retain unclamped conversion");
}

int main(int argc, char** argv) {
	try {
		Require(argc >= 1 && argc <= 3, "usage: render_target_layout_tests [layout_fixture [clear_fixture]]");
		TestAdmissionAndLayout();
		TestClearWords();
		if (argc >= 2) {
			Replay(argv[1]);
		}

		if (argc == 3) {
			ReplayClear(argv[2]);
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
