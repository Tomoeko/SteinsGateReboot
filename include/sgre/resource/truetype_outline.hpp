#pragma once

#include "sgre/resource/sfnt.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::resource {

	enum class TrueTypePointKind : std::uint8_t {
		ConicControl = 0,
		OnCurve = 1
	};

	struct TrueTypePoint {
		std::int32_t X = 0;
		std::int32_t Y = 0;
		TrueTypePointKind Kind = TrueTypePointKind::OnCurve;
		bool operator==(const TrueTypePoint&) const = default;
	};

	struct TrueTypeBounds {
		std::int32_t MinimumX = 0;
		std::int32_t MinimumY = 0;
		std::int32_t MaximumX = 0;
		std::int32_t MaximumY = 0;
		bool operator==(const TrueTypeBounds&) const = default;
	};

	enum class TrueTypeGlyphKind {
		Empty,
		Simple,
		Composite
	};

	struct TrueTypeComponent {
		std::uint16_t GlyphIndex = 0;
		std::uint16_t Flags = 0;
		// Signed offsets when flag 2 is set; unsigned point indices otherwise.
		std::array<std::int32_t, 2> Arguments{};
		// Native FT_Matrix order XX, XY, YX, YY in signed 16.16.
		std::array<std::int32_t, 4> Matrix{65536, 0, 0, 65536};
	};

	struct TrueTypeGlyph {
		TrueTypeGlyphKind Kind = TrueTypeGlyphKind::Empty;
		TrueTypeBounds HeaderBounds;
		std::vector<TrueTypePoint> Points;
		std::vector<std::uint16_t> ContourEnds;
		std::vector<TrueTypeComponent> Components;
		// Checked but unexecuted bytecode borrows the immutable font resource.
		std::span<const std::uint8_t> Instructions;
	};

	class TrueTypeFont {
	public:
		// The actual SFNT reader and its immutable bytes outlive this reader and instruction views.
		explicit TrueTypeFont(const SfntFont& font);
		[[nodiscard]] TrueTypeGlyph ReadGlyph(std::uint32_t glyph_index) const;

	private:
		const SfntFont& m_Sfnt;
		std::span<const std::uint8_t> m_Glyphs;
		std::vector<std::uint32_t> m_Offsets;
	};

} // namespace sgre::resource
