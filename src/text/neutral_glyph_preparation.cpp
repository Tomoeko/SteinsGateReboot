#include "sgre/text/neutral_glyph_preparation.hpp"

#include "sgre/text/nominal_glyph.hpp"
#include "sgre/text/unicode_mirroring.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace sgre::text {

	static constexpr std::size_t kMaximumPreparationFeatures = 4096;
	static constexpr std::size_t kMaximumPreparationRecords = 4096;
	static constexpr std::size_t kMaximumUserFeatureRequests = 128;
	static constexpr std::uint32_t kUnsafeToBreakMask = 1;

	static void ValidatePreparationDirection(GlyphShapingDirection direction) {
		if (direction != GlyphShapingDirection::LeftToRight && direction != GlyphShapingDirection::RightToLeft && direction != GlyphShapingDirection::TopToBottom && direction != GlyphShapingDirection::BottomToTop) {
			throw std::runtime_error("neutral glyph preparation direction is outside its recovered range");
		}
	}

	static void ValidatePreparationTag(std::string_view tag) {
		if (tag.size() != 4 || std::any_of(tag.begin(), tag.end(), [](char value) {
			    return value < 0x20 || value > 0x7e;
		    })) {
			throw std::runtime_error("neutral glyph preparation feature tag requires four ASCII bytes");
		}
	}

	static const VectorFontFeatureMask* FindPreparationFeature(const VectorFontFeatureMap& features, std::string_view tag) {
		const auto found = std::lower_bound(features.Features.begin(), features.Features.end(), tag, [](const auto& feature, std::string_view value) {
			return feature.Tag < value;
		});

		return found != features.Features.end() && found->Tag == tag ? &*found : nullptr;
	}

	static std::uint32_t PreparationOneMask(const VectorFontFeatureMap& features, std::string_view tag) {
		const auto feature = FindPreparationFeature(features, tag);

		return feature == nullptr ? 0 : feature->OneMask;
	}

	static bool IsForwardPreparationDirection(GlyphShapingDirection direction) {
		return direction == GlyphShapingDirection::LeftToRight || direction == GlyphShapingDirection::TopToBottom;
	}

	static void AcceptPreparedMirrors(std::span<SubstitutionGlyph> glyphs, const resource::SfntFont& font, GlyphShapingDirection original_direction, std::uint32_t fallback_mask) {
		if (IsForwardPreparationDirection(original_direction)) {
			return;
		}

		for (auto& glyph : glyphs) {
			const auto mirrored = MirrorUnicodeCodepoint(glyph.GlyphIndex);
			if (mirrored != glyph.GlyphIndex && QueryVectorFontNominalGlyph(font, mirrored)) {
				glyph.GlyphIndex = mirrored;
			} else {
				glyph.Mask |= fallback_mask;
			}
		}
	}

	static void MarkFractionRangeUnsafe(std::span<SubstitutionGlyph> glyphs, UnicodeBufferScratchFlags& scratch_flags) {
		const auto minimum = std::min_element(glyphs.begin(), glyphs.end(), [](const auto& first, const auto& second) {
			return first.Cluster < second.Cluster;
		})->Cluster;
		for (auto& glyph : glyphs) {
			if (glyph.Cluster != minimum) {
				glyph.Mask |= kUnsafeToBreakMask;
				scratch_flags = static_cast<UnicodeBufferScratchFlags>(static_cast<std::uint32_t>(scratch_flags) | static_cast<std::uint32_t>(UnicodeBufferScratchFlags::HasUnsafeToBreak));
			}
		}
	}

	static void ApplyFractionFeatureMasks(std::span<SubstitutionGlyph> glyphs, UnicodeBufferScratchFlags& scratch_flags, const VectorFontFeatureMap& features, GlyphShapingDirection buffer_direction) {
		const auto fraction = PreparationOneMask(features, "frac");
		const auto numerator = PreparationOneMask(features, "numr");
		const auto denominator = PreparationOneMask(features, "dnom");
		if ((static_cast<std::uint32_t>(scratch_flags) & static_cast<std::uint32_t>(UnicodeBufferScratchFlags::HasNonAscii)) == 0 || (fraction == 0 && (numerator == 0 || denominator == 0))) {
			return;
		}

		const auto before_mask = fraction | (IsForwardPreparationDirection(buffer_direction) ? numerator : denominator);
		const auto after_mask = fraction | (IsForwardPreparationDirection(buffer_direction) ? denominator : numerator);
		for (std::size_t index = 0; index < glyphs.size(); ++index) {
			if (glyphs[index].GlyphIndex != 0x2044) {
				continue;
			}

			auto first = index;
			while (first > 0 && (glyphs[first - 1].CharacterState & 0x1fU) == 13) {
				--first;
			}
			auto last = index + 1;
			while (last < glyphs.size() && (glyphs[last].CharacterState & 0x1fU) == 13) {
				++last;
			}
			if (last - first >= 2) {
				MarkFractionRangeUnsafe(glyphs.subspan(first, last - first), scratch_flags);
			}
			for (std::size_t before = first; before < index; ++before) {
				glyphs[before].Mask |= before_mask;
			}
			glyphs[index].Mask |= fraction;
			for (auto after = index + 1; after < last; ++after) {
				glyphs[after].Mask |= after_mask;
			}
			index = last - 1;
		}
	}

	static void ApplyRequestedFeatureMasks(std::span<SubstitutionGlyph> glyphs, const VectorFontFeatureMap& features, std::span<const VectorFontFeatureRequest> requests) {
		for (const auto& request : requests) {
			if (request.StartCluster == 0 && request.EndCluster == std::numeric_limits<std::uint32_t>::max()) {
				continue;
			}

			const auto feature = FindPreparationFeature(features, request.Tag);
			if (feature == nullptr || feature->Mask == 0) {
				continue;
			}

			const auto value = (request.Value << (feature->Shift & 31U)) & feature->Mask;
			for (auto& glyph : glyphs) {
				if (glyph.Cluster >= request.StartCluster && glyph.Cluster < request.EndCluster) {
					glyph.Mask = (glyph.Mask & ~feature->Mask) | value;
				}
			}
		}
	}

	static std::uint32_t FallbackMarkClass(std::uint32_t codepoint, std::uint32_t combining_class) {
		if (combining_class >= 200) {
			return combining_class;
		}
		if ((codepoint & 0xffffff00U) == 0xe00) {
			if (combining_class == 0) {
				switch (codepoint) {
				case 0xe31:
				case 0xe34:
				case 0xe35:
				case 0xe36:
				case 0xe37:
				case 0xe47:
				case 0xe4c:
				case 0xe4d:
				case 0xe4e:
					return 232;
				case 0xeb1:
				case 0xeb4:
				case 0xeb5:
				case 0xeb6:
				case 0xeb7:
				case 0xebb:
				case 0xecc:
				case 0xecd:
					return 230;
				case 0xebc:
					return 220;
				default:
					break;
				}
			} else if (codepoint == 0xe3a) {
				return 222;
			}
		}
		switch (combining_class) {
		case 3:
			return 222;
		case 10:
		case 107:
			return 232;
		case 11:
		case 14:
			return 228;
		case 13:
			return 214;
		case 15:
		case 16:
		case 17:
		case 18:
		case 19:
		case 20:
		case 21:
		case 22:
		case 23:
		case 24:
		case 25:
		case 30:
		case 33:
		case 118:
		case 129:
		case 131:
			return 220;
		case 26:
		case 27:
		case 28:
		case 29:
		case 31:
		case 32:
		case 34:
		case 35:
		case 36:
		case 122:
		case 132:
			return 230;
		default:
			return combining_class;
		}
	}

	static void ApplyFallbackMarkClasses(std::span<SubstitutionGlyph> glyphs) {
		for (auto& glyph : glyphs) {
			if ((glyph.CharacterState & 0x1fU) == 12) {
				const auto combining_class = FallbackMarkClass(glyph.GlyphIndex, (glyph.CharacterState & 0xffffU) >> 8);
				glyph.CharacterState = (glyph.CharacterState & 0xffff00ffU) | (combining_class << 8);
			}
		}
	}

	NormalizedGlyphContent PrepareNeutralGlyphIndices(UnicodeNormalizationBuffer& buffer, const resource::SfntFont& font, const VectorFontFeatureMap& features, const NeutralGlyphPreparationContext& context, std::span<const VectorFontFeatureRequest> requests) {
		ValidatePreparationDirection(context.OriginalDirection);
		ValidatePreparationDirection(context.BufferDirection);
		if (context.ClusterLevel != GlyphClusterLevel::MonotoneGraphemes && context.ClusterLevel != GlyphClusterLevel::MonotoneCharacters && context.ClusterLevel != GlyphClusterLevel::Characters) {
			throw std::runtime_error("neutral glyph preparation cluster level is outside its recovered range");
		}
		if (features.Features.size() > kMaximumPreparationFeatures || requests.size() > kMaximumUserFeatureRequests) {
			throw std::runtime_error("neutral glyph preparation feature work exceeds its limit");
		}
		std::string_view previous;
		for (const auto& feature : features.Features) {
			ValidatePreparationTag(feature.Tag);
			if (!previous.empty() && previous >= feature.Tag) {
				throw std::runtime_error("neutral glyph preparation features require unique sorted tags");
			}
			previous = feature.Tag;
		}
		for (const auto& request : requests) {
			ValidatePreparationTag(request.Tag);
		}
		if (buffer.m_State.InputCount > kMaximumPreparationRecords || (!buffer.m_AllocatorGrowth && buffer.m_State.MaximumLength > kMaximumPreparationRecords)) {
			throw std::runtime_error("neutral glyph preparation exceeds its resident record limit");
		}

		auto input = std::span<SubstitutionGlyph>(buffer.m_Storage[buffer.m_InputStorage]).first(buffer.m_State.InputCount);
		AcceptPreparedMirrors(input, font, context.OriginalDirection, PreparationOneMask(features, "rtlm"));
		static_cast<void>(buffer.NormalizeNeutral(font, context.ClusterLevel));
		input = std::span<SubstitutionGlyph>(buffer.m_Storage[buffer.m_InputStorage]).first(buffer.m_State.InputCount);
		ApplyFractionFeatureMasks(input, buffer.m_State.ScratchFlags, features, context.BufferDirection);
		// The actual neutral descriptor's post-normalization hook at +0x48 is null.
		ApplyRequestedFeatureMasks(input, features, requests);
		if (context.FallbackMarkPositioning) {
			ApplyFallbackMarkClasses(input);
		}

		return buffer.PublishNominalGlyphIndices();
	}

} // namespace sgre::text
