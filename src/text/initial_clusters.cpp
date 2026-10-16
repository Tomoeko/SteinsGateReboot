#include "sgre/text/initial_clusters.hpp"

#include <algorithm>
#include <stdexcept>

namespace sgre::text {

	// Equal-cluster boundary expansion can revisit prior records; this bounds total pass work.
	static constexpr std::size_t kMaximumInitialClusterGlyphs = 4096;
	static constexpr std::uint32_t kUnsafeToBreakMask = 1;
	static constexpr std::uint32_t kHasUnsafeToBreakScratch = static_cast<std::uint32_t>(UnicodeBufferScratchFlags::HasUnsafeToBreak);

	static bool ContinuesInitialCluster(const SubstitutionGlyph& glyph) {
		const auto properties = glyph.CharacterState & 0xffffU;
		const auto category = properties & 0x1fU;

		return (category >= 10 && category <= 12) || (category == 1 && (properties & 0x300U) != 0);
	}

	static std::uint32_t MinimumCluster(std::span<const SubstitutionGlyph> glyphs) {
		auto minimum = glyphs.front().Cluster;
		for (const auto& glyph : glyphs.subspan(1)) {
			minimum = std::min(minimum, glyph.Cluster);
		}

		return minimum;
	}

	static void MergeInitialCluster(std::span<SubstitutionGlyph> glyphs, std::size_t first, std::size_t last) {
		const auto minimum = MinimumCluster(glyphs.subspan(first, last - first));
		// Native 0x1403a5db0 extends both boundaries through adjacent equal input clusters.
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

	static void MarkInitialClusterUnsafe(std::span<SubstitutionGlyph> glyphs, std::uint32_t& scratch_bits) {
		const auto minimum = MinimumCluster(glyphs);
		for (auto& glyph : glyphs) {
			if (glyph.Cluster != minimum) {
				glyph.Mask |= kUnsafeToBreakMask;
				scratch_bits |= kHasUnsafeToBreakScratch;
			}
		}
	}

	static void PrepareInitialCluster(std::span<SubstitutionGlyph> glyphs, std::size_t first, std::size_t last, GlyphClusterLevel level, std::uint32_t& scratch_bits) {
		if (last - first < 2) {
			return;
		}

		if (level == GlyphClusterLevel::MonotoneGraphemes) {
			MergeInitialCluster(glyphs, first, last);
		} else {
			MarkInitialClusterUnsafe(glyphs.subspan(first, last - first), scratch_bits);
		}
	}

	UnicodeBufferScratchFlags InitializeUnicodeGlyphClusters(std::span<SubstitutionGlyph> glyphs, GlyphClusterLevel level, UnicodeBufferScratchFlags scratch_flags) {
		if (glyphs.size() > kMaximumInitialClusterGlyphs) {
			throw std::runtime_error("initial Unicode clustering exceeds its record count limit");
		}
		if (level != GlyphClusterLevel::MonotoneGraphemes && level != GlyphClusterLevel::MonotoneCharacters && level != GlyphClusterLevel::Characters) {
			throw std::runtime_error("unsupported initial Unicode cluster level");
		}

		auto scratch_bits = static_cast<std::uint32_t>(scratch_flags);
		if ((scratch_bits & static_cast<std::uint32_t>(UnicodeBufferScratchFlags::HasNonAscii)) == 0 || glyphs.size() < 2) {
			return scratch_flags;
		}

		std::size_t first = 0;
		for (std::size_t next = 1; next < glyphs.size(); ++next) {
			if (ContinuesInitialCluster(glyphs[next])) {
				continue;
			}

			PrepareInitialCluster(glyphs, first, next, level, scratch_bits);
			first = next;
		}
		PrepareInitialCluster(glyphs, first, glyphs.size(), level, scratch_bits);

		return static_cast<UnicodeBufferScratchFlags>(scratch_bits);
	}

} // namespace sgre::text
