#include "sgre/scene/motion_deformation_chain.hpp"

#include <stdexcept>

namespace sgre::scene {

	static void ValidateHierarchy(std::span<const MotionDeformationNode> nodes, std::span<MotionDeformationLink> links) {
		static constexpr std::size_t kMaxLayers = 1000000;
		static constexpr std::size_t kMaxDepth = 128;
		if (nodes.size() != links.size() || nodes.size() > kMaxLayers) {
			throw std::runtime_error("motion deformation hierarchy count is invalid");
		}

		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto parent = nodes[index].Parent;
			if (parent && *parent >= index) {
				throw std::runtime_error("motion deformation parent must precede its child");
			}

			auto ancestor = parent;
			std::size_t depth = 0;
			while (ancestor) {
				if (++depth > kMaxDepth) {
					throw std::runtime_error("motion deformation hierarchy exceeds its depth limit");
				}

				ancestor = nodes[*ancestor].Parent;
			}
		}
	}

	static bool CombinedChainReachesAncestor(std::span<const MotionDeformationNode> nodes, std::uint32_t index, std::uint32_t ancestor) {
		while (nodes[index].MeshCombine) {
			const auto parent = nodes[index].Parent;
			if (!parent) {
				return false;
			}

			index = *parent;
			if (index == ancestor) {
				return true;
			}
		}

		return false;
	}

	void ResolveMotionDeformationLinks(std::span<const MotionDeformationNode> nodes, std::span<MotionDeformationLink> links) {
		ValidateHierarchy(nodes, links);
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& node = nodes[index];
			MotionDeformationLink link;
			if (node.Parent) {
				const auto& parent = links[*node.Parent];
				link.Ancestor = parent.ExportToChildren || parent.StopsInheritance ? node.Parent : parent.Ancestor;
			}

			// Native 0x140520d80 treats cleared mesh inheritance as a stopping boundary.
			if ((node.InheritMask & 0x02000000) == 0 && link.Ancestor) {
				link.StopsInheritance = true;
				link.Active = CombinedChainReachesAncestor(nodes, static_cast<std::uint32_t>(index), *link.Ancestor);
			}

			if (!node.Hidden && node.MeshTransform != 0 && node.HasPatch && node.SourceReady) {
				link.Active = true;
				link.ExportToChildren = (node.MeshSyncChildMask & 8) != 0;
			}

			links[index] = link;
		}
	}

} // namespace sgre::scene
