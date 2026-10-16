#include "sgre/text/combining_reorder.hpp"

#include <algorithm>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumCombiningReorderGlyphs = 4096;
	static constexpr std::size_t kMaximumSortedCombiningRun = 32;
	static constexpr std::uint32_t kUnsafeToBreakMask = 1;
	static constexpr std::uint32_t kHasUnsafeToBreakScratch = 0x10;

	static std::uint32_t ModifiedCombiningClass(const SubstitutionGlyph& glyph) {
		const auto properties = glyph.CharacterState & 0xffffU;
		const auto category = properties & 0x1fU;
		if (category < 10 || category > 12) {
			return 0;
		}

		return properties >> 8;
	}

	static std::uint32_t MinimumCluster(std::span<const SubstitutionGlyph> glyphs) {
		auto minimum = glyphs.front().Cluster;
		for (const auto& glyph : glyphs.subspan(1)) {
			minimum = std::min(minimum, glyph.Cluster);
		}

		return minimum;
	}

	static void SetMergedCluster(SubstitutionGlyph& glyph, std::uint32_t cluster) {
		if (glyph.Cluster != cluster) {
			glyph.Mask &= ~kUnsafeToBreakMask;
		}
		glyph.Cluster = cluster;
	}

	static void MergeReorderedClusters(std::span<SubstitutionGlyph> glyphs, std::span<SubstitutionGlyph> previous_input, std::size_t first, std::size_t last, GlyphClusterLevel level, std::uint32_t& scratch_bits) {
		const auto minimum = MinimumCluster(glyphs.subspan(first, last - first));
		if (level == GlyphClusterLevel::Characters) {
			for (auto& glyph : glyphs.subspan(first, last - first)) {
				if (glyph.Cluster != minimum) {
					glyph.Mask |= kUnsafeToBreakMask;
					scratch_bits |= kHasUnsafeToBreakScratch;
				}
			}

			return;
		}

		// Native 0x1403a5db0 extends equal-cluster boundaries before updating any record.
		while (last < glyphs.size() && glyphs[last - 1].Cluster == glyphs[last].Cluster) {
			++last;
		}
		while (first > 0 && glyphs[first - 1].Cluster == glyphs[first].Cluster) {
			--first;
		}
		if (first == 0) {
			for (std::size_t count = previous_input.size(); count > 0; --count) {
				auto& glyph = previous_input[count - 1];
				// Reload the current input boundary: the original buffers can have the same address.
				if (glyph.Cluster != glyphs[first].Cluster) {
					break;
				}
				SetMergedCluster(glyph, minimum);
			}
		}

		for (auto& glyph : glyphs.subspan(first, last - first)) {
			SetMergedCluster(glyph, minimum);
		}
	}

	static void SortCombiningRun(std::span<SubstitutionGlyph> glyphs, std::span<SubstitutionGlyph> previous_input, std::size_t first, std::size_t last, GlyphClusterLevel level, std::uint32_t& scratch_bits) {
		for (std::size_t next = first + 1; next < last; ++next) {
			auto destination = next;
			const auto combining_class = ModifiedCombiningClass(glyphs[next]);
			while (destination > first && ModifiedCombiningClass(glyphs[destination - 1]) > combining_class) {
				--destination;
			}
			if (destination == next) {
				continue;
			}

			MergeReorderedClusters(glyphs, previous_input, destination, next + 1, level, scratch_bits);
			const auto moved = glyphs[next];
			for (std::size_t index = next; index > destination; --index) {
				glyphs[index] = glyphs[index - 1];
			}
			glyphs[destination] = moved;
		}
	}

	UnicodeBufferScratchFlags ReorderNeutralUnicodeCombiningGlyphs(std::span<SubstitutionGlyph> glyphs, std::span<SubstitutionGlyph> previous_input, GlyphClusterLevel level, UnicodeBufferScratchFlags scratch_flags) {
		if (glyphs.size() > kMaximumCombiningReorderGlyphs || previous_input.size() > kMaximumCombiningReorderGlyphs) {
			throw std::runtime_error("neutral Unicode combining reorder exceeds its record count limit");
		}
		if (level != GlyphClusterLevel::MonotoneGraphemes && level != GlyphClusterLevel::MonotoneCharacters && level != GlyphClusterLevel::Characters) {
			throw std::runtime_error("unsupported neutral Unicode combining cluster level");
		}

		auto scratch_bits = static_cast<std::uint32_t>(scratch_flags);
		std::size_t first = 0;
		while (first < glyphs.size()) {
			if (ModifiedCombiningClass(glyphs[first]) == 0) {
				++first;
				continue;
			}

			auto last = first + 1;
			while (last < glyphs.size() && ModifiedCombiningClass(glyphs[last]) != 0) {
				++last;
			}
			if (last - first >= 2 && last - first <= kMaximumSortedCombiningRun) {
				SortCombiningRun(glyphs, previous_input, first, last, level, scratch_bits);
			}

			first = last;
			if (first < glyphs.size()) {
				++first;
			}
		}

		return static_cast<UnicodeBufferScratchFlags>(scratch_bits);
	}

} // namespace sgre::text
