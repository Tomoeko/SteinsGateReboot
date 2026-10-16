#include "sgre/text/direction_adjustment.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumDirectionAdjustmentGlyphs = 4096;
	static constexpr std::uint32_t kUnsafeToBreakMask = 1;

	// Exact native script-direction cases from 0x1403a3aa0; no tag normalization or host data.
	static constexpr std::array<std::uint32_t, 27> kRightToLeftScripts = {
	    0x41646c6d, // Adlm
	    0x41726162, // Arab
	    0x41726d69, // Armi
	    0x41767374, // Avst
	    0x43707274, // Cprt
	    0x48656272, // Hebr
	    0x48756e67, // Hung
	    0x4b686172, // Khar
	    0x4c796469, // Lydi
	    0x4d616e64, // Mand
	    0x4d616e69, // Mani
	    0x4d656e64, // Mend
	    0x4d657263, // Merc
	    0x4d65726f, // Mero
	    0x4e617262, // Narb
	    0x4e626174, // Nbat
	    0x4e6b6f6f, // Nkoo
	    0x4f726b68, // Orkh
	    0x50616c6d, // Palm
	    0x50686c69, // Phli
	    0x50686c70, // Phlp
	    0x50686e78, // Phnx
	    0x50727469, // Prti
	    0x53616d72, // Samr
	    0x53617262, // Sarb
	    0x53797263, // Syrc
	    0x54686161, // Thaa
	};

	static GlyphShapingDirection OriginalScriptDirection(std::uint32_t iso15924_tag) {
		if (std::find(kRightToLeftScripts.begin(), kRightToLeftScripts.end(), iso15924_tag) != kRightToLeftScripts.end()) {
			return GlyphShapingDirection::RightToLeft;
		}

		return GlyphShapingDirection::LeftToRight;
	}

	static void MergeDirectionGroupClusters(std::span<SubstitutionGlyph> glyphs, std::size_t first, std::size_t last) {
		auto minimum = glyphs[first].Cluster;
		for (const auto& glyph : glyphs.subspan(first + 1, last - first - 1)) {
			minimum = std::min(minimum, glyph.Cluster);
		}
		while (last < glyphs.size() && glyphs[last - 1].Cluster == glyphs[last].Cluster) {
			++last;
		}
		while (first > 0 && glyphs[first - 1].Cluster == glyphs[first].Cluster) {
			--first;
		}

		for (auto& glyph : glyphs.subspan(first, last - first)) {
			if (glyph.Cluster != minimum) {
				glyph.Mask &= ~kUnsafeToBreakMask;
			}

			glyph.Cluster = minimum;
		}
	}

	static void ReverseDirectionGroup(std::span<SubstitutionGlyph> glyphs, std::size_t first, std::size_t last, GlyphClusterLevel level) {
		if (last - first < 2) {
			return;
		}

		if (level == GlyphClusterLevel::MonotoneCharacters) {
			MergeDirectionGroupClusters(glyphs, first, last);
		}
		const auto group = glyphs.subspan(first, last - first);
		std::reverse(group.begin(), group.end());
	}

	GlyphShapingDirection AdjustUnicodeGlyphDirection(std::span<SubstitutionGlyph> glyphs, GlyphClusterLevel level, GlyphShapingDirection direction, std::uint32_t iso15924_tag) {
		if (glyphs.size() > kMaximumDirectionAdjustmentGlyphs) {
			throw std::runtime_error("Unicode direction adjustment exceeds its record count limit");
		}
		if (level != GlyphClusterLevel::MonotoneGraphemes && level != GlyphClusterLevel::MonotoneCharacters && level != GlyphClusterLevel::Characters) {
			throw std::runtime_error("unsupported Unicode direction cluster level");
		}
		if (direction != GlyphShapingDirection::LeftToRight && direction != GlyphShapingDirection::RightToLeft && direction != GlyphShapingDirection::TopToBottom && direction != GlyphShapingDirection::BottomToTop) {
			throw std::runtime_error("unsupported Unicode shaping direction");
		}

		const bool horizontal = direction == GlyphShapingDirection::LeftToRight || direction == GlyphShapingDirection::RightToLeft;
		const auto native_direction = horizontal ? OriginalScriptDirection(iso15924_tag) : GlyphShapingDirection::TopToBottom;
		if (direction == native_direction) {
			return direction;
		}

		std::size_t first = 0;
		for (std::size_t next = 1; next < glyphs.size(); ++next) {
			const auto category = glyphs[next].CharacterState & 0x1fU;
			if (category >= 10 && category <= 12) {
				continue;
			}

			ReverseDirectionGroup(glyphs, first, next, level);
			first = next;
		}
		ReverseDirectionGroup(glyphs, first, glyphs.size(), level);
		std::reverse(glyphs.begin(), glyphs.end());

		return native_direction;
	}

} // namespace sgre::text
