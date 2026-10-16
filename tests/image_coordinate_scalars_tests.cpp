#include "image_fixture.hpp"

#include "image_coordinate_native_vectors.hpp"

#include <cmath>
#include <limits>

static sgre::resource::PsbValue Scalar(std::uint8_t kind, std::uint64_t bits) {
	if (kind == 2 || kind == 3) {
		sgre::resource::PsbValue value;
		value.Data = kind == 2;

		return value;
	}
	if (kind == 8) {
		return Integer(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
	}
	if (kind == 12) {
		return Integer(std::bit_cast<std::int64_t>(bits));
	}

	sgre::resource::PsbValue value;
	if (kind == 29) {
		value.Data = 0.0;
	} else if (kind == 30) {
		value.Data = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
	} else if (kind == 31) {
		value.Data = std::bit_cast<double>(bits);
	} else {
		throw std::runtime_error("unexpected scalar kind in native Image fixture");
	}

	return value;
}

static void TestNativeCoordinateWords() {
	ImageFixture fixture;
	for (const auto& row : kImageScalarNativeRows) {
		const auto field = [&](std::size_t index) {
			return Scalar(row.Kinds[index], row.Bits[index]);
		};
		const auto tile = Object({{"left", field(2)}, {"top", field(3)}, {"width", field(4)}, {"height", field(5)}, {"image", Texture(0)}});
		const auto document = Document({Object({{"label", Text("scalar")}, {"width", field(0)}, {"height", field(1)}, {"texture", Array({tile})}})});
		const auto asset = fixture.Catalog.Prepare(document, fixture.Backing);
		Require(asset->Valid && asset->Model.Width == row.Output[0] && asset->Model.Height == row.Output[1] && asset->TileBounds.size() == 1, "native Image scalar dimensions differ from original constructor output");
		for (std::size_t index = 0; index < 4; ++index) {
			Require(std::bit_cast<std::uint32_t>(asset->TileBounds[0].Bounds[index]) == row.Output[index + 2], "native Image scalar tile bounds differ from original constructor words");
		}
	}
}

static void RequireCoordinateRejection(const sgre::resource::PsbValue& coordinate) {
	ImageFixture fixture;
	const auto tile = Object({{"left", coordinate}, {"top", Integer(0)}, {"width", Integer(1)}, {"height", Integer(1)}, {"image", Texture(0)}});
	bool rejected = false;
	try {
		static_cast<void>(fixture.Catalog.Prepare(Document({Entry("invalid", 1, 1, {tile})}), fixture.Backing));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && fixture.Uploads == 0 && fixture.Catalog.DocumentCount() == 0, "unsupported Image scalar must fail before cache or catalog publication");
}

static void TestNonnumericAndBoundedFailures() {
	RequireCoordinateRejection({});
	RequireCoordinateRejection(Text("0"));
	RequireCoordinateRejection(Object({}));
	for (const auto number : {-1.00001, 16385.0, 4294967297.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
		sgre::resource::PsbValue value;
		value.Data = number;
		RequireCoordinateRejection(value);
	}
}

int main() {
	try {
		TestNativeCoordinateWords();
		TestNonnumericAndBoundedFailures();
		std::cout << "Image coordinates: 64 original constructor cases, 384 words and bounded nonnumeric failures passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
