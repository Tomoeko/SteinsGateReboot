#include "sgre/scene/movie_packet.hpp"

#include "fixtures/movie_quad_native.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestOriginalQuadWords() {
	for (const auto& expected : kMovieQuadNative) {
		sgre::scene::MovieQuadDescription description;
		description.SourceWidth = expected.Width;
		description.SourceHeight = expected.Height;
		description.TextureWidth = expected.TextureWidth;
		description.TextureHeight = expected.TextureHeight;
		description.Position = {-static_cast<float>(expected.Width) * 0.5f, -static_cast<float>(expected.Height) * 0.5f};
		description.Extent = {static_cast<float>(expected.Width), static_cast<float>(expected.Height)};
		description.Opacity = expected.Opacity;
		if (expected.Alpha != 0) {
			const auto packet = sgre::scene::PrepareMovieAlphaDrawPacket(description);
			Require(packet.Indices == std::array<std::uint16_t, 4>{0, 1, 2, 3}, "movie native index16 strip differs");
			for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
				const auto& vertex = packet.Vertices[index];
				const std::array<std::uint32_t, 7> words{std::bit_cast<std::uint32_t>(vertex.Position.X), std::bit_cast<std::uint32_t>(vertex.Position.Y), vertex.Color, std::bit_cast<std::uint32_t>(vertex.ColorUv.X), std::bit_cast<std::uint32_t>(vertex.ColorUv.Y), std::bit_cast<std::uint32_t>(vertex.AlphaUv.X), std::bit_cast<std::uint32_t>(vertex.AlphaUv.Y)};
				for (std::size_t word = 0; word < words.size(); ++word) {
					Require(words[word] == expected.Words[index * 7 + word], "movie alpha quad differs from unchanged native instruction words");
				}
			}
		} else {
			const auto packet = sgre::scene::PrepareMovieDrawPacket(description);
			Require(packet.Color == ((expected.Opacity & 255u) << 24 | 0x00ffffffu), "movie opacity must use native low byte");
			for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
				const auto& vertex = packet.Vertices[index];
				const std::array<float, 4> values{vertex.Position.X, vertex.Position.Y, vertex.Uv.X, vertex.Uv.Y};
				for (std::size_t word = 0; word < values.size(); ++word) {
					Require(std::bit_cast<std::uint32_t>(values[word]) == expected.Words[index * 4 + word], "movie ordinary quad differs from unchanged native instruction words");
				}
			}
		}
	}
}

static void TestBounds() {
	sgre::scene::MovieQuadDescription description;
	description.SourceWidth = 1;
	description.SourceHeight = 1;
	description.TextureWidth = 16;
	description.TextureHeight = 16;
	for (std::size_t attempt = 0; attempt < 3; ++attempt) {
		auto invalid = description;
		if (attempt == 0) {
			invalid.SourceHeight = 17;
		} else if (attempt == 1) {
			invalid.TextureWidth = 0;
		} else {
			invalid.Position.X = std::numeric_limits<float>::infinity();
		}
		bool rejected = false;
		try {
			static_cast<void>(sgre::scene::PrepareMovieAlphaDrawPacket(invalid));
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected, "movie quad accepted invalid native dimensions/coordinates");
	}
}

int main() {
	try {
		TestOriginalQuadWords();
		TestBounds();
		std::cout << "movie quad: all64 native word vectors passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
