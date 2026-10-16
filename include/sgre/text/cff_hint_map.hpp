#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sgre::text {

	inline constexpr std::size_t kCffHintMapEdgeLimit = 192;

	enum class CffHintEdgeFlags : std::uint8_t {
		Inactive = 0,
		LowerGhost = 1,
		UpperGhost = 2,
		LowerStem = 4,
		UpperStem = 8,
		Locked = 16,
		Synthetic = 32
	};

	[[nodiscard]] constexpr CffHintEdgeFlags operator|(CffHintEdgeFlags left, CffHintEdgeFlags right) {
		return static_cast<CffHintEdgeFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
	}

	struct CffHintEdge {
		CffHintEdgeFlags Flags = CffHintEdgeFlags::Inactive;
		std::size_t StemIndex = 0;
		std::int32_t CharacterPosition16Dot16 = 0;
		std::int32_t DevicePosition16Dot16 = 0;
		std::int32_t SegmentScale16Dot16 = 65536;
	};

	struct CffHintMap {
		std::array<CffHintEdge, kCffHintMapEdgeLimit> Edges{};
		std::size_t EdgeCount = 0;
		std::size_t Cursor = 0;
		std::int32_t BaseScale16Dot16 = 65536;
		bool Enabled = true;
		bool Initialized = false;
	};

	struct CffHintInsertion {
		bool Inserted = false;
		CffHintEdge Lower;
		CffHintEdge Upper;
	};

	// These numeric helpers consume prepared CF2 edges; blue zones, masks and glyph loading remain separate.
	[[nodiscard]] std::int32_t EvaluateCffHintMap(CffHintMap& map, std::int32_t character_position_16_dot_16);
	// The native insertion remaps candidates before some rejection checks; returned candidates retain that mutation.
	[[nodiscard]] CffHintInsertion InsertCffHintEdges(CffHintMap& map, CffHintMap& inherited_map, CffHintEdge lower, CffHintEdge upper);
	void SnapCffHintMap(CffHintMap& map);

} // namespace sgre::text
