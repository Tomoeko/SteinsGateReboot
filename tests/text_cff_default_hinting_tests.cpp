#include "sgre/text/cff_default_hinting.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void CheckDefaultModeAndScale() {
	const auto normal = sgre::text::PrepareDefaultCffHintedConfiguration({}, 24, 1000);
	Require(normal.Contour.BaseScale16Dot16 == 1573 && normal.Contour.Transform.HorizontalCharacterScale16Dot16 == 1573 && normal.Contour.BlueZones.VerticalScale16Dot16 == 1573, "native default CFF scale setup must round the font scale into character pixels once");
	Require(normal.Contour.HintingEnabled && !normal.Contour.Darkening.Enabled && !normal.Contour.Darkening.ReverseWinding && normal.Contour.Darkening.HorizontalDarkening16Dot16 == 0 && normal.Contour.Darkening.VerticalDarkening16Dot16 == 0, "constructor-default no-stem-darkening leaves hinting enabled with zero contour expansion");
	Require(normal.Contour.Transform.OutputXFromHorizontal16Dot16 == 65536 && normal.Contour.Transform.OutputYFromVertical16Dot16 == 65536 && normal.Contour.Transform.OutputXFromVertical16Dot16 == 0 && normal.Contour.Transform.OutputYFromHorizontal16Dot16 == 0, "the default recovered output matrix remains identity after character scaling");
	const auto small = sgre::text::PrepareDefaultCffHintedConfiguration({}, 1, 16384);
	const auto large = sgre::text::PrepareDefaultCffHintedConfiguration({}, 2000, 16);
	Require(small.Contour.BaseScale16Dot16 == 4 && large.Contour.BaseScale16Dot16 == 8192000, "normalized em and positive pixel-size boundary requests retain native character scales");
}

static void CheckPrivateParametersAndOwnedZones() {
	sgre::resource::CffDriverHintParameters hints;
	hints.DefaultWidthFontUnits = 500;
	hints.NominalWidthFontUnits = 7;
	hints.BlueValuesFontUnits = {-10, 0, 700, 710};
	const auto configuration = sgre::text::PrepareDefaultCffHintedConfiguration(hints, 24, 1000);
	hints.BlueValuesFontUnits[0] = -100;
	Require(configuration.DefaultWidthFontUnits == 500 && configuration.NominalWidthFontUnits == 7 && configuration.Contour.BlueZones.ZoneCount == 2 && configuration.Contour.BlueZones.Zones[0].CharacterBottom16Dot16 == -10 * 65536, "normalized driver values and zones must belong to the returned glyph configuration");
	const std::array<std::uint8_t, 6> glyph{139, 139, 21, 140, 6, 14};
	sgre::text::CffCharstringPrograms programs;
	programs.Glyph = glyph;
	const auto result = sgre::text::ExecuteCffHintedCharstring(programs, configuration);
	Require(result.Width.Width16Dot16 == 500 * 65536 && result.Callbacks.size() == 3 && result.Callbacks[1].Points[1].X16Dot16 == 1573, "the default numeric configuration must feed actual hinted operator execution");
}

static void CheckRejectedRequests() {
	for (const auto pixels : {-1, 0, 2001}) {
		bool failed = false;
		try {
			(void)sgre::text::PrepareDefaultCffHintedConfiguration({}, pixels, 1000);
		} catch (const std::runtime_error&) {
			failed = true;
		}
		Require(failed, "default CFF pixel requests must stay inside the recovered positive scaled-em limit");
	}
	bool failed = false;
	try {
		(void)sgre::text::PrepareDefaultCffHintedConfiguration({}, 24, 0);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "an absent em size must fail before scale arithmetic");
}

int main() {
	try {
		CheckDefaultModeAndScale();
		CheckPrivateParametersAndOwnedZones();
		CheckRejectedRequests();
		std::cout << "CFF default hinting tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
