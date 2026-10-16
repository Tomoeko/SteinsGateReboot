#include "sgre/text/cff_hint_map.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::int32_t kFixedUnit = 65536;
	static constexpr std::int32_t kFixedHalf = 32768;

	struct DeferredHintAdjustment {
		std::size_t EdgeIndex = 0;
		std::int32_t Delta16Dot16 = 0;
	};

	static bool HasFlag(CffHintEdgeFlags flags, CffHintEdgeFlags flag) {
		return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
	}

	static bool IsStemEdge(const CffHintEdge& edge) {
		return HasFlag(edge.Flags, CffHintEdgeFlags::LowerStem) || HasFlag(edge.Flags, CffHintEdgeFlags::UpperStem);
	}

	static std::int32_t CheckedFixed(std::int64_t value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("CFF hint-map arithmetic exceeds signed 16.16 storage");
		}

		return static_cast<std::int32_t>(value);
	}

	static std::int32_t MultiplyFixed(std::int32_t value, std::int32_t scale) {
		const auto product = std::int64_t(value) * scale;
		const auto magnitude = product < 0 ? -product : product;
		const auto rounded = (magnitude + kFixedHalf) / kFixedUnit;

		return CheckedFixed(product < 0 ? -rounded : rounded);
	}

	static std::int32_t DivideFixed(std::int32_t value, std::int32_t divisor) {
		if (divisor == 0) {
			throw std::runtime_error("CFF hint-map segment has no character-space extent");
		}

		// Native FT_DivFix retains the low 32 quotient bits, including the one-unit em-box slope case.
		const auto numerator_bits = value < 0 ? 0U - std::bit_cast<std::uint32_t>(value) : std::bit_cast<std::uint32_t>(value);
		const auto denominator_bits = divisor < 0 ? 0U - std::bit_cast<std::uint32_t>(divisor) : std::bit_cast<std::uint32_t>(divisor);
		const auto numerator = static_cast<std::uint64_t>(std::int64_t(std::bit_cast<std::int32_t>(numerator_bits))) << 16;
		const auto denominator = static_cast<std::uint64_t>(std::int64_t(std::bit_cast<std::int32_t>(denominator_bits)));
		const auto rounded_bits = static_cast<std::uint32_t>((numerator + (denominator >> 1)) / denominator);

		return std::bit_cast<std::int32_t>((value < 0) != (divisor < 0) ? 0U - rounded_bits : rounded_bits);
	}

	static void ValidateEdge(const CffHintEdge& edge) {
		const auto flags = static_cast<std::uint8_t>(edge.Flags);
		const auto kind = flags & 15U;
		if ((flags & ~63U) != 0 || (kind != 0 && kind != 1 && kind != 2 && kind != 4 && kind != 8)) {
			throw std::runtime_error("CFF hint-map edge has an unsupported flag combination");
		}
	}

	static void ValidateMap(const CffHintMap& map) {
		if (map.EdgeCount > map.Edges.size() || (map.EdgeCount != 0 && map.Cursor >= map.EdgeCount) || (map.EdgeCount == 0 && map.Cursor != 0)) {
			throw std::runtime_error("CFF hint-map count or cursor exceeds its edge storage");
		}
		if (map.BaseScale16Dot16 <= 0) {
			throw std::runtime_error("CFF hint-map requires a positive base scale");
		}

		for (std::size_t index = 0; index < map.EdgeCount; ++index) {
			ValidateEdge(map.Edges[index]);
			if (index != 0 && map.Edges[index].CharacterPosition16Dot16 < map.Edges[index - 1].CharacterPosition16Dot16) {
				throw std::runtime_error("CFF hint-map character positions are out of order");
			}
		}
	}

	std::int32_t EvaluateCffHintMap(CffHintMap& map, std::int32_t character_position_16_dot_16) {
		ValidateMap(map);
		if (map.EdgeCount == 0 || !map.Enabled) {
			return MultiplyFixed(character_position_16_dot_16, map.BaseScale16Dot16);
		}

		while (map.Cursor + 1 < map.EdgeCount && character_position_16_dot_16 >= map.Edges[map.Cursor + 1].CharacterPosition16Dot16) {
			++map.Cursor;
		}
		while (map.Cursor != 0 && character_position_16_dot_16 < map.Edges[map.Cursor].CharacterPosition16Dot16) {
			--map.Cursor;
		}

		const auto& edge = map.Edges[map.Cursor];
		const auto scale = map.Cursor == 0 && character_position_16_dot_16 < edge.CharacterPosition16Dot16 ? map.BaseScale16Dot16 : edge.SegmentScale16Dot16;
		const auto distance = CheckedFixed(std::int64_t(character_position_16_dot_16) - edge.CharacterPosition16Dot16);

		return CheckedFixed(std::int64_t(MultiplyFixed(distance, scale)) + edge.DevicePosition16Dot16);
	}

	static void RemapCandidates(CffHintMap& map, CffHintMap& inherited_map, CffHintEdge& first, CffHintEdge& upper, bool paired) {
		if (!inherited_map.Initialized || HasFlag(first.Flags, CffHintEdgeFlags::Locked)) {
			return;
		}
		if (!paired) {
			first.DevicePosition16Dot16 = EvaluateCffHintMap(inherited_map, first.CharacterPosition16Dot16);
			return;
		}

		const auto sum = CheckedFixed(std::int64_t(first.CharacterPosition16Dot16) + upper.CharacterPosition16Dot16);
		const auto extent = CheckedFixed(std::int64_t(upper.CharacterPosition16Dot16) - first.CharacterPosition16Dot16);
		const auto center = EvaluateCffHintMap(inherited_map, sum / 2);
		const auto half_width = MultiplyFixed(extent / 2, map.BaseScale16Dot16);
		first.DevicePosition16Dot16 = CheckedFixed(std::int64_t(center) - half_width);
		upper.DevicePosition16Dot16 = CheckedFixed(std::int64_t(center) + half_width);
	}

	CffHintInsertion InsertCffHintEdges(CffHintMap& map, CffHintMap& inherited_map, CffHintEdge lower, CffHintEdge upper) {
		ValidateMap(map);
		ValidateMap(inherited_map);
		ValidateEdge(lower);
		ValidateEdge(upper);
		CffHintInsertion result;
		result.Lower = lower;
		result.Upper = upper;
		auto& first = lower.Flags != CffHintEdgeFlags::Inactive ? result.Lower : result.Upper;
		const bool paired = lower.Flags != CffHintEdgeFlags::Inactive && upper.Flags != CffHintEdgeFlags::Inactive;
		if (paired && upper.CharacterPosition16Dot16 < first.CharacterPosition16Dot16) {
			return result;
		}

		std::size_t insertion = 0;
		while (insertion < map.EdgeCount && map.Edges[insertion].CharacterPosition16Dot16 < first.CharacterPosition16Dot16) {
			++insertion;
		}
		if (insertion < map.EdgeCount) {
			const auto& next = map.Edges[insertion];
			if (next.CharacterPosition16Dot16 == first.CharacterPosition16Dot16 || (paired && next.CharacterPosition16Dot16 <= upper.CharacterPosition16Dot16) || HasFlag(next.Flags, CffHintEdgeFlags::UpperStem)) {
				return result;
			}
		}

		RemapCandidates(map, inherited_map, first, result.Upper, paired);
		if (insertion != 0 && first.DevicePosition16Dot16 < map.Edges[insertion - 1].DevicePosition16Dot16) {
			return result;
		}
		if (insertion < map.EdgeCount && (paired ? result.Upper : first).DevicePosition16Dot16 > map.Edges[insertion].DevicePosition16Dot16) {
			return result;
		}
		if (map.EdgeCount + std::size_t(paired) >= map.Edges.size()) {
			return result;
		}

		const std::size_t added_count = paired ? 2 : 1;
		std::move_backward(map.Edges.begin() + insertion, map.Edges.begin() + map.EdgeCount, map.Edges.begin() + map.EdgeCount + added_count);
		map.Edges[insertion] = first;
		if (paired) {
			map.Edges[insertion + 1] = result.Upper;
		}
		map.EdgeCount += added_count;
		result.Inserted = true;

		return result;
	}

	static void ValidateStemPairs(const CffHintMap& map) {
		for (std::size_t index = 0; index < map.EdgeCount; ++index) {
			const auto& edge = map.Edges[index];
			if (!IsStemEdge(edge)) {
				continue;
			}
			if (!HasFlag(edge.Flags, CffHintEdgeFlags::LowerStem) || index + 1 >= map.EdgeCount || !HasFlag(map.Edges[index + 1].Flags, CffHintEdgeFlags::UpperStem)) {
				throw std::runtime_error("CFF hint-map stem edges are not paired lower then upper");
			}

			++index;
		}
	}

	static void UpdateSegmentScale(CffHintEdge& first, const CffHintEdge& second) {
		if (first.CharacterPosition16Dot16 == second.CharacterPosition16Dot16) {
			return;
		}

		const auto device_extent = CheckedFixed(std::int64_t(second.DevicePosition16Dot16) - first.DevicePosition16Dot16);
		const auto character_extent = CheckedFixed(std::int64_t(second.CharacterPosition16Dot16) - first.CharacterPosition16Dot16);
		first.SegmentScale16Dot16 = DivideFixed(device_extent, character_extent);
	}

	static std::int32_t Fraction(std::int32_t value) {
		return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) & 65535U);
	}

	static void TranslateEdge(CffHintEdge& edge, std::int32_t delta) {
		edge.DevicePosition16Dot16 = CheckedFixed(std::int64_t(edge.DevicePosition16Dot16) + delta);
	}

	void SnapCffHintMap(CffHintMap& map) {
		ValidateMap(map);
		ValidateStemPairs(map);
		std::array<DeferredHintAdjustment, kCffHintMapEdgeLimit> pending{};
		std::size_t pending_count = 0;
		for (std::size_t index = 0; index < map.EdgeCount; ++index) {
			const bool paired = IsStemEdge(map.Edges[index]);
			const auto last = index + std::size_t(paired);
			if (!HasFlag(map.Edges[index].Flags, CffHintEdgeFlags::Locked)) {
				const auto lower = map.Edges[index].DevicePosition16Dot16;
				const auto upper = map.Edges[last].DevicePosition16Dot16;
				const auto lower_fraction = Fraction(lower);
				const auto upper_fraction = Fraction(upper);
				const auto down = std::max(-lower_fraction, -upper_fraction);
				const auto up = std::min(lower_fraction == 0 ? 0 : kFixedUnit - lower_fraction, upper_fraction == 0 ? 0 : kFixedUnit - upper_fraction);
				const bool room_above = last + 1 >= map.EdgeCount || map.Edges[last + 1].DevicePosition16Dot16 >= CheckedFixed(std::int64_t(upper) + up + kFixedHalf);
				const bool room_below = index == 0 || map.Edges[index - 1].DevicePosition16Dot16 <= CheckedFixed(std::int64_t(lower) + down - kFixedHalf);
				std::int32_t offset = 0;
				bool defer = false;
				if (room_above) {
					offset = room_below && -down < up ? down : up;
				} else if (!room_below) {
					defer = true;
				} else {
					offset = down;
					defer = up < -down;
				}
				if (defer && !HasFlag(map.Edges[last + 1].Flags, CffHintEdgeFlags::Locked)) {
					pending[pending_count].EdgeIndex = last;
					pending[pending_count].Delta16Dot16 = up - offset;
					++pending_count;
				}

				TranslateEdge(map.Edges[index], offset);
				if (paired) {
					TranslateEdge(map.Edges[last], offset);
				}
			}
			if (index != 0) {
				UpdateSegmentScale(map.Edges[index - 1], map.Edges[index]);
			}
			if (paired) {
				UpdateSegmentScale(map.Edges[index], map.Edges[last]);
				++index;
			}
		}

		// Native late adjustments deliberately leave the previously computed segment slopes intact.
		while (pending_count != 0) {
			const auto& adjustment = pending[--pending_count];
			const auto index = adjustment.EdgeIndex;
			const auto candidate = CheckedFixed(std::int64_t(map.Edges[index].DevicePosition16Dot16) + adjustment.Delta16Dot16);
			if (map.Edges[index + 1].DevicePosition16Dot16 >= CheckedFixed(std::int64_t(candidate) + kFixedHalf)) {
				map.Edges[index].DevicePosition16Dot16 = candidate;
				if (IsStemEdge(map.Edges[index])) {
					TranslateEdge(map.Edges[index - 1], adjustment.Delta16Dot16);
				}
			}
		}
	}

} // namespace sgre::text
