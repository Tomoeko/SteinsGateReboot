#include "sgre/scene/affine_layer_order_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	class AffineOrderWorkspaceCall {
	public:
		explicit AffineOrderWorkspaceCall(bool& sorting)
		    : m_Sorting(sorting) {
			if (sorting) {
				throw std::runtime_error("affine order workspace cannot reenter its active sort");
			}

			m_Sorting = true;
		}

		~AffineOrderWorkspaceCall() {
			m_Sorting = false;
		}

	private:
		bool& m_Sorting;
	};

	static std::size_t AffineOrderWorkspaceSize(std::size_t maximum_layers) {
		if (maximum_layers == 0 || maximum_layers > 65536) {
			throw std::runtime_error("affine order workspace requires a bounded positive layer limit");
		}

		return maximum_layers + 1;
	}

	AffineLayerOrderWorkspace::AffineLayerOrderWorkspace(std::size_t maximum_layers)
	    : m_Next(AffineOrderWorkspaceSize(maximum_layers)), m_Previous(m_Next.size()), m_Value(m_Next.size()), m_Seen(maximum_layers) {
	}

	std::size_t AffineLayerOrderWorkspace::Capacity() const noexcept {
		return m_Seen.size();
	}

	bool AffineLayerOrderWorkspace::Compare(std::span<const AffineLayerOrderNode> graph, const Comparison* comparison, std::uint32_t left, std::uint32_t right, std::size_t& remaining) const {
		if (remaining == 0) {
			throw std::runtime_error("affine list sort exceeds its comparison budget");
		}

		--remaining;

		return comparison ? (*comparison)(m_Value[left], m_Value[right]) : CompareAffineLayerOrder(graph, m_Value[left], m_Value[right]);
	}

	void AffineLayerOrderWorkspace::SpliceBefore(std::uint32_t begin, std::uint32_t end, std::uint32_t before) {
		const auto source_previous = m_Previous[begin];
		const auto range_last = m_Previous[end];
		const auto destination_previous = m_Previous[before];
		m_Next[source_previous] = end;
		m_Next[range_last] = before;
		m_Next[destination_previous] = begin;
		m_Previous[before] = range_last;
		m_Previous[end] = source_previous;
		m_Previous[begin] = destination_previous;
	}

	std::uint32_t AffineLayerOrderWorkspace::SortRange(std::span<const AffineLayerOrderNode> graph, const Comparison* comparison, std::uint32_t& begin, std::size_t count, std::size_t& remaining) {
		if (count == 0) {
			return begin;
		}
		if (count == 1) {
			return m_Next[begin];
		}

		const auto left_count = count / 2;
		auto middle = SortRange(graph, comparison, begin, left_count, remaining);
		const auto end = SortRange(graph, comparison, middle, count - left_count, remaining);
		auto right = middle;
		auto left = begin;
		auto result = right;
		if (!Compare(graph, comparison, right, left, remaining)) {
			result = left;
			while (true) {
				left = m_Next[left];
				if (left == right) {
					begin = result;

					return end;
				}
				if (Compare(graph, comparison, right, left, remaining)) {
					break;
				}
			}
		}

		while (true) {
			const auto range_begin = right;
			do {
				right = m_Next[right];
			} while (right != end && Compare(graph, comparison, right, left, remaining));
			SpliceBefore(range_begin, right, left);
			if (right == end) {
				break;
			}

			while (true) {
				left = m_Next[left];
				if (left == right) {
					begin = result;

					return end;
				}
				if (Compare(graph, comparison, right, left, remaining)) {
					break;
				}
			}
		}

		begin = result;

		return end;
	}

	void AffineLayerOrderWorkspace::Sort(std::span<const AffineLayerOrderNode> graph, std::span<std::uint32_t> order) {
		const AffineOrderWorkspaceCall call(m_Sorting);
		if (graph.size() > Capacity() || order.size() > Capacity()) {
			throw std::runtime_error("affine list sort exceeds its workspace capacity");
		}

		InitializeOrder(order, graph.size());
		SortPreparedOrder(graph, nullptr, order);
	}

	void AffineLayerOrderWorkspace::Sort(std::span<std::uint32_t> order, const Comparison& comparison) {
		const AffineOrderWorkspaceCall call(m_Sorting);
		if (order.size() > Capacity() || !comparison) {
			throw std::runtime_error("affine live sort requires its bounded workspace and actual comparison");
		}

		InitializeOrder(order, Capacity());
		SortPreparedOrder({}, &comparison, order);
	}

	void AffineLayerOrderWorkspace::InitializeOrder(std::span<const std::uint32_t> order, std::size_t value_limit) {
		std::fill(m_Seen.begin(), m_Seen.end(), false);
		for (std::size_t index = 0; index < order.size(); ++index) {
			const auto value = order[index];
			if (value >= value_limit || m_Seen[value]) {
				throw std::runtime_error("affine list membership contains an invalid or duplicate node");
			}

			m_Seen[value] = true;
			const auto slot = static_cast<std::uint32_t>(index + 1);
			m_Value[slot] = value;
			m_Previous[slot] = slot - 1;
			m_Next[slot] = index + 1 == order.size() ? 0u : slot + 1;
		}

		m_Next[0] = order.empty() ? 0u : 1u;
		m_Previous[0] = static_cast<std::uint32_t>(order.size());
	}

	void AffineLayerOrderWorkspace::SortPreparedOrder(std::span<const AffineLayerOrderNode> graph, const Comparison* comparison, std::span<std::uint32_t> order) {
		auto remaining = order.size() * 64;
		if (SortRange(graph, comparison, m_Next[0], order.size(), remaining) != 0) {
			throw std::runtime_error("affine list sort lost its terminal node");
		}

		auto current = m_Next[0];
		std::uint32_t previous = 0;
		for (auto& value : order) {
			if (current == 0 || current > order.size() || m_Previous[current] != previous) {
				throw std::runtime_error("affine list sort produced an invalid intrusive chain");
			}

			value = m_Value[current];
			previous = current;
			current = m_Next[current];
		}

		if (current != 0 || m_Previous[0] != previous) {
			throw std::runtime_error("affine list sort produced invalid terminal links");
		}
	}

	float PrepareAffineLayerPriority(std::uint32_t rank, float absolute_base) {
		if (rank >= 65536 || !std::isfinite(absolute_base)) {
			throw std::runtime_error("affine manager priority exceeds its finite bounded domain");
		}

		return static_cast<float>(rank) + absolute_base;
	}

} // namespace sgre::scene
