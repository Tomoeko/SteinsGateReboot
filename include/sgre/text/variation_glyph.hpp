#pragma once

#include "sgre/resource/sfnt.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace sgre::text {

	class VectorFontVariationMap {
	public:
		// The font and its immutable resource bytes must outlive this prepared borrowed query.
		explicit VectorFontVariationMap(const resource::SfntFont& font);
		[[nodiscard]] std::optional<std::uint32_t> Query(std::uint32_t codepoint, std::uint32_t selector) const;

	private:
		const resource::SfntFont* m_Font = nullptr;
		std::span<const std::uint8_t> m_Map;
	};

} // namespace sgre::text
