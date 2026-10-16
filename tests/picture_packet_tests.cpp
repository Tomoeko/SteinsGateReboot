#include "sgre/scene/picture_packet.hpp"

#include "picture_packet_native_vectors.hpp"

#include <bit>
#include <fstream>
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

static void CompareNativeRow(const std::array<std::uint32_t, 32>& row) {
	sgre::scene::PictureLayerTaskState state;
	state.Coordinate = {std::bit_cast<float>(row[0]), std::bit_cast<float>(row[1])};
	state.Opacity = row[12];
	state.BlendMode = row[13];
	sgre::scene::PictureRegion region;
	for (std::size_t index = 0; index != 4; ++index) {
		region.Destination[index] = std::bit_cast<float>(row[index + 4]);
		region.SourceBounds[index] = std::bit_cast<float>(row[index + 8]);
	}
	const auto packet = sgre::scene::PreparePictureDrawPacket(state, region, {static_cast<float>(row[2]), static_cast<float>(row[3])});
	Require(packet.Color == row[30] && packet.TextureAttributes == row[31], "Picture raw opacity/attribute words differ from original native packet");
	for (std::size_t index = 0; index != 4; ++index) {
		const auto& vertex = packet.Vertices[index];
		const std::array<float, 4> actual{vertex.Position.X, vertex.Position.Y, vertex.Uv.X, vertex.Uv.Y};
		for (std::size_t word = 0; word != 4; ++word) {
			Require(std::bit_cast<std::uint32_t>(actual[word]) == row[14 + index * 4 + word], "Picture XYUV word differs from original native nonindexed strip");
		}
	}
}

static void TestActualPaddedTwoTileDomain() {
	sgre::scene::PictureLayerTaskState state;
	state.Coordinate = {-960, -540};
	sgre::scene::PictureRegion first;
	first.Destination = {0, 0, 1920, 1024};
	first.SourceBounds = {0, 0, 1920, 1024};
	const auto a = sgre::scene::PreparePictureDrawPacket(state, first, {2048, 1024});
	Require(a.Vertices[0].Position.X == -960 && a.Vertices[0].Position.Y == -540 && a.Vertices[3].Position.X == 960 && a.Vertices[3].Position.Y == 484 && a.Vertices[3].Uv.X == 0.9375f && a.Vertices[3].Uv.Y == 1 && a.TextureAttributes == 4, "Picture first padded source differs from actual logical crop and native texture-normalized UV");
	sgre::scene::PictureRegion second;
	second.Destination = {0, 1024, 1920, 1080};
	second.SourceBounds = {0, 0, 1920, 56};
	const auto b = sgre::scene::PreparePictureDrawPacket(state, second, {2048, 64});
	Require(b.Vertices[0].Position.Y == 484 && b.Vertices[3].Position.Y == 540 && b.Vertices[3].Uv.X == 0.9375f && b.Vertices[3].Uv.Y == 0.875f, "Picture second tile must divide its actual cropped56 rows by physical64, retaining adjacent logical destination");
	state.Opacity = 0x1ff;
	const auto opaque_low_byte = sgre::scene::PreparePictureDrawPacket(state, second, {2048, 64});
	Require(opaque_low_byte.Color == 0xffffffff && opaque_low_byte.TextureAttributes == 0, "Picture full opacity word differs from its renderer low byte");
	bool rejected = false;
	state.Coordinate.X = std::numeric_limits<float>::quiet_NaN();
	try {
		static_cast<void>(sgre::scene::PreparePictureDrawPacket(state, first, {2048, 1024}));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "Picture packet accepted nonfinite geometry");
}

static void TestNativeChannelAndSamplerSelection() {
	for (const auto format : {sgre::resource::PixelFormat::Bc7, sgre::resource::PixelFormat::Alpha8, sgre::resource::PixelFormat::Luminance8}) {
		const auto base = format == sgre::resource::PixelFormat::Bc7 ? 180u : format == sgre::resource::PixelFormat::Alpha8 ? 360u
		                                                                                                                    : 540u;
		for (const auto attributes : {0u, 4u}) {
			for (std::int32_t minimum = 0; minimum != 2; ++minimum) {
				for (std::int32_t maximum = 0; maximum != 2; ++maximum) {
					const auto pipeline = sgre::scene::PreparePicturePipeline(format, attributes, 0, minimum, maximum);
					const auto expected_filter = minimum == maximum ? (minimum == 0 ? 0u : 21u) : 4u;
					Require(pipeline.NativeVertexShaderIndex == 4 && pipeline.NativePixelShaderIndex == base + (attributes == 0 ? 60u : 0u), "Picture shader channel class differs from original format28/98/65/61 draw witnesses");
					Require(pipeline.Sampler.NativeFilterCode == expected_filter && pipeline.Sampler.AddressU == sgre::scene::TextureAddress::Mirror && pipeline.Sampler.AddressV == sgre::scene::TextureAddress::Mirror, "Picture sampler differs from native mirror-addressed filter0/4/21");
					Require(pipeline.Sampler.Minification == (minimum == 1 && maximum == 1 ? sgre::scene::TextureSampling::Linear : sgre::scene::TextureSampling::Nearest) && pipeline.Sampler.Magnification == (minimum != maximum || maximum == 1 ? sgre::scene::TextureSampling::Linear : sgre::scene::TextureSampling::Nearest), "Picture mixed filter must preserve native min-nearest/mag-linear projection");
				}
			}
		}
	}
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<char, 4> bytes{};
	input.read(bytes.data(), bytes.size());
	Require(static_cast<bool>(input), "Picture native word stream is truncated");

	return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24);
}

int main(int argc, char** argv) {
	try {
		for (const auto& row : kPicturePacketNativeRows) {
			CompareNativeRow(row);
		}
		TestActualPaddedTwoTileDomain();
		TestNativeChannelAndSamplerSelection();
		if (argc == 2) {
			std::ifstream input(argv[1], std::ios::binary);
			Require(static_cast<bool>(input) && ReadWord(input) == 0x50494354, "Picture native word stream has invalid header");
			const auto count = ReadWord(input);
			Require(count <= 4096, "Picture native word stream exceeds bounded comparison count");
			for (std::uint32_t index = 0; index != count; ++index) {
				std::array<std::uint32_t, 32> row{};
				for (auto& word : row) {
					word = ReadWord(input);
				}
				CompareNativeRow(row);
			}
			Require(input.peek() == std::char_traits<char>::eof(), "Picture native word stream has trailing bytes");
		}
		Require(argc <= 2, "Picture packet fixture accepts one optional native word stream");
		std::cout << "Picture packet native words, padded two-tile source and raw opacity passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
