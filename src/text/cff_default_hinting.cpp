#include "sgre/text/cff_default_hinting.hpp"

#include "sgre/text/vector_metrics.hpp"

#include <stdexcept>

namespace sgre::text {

	static constexpr std::int32_t kMaximumScaledEmPixels = 2000;

	CffHintedCharstringConfiguration PrepareDefaultCffHintedConfiguration(const resource::CffDriverHintParameters& hints, std::int32_t pixel_size, std::uint16_t units_per_em) {
		if (pixel_size > kMaximumScaledEmPixels) {
			throw std::runtime_error("default CFF hinting exceeds the native scaled-em limit");
		}

		const auto scale = PrepareVectorFontScale(pixel_size, units_per_em);
		// Native CFF setup rounds the 26.6-output scale into its 16.16 pixel-space character scale.
		const auto character_scale = static_cast<std::int32_t>((std::int64_t(scale.Scale16Dot16) + 32) / 64);
		CffBlueZoneInput blue_parameters;
		blue_parameters.BlueValues = hints.BlueValuesFontUnits;
		blue_parameters.OtherBlues = hints.OtherBluesFontUnits;
		blue_parameters.FamilyBlues = hints.FamilyBluesFontUnits;
		blue_parameters.FamilyOtherBlues = hints.FamilyOtherBluesFontUnits;
		blue_parameters.BlueScale16Dot16 = resource::NormalizeCffDriverBlueScale(hints.BlueScaleTimes1000In16Dot16);
		blue_parameters.BlueShiftFontUnits = hints.BlueShiftFontUnits;
		blue_parameters.BlueFuzzFontUnits = hints.BlueFuzzFontUnits;
		blue_parameters.VerticalScale16Dot16 = character_scale;
		blue_parameters.LanguageGroup = static_cast<CffBlueLanguageGroup>(hints.LanguageGroup);
		CffHintedCharstringConfiguration configuration;
		configuration.DefaultWidthFontUnits = hints.DefaultWidthFontUnits;
		configuration.NominalWidthFontUnits = hints.NominalWidthFontUnits;
		configuration.Contour.BaseScale16Dot16 = character_scale;
		configuration.Contour.Transform.HorizontalCharacterScale16Dot16 = character_scale;
		configuration.Contour.BlueZones = BuildCffBlueZones(blue_parameters);

		return configuration;
	}

} // namespace sgre::text
