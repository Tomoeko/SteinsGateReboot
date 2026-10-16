#pragma once

#include "sgre/scene/motion_root_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sgre::scene {
	class MotionLocalFrames;
	class MotionSelectedLayerNodes;
	class MotionLayerMeshLocalOwner;
	class MotionSelectedMeshAncestry;

	struct MotionLayerOverrides {
		std::array<float, 3> Coordinate{};
		float Angle = 0;
		float ZoomX = 1;
		float ZoomY = 1;
		float ShearX = 0;
		float ShearY = 0;
		std::uint32_t Opacity = 255;
		bool Enabled = true;
		bool Visible = true;
		bool FlipX = false;
		bool FlipY = false;
		bool operator==(const MotionLayerOverrides&) const = default;
	};

	struct MotionLayerVisibility {
		bool Visible = true;
		bool BranchVisible = true;
	};

	struct MotionComposedLayer {
		MotionHierarchyState Hierarchy;
		std::array<float, 3> Displacement{};
		// A missing parent denotes the real root; selected parents index this owner's child span.
		std::optional<std::uint32_t> EffectiveParent;
		// Frame absence, inherited visibility and hierarchy activity are distinct native gates.
		bool FrameHidden = true;
		bool Visible = false;
		bool Active = false;
	};

	class MotionLayerComposition {
	public:
		// Preparation remains immutable; preparation and root outlive this selected owner.
		// Each input sample's borrowed resource fields remain alive through the published frame.
		MotionLayerComposition(const PreparedMotionTimeline& timeline, MotionRootScene& root, bool normalize_against_root);
		// Kind3/kind12 numeric outputs require the exact selected root and exclusive control owners.
		MotionLayerComposition(MotionSelectedLayerNodes& selected, MotionRootScene& root, bool normalize_against_root);
		~MotionLayerComposition() noexcept;
		MotionLayerComposition(const MotionLayerComposition&) = delete;
		MotionLayerComposition& operator=(const MotionLayerComposition&) = delete;
		void SetOverrides(std::uint32_t layer, const MotionLayerOverrides& overrides, bool force_dirty = false);
		[[nodiscard]] const MotionLayerOverrides& Overrides(std::uint32_t layer) const;
		// Actual local frame preparation/variable/actions must precede this forced numeric composition.
		// Source readiness, geometry, clips and draw-packet publication follow as separate required phases.
		void Compose(std::span<const MotionLayerSample> local_samples, bool tick_assigned);
		// Actual cursor/source/action selection precedes this normal conditional numeric phase.
		// CompleteComposition on the local owner follows the required geometry/other producer phases.
		void Compose(MotionLocalFrames& local_frames, bool tick_assigned, bool force_all = false);
		// Output borrows this owner until the next composition; a failed mutating phase leaves no published frame.
		[[nodiscard]] std::span<const MotionComposedLayer> Layers() const noexcept;
		// Raw native getters read owned numeric words before initial composition and while newer input remains pending.
		[[nodiscard]] std::array<float, 3> RetainedCoordinate(std::uint32_t layer) const;
		[[nodiscard]] MotionLayerVisibility RetainedVisibility(std::uint32_t layer) const;
		// Borrows completed numeric storage until this composition owner mutates or retires.
		[[nodiscard]] const MotionComposedLayer& RetainedLayer(std::uint32_t layer) const;
		// The normal conditional phase exposes the actual native Changed gate consumed by geometry.
		[[nodiscard]] bool Changed(std::uint32_t layer) const;
		[[nodiscard]] const MotionLocalFrames& LocalFrames() const;

	private:
		friend class MotionSceneEvaluation;
		friend class MotionLocalFrames;
		friend class MotionLayerShapeOwner;
		friend class MotionSelectedMeshGeometry;
		friend class MotionSelectedMeshAncestry;
		MotionLayerComposition(const PreparedMotionTimeline& timeline, MotionRootScene& root, bool normalize_against_root, MotionSelectedLayerNodes* selected);
		void RequireSelectedOwner(bool require_current_parameters = true) const;
		[[nodiscard]] const MotionLayerMeshLocalOwner* RequireMeshOwner(std::size_t layer) const;
		[[nodiscard]] std::optional<std::uint32_t> EffectiveParent(std::size_t layer) const;
		void ComposeLayer(std::size_t index, const MotionLayerSample& sample, bool tick_assigned);
		const PreparedMotionTimeline& m_Timeline;
		MotionRootScene& m_Root;
		MotionSelectedLayerNodes* m_SelectedOwner = nullptr;
		const MotionSelectedMeshAncestry* m_DeformationOwner = nullptr;
		const MotionLocalFrames* m_LocalFrames = nullptr;
		bool m_NormalizeAgainstRoot = false;
		std::vector<MotionLayerOverrides> m_Overrides;
		std::vector<MotionComposedLayer> m_Layers;
		std::vector<bool> m_OverrideDirty;
		std::vector<bool> m_Changes;
		bool m_Composed = false;
		bool m_ConditionalTrace = false;
		std::uint64_t m_ComposedSelectionSerial = 0;
		std::uint64_t m_CompositionSerial = 0;
	};

} // namespace sgre::scene
