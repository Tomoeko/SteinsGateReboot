#include "sgre/resource/image.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "image failure omitted required context");

		return;
	}

	throw std::runtime_error("invalid image was accepted: " + std::string(context));
}

template <typename Value>
static sgre::resource::PsbValue MakeValue(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::ImageTexture MakeTexture(sgre::resource::PixelFormat format, std::uint32_t width, std::uint32_t height, std::uint64_t offset, std::uint64_t size) {
	sgre::resource::ImageTexture texture;
	texture.Format = format;
	texture.Width = width;
	texture.Height = height;
	texture.TruncatedWidth = width;
	texture.TruncatedHeight = height;
	texture.PixelRange.Offset = offset;
	texture.PixelRange.Size = size;

	return texture;
}

struct Bc7Vector {
	std::array<std::uint8_t, 16> Block;
	std::array<std::array<std::uint8_t, 4>, 16> Pixels;
};

// Synthetic blocks decoded independently with Pillow's DDS decoder.
static constexpr std::array<Bc7Vector, 8> kBc7Vectors = {{
    {{0x6d, 0x66, 0x4d, 0xf0, 0x44, 0x42, 0xa6, 0x8a, 0xde, 0x19, 0xa0, 0x53, 0xe7, 0xe9, 0xc7, 0x3f},
        {{
            {49, 115, 82, 255},
            {49, 56, 82, 255},
            {75, 40, 217, 255},
            {61, 38, 222, 255},
            {49, 92, 82, 255},
            {49, 33, 82, 255},
            {90, 42, 212, 255},
            {132, 49, 198, 255},
            {134, 23, 171, 255},
            {111, 18, 222, 255},
            {99, 16, 247, 255},
            {169, 31, 91, 255},
            {111, 18, 222, 255},
            {99, 16, 247, 255},
            {99, 16, 247, 255},
            {181, 33, 66, 255},
        }}},
    {{0xe6, 0xbe, 0x47, 0x1b, 0x68, 0x6e, 0x56, 0x65, 0x32, 0x37, 0x0f, 0x9f, 0xcd, 0x1a, 0xa9, 0x18},
        {{
            {197, 192, 103, 255},
            {211, 155, 207, 255},
            {52, 96, 76, 255},
            {122, 231, 38, 255},
            {176, 202, 86, 255},
            {158, 212, 70, 255},
            {185, 145, 185, 255},
            {133, 126, 142, 255},
            {158, 212, 70, 255},
            {233, 173, 135, 255},
            {159, 136, 164, 255},
            {159, 136, 164, 255},
            {78, 105, 97, 255},
            {251, 163, 151, 255},
            {197, 192, 103, 255},
            {211, 155, 207, 255},
        }}},
    {{0xfc, 0xac, 0xb2, 0xac, 0xef, 0xe6, 0x1c, 0xcd, 0xdf, 0xed, 0x7f, 0xc8, 0x21, 0xb1, 0xe8, 0xe7},
        {{
            {181, 107, 115, 255},
            {114, 112, 187, 255},
            {181, 107, 115, 255},
            {149, 110, 150, 255},
            {239, 206, 33, 255},
            {82, 115, 222, 255},
            {114, 112, 187, 255},
            {181, 107, 115, 255},
            {239, 206, 33, 255},
            {233, 222, 41, 255},
            {99, 165, 57, 255},
            {99, 165, 57, 255},
            {222, 255, 57, 255},
            {239, 206, 33, 255},
            {222, 255, 57, 255},
            {154, 131, 190, 255},
        }}},
    {{0x98, 0x9f, 0x97, 0x43, 0x54, 0x50, 0x85, 0x66, 0x85, 0xee, 0xa8, 0x32, 0x22, 0x07, 0x1d, 0x76},
        {{
            {206, 130, 66, 255},
            {134, 208, 80, 255},
            {117, 169, 120, 255},
            {168, 99, 182, 255},
            {150, 84, 238, 255},
            {206, 130, 66, 255},
            {134, 208, 80, 255},
            {98, 128, 163, 255},
            {168, 99, 182, 255},
            {150, 84, 238, 255},
            {134, 208, 80, 255},
            {134, 208, 80, 255},
            {81, 89, 203, 255},
            {168, 99, 182, 255},
            {150, 84, 238, 255},
            {134, 208, 80, 255},
        }}},
    {{0x30, 0x40, 0xbf, 0xcf, 0x1b, 0xd0, 0xb6, 0x38, 0x14, 0x21, 0xd1, 0x56, 0x68, 0x34, 0xbb, 0xec},
        {{
            {0, 166, 190, 70},
            {51, 212, 148, 144},
            {77, 166, 190, 70},
            {77, 166, 190, 70},
            {131, 123, 231, 0},
            {0, 255, 107, 214},
            {51, 166, 190, 70},
            {77, 123, 231, 0},
            {105, 212, 148, 144},
            {156, 212, 148, 144},
            {105, 123, 231, 0},
            {131, 212, 148, 144},
            {77, 123, 231, 0},
            {26, 123, 231, 0},
            {77, 166, 190, 70},
            {182, 212, 148, 144},
        }}},
    {{0xa0, 0xb8, 0xa3, 0x84, 0xbb, 0xe4, 0x64, 0xd6, 0x89, 0x51, 0x70, 0x57, 0xad, 0xc2, 0x03, 0xf2},
        {{
            {112, 153, 151, 36},
            {122, 117, 120, 85},
            {112, 129, 151, 36},
            {143, 129, 56, 185},
            {112, 129, 151, 36},
            {133, 153, 87, 136},
            {133, 153, 87, 136},
            {112, 117, 151, 36},
            {112, 117, 151, 36},
            {133, 153, 87, 136},
            {143, 153, 56, 185},
            {133, 153, 87, 136},
            {143, 129, 56, 185},
            {133, 153, 87, 136},
            {133, 117, 87, 136},
            {133, 117, 87, 136},
        }}},
    {{0x40, 0x94, 0xe6, 0x79, 0x39, 0xf8, 0x76, 0x9a, 0xc6, 0x4a, 0x33, 0x7f, 0x9a, 0xf1, 0x55, 0xc8},
        {{
            {75, 136, 37, 105},
            {58, 69, 102, 66},
            {62, 83, 88, 74},
            {73, 129, 44, 101},
            {75, 136, 37, 105},
            {75, 136, 37, 105},
            {52, 46, 124, 52},
            {67, 106, 66, 88},
            {62, 83, 88, 74},
            {64, 92, 80, 79},
            {79, 152, 22, 115},
            {52, 46, 124, 52},
            {71, 122, 51, 97},
            {71, 122, 51, 97},
            {66, 99, 73, 83},
            {58, 69, 102, 66},
        }}},
    {{0x80, 0x2c, 0xc5, 0x17, 0x68, 0x8e, 0x6e, 0x4d, 0x6f, 0x1d, 0x8e, 0x7c, 0x50, 0x3d, 0xdc, 0x99},
        {{
            {166, 215, 174, 60},
            {61, 146, 100, 185},
            {186, 65, 121, 65},
            {176, 219, 141, 115},
            {125, 105, 111, 123},
            {195, 227, 73, 227},
            {195, 227, 73, 227},
            {186, 65, 121, 65},
            {186, 65, 121, 65},
            {195, 227, 73, 227},
            {176, 219, 141, 115},
            {0, 186, 89, 243},
            {176, 219, 141, 115},
            {61, 146, 100, 185},
            {125, 105, 111, 123},
            {185, 223, 106, 172},
        }}},
}};

static void TestBc7IndependentVectors() {
	const auto texture = MakeTexture(sgre::resource::PixelFormat::Bc7, 4, 4, 0, 16);
	for (std::size_t mode = 0; mode < kBc7Vectors.size(); ++mode) {
		const auto& vector = kBc7Vectors[mode];
		const auto decoded = sgre::resource::DecodeTexture(texture, vector.Block, 64);
		for (std::size_t pixel = 0; pixel < 16; ++pixel) {
			for (std::size_t channel = 0; channel < 4; ++channel) {
				Require(decoded.Pixels[pixel * 4 + channel] == vector.Pixels[pixel][channel], "BC7 golden vector changed for mode " + std::to_string(mode));
			}
		}
	}

	std::array<std::uint8_t, 16> reserved{};
	reserved[15] = 255;
	const auto decoded = sgre::resource::DecodeTexture(texture, reserved, 64);
	Require(std::all_of(decoded.Pixels.begin(), decoded.Pixels.end(), [](std::uint8_t value) {
		return value == 0;
	}),
	    "reserved BC7 mode did not produce transparent black");
}

static void TestPartialBc7BlocksAndOrder() {
	std::vector<std::uint8_t> blocks;
	for (std::size_t block = 0; block < 4; ++block) {
		blocks.insert(blocks.end(), kBc7Vectors[block].Block.begin(), kBc7Vectors[block].Block.end());
	}

	const auto texture = MakeTexture(sgre::resource::PixelFormat::Bc7, 5, 5, 0, 64);
	const auto decoded = sgre::resource::DecodeTexture(texture, blocks, 100);
	for (std::size_t y = 0; y < 5; ++y) {
		for (std::size_t x = 0; x < 5; ++x) {
			const auto block = (y / 4) * 2 + x / 4;
			const auto pixel = (y % 4) * 4 + x % 4;
			for (std::size_t channel = 0; channel < 4; ++channel) {
				Require(decoded.Pixels[(y * 5 + x) * 4 + channel] == kBc7Vectors[block].Pixels[pixel][channel], "BC7 block order or edge clipping changed");
			}
		}
	}
}

static void TestNativeSingleChannelFormats() {
	const std::array<std::uint8_t, 6> bytes = {17, 0, 127, 255, 41, 42};
	const auto alpha = MakeTexture(sgre::resource::PixelFormat::Alpha8, 3, 1, 1, 3);
	const auto luminance = MakeTexture(sgre::resource::PixelFormat::Luminance8, 3, 1, 1, 3);
	const auto decoded_alpha = sgre::resource::DecodeTexture(alpha, bytes, 12);
	const auto decoded_luminance = sgre::resource::DecodeTexture(luminance, bytes, 12);
	Require(decoded_alpha.Pixels == std::vector<std::uint8_t>({0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 255}), "native A8 sampled channel mapping changed");
	Require(decoded_luminance.Pixels == std::vector<std::uint8_t>({0, 0, 0, 255, 127, 0, 0, 255, 255, 0, 0, 255}), "native L8 sampled channel mapping changed");
}

static void TestTileAssemblyAndPadding() {
	const std::array<std::uint8_t, 10> bytes = {10, 20, 90, 30, 40, 91, 50, 60, 70, 92};
	sgre::resource::ImageEntry image;
	image.Width = 2;
	image.Height = 3;
	sgre::resource::ImageTile top;
	top.Width = 3;
	top.Height = 2;
	top.Texture = MakeTexture(sgre::resource::PixelFormat::Alpha8, 3, 2, 0, 6);
	sgre::resource::ImageTile bottom;
	bottom.Top = 2;
	bottom.Width = 4;
	bottom.Height = 1;
	bottom.Texture = MakeTexture(sgre::resource::PixelFormat::Alpha8, 4, 1, 6, 4);
	image.Tiles = {top, bottom};

	const auto decoded = sgre::resource::DecodeImage(image, bytes, 48);
	const std::array<std::uint8_t, 6> alpha = {10, 20, 30, 40, 50, 60};
	Require(decoded.Width == 2 && decoded.Height == 3, "logical image dimensions changed");
	for (std::size_t pixel = 0; pixel < alpha.size(); ++pixel) {
		Require(decoded.Pixels[pixel * 4 + 3] == alpha[pixel], "tile assembly included row padding or misplaced a tile");
	}

	RequireFailure([&]() {
		static_cast<void>(sgre::resource::DecodeImage(image, bytes, 47));
	},
	    "allocation limit");
	image.Tiles[0].Left = 2;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::DecodeImage(image, bytes));
	},
	    "tile dimensions or origin");
}

static sgre::resource::PsbValue TextureValue(std::string type = "A8", std::uint32_t chunk = 0, bool extra = false) {
	sgre::resource::PsbChunkReference reference;
	reference.Index = chunk;
	reference.Extra = extra;

	return MakeValue(sgre::resource::PsbObject{{"type", MakeValue(std::move(type))}, {"width", MakeValue(std::int64_t{2})}, {"height", MakeValue(std::int64_t{2})}, {"truncated_width", MakeValue(std::int64_t{2})}, {"truncated_height", MakeValue(std::int64_t{2})}, {"pixel", MakeValue(reference)}});
}

static sgre::resource::PsbDocument MakeDocument() {
	sgre::resource::PsbDocument document;
	document.Header.Version = 4;
	sgre::resource::PsbChunkRange chunk;
	chunk.Offset = 5;
	chunk.Size = 4;
	document.Chunks.push_back(chunk);
	sgre::resource::PsbValue tile = MakeValue(sgre::resource::PsbObject{{"left", MakeValue(0.0)}, {"top", MakeValue(0.0)}, {"width", MakeValue(2.0)}, {"height", MakeValue(2.0)}, {"image", TextureValue()}});
	sgre::resource::PsbValue entry = MakeValue(sgre::resource::PsbObject{{"label", MakeValue(std::string("Synthetic"))}, {"width", MakeValue(std::int64_t{2})}, {"height", MakeValue(std::int64_t{2})}, {"texture", MakeValue(sgre::resource::PsbArray{std::move(tile)})}});
	document.Root = MakeValue(sgre::resource::PsbObject{{"id", MakeValue(std::string("image"))}, {"spec", MakeValue(std::string("win"))}, {"version", MakeValue(1.0)}, {"label", MakeValue(std::string("Fixture"))}, {"imageList", MakeValue(sgre::resource::PsbArray{std::move(entry)})}});

	return document;
}

static sgre::resource::PsbValue& Field(sgre::resource::PsbValue& value, std::string_view name) {
	for (auto& member : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (member.first == name) {
			return member.second;
		}
	}

	throw std::runtime_error("missing test field");
}

static void TestMetadataAndFailures() {
	auto document = MakeDocument();
	const auto resource = sgre::resource::ReadImageResource(document);
	Require(resource.Label == "Fixture" && resource.Images.size() == 1 && resource.Images[0].Label == "Synthetic", "image labels changed");
	Require(resource.Images[0].Tiles[0].Texture.PixelRange.Offset == 5, "image chunk identity changed");
	auto texture_value = TextureValue("A8");
	texture_value.SerializedOffset = 0x400;
	Require(sgre::resource::ReadImageTexture(document, texture_value).SerializedValueOffset == texture_value.SerializedOffset, "texture metadata must preserve native serialized value identity separately from pixel chunk identity");
	Field(document.Root, "imageList").Data = sgre::resource::PsbArray{};
	Require(sgre::resource::ReadImageResource(document).Images.empty(), "original empty movie-reference imageList was rejected");
	Field(document.Root, "version").Data = 1.1;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadImageResource(document));
	},
	    "schema version");
	document = MakeDocument();
	document.Header.Flags = 1;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadImageResource(document));
	},
	    "unfiltered PSB");
	document.Header.Flags = 0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadImageTexture(document, TextureValue("BC6H")));
	},
	    "unsupported image texture type");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadImageTexture(document, TextureValue("A8", 1)));
	},
	    "chunk index");
	document.ExtraChunks = document.Chunks;
	const auto extra = sgre::resource::ReadImageTexture(document, TextureValue("A8", 0, true));
	Require(extra.PixelRange.Offset == 5, "extra chunk reference changed");
	auto value = TextureValue();
	Field(value, "width").Data = 2.5;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadImageTexture(document, value));
	},
	    "width");
	Field(value, "width").Data = std::numeric_limits<double>::quiet_NaN();
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadImageTexture(document, value));
	},
	    "width");
	value = TextureValue();
	Field(value, "truncated_width").Data = std::int64_t{3};
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadImageTexture(document, value));
	},
	    "truncated dimensions");
	document.Chunks[0].Size = 3;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadImageTexture(document, TextureValue()));
	},
	    "chunk length");
}

static void TestDecodeSafety() {
	const std::array<std::uint8_t, 4> bytes{};
	auto texture = MakeTexture(sgre::resource::PixelFormat::Alpha8, 2, 2, 0, 4);
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::DecodeTexture(texture, bytes, 15));
	},
	    "allocation limit");
	texture.PixelRange.Offset = std::numeric_limits<std::uint64_t>::max();
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::DecodeTexture(texture, bytes));
	},
	    "pixel range");
	texture = MakeTexture(sgre::resource::PixelFormat::Bc7, 4, 4, 0, 16);
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::DecodeTexture(texture, bytes));
	},
	    "pixel range");
	texture.Width = std::numeric_limits<std::uint32_t>::max();
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::DecodeTexture(texture, bytes));
	},
	    "texture dimensions");
	texture.Width = 0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::DecodeTexture(texture, bytes));
	},
	    "texture dimensions");
}

static void TestStoredBc7Views() {
	const auto description = MakeTexture(sgre::resource::PixelFormat::Bc7, 4, 4, 3, 16);
	for (const auto& vector : kBc7Vectors) {
		std::vector<std::uint8_t> input{17, 18, 19};
		input.insert(input.end(), vector.Block.begin(), vector.Block.end());
		const auto view = sgre::resource::BorrowBc7Texture(description, input, 64);
		Require(view.Width == 4 && view.Height == 4 && view.BlockRowBytes == 16 && view.Blocks.size() == 16 && view.Blocks.data() == input.data() + 3, "BC7 view must borrow the exact original block without decoded storage");
		Require(std::equal(view.Blocks.begin(), view.Blocks.end(), vector.Block.begin()), "BC7 block mode or byte ordering changed before upload");
	}

	std::array<std::uint8_t, 64> blocks{};
	for (std::size_t i = 0; i < 4; ++i) {
		std::copy(kBc7Vectors[i].Block.begin(), kBc7Vectors[i].Block.end(), blocks.begin() + static_cast<std::ptrdiff_t>(i * 16));
	}
	auto odd = MakeTexture(sgre::resource::PixelFormat::Bc7, 5, 5, 0, 64);
	const auto view = sgre::resource::BorrowBc7Texture(odd, blocks, 100);
	Require(view.Width == 5 && view.Height == 5 && view.BlockRowBytes == 32 && view.Blocks.size() == 64, "partial BC7 edges must retain the complete final row and column blocks");
	RequireFailure([&] {
		static_cast<void>(sgre::resource::BorrowBc7Texture(odd, blocks, 99));
	}, "decoded extent");
	odd.PixelRange.Size = 63;
	RequireFailure([&] {
		static_cast<void>(sgre::resource::BorrowBc7Texture(odd, blocks));
	}, "pixel range");
	odd.PixelRange.Size = 64;
	odd.PixelRange.Offset = std::numeric_limits<std::uint64_t>::max();
	RequireFailure([&] {
		static_cast<void>(sgre::resource::BorrowBc7Texture(odd, blocks));
	}, "pixel range");
	odd.PixelRange.Offset = 0;
	odd.Width = 0;
	RequireFailure([&] {
		static_cast<void>(sgre::resource::BorrowBc7Texture(odd, blocks));
	}, "texture dimensions");
	odd.Width = std::numeric_limits<std::uint32_t>::max();
	RequireFailure([&] {
		static_cast<void>(sgre::resource::BorrowBc7Texture(odd, blocks));
	}, "texture dimensions");
	odd.Format = sgre::resource::PixelFormat::Alpha8;
	RequireFailure([&] {
		static_cast<void>(sgre::resource::BorrowBc7Texture(odd, blocks));
	}, "actual BC7");
}

int main() {
	try {
		TestBc7IndependentVectors();
		TestPartialBc7BlocksAndOrder();
		TestNativeSingleChannelFormats();
		TestTileAssemblyAndPadding();
		TestMetadataAndFailures();
		TestDecodeSafety();
		TestStoredBc7Views();
		std::cout << "Image metadata, all BC7 modes, native channels, tile assembly, and safety checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
