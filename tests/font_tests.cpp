#include "sgre/resource/font.hpp"

#include <array>
#include <bit>
#include <cmath>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "font failure omitted required context");

		return;
	}

	throw std::runtime_error("invalid font was accepted: " + std::string(context));
}

template <typename Value>
static sgre::resource::PsbValue MakeValue(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue MakeGlyph() {
	return MakeValue(sgre::resource::PsbObject{{"id", MakeValue(std::int64_t{0})}, {"x", MakeValue(std::int64_t{1})}, {"y", MakeValue(std::int64_t{2})}, {"width", MakeValue(std::int64_t{5})}, {"height", MakeValue(std::int64_t{6})}, {"w", MakeValue(std::int64_t{3})}, {"h", MakeValue(std::int64_t{4})}, {"a", MakeValue(std::int64_t{2})}, {"b", MakeValue(std::int64_t{9})}, {"d", MakeValue(std::int64_t{10})}});
}

static sgre::resource::PsbDocument MakeDocument() {
	sgre::resource::PsbChunkReference reference;
	sgre::resource::PsbValue source = MakeValue(sgre::resource::PsbObject{{"type", MakeValue(std::string("A8"))}, {"width", MakeValue(std::int64_t{8})}, {"height", MakeValue(std::int64_t{8})}, {"truncated_width", MakeValue(std::int64_t{8})}, {"truncated_height", MakeValue(std::int64_t{8})}, {"pixel", MakeValue(reference)}});
	sgre::resource::PsbDocument document;
	document.Header.Version = 4;
	sgre::resource::PsbChunkRange range;
	range.Size = 64;
	document.Chunks.push_back(range);
	document.Root = MakeValue(sgre::resource::PsbObject{{"id", MakeValue(std::string("font"))}, {"spec", MakeValue(std::string("win"))}, {"version", MakeValue(1.0800000429153442)}, {"label", MakeValue(std::string("Synthetic font"))}, {"minWidth", MakeValue(std::int64_t{3})}, {"minHeight", MakeValue(std::int64_t{6})}, {"maxWidth", MakeValue(std::int64_t{8})}, {"maxHeight", MakeValue(std::int64_t{6})}, {"source", MakeValue(sgre::resource::PsbArray{std::move(source)})}, {"code", MakeValue(sgre::resource::PsbObject{{"A", MakeGlyph()}, {"あ", MakeGlyph()}})}});

	return document;
}

static sgre::resource::PsbValue& Field(sgre::resource::PsbValue& value, std::string_view name) {
	for (auto& member : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (member.first == name) {
			return member.second;
		}
	}

	throw std::runtime_error("missing font test field");
}

static void TestMetadataAndLookup() {
	const auto document = MakeDocument();
	const auto font = sgre::resource::ReadBitmapFont(document);
	Require(font.Label == "Synthetic font" && font.Glyphs.size() == 2 && font.Sources.size() == 1, "font identity or entries changed");
	const auto* ascii = font.FindGlyph("A");
	const auto* unicode = font.FindGlyph("あ");
	Require(ascii != nullptr && ascii->Codepoint == 65 && unicode != nullptr && unicode->Codepoint == 0x3042, "UTF-8 glyph identity changed");
	Require(font.FindGlyph("a") == nullptr && font.FindGlyph("AA") == nullptr, "glyph lookup normalized or silently substituted its key");
	Require(ascii->NominalWidth == 5 && ascii->NominalHeight == 6 && ascii->InkWidth == 3 && ascii->InkHeight == 4, "font nominal metrics were conflated with atlas metrics");
	Require(ascii->VerticalBearing == 2 && ascii->MetricB == 9 && ascii->MetricD == 10, "recovered font metrics changed");
	Require(font.MinWidth == 3 && font.MaxWidth == 8 && font.MinHeight == 6 && font.MaxHeight == 6, "font global bounds changed");
}

static void TestNativeQuadFormula() {
	const auto font = sgre::resource::ReadBitmapFont(MakeDocument());
	const auto& glyph = *font.FindGlyph("A");
	const auto quad = sgre::resource::EvaluateGlyphQuad(glyph, font.Sources[0], 10, 20, 2, 3);
	Require(quad.Left == 10 && quad.Right == 16 && quad.Top == 14 && quad.Bottom == 26, "native glyph placement or vertical bearing changed");
	Require(quad.U0 == 0.125f && quad.V0 == 0.25f && quad.U1 == 0.5f && quad.V1 == 0.75f, "glyph atlas coordinates changed");
	const auto mirrored = sgre::resource::EvaluateGlyphQuad(glyph, font.Sources[0], 10, 20, -2, 0);
	Require(mirrored.Left == 10 && mirrored.Right == 4 && mirrored.Top == 20 && mirrored.Bottom == 20, "finite mirrored or collapsed native scaling was rejected");
	auto below_baseline = glyph;
	below_baseline.VerticalBearing = -2;
	const auto below = sgre::resource::EvaluateGlyphQuad(below_baseline, font.Sources[0], 10, 20, 2, 3);
	Require(below.Top == 26 && below.Bottom == 38, "negative glyph bearing changed");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::EvaluateGlyphQuad(glyph, font.Sources[0], 0, 0, 1, std::numeric_limits<float>::infinity()));
	},
	    "nonfinite");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::EvaluateGlyphQuad(glyph, font.Sources[0], 0, 0, std::numeric_limits<float>::max(), 1));
	},
	    "overflows");
}

struct NativeGlyphQuadFixture {
	// Atlas x/y, ink width/height, bearing, draw x/y, and final x/y scales.
	std::array<std::uint32_t, 9> Input{};
	std::array<std::uint32_t, 8> Output{};
};

static void TestOriginalQueuedGlyphGeometry() {
	// Original 0x1404c1de0..0x1404c1ecd blocks include queue rounding and endpoint subtraction.
	const std::array<NativeGlyphQuadFixture, 3> fixtures = {{
	    {{0x42fa0000, 0x43958000, 0x43750000, 0x43210000, 0xc1e80000, 0x44ae04a1, 0x43573ef2, 0xbffd51e5, 0x40059587}, {0x44ae04a1, 0x4389e35a, 0x4462d192, 0x4418f4b6, 0x3dfa0000, 0x3e958000, 0x3eb90000, 0x3ee60000}},
	    {{0x43acbf3d, 0x43377a48, 0x43a37679, 0x43299f57, 0xc16e1548, 0xc40e9808, 0x4455e27a, 0x4073c656, 0x406f201c}, {0xc40e9808, 0x4463c8b6, 0x4428b82a, 0x44c11ce0, 0x3eacbf3d, 0x3e377a48, 0x3f281adb, 0x3eb08cd0}},
	    {{0x433696f3, 0x4379761f, 0x42619f22, 0x42efe505, 0x40d375dd, 0x44661831, 0xc48bbeb1, 0x3e50d6a9, 0xc057abe1}, {0x44661831, 0xc488f619, 0x4468f86b, 0xc4bb7cb3, 0x3e3696f3, 0x3e79761f, 0x3e6efebc, 0x3eb8b451}},
	}};
	for (const auto& fixture : fixtures) {
		sgre::resource::BitmapGlyph glyph;
		glyph.AtlasX = std::bit_cast<float>(fixture.Input[0]);
		glyph.AtlasY = std::bit_cast<float>(fixture.Input[1]);
		glyph.InkWidth = std::bit_cast<float>(fixture.Input[2]);
		glyph.InkHeight = std::bit_cast<float>(fixture.Input[3]);
		glyph.VerticalBearing = std::bit_cast<float>(fixture.Input[4]);
		sgre::resource::ImageTexture source;
		source.Width = 1024;
		source.Height = 1024;
		const auto quad = sgre::resource::EvaluateGlyphQuad(glyph, source, std::bit_cast<float>(fixture.Input[5]), std::bit_cast<float>(fixture.Input[6]), std::bit_cast<float>(fixture.Input[7]), std::bit_cast<float>(fixture.Input[8]));
		const std::array<float, 8> actual{quad.Left, quad.Top, quad.Right, quad.Bottom, quad.U0, quad.V0, quad.U1, quad.V1};
		for (std::size_t index = 0; index < actual.size(); ++index) {
			Require(std::bit_cast<std::uint32_t>(actual[index]) == fixture.Output[index], "queued bitmap glyph geometry differs from original instructions");
		}
	}
}

static void TestNativeCodeUnitLookup() {
	sgre::resource::BitmapFont font;
	const std::array<std::pair<std::uint16_t, std::string_view>, 9> keys = {{{0x7f, "\x7f"}, {0x80, "\xc2\x80"}, {0x7ff, "\xdf\xbf"}, {0x800, "\xe0\xa0\x80"}, {0x3042, "\xe3\x81\x82"}, {0xd7ff, "\xed\x9f\xbf"}, {0xe000, "\xee\x80\x80"}, {0xffff, "\xef\xbf\xbf"}, {0xfffd, "\xef\xbf\xbd"}}};
	for (const auto& [code, key] : keys) {
		sgre::resource::BitmapGlyph glyph;
		glyph.Codepoint = code;
		font.Glyphs.emplace(key, glyph);
	}

	for (const auto& [code, key] : keys) {
		Require(font.FindGlyphCodeUnit(code) == font.FindGlyph(key), "native UTF-16 code-unit lookup changed its UTF-8 key");
	}

	const auto* replacement = font.FindGlyph("\xef\xbf\xbd");
	for (std::uint32_t code = 0xd800; code <= 0xdfff; ++code) {
		Require(font.FindGlyphCodeUnit(static_cast<std::uint16_t>(code)) == replacement, "single native surrogate did not select the replacement key");
	}

	Require(font.FindGlyphCodeUnit(0) == nullptr && font.FindGlyphCodeUnit(0x41) == nullptr, "missing code unit silently substituted a glyph");
}

static void TestNominalMeasurement() {
	const auto font = sgre::resource::ReadBitmapFont(MakeDocument());
	const auto* glyph = font.FindGlyph("A");
	const auto size = sgre::resource::MeasureBitmapGlyph(glyph, 2, 3);
	Require(size.Width == 10 && size.Height == 18, "measurement used ink size or bearing instead of nominal metrics");
	const auto mirrored = sgre::resource::MeasureBitmapGlyph(glyph, -2, 0);
	Require(mirrored.Width == -10 && mirrored.Height == 0, "finite mirrored/collapsed font measurement changed");
	const auto missing = sgre::resource::MeasureBitmapGlyph(nullptr, 2, 3);
	Require(missing.Width == 0 && missing.Height == 0, "missing glyph measurement substituted nominal dimensions");
	RequireFailure([&] {
		static_cast<void>(sgre::resource::MeasureBitmapGlyph(glyph, std::numeric_limits<float>::infinity(), 1));
	},
	    "nonfinite");
	RequireFailure([&] {
		static_cast<void>(sgre::resource::MeasureBitmapGlyph(glyph, std::numeric_limits<float>::max(), 1));
	},
	    "overflows");

	auto invalid = *glyph;
	invalid.NominalWidth = -1;
	RequireFailure([&] {
		static_cast<void>(sgre::resource::MeasureBitmapGlyph(&invalid, 1, 1));
	},
	    "nominal metrics");
}

struct NativeMeasurementFixture {
	std::array<std::uint32_t, 4> Inputs{};
	std::array<std::uint32_t, 2> Output{};
};

static void TestOriginalMeasurementVectors() {
	// Original cached-glyph lookup and 0x1404c1700 produced these nominal size words.
	const std::array<NativeMeasurementFixture, 6> fixtures = {{
	    {{0x4331973a, 0x42f24009, 0x4004bb4d, 0x4037517c}, {0x43b827d2, 0x43ad78e2}},
	    {{0x4336e4fa, 0x4289ea10, 0xbf9c3419, 0x3f895ad5}, {0xc35f3181, 0x4293fe63}},
	    {{0x4060a427, 0x4337d792, 0xbf52d6f0, 0x3ab087bf}, {0xc0390344, 0x3e7d8b60}},
	    {{0x42da62c4, 0x428481a2, 0x3ea28b82, 0xbfbd3124}, {0x420aa983, 0xc2c3da48}},
	    {{0x40d2be19, 0x43092421, 0x4035048f, 0x3f402169}, {0x41950428, 0x42cdd9fd}},
	    {{0x42aec06c, 0x4212e594, 0x400074be, 0xc02d24b1}, {0x432f5fce, 0xc2c6b466}},
	}};
	for (const auto& fixture : fixtures) {
		sgre::resource::BitmapGlyph glyph;
		glyph.NominalWidth = std::bit_cast<float>(fixture.Inputs[0]);
		glyph.NominalHeight = std::bit_cast<float>(fixture.Inputs[1]);
		const auto actual = sgre::resource::MeasureBitmapGlyph(&glyph, std::bit_cast<float>(fixture.Inputs[2]), std::bit_cast<float>(fixture.Inputs[3]));
		Require(std::bit_cast<std::uint32_t>(actual.Width) == fixture.Output[0] && std::bit_cast<std::uint32_t>(actual.Height) == fixture.Output[1], "nominal bitmap measurement differs from original instructions");
	}
}

static void TestSchemaAndAtlasSafety() {
	auto document = MakeDocument();
	Field(document.Root, "version").Data = 1.09;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadBitmapFont(document));
	},
	    "schema version");
	document = MakeDocument();
	Field(document.Root, "spec").Data = std::string("other");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadBitmapFont(document));
	},
	    "spec=win");
	document = MakeDocument();
	Field(document.Root, "minWidth").Data = std::int64_t{9};
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadBitmapFont(document));
	},
	    "minimum metrics");
	document = MakeDocument();
	Field(Field(Field(document.Root, "code"), "A"), "id").Data = 0.5;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadBitmapFont(document));
	},
	    "source index");
	document = MakeDocument();
	Field(Field(Field(document.Root, "code"), "A"), "x").Data = std::int64_t{7};
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadBitmapFont(document));
	},
	    "rectangle exceeds");
	document = MakeDocument();
	Field(Field(Field(document.Root, "code"), "A"), "a").Data = std::numeric_limits<double>::quiet_NaN();
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadBitmapFont(document));
	},
	    "metric limit");
	document = MakeDocument();
	Field(document.Root, "source").Data = sgre::resource::PsbArray{};
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadBitmapFont(document));
	},
	    "source array");
}

static void TestUtf8AndDuplicateSafety() {
	const std::vector<std::string> malformed = {"", "AA", std::string("\xc0\xaf", 2), std::string("\xe0\x80\x80", 3), std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4), std::string("\xe3\x01\x82", 3)};
	for (const auto& key : malformed) {
		auto document = MakeDocument();
		Field(document.Root, "code").Data = sgre::resource::PsbObject{{key, MakeGlyph()}};
		RequireFailure([&]() {
			static_cast<void>(sgre::resource::ReadBitmapFont(document));
		},
		    "UTF-8");
	}

	auto document = MakeDocument();
	Field(document.Root, "code").Data = sgre::resource::PsbObject{{"A", MakeGlyph()}, {"A", MakeGlyph()}};
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadBitmapFont(document));
	},
	    "duplicate");
}

int main() {
	try {
		TestMetadataAndLookup();
		TestNativeQuadFormula();
		TestOriginalQueuedGlyphGeometry();
		TestNativeCodeUnitLookup();
		TestNominalMeasurement();
		TestOriginalMeasurementVectors();
		TestSchemaAndAtlasSafety();
		TestUtf8AndDuplicateSafety();
		std::cout << "Bitmap font schema, UTF-8 lookup, native quad placement, and safety checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
