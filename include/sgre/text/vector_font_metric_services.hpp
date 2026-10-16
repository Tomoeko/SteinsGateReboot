#pragma once

#include "sgre/resource/sfnt.hpp"

#include <cstdint>
#include <optional>

namespace sgre::text {
	class VectorFont;

	enum class VectorFontMetricScaleSign : std::uint8_t {
		Nonnegative,
		Negative
	};

	struct VectorFontGlyphOrigin {
		std::int32_t X26Dot6 = 0;
		std::int32_t Y26Dot6 = 0;
	};

	class VectorFontMetricServices {
	public:
		// The same immovable live VectorFont encloses this binding; no resource/self pin is added.
		// Actual initialized HB scale signs are required; their unknown magnitudes are not inferred from FT size.
		VectorFontMetricServices(const VectorFont& actual_font, VectorFontMetricScaleSign actual_initial_hb_x_sign, VectorFontMetricScaleSign actual_initial_hb_y_sign);
		VectorFontMetricServices(const VectorFontMetricServices&) = delete;
		VectorFontMetricServices& operator=(const VectorFontMetricServices&) = delete;
		VectorFontMetricServices(VectorFontMetricServices&&) = delete;
		VectorFontMetricServices& operator=(VectorFontMetricServices&&) = delete;
		[[nodiscard]] const resource::SfntFont& Font() const;
		[[nodiscard]] bool HasHorizontalOrigin() const noexcept;
		[[nodiscard]] bool HasVerticalOrigin() const noexcept;
		// Installed flags-2 callbacks return zero for an invalid glyph. Unsupported size/profile is rejected.
		[[nodiscard]] std::int32_t HorizontalAdvance(std::uint32_t glyph_index) const;
		[[nodiscard]] std::int32_t VerticalAdvance(std::uint32_t glyph_index) const;
		[[nodiscard]] std::optional<VectorFontGlyphOrigin> VerticalOrigin(std::uint32_t glyph_index) const;

	private:
		[[nodiscard]] std::int32_t RequireAppliedPixelSize() const;
		const VectorFont& m_Font;
		const bool m_XNegative;
		const bool m_YNegative;
	};
} // namespace sgre::text
