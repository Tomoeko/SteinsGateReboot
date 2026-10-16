#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace sgre::scene {

	struct MotionDeformationNode {
		std::optional<std::uint32_t> Parent;
		std::uint32_t InheritMask = 0;
		std::uint32_t MeshTransform = 0;
		std::uint32_t MeshSyncChildMask = 0;
		std::uint32_t MeshDivision = 0;
		bool MeshCombine = false;
		bool Hidden = true;
		bool SourceReady = false;
		bool HasPatch = false;
	};

	struct MotionDeformationLink {
		std::optional<std::uint32_t> Ancestor;
		bool Active = false;
		bool ExportToChildren = false;
		bool StopsInheritance = false;
	};

	// Input parents must precede their children; links borrow no input state.
	void ResolveMotionDeformationLinks(std::span<const MotionDeformationNode> nodes, std::span<MotionDeformationLink> links);

} // namespace sgre::scene
