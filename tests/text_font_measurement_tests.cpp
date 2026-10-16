#include "sgre/script/text_font_info.hpp"

#include "sgre/text/formatting.hpp"

#include "font_info_fixture.hpp"
#include "vector_font_fixture.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("text font measurement fixture expected failure");
}

class FontMeasurementFixture {
public:
	FontMeasurementFixture() : Objects(Vm), Psb(Vm, Objects), FontBytes(std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont())), Vectors(Vm, Objects, [this](auto, auto) {
		                           return [this]() -> std::optional<sgre::script::ScriptResourceAsset> {
			                           sgre::script::ScriptResourceAsset asset;
			                           asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			                           asset.Backing = FontBytes;

			                           return asset;
		                           };
	                           },
	                                                                                                                                                          [](auto, auto) {
	                                                                                                                                                          }),
	                           Textures([this](auto, const auto&) {
		                           return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	                           },
	                               [](auto) {
	                               }),
	                           Catalog(Textures) {
	}

	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	std::shared_ptr<const std::vector<std::uint8_t>> FontBytes;
	sgre::script::ScriptVectorFontBindings Vectors;
	std::uint32_t Uploads = 0;
	sgre::scene::TextureCache Textures;
	sgre::scene::BitmapFontAssetCatalog Catalog;
};

static std::shared_ptr<sgre::resource::PsbDocument> SpaceDocument() {
	auto document = font_info_test::Document();
	for (auto& [name, value] : std::get<sgre::resource::PsbObject>(document->Root.Data)) {
		if (name == "code") {
			auto& codes = std::get<sgre::resource::PsbObject>(value.Data);
			const auto glyph = codes.front().second;
			codes.emplace_back("\xe3\x80\x80", glyph);

			return document;
		}
	}

	throw std::runtime_error("measurement PSB fixture has no glyph table");
}

static void TestActualBitmapAndMissingUnit() {
	FontMeasurementFixture fixture;
	const auto source = fixture.Psb.WrapDocument(SpaceDocument(), font_info_test::Backing());
	sgre::script::ScriptTextFontInfo font(fixture.Vm, fixture.Objects, fixture.Psb, fixture.Vectors, fixture.Catalog, fixture.Textures, source, {});
	font.SetSize(18);
	const auto metrics = font.MeasureCodeUnit(0x3000);
	Require(metrics.Width == 15 && metrics.Height == 18 && fixture.Uploads == 2, "actual FontInfo bitmap unit route must delegate nominal sizing through its existing scaled provider");
	const auto missing = font.MeasureCodeUnit(u'Z');
	Require(missing.Width == 0 && missing.Height == 0, "missing bitmap unit must preserve native zero dimensions");
	font.Close();
	RequireFailure([&] {
		(void)font.MeasureCodeUnit(u'A');
	},
	    "retired");
	fixture.Vm.Close();
}

static void TestUnavailableIsAnActualZeroProviderBranch() {
	FontMeasurementFixture fixture;
	sgre::script::ScriptTextFontInfo font(fixture.Vm, fixture.Objects, fixture.Psb, fixture.Vectors, fixture.Catalog, fixture.Textures, std::int32_t{17}, {});
	font.SetSize(24);
	const auto metrics = font.MeasureCodeUnit(0x3000);
	Require(metrics.Width == 0 && metrics.Height == 0 && fixture.Uploads == 0 && font.Kind() == sgre::script::TextFontProviderKind::Unavailable, "native null private interface must return zero without manufacturing a font or texture");
	font.Close();
	fixture.Vm.Close();
}

static void TestRealProviderFontResetAndVerticalWidth() {
	FontMeasurementFixture fixture;
	const auto source = fixture.Psb.WrapDocument(SpaceDocument(), font_info_test::Backing());
	sgre::script::ScriptTextFontInfo normal(fixture.Vm, fixture.Objects, fixture.Psb, fixture.Vectors, fixture.Catalog, fixture.Textures, source, {});
	sgre::script::ScriptTextFontInfo ruby(fixture.Vm, fixture.Objects, fixture.Psb, fixture.Vectors, fixture.Catalog, fixture.Textures, source, {});
	sgre::text::TextFormattingState state;
	state.FontScale = 2;
	sgre::text::TextFontResetCallbacks callbacks;
	callbacks.SelectNormal = [&] {
		const auto request = sgre::text::PrepareTextFontSelectionRequest(state, sgre::text::TextFontFamily::Normal, false);
		normal.SetSize(request.Key.RequestedSize);
		normal.SetFiltering(request.Filtering);
	};
	callbacks.MeasureNormal = [&](std::uint16_t unit) {
		return normal.MeasureCodeUnit(unit);
	};
	callbacks.SelectRuby = [&] {
		const auto request = sgre::text::PrepareTextFontSelectionRequest(state, sgre::text::TextFontFamily::Ruby, false);
		ruby.SetSize(request.Key.RequestedSize);
		ruby.SetFiltering(request.Filtering);
	};
	sgre::text::ResetTextFontDefaults(state, callbacks);
	Require(state.Current.FontBaseWidth == 40 && normal.BitmapScale().Width == 8 && ruby.BitmapScale().Width == 20.0f / 6.0f, "reset must use real PSB unit measurement after native scaled normal/ruby size requests");
	state.Vertical = true;
	state.Defaults.FontSize = 25;
	sgre::text::ResetTextFontDefaults(state, callbacks);
	Require(state.Current.FontBaseWidth == 50, "vertical reset must still measure its provider and then select current size multiplied by scale");
	normal.Close();
	ruby.Close();
	fixture.Vm.Close();
}

static void TestVectorUnitAndExpiredSourceAreExplicit() {
	FontMeasurementFixture fixture;
	const std::array<sgre::script::SquirrelValue, 2> arguments{fixture.Vm.RootTable(), std::string("font.otf")};
	const auto source = fixture.Vm.Invoke(fixture.Vectors.VectorFontClass(), arguments).Value;
	const std::array<sgre::script::SquirrelValue, 1> receiver{source};
	Require(!std::get<bool>(fixture.Vm.Invoke(fixture.Vm.GetSlot(source, std::string("getLoading")), receiver).Value), "measurement vector source did not complete its actual resource");
	sgre::script::ScriptTextFontInfo vector(fixture.Vm, fixture.Objects, fixture.Psb, fixture.Vectors, fixture.Catalog, fixture.Textures, source, {});
	vector.SetSize(24);
	const auto unit = vector.MeasureCodeUnit(0x3000);
	Require(unit.Width == 24 && unit.Height == 0 && fixture.Vectors.Provider(source).SizeState().Word == 24 && fixture.Uploads == 0, "actual vector unit measurement must apply its private size and original missing-glyph advance without bitmap synthesis");
	RequireFailure([&] {
		static_cast<void>(vector.MeasureCodeUnit(0x301));
	},
	    "mark/control");
	const auto bitmap_source = fixture.Psb.WrapDocument(SpaceDocument(), font_info_test::Backing());
	const auto weak = fixture.Vm.CreateWeakReference(bitmap_source);
	sgre::script::ScriptTextFontInfo bitmap(fixture.Vm, fixture.Objects, fixture.Psb, fixture.Vectors, fixture.Catalog, fixture.Textures, weak, {});
	fixture.Objects.ReleaseInstance(bitmap_source);
	RequireFailure([&] {
		(void)bitmap.MeasureCodeUnit(u'A');
	},
	    "retired during");
	bitmap.Close();
	vector.Close();
	fixture.Vm.Close();
}

int main() {
	try {
		TestActualBitmapAndMissingUnit();
		TestUnavailableIsAnActualZeroProviderBranch();
		TestRealProviderFontResetAndVerticalWidth();
		TestVectorUnitAndExpiredSourceAreExplicit();
		std::cout << "Four independent actual FontInfo unit measurement/reset groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
