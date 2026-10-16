#include "sgre/text/cff_hint_map.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sgre::text::test {

	static void Require(bool condition, std::string_view message) {
		if (!condition) {
			throw std::runtime_error(std::string(message));
		}
	}

	template <typename Operation>
	static void RequireError(Operation operation, std::string_view message) {
		try {
			operation();
		} catch (const std::runtime_error&) {
			return;
		}

		throw std::runtime_error(std::string(message));
	}

	static CffHintEdge Edge(CffHintEdgeFlags flags, std::int32_t character, std::int32_t device, std::int32_t scale = 65536) {
		CffHintEdge edge;
		edge.Flags = flags;
		edge.CharacterPosition16Dot16 = character;
		edge.DevicePosition16Dot16 = device;
		edge.SegmentScale16Dot16 = scale;

		return edge;
	}

	static void CheckMappingAndCursor() {
		CffHintMap map;
		map.BaseScale16Dot16 = 32768;
		map.Enabled = false;
		Require(EvaluateCffHintMap(map, -1) == -1, "negative fixed-point half rounds away from zero");
		map.Enabled = true;
		map.Edges[0] = Edge(CffHintEdgeFlags::LowerGhost, 65536, 131072, 32768);
		map.Edges[1] = Edge(CffHintEdgeFlags::UpperGhost, 196608, 262144, 131072);
		map.EdgeCount = 2;
		Require(EvaluateCffHintMap(map, 0) == 98304 && map.Cursor == 0, "below-first mapping uses the base scale");
		Require(EvaluateCffHintMap(map, 327680) == 524288 && map.Cursor == 1, "above-last mapping uses the last segment scale");
		Require(EvaluateCffHintMap(map, 131072) == 163840 && map.Cursor == 0, "backward lookup updates the cached cursor");
		Require(EvaluateCffHintMap(map, 196608) == 262144 && map.Cursor == 1, "a knot belongs to its following segment");
		map.Enabled = false;
		Require(EvaluateCffHintMap(map, 65536) == 32768 && map.Cursor == 1, "disabled mapping preserves its cached cursor");
	}

	static void CheckInsertion() {
		CffHintMap map;
		CffHintMap parent;
		parent.Initialized = true;
		parent.Enabled = false;
		parent.BaseScale16Dot16 = 32768;
		auto lower = Edge(CffHintEdgeFlags::LowerStem, 65535, -123);
		auto upper = Edge(CffHintEdgeFlags::UpperStem, 131072, -456);
		const auto pair = InsertCffHintEdges(map, parent, lower, upper);
		Require(pair.Inserted && map.EdgeCount == 2, "a valid inherited stem pair is inserted");
		Require(pair.Lower.DevicePosition16Dot16 == 16384 && pair.Upper.DevicePosition16Dot16 == 81920, "pair center and half extent truncate before fixed-point scaling");
		const auto duplicate = InsertCffHintEdges(map, parent, lower, upper);
		Require(!duplicate.Inserted && duplicate.Lower.DevicePosition16Dot16 == -123, "character overlap rejects before candidate remapping");

		CffHintMap constrained;
		constrained.Edges[0] = Edge(CffHintEdgeFlags::UpperGhost, 327680, 0);
		constrained.EdgeCount = 1;
		parent.BaseScale16Dot16 = 65536;
		const auto rejected = InsertCffHintEdges(constrained, parent, Edge(CffHintEdgeFlags::LowerGhost, 65536, -500), {});
		Require(!rejected.Inserted && rejected.Lower.DevicePosition16Dot16 == 65536 && constrained.EdgeCount == 1, "device-order rejection retains the already remapped candidate");

		parent.Initialized = false;
		CffHintMap capacity;
		for (std::size_t index = 0; index < 191; ++index) {
			capacity.Edges[index] = Edge(CffHintEdgeFlags::LowerGhost, static_cast<std::int32_t>(index * 65536), static_cast<std::int32_t>(index * 65536));
		}
		capacity.EdgeCount = 191;
		const auto full = InsertCffHintEdges(capacity, parent, Edge(CffHintEdgeFlags::LowerGhost, 192 * 65536, 192 * 65536), {});
		Require(full.Inserted && capacity.EdgeCount == 192, "the final single edge fills native map capacity");
		const auto overflow = InsertCffHintEdges(capacity, parent, Edge(CffHintEdgeFlags::LowerGhost, 193 * 65536, 193 * 65536), {});
		Require(!overflow.Inserted && capacity.EdgeCount == 192, "a full map rejects another edge");
	}

	static void CheckSnapping() {
		CffHintMap tie;
		tie.Edges[0] = Edge(CffHintEdgeFlags::LowerGhost, 0, 32768);
		tie.EdgeCount = 1;
		SnapCffHintMap(tie);
		Require(tie.Edges[0].DevicePosition16Dot16 == 65536, "a half-pixel snapping tie chooses upward");

		CffHintMap pair;
		pair.Edges[0] = Edge(CffHintEdgeFlags::LowerStem, 0, 13107);
		pair.Edges[1] = Edge(CffHintEdgeFlags::UpperStem, 65536, 78643);
		pair.EdgeCount = 2;
		SnapCffHintMap(pair);
		Require(pair.Edges[0].DevicePosition16Dot16 == 0 && pair.Edges[1].DevicePosition16Dot16 == 65536 && pair.Edges[0].SegmentScale16Dot16 == 65536, "a stem pair translates together before its slope is updated");

		CffHintMap delayed;
		delayed.Edges[0] = Edge(CffHintEdgeFlags::LowerGhost | CffHintEdgeFlags::Locked, 0, 39322);
		delayed.Edges[1] = Edge(CffHintEdgeFlags::LowerGhost, 65536, 52429);
		delayed.Edges[2] = Edge(CffHintEdgeFlags::UpperGhost, 131072, 78643);
		delayed.EdgeCount = 3;
		SnapCffHintMap(delayed);
		Require(delayed.Edges[0].DevicePosition16Dot16 == 39322 && delayed.Edges[1].DevicePosition16Dot16 == 65536 && delayed.Edges[2].DevicePosition16Dot16 == 131072, "reverse pending adjustment uses space made by the following edge");
		Require(delayed.Edges[0].SegmentScale16Dot16 == 13107 && delayed.Edges[1].SegmentScale16Dot16 == 78643, "late adjustment leaves the previously computed segment slopes intact");
	}

	static void CheckInvalidState() {
		CffHintMap map;
		map.EdgeCount = 193;
		RequireError([&] {
			(void)EvaluateCffHintMap(map, 0);
		},
		    "excessive map count must fail");
		map.EdgeCount = 1;
		map.Cursor = 1;
		RequireError([&] {
			(void)EvaluateCffHintMap(map, 0);
		},
		    "invalid map cursor must fail");
		map.Cursor = 0;
		map.Edges[0].Flags = static_cast<CffHintEdgeFlags>(3);
		RequireError([&] {
			(void)EvaluateCffHintMap(map, 0);
		},
		    "combined edge kinds must fail");
		map.Edges[0] = Edge(CffHintEdgeFlags::LowerStem, 0, 0);
		RequireError([&] {
			SnapCffHintMap(map);
		},
		    "an unpaired stem edge must fail");
		map.Edges[0] = Edge(CffHintEdgeFlags::LowerGhost, std::numeric_limits<std::int32_t>::min(), 0);
		RequireError([&] {
			(void)EvaluateCffHintMap(map, std::numeric_limits<std::int32_t>::max());
		},
		    "a fixed-point coordinate difference that overflows must fail");
		map.EdgeCount = 0;
		map.BaseScale16Dot16 = 0;
		RequireError([&] {
			(void)EvaluateCffHintMap(map, 0);
		},
		    "a nonpositive base scale must fail");
	}

} // namespace sgre::text::test

static void CheckWrappedSegmentSlopes() {
	sgre::text::CffHintMap map;
	map.EdgeCount = 2;
	map.Edges[0].Flags = sgre::text::CffHintEdgeFlags::LowerGhost | sgre::text::CffHintEdgeFlags::Locked;
	map.Edges[1].Flags = sgre::text::CffHintEdgeFlags::UpperGhost | sgre::text::CffHintEdgeFlags::Locked;
	map.Edges[1].CharacterPosition16Dot16 = 1;
	map.Edges[1].DevicePosition16Dot16 = 32768;
	sgre::text::SnapCffHintMap(map);
	sgre::text::test::Require(map.Edges[0].SegmentScale16Dot16 == std::numeric_limits<std::int32_t>::min(), "a one-unit character interval must preserve native signed quotient wrapping");
	map.Edges[1].DevicePosition16Dot16 = 65536;
	sgre::text::SnapCffHintMap(map);
	sgre::text::test::Require(map.Edges[0].SegmentScale16Dot16 == 0, "a full overflowing quotient must preserve its low 32 bits");
}

int main() {
	try {
		CheckWrappedSegmentSlopes();
		sgre::text::test::CheckMappingAndCursor();
		sgre::text::test::CheckInsertion();
		sgre::text::test::CheckSnapping();
		sgre::text::test::CheckInvalidState();
		std::cout << "CFF hint-map tests passed\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
