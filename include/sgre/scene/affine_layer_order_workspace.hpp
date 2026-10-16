#pragma once

#include "sgre/scene/affine_layer_order.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace sgre::scene {

	class AffineLayerOrderWorkspace {
	public:
		using Comparison = std::function<bool(std::uint32_t, std::uint32_t)>;
		// Storage is allocated at owner construction and reused for native list-sort traversals.
		explicit AffineLayerOrderWorkspace(std::size_t maximum_layers = 65536);
		AffineLayerOrderWorkspace(const AffineLayerOrderWorkspace&) = delete;
		AffineLayerOrderWorkspace& operator=(const AffineLayerOrderWorkspace&) = delete;
		AffineLayerOrderWorkspace(AffineLayerOrderWorkspace&&) = delete;
		AffineLayerOrderWorkspace& operator=(AffineLayerOrderWorkspace&&) = delete;

		// Order is the current intrusive membership order; indices identify immutable graph records.
		void Sort(std::span<const AffineLayerOrderNode> graph, std::span<std::uint32_t> order);
		// Actual owners provide live comparison and stable unique membership identifiers below Capacity.
		// The callback and owner state outlive Sort; scalar/link changes remain visible to later comparisons.
		void Sort(std::span<std::uint32_t> order, const Comparison& comparison);
		[[nodiscard]] std::size_t Capacity() const noexcept;

	private:
		[[nodiscard]] bool Compare(std::span<const AffineLayerOrderNode> graph, const Comparison* comparison, std::uint32_t left, std::uint32_t right, std::size_t& remaining) const;
		[[nodiscard]] std::uint32_t SortRange(std::span<const AffineLayerOrderNode> graph, const Comparison* comparison, std::uint32_t& begin, std::size_t count, std::size_t& remaining);
		void InitializeOrder(std::span<const std::uint32_t> order, std::size_t value_limit);
		void SortPreparedOrder(std::span<const AffineLayerOrderNode> graph, const Comparison* comparison, std::span<std::uint32_t> order);
		void SpliceBefore(std::uint32_t begin, std::uint32_t end, std::uint32_t before);
		std::vector<std::uint32_t> m_Next;
		std::vector<std::uint32_t> m_Previous;
		std::vector<std::uint32_t> m_Value;
		std::vector<bool> m_Seen;
		bool m_Sorting = false;
	};

	// Native manager priority is binary32(rank) + the current owner's absolute base.
	[[nodiscard]] float PrepareAffineLayerPriority(std::uint32_t rank, float absolute_base);

} // namespace sgre::scene
