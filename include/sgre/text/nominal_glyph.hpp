#pragma once

#include "sgre/resource/sfnt.hpp"

#include <cstdint>
#include <optional>

namespace sgre::text {

	// Borrows the reader and its immutable font bytes; uses only its recovered native-preferred Unicode map.
	// Absence matches the original callback's false result without writing a caller output.
	[[nodiscard]] std::optional<std::uint32_t> QueryVectorFontNominalGlyph(const resource::SfntFont& font, std::uint32_t codepoint);

} // namespace sgre::text
