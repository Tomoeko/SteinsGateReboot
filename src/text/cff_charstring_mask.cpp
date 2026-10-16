#include "sgre/text/cff_charstring_mask.hpp"

#include <algorithm>
#include <stdexcept>

namespace sgre::text {

	std::size_t UpdateCffCharstringHintMask(CffHintMask& mask, std::span<const std::uint8_t> program_bytes, std::size_t horizontal_stem_count, std::size_t vertical_stem_count) {
		if (horizontal_stem_count > kCffStemHintLimit || vertical_stem_count > kCffStemHintLimit - horizontal_stem_count) {
			throw std::runtime_error("CFF charstring mask declaration count exceeds 96 hints");
		}

		const auto hint_count = horizontal_stem_count + vertical_stem_count;
		const auto byte_count = (hint_count + 7) / 8;
		if (program_bytes.size() < byte_count) {
			throw std::runtime_error("CFF charstring hint mask bytes are truncated");
		}

		std::copy_n(program_bytes.begin(), byte_count, mask.Bytes.begin());
		mask.HintCount = hint_count;
		mask.Valid = true;
		mask.New = true;

		return byte_count;
	}

	CffCounterMaskPreparation PrepareCffCounterHintMap(CffHintMap& initial_map, const CffBlueZones& blue_zones, std::span<CffStemHint> horizontal_stems, std::size_t vertical_stem_count, std::span<const std::uint8_t> program_bytes, const CffCounterMaskConfiguration& configuration) {
		if (configuration.BaseScale16Dot16 <= 0 || initial_map.BaseScale16Dot16 != configuration.BaseScale16Dot16 || initial_map.Enabled != configuration.HintingEnabled) {
			throw std::runtime_error("CFF counter mask and initial map belong to incompatible glyph scale/hint state");
		}

		CffCounterMaskPreparation result;
		result.ConsumedByteCount = UpdateCffCharstringHintMask(result.Mask, program_bytes, horizontal_stems.size(), vertical_stem_count);
		result.Map.BaseScale16Dot16 = configuration.BaseScale16Dot16;
		result.Map.Enabled = configuration.HintingEnabled;
		CffStemMapCoordinates coordinates;
		coordinates.VerticalDarkening16Dot16 = configuration.VerticalDarkening16Dot16;

		BuildCffGlyphHintMap(result.Map, initial_map, blue_zones, horizontal_stems, vertical_stem_count, result.Mask, coordinates);

		return result;
	}

} // namespace sgre::text
