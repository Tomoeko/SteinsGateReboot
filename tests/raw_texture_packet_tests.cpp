#include "sgre/scene/raw_texture_packet.hpp"

#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}
static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "RawTex packet error omitted context");
		return;
	}
	throw std::runtime_error("unsupported RawTex packet input accepted");
}
static void TestStripOrderAndColor() {
	const auto packet = sgre::scene::PrepareRawTextureDrawPacket({2.5f, -3.25f}, {7.25f, 5.5f}, 0x78123456);
	const std::array<std::array<float, 4>, 4> expected{{{2.5f, -3.25f, 0, 0}, {2.5f, 2.25f, 0, 1}, {9.75f, -3.25f, 1, 0}, {9.75f, 2.25f, 1, 1}}};
	Require(packet.Color == 0x78123456 && packet.TextureAttributes == 0, "packed color did not select uniform-color attributes");
	for (std::size_t index = 0; index < 4; ++index) {
		const auto& vertex = packet.Vertices[index];
		const std::array<float, 4> words{vertex.Position.X, vertex.Position.Y, vertex.Uv.X, vertex.Uv.Y};
		Require(words == expected[index], "RawTex nonindexed triangle-strip order or UV changed");
	}
	Require(sgre::scene::PrepareRawTextureDrawPacket({}, {1, 1}, 0xffffffff).TextureAttributes == 4, "exact white failed to select white attributes");
	Require(sgre::scene::PrepareRawTextureDrawPacket({}, {1, 1}, 0xfeffffff).TextureAttributes == 0, "partial-opacity white incorrectly bypassed uniform color");
}
static void TestFiniteDegenerateAndRounding() {
	const auto zero = sgre::scene::PrepareRawTextureDrawPacket({-0.0f, -0.0f}, {0, 0}, 0);
	Require(std::bit_cast<std::uint32_t>(zero.Vertices[0].Position.X) == 0x80000000, "initial negative-zero position was changed");
	Require(std::bit_cast<std::uint32_t>(zero.Vertices[3].Position.X) == 0, "native float addition of signed-zero extent was changed");
	const auto rounded = sgre::scene::PrepareRawTextureDrawPacket({16777216, 0}, {1, 0}, 0);
	Require(rounded.Vertices[3].Position.X == 16777216, "right extent used wider intermediate precision");
	const auto reversed = sgre::scene::PrepareRawTextureDrawPacket({4, 3}, {-2, -1}, 0);
	Require(reversed.Vertices[3].Position.X == 2 && reversed.Vertices[3].Position.Y == 2, "prepared finite negative size was silently normalized");
}
static void TestUnsupportedInputs() {
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareRawTextureDrawPacket({std::numeric_limits<float>::infinity(), 0}, {1, 1}, 0));
	},
	    "finite position");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareRawTextureDrawPacket({}, {std::numeric_limits<float>::quiet_NaN(), 1}, 0));
	},
	    "finite position");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareRawTextureDrawPacket({std::numeric_limits<float>::max(), 0}, {std::numeric_limits<float>::max(), 1}, 0));
	},
	    "extent is not finite");
}
int main() {
	try {
		TestStripOrderAndColor();
		TestFiniteDegenerateAndRounding();
		TestUnsupportedInputs();
		std::cout << "RawTex packet strip order, color flags, float rounding and safety checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
