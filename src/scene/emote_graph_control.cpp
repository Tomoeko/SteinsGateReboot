#include "sgre/scene/emote_graph_control.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaximumGraphElements = 4096;
	static constexpr std::size_t kMaximumGraphWork = 4 * 1024 * 1024;
	static constexpr std::size_t kNoGraphNode = std::numeric_limits<std::size_t>::max();

	struct GraphConnection {
		std::size_t Node = 0;
		float Distance = 0;
	};

	struct GraphRoute {
		std::vector<std::size_t> Nodes;
		std::vector<float> Distances;
		std::int32_t First = 0;
		std::int32_t Last = 0;
		float InitialDistance = 0;
		float FinalDistance = 0;
		float TotalDistance = 0;
	};

	static void ChargeGraphWork(std::size_t& work, std::size_t amount) {
		if (amount > kMaximumGraphWork - work) {
			throw std::runtime_error("Emote frame graph exceeds bounded initialization or route work");
		}
		work += amount;
	}

	static std::int32_t FrameInteger(float value) {
		if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f) {
			throw std::runtime_error("Emote graph frame exceeds native finite signed conversion");
		}
		return static_cast<std::int32_t>(value);
	}

	class EmoteGraphControl::Implementation {
	public:
		explicit Implementation(EmoteGraphDefinition definition) : Definition(std::move(definition)) {
			if (Definition.Edges.size() > kMaximumGraphElements || Definition.Nodes.size() > kMaximumGraphElements) {
				throw std::runtime_error("Emote graph exceeds bounded edge/node ownership");
			}
			std::size_t point_count = 0;
			for (const auto& node : Definition.Nodes) {
				if (node.size() > kMaximumGraphElements - point_count) {
					throw std::runtime_error("Emote graph exceeds bounded node point ownership");
				}
				point_count += node.size();
			}
			for (auto& edge : Definition.Edges) {
				if (edge.Lower > edge.Upper) {
					std::swap(edge.Lower, edge.Upper);
				}
			}
			std::size_t work = 0;
			MergeInteriorPoints(work);
			SplitEdges(work);
			for (const auto& edge : Definition.Edges) {
				EnsureNode(edge.Lower, work);
				EnsureNode(edge.Upper, work);
			}
			Connections.resize(Definition.Nodes.size());
			for (const auto& edge : Definition.Edges) {
				const auto first = FindNode(edge.Lower, work);
				const auto last = FindNode(edge.Upper, work);
				EndpointGroups.push_back({first, last});
				const auto distance = static_cast<std::int64_t>(edge.Upper) - edge.Lower;
				if (distance > std::numeric_limits<std::int32_t>::max()) {
					throw std::runtime_error("Emote graph edge exceeds native nonnegative distance domain");
				}
				if (first != last) {
					Connections[first].push_back({last, static_cast<float>(distance)});
					Connections[last].push_back({first, static_cast<float>(distance)});
				}
			}
			Distances.resize(Connections.size());
			Previous.resize(Connections.size());
			Visited.resize(Connections.size());
			for (auto& route : Routes) {
				route.Nodes.reserve(Connections.size());
				route.Distances.reserve(Connections.size());
			}
			// Reserve a worst-case bound so a valid control change cannot invalidate a live route mid-plan.
			std::size_t route_work = 0;
			ChargeGraphWork(route_work, 4 * Connections.size() * Connections.size());
			ChargeGraphWork(route_work, 8 * Definition.Edges.size());
			for (const auto& node : Definition.Nodes) {
				ChargeGraphWork(route_work, 8 * node.size());
			}
		}

		EmoteGraphDefinition Definition;
		std::vector<std::vector<GraphConnection>> Connections;
		std::vector<std::array<std::size_t, 2>> EndpointGroups;
		std::vector<float> Distances;
		std::vector<std::size_t> Previous;
		std::vector<bool> Visited;
		std::array<GraphRoute, 4> Routes;
		float Start = 0;
		float Target = 0;
		std::size_t SelectedRoute = kNoGraphNode;
		bool SameEdge = false;

		void MergeInteriorPoints(std::size_t& work) {
			for (const auto& edge : Definition.Edges) {
				for (auto& node : Definition.Nodes) {
					std::uint32_t sum = 0;
					std::size_t count = 0;
					for (const auto point : node) {
						ChargeGraphWork(work, 1);
						if (point >= edge.Lower && point <= edge.Upper) {
							sum += static_cast<std::uint32_t>(point);
							++count;
						}
					}
					if (count < 2) {
						continue;
					}
					std::erase_if(node, [&](auto point) {
						return point >= edge.Lower && point <= edge.Upper;
					});
					// Native packed-add wraps at 32 bits, then MOVSXD + unsigned DIV, even for a negative sum.
					const auto dividend = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::bit_cast<std::int32_t>(sum)));
					const auto average = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(dividend / count));
					node.push_back(average);
				}
			}
		}

		void SplitEdges(std::size_t& work) {
			std::vector<EmoteGraphEdge> split;
			split.reserve(kMaximumGraphElements);
			for (std::size_t index = 0; index != Definition.Edges.size(); ++index) {
				for (const auto& node : Definition.Nodes) {
					for (const auto point : node) {
						ChargeGraphWork(work, 1);
						auto& edge = Definition.Edges[index];
						if (point > edge.Lower && point < edge.Upper) {
							if (split.size() >= kMaximumGraphElements - Definition.Edges.size()) {
								throw std::runtime_error("Emote graph subdivision exceeds bounded edge ownership");
							}
							split.push_back({point, edge.Upper});
							edge.Upper = point;
						}
					}
				}
				Definition.Edges.insert(Definition.Edges.end(), split.begin(), split.end());
				split.clear();
			}
		}

		std::size_t FindNode(std::int32_t point, std::size_t& work) const {
			for (std::size_t index = 0; index != Definition.Nodes.size(); ++index) {
				for (const auto candidate : Definition.Nodes[index]) {
					ChargeGraphWork(work, 1);
					if (point == candidate) {
						return index;
					}
				}
			}
			return kNoGraphNode;
		}

		void EnsureNode(std::int32_t point, std::size_t& work) {
			if (FindNode(point, work) != kNoGraphNode) {
				return;
			}
			if (Definition.Nodes.size() == kMaximumGraphElements) {
				throw std::runtime_error("Emote graph endpoints exceed bounded node ownership");
			}
			Definition.Nodes.push_back({point});
		}

		std::size_t FindEdge(std::int32_t point) const {
			for (std::size_t index = 0; index != Definition.Edges.size(); ++index) {
				const auto& edge = Definition.Edges[index];
				if (point >= edge.Lower && point <= edge.Upper) {
					return index;
				}
			}
			return kNoGraphNode;
		}

		bool ShortestPath(std::size_t first, std::size_t last, GraphRoute& route, std::size_t& work) {
			route.Nodes.clear();
			route.Distances.clear();
			route.TotalDistance = 0;
			if (first == last) {
				route.Nodes.push_back(first);
				return true;
			}
			std::fill(Distances.begin(), Distances.end(), std::numeric_limits<float>::max());
			std::fill(Previous.begin(), Previous.end(), kNoGraphNode);
			std::fill(Visited.begin(), Visited.end(), false);
			Distances[first] = 0;
			for (std::size_t iteration = 0; iteration != Connections.size(); ++iteration) {
				float minimum = std::numeric_limits<float>::max();
				auto current = kNoGraphNode;
				for (std::size_t node = 0; node != Connections.size(); ++node) {
					ChargeGraphWork(work, 1);
					if (!Visited[node] && minimum > Distances[node]) {
						minimum = Distances[node];
						current = node;
					}
				}
				if (current == kNoGraphNode) {
					break;
				}
				Visited[current] = true;
				for (const auto& connection : Connections[current]) {
					ChargeGraphWork(work, 1);
					const auto next_distance = Distances[current] + connection.Distance;
					if (Distances[connection.Node] > next_distance) {
						Distances[connection.Node] = next_distance;
						Previous[connection.Node] = current;
					}
				}
			}
			auto current = last;
			route.Nodes.push_back(current);
			while (current != first) {
				const auto previous = Previous[current];
				if (previous == kNoGraphNode || route.Nodes.size() == Connections.size()) {
					return false;
				}
				const auto edge = std::find_if(Connections[previous].begin(), Connections[previous].end(), [&](const auto& connection) {
					return connection.Node == current;
				});
				if (edge == Connections[previous].end()) {
					throw std::runtime_error("Emote graph predecessor lost its actual connecting edge");
				}
				route.Distances.push_back(edge->Distance);
				route.TotalDistance += edge->Distance;
				current = previous;
				route.Nodes.push_back(current);
			}
			std::reverse(route.Nodes.begin(), route.Nodes.end());
			std::reverse(route.Distances.begin(), route.Distances.end());
			return true;
		}

		void Prepare(float start, float target) {
			const auto first = FindEdge(FrameInteger(start));
			const auto last = FindEdge(FrameInteger(target));
			Start = start;
			Target = target;
			SameEdge = first != kNoGraphNode && first == last;
			SelectedRoute = kNoGraphNode;
			if (first == kNoGraphNode || last == kNoGraphNode || SameEdge) {
				return;
			}
			std::size_t work = 0;
			float minimum = std::numeric_limits<float>::max();
			for (std::size_t index = 0; index != Routes.size(); ++index) {
				auto& route = Routes[index];
				route.First = (index & 1) != 0 ? Definition.Edges[first].Upper : Definition.Edges[first].Lower;
				route.Last = (index & 2) != 0 ? Definition.Edges[last].Upper : Definition.Edges[last].Lower;
				if (!ShortestPath(FindNode(route.First, work), FindNode(route.Last, work), route, work)) {
					continue;
				}
				route.InitialDistance = std::abs(start - static_cast<float>(route.First));
				route.FinalDistance = std::abs(target - static_cast<float>(route.Last));
				route.TotalDistance = route.InitialDistance + route.TotalDistance;
				route.TotalDistance = route.TotalDistance + route.FinalDistance;
				if (route.TotalDistance < minimum) {
					minimum = route.TotalDistance;
					SelectedRoute = index;
				}
			}
		}

		float Evaluate(float ratio) const {
			if (!std::isfinite(ratio)) {
				throw std::runtime_error("Emote graph ratio requires a finite control value");
			}
			if (!(ratio > 0)) {
				return Start;
			}
			if (!(ratio < 1)) {
				return Target;
			}
			if (SameEdge) {
				return Start + ratio * (Target - Start);
			}
			if (SelectedRoute == kNoGraphNode) {
				return Target;
			}
			const auto& route = Routes[SelectedRoute];
			if (route.TotalDistance == 0) {
				return Target;
			}
			auto distance = ratio * route.TotalDistance;
			if (distance <= route.InitialDistance) {
				return Start + (distance / route.InitialDistance) * (static_cast<float>(route.First) - Start);
			}
			const auto remaining = route.TotalDistance - distance;
			if (remaining <= route.FinalDistance) {
				return Target + ((static_cast<float>(route.Last) - Target) * remaining) / route.FinalDistance;
			}
			distance -= route.InitialDistance;
			for (std::size_t index = 0; index != route.Distances.size(); ++index) {
				const auto length = route.Distances[index];
				if (length >= distance) {
					for (std::size_t edge_index = 0; edge_index != Definition.Edges.size(); ++edge_index) {
						const auto& edge = Definition.Edges[edge_index];
						const auto lower = EndpointGroups[edge_index][0];
						const auto upper = EndpointGroups[edge_index][1];
						if (lower == route.Nodes[index] && upper == route.Nodes[index + 1]) {
							return static_cast<float>(edge.Lower) + (distance / length) * static_cast<float>(edge.Upper - edge.Lower);
						}
						if (upper == route.Nodes[index] && lower == route.Nodes[index + 1]) {
							return static_cast<float>(edge.Upper) + (distance / length) * static_cast<float>(edge.Lower - edge.Upper);
						}
					}
					break;
				}
				distance -= length;
			}
			return static_cast<float>(route.Last);
		}
	};

	EmoteGraphControl::EmoteGraphControl(EmoteGraphDefinition definition) : m_Implementation(std::make_unique<Implementation>(std::move(definition))) {
	}

	EmoteGraphControl::~EmoteGraphControl() noexcept = default;
	EmoteGraphControl::EmoteGraphControl(EmoteGraphControl&&) noexcept = default;
	EmoteGraphControl& EmoteGraphControl::operator=(EmoteGraphControl&&) noexcept = default;

	void EmoteGraphControl::Prepare(float start, float target) {
		m_Implementation->Prepare(start, target);
	}

	void EmoteGraphControl::AssignPath(const EmoteGraphControl& source) {
		if (!m_Implementation || !source.m_Implementation) {
			throw std::runtime_error("Emote graph assignment requires two live owned graphs");
		}

		m_Implementation->Prepare(source.m_Implementation->Start, source.m_Implementation->Target);
	}

	float EmoteGraphControl::Evaluate(float ratio) const {
		return m_Implementation->Evaluate(ratio);
	}

} // namespace sgre::scene
