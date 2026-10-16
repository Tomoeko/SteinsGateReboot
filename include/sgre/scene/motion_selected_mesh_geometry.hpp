#pragma once

#include "sgre/scene/motion_clip_bounds.hpp"
#include "sgre/scene/motion_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sgre::scene {

	class MotionLayerComposition;
	class MotionManagerRuntime;
	class MotionSelectedLayerNodes;
	class MotionSelectedLayerSources;
	struct MotionSelectedLayerNode;

	class MotionSelectedMeshGeometry {
	public:
		// Every dependency is the actual containing selected/local/source or manager policy owner.
		// Ordinary recomputed roots and kind0 mode1 sync0/8 only; containing-child transfer remains unsupported.
		MotionSelectedMeshGeometry(MotionSelectedLayerNodes& selected, MotionLayerComposition& composition, MotionSelectedLayerSources& sources, const MotionManagerRuntime& manager);
		~MotionSelectedMeshGeometry() noexcept;
		MotionSelectedMeshGeometry(const MotionSelectedMeshGeometry&) = delete;
		MotionSelectedMeshGeometry& operator=(const MotionSelectedMeshGeometry&) = delete;
		void Compose();
		[[nodiscard]] const MotionDeformationLink& Link(std::uint32_t layer) const;
		// Actual node+42 is set by the native rebuilding branch, including hidden/disabled retained geometry.
		[[nodiscard]] bool Changed(std::uint32_t layer) const;
		[[nodiscard]] bool Allocated(std::uint32_t layer) const;
		[[nodiscard]] MotionClipGeometry& Geometry(std::uint32_t layer);
		[[nodiscard]] const MotionMeshSubdivision& Subdivision(std::uint32_t layer) const;

	private:
		struct Entry {
			const MotionSelectedLayerNode* Node = nullptr;
			std::vector<resource::MotionPoint> Positions;
			MotionClipGeometry Geometry;
			MotionMeshSubdivision Subdivision;
			bool HasFrame = false;
			bool Rebuilt = false;
			bool Allocated = false;
		};
		void RequireOwners() const;
		void RequirePhase(std::uint32_t layer) const;
		MotionSelectedLayerNodes& m_Selected;
		MotionLayerComposition& m_Composition;
		MotionSelectedLayerSources& m_Sources;
		const MotionManagerRuntime& m_Manager;
		std::vector<MotionDeformationNode> m_Nodes;
		std::vector<MotionDeformationLink> m_Links;
		std::vector<MotionDeformationLink> m_Candidates;
		std::vector<MotionGeometryFrame> m_Frames;
		std::vector<MotionGeometryCache> m_Caches;
		std::vector<MotionMeshDivisionCache> m_Divisions;
		std::vector<Entry> m_Entries;
		std::optional<PreparedMotionGeometry> m_Geometry;
		std::uint64_t m_CompositionSerial = 0;
		bool m_Composed = false;
	};

} // namespace sgre::scene
