#include "sgre/scene/bitmap_indicator.hpp"

#include <array>
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

static sgre::resource::BitmapFont MakeFont() {
	sgre::resource::BitmapFont font;
	font.MaxWidth = 16;
	font.MaxHeight = 12;
	const std::array<std::string_view, 3> keys{{"A", "B", "C"}};
	const std::array<sgre::resource::GlyphSize, 3> sizes{{{5, 6}, {2, 9}, {7, 3}}};
	for (std::size_t index = 0; index < keys.size(); ++index) {
		sgre::resource::BitmapGlyph glyph;
		glyph.NominalWidth = sizes[index].Width;
		glyph.NominalHeight = sizes[index].Height;
		font.Glyphs.emplace(keys[index], glyph);
	}

	return font;
}

static void TestOwnedFontAndMeasuredRefresh() {
	auto font = MakeFont();
	sgre::scene::BitmapIndicator indicator(font);
	font.Glyphs.at("A").NominalWidth = 999;
	Require(!indicator.Placement().Visible() && indicator.FontScale().Width == 1 && indicator.MaximumFontSize().Width == 16 && indicator.FontColor() == 0xffffffffu && !indicator.RecognizeTags() && indicator.Opacity() == 255 && !indicator.DrawAtIntegerCoordinates(), "native bitmap Indicator defaults or copied font ownership changed");
	indicator.Print("AB\nC");
	Require(indicator.Placement().Size() == sgre::scene::LayerPoint{7, 12} && !indicator.Placement().Visible(), "printing must measure owned nominal glyphs without making the view visible");
	indicator.SetUniformFontScale(2);
	Require(indicator.Placement().Size() == sgre::scene::LayerPoint{14, 24} && indicator.MaximumFontSize().Width == 16 && indicator.MaximumFontSize().Height == 12, "font scale must refresh existing text while preserving constructor-cached maximum dimensions");
	indicator.SetFontScaleX(0.5f);
	indicator.SetSpacing({1, 2});
	Require(indicator.FontScale().Width == 0.5f && indicator.FontScale().Height == 2 && indicator.Placement().Size() == sgre::scene::LayerPoint{4.5f, 26}, "axis scale and spacing changes must preserve the other component and remeasure existing elements");
	indicator.SetFontScaleY(0);
	indicator.Placement().SetCoordinate({20, 30});
	Require(indicator.Placement().Bounds() == sgre::scene::LayerClip{20, 30, 24.5f, 32}, "collapsed font height retains native line spacing and current coordinates");
}

static void TestStyleSnapshotAndRecognitionRefresh() {
	sgre::scene::BitmapIndicator indicator(MakeFont());
	indicator.Print("#{scale,2}AB");
	Require(indicator.Elements().size() == 12 && indicator.Placement().Size() == sgre::scene::LayerPoint{7, 9}, "initial disabled tag recognition must retain literal elements");
	indicator.SetRecognizeTags(true);
	Require(indicator.Elements().size() == 12 && indicator.Placement().Size() == sgre::scene::LayerPoint{7, 9}, "recognition changes remeasure existing elements rather than reparsing their text");
	indicator.SetFontColor(0x11223344u);
	Require(indicator.Elements().back().Color == 0xffffffffu && indicator.FontColor() == 0x11223344u, "color changes affect future print preparation without rewriting prior element snapshots");
	indicator.Print("#{scale,2}AB");
	Require(indicator.Elements().size() == 2 && indicator.Elements()[0].Color == 0x11223344u && indicator.Placement().Size() == sgre::scene::LayerPoint{14, 18}, "future print must use the updated native style and recognition settings");
	indicator.SetOpacity(0xabcdef01u);
	indicator.SetAlignment(-7);
	indicator.SetDrawAtIntegerCoordinates(true);
	Require(indicator.Opacity() == 0xabcdef01u && indicator.Alignment() == -7 && indicator.DrawAtIntegerCoordinates(), "native opacity/alignment fields must retain their full values rather than applying invented clamps");
}

static void TestCStringPrintBoundaryAndAtomicFailure() {
	sgre::scene::BitmapIndicator indicator(MakeFont());
	const std::string input("AB\0\xff", 4);
	indicator.Print(input);
	Require(indicator.Elements().size() == 2 && indicator.Placement().Size() == sgre::scene::LayerPoint{7, 9}, "script print must stop at the first NUL before UTF-8 conversion");
	bool rejected = false;
	try {
		indicator.Print(std::string_view("\xff", 1));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && indicator.Elements().size() == 2 && indicator.Placement().Size() == sgre::scene::LayerPoint{7, 9}, "authored malformed print rejection must preserve existing text and dimensions");
	bool scale_rejected = false;
	try {
		indicator.SetFontScale({std::numeric_limits<float>::max(), 1});
	} catch (const std::runtime_error&) {
		scale_rejected = true;
	}

	Require(scale_rejected && indicator.FontScale().Width == 1 && indicator.Placement().Size() == sgre::scene::LayerPoint{7, 9}, "overflowing font measurement must fail before changing the visible scale or extent");
	indicator.Print("");
	Require(indicator.Elements().empty() && indicator.Placement().Size() == sgre::scene::LayerPoint{}, "empty print must replace prior elements and measured extents");
}

static void TestNativeDetachedSentinels() {
	sgre::scene::BitmapIndicator indicator(MakeFont());
	indicator.Print("ABC");
	indicator.SetFontColor(1);
	indicator.SetUniformFontScale(3);
	indicator.SetSpacing({4, 5});
	indicator.SetOpacity(12);
	indicator.SetAlignment(-8);
	indicator.Placement().SetVisible(true);
	indicator.Placement().Detach();
	indicator.Print(std::string_view("\xff", 1));
	indicator.SetFontScaleX(std::numeric_limits<float>::infinity());
	indicator.SetSpacingY(std::numeric_limits<float>::quiet_NaN());
	indicator.SetFontColor(0);
	indicator.SetRecognizeTags(false);
	indicator.SetOpacity(0);
	indicator.SetAlignment(5);
	indicator.SetDrawAtIntegerCoordinates(false);
	Require(indicator.Elements().empty() && !indicator.Placement().Visible() && indicator.Placement().Size() == sgre::scene::LayerPoint{}, "a detached view must emit no elements or placement state");
	Require(indicator.FontScale().Width == 1 && indicator.FontScale().Height == 1 && indicator.MaximumFontSize().Width == 1 && indicator.MaximumFontSize().Height == 1 && indicator.Spacing().Width == 1 && indicator.Spacing().Height == 1, "native detached font scale, maximum size and spacing getters return unit sentinels");
	Require(indicator.FontColor() == 0xffffffffu && indicator.RecognizeTags() && indicator.Opacity() == 255 && indicator.Alignment() == 0 && indicator.DrawAtIntegerCoordinates(), "native detached style getter sentinels differ from the live defaults and must remain exact");
}

int main() {
	try {
		TestOwnedFontAndMeasuredRefresh();
		TestStyleSnapshotAndRecognitionRefresh();
		TestCStringPrintBoundaryAndAtomicFailure();
		TestNativeDetachedSentinels();
		std::cout << "Bitmap Indicator owned text and style refresh fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
