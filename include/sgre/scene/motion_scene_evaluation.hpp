#pragma once

#include "sgre/scene/motion_child_construction.hpp"
#include "sgre/scene/motion_join_state.hpp"
#include "sgre/scene/motion_layer_clip_rectangles.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_numeric_storage.hpp"
#include "sgre/scene/motion_selected_layer_sources.hpp"
#include "sgre/scene/motion_selected_mesh_mask_packets.hpp"
#include "sgre/scene/motion_shape_owner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sgre::scene {
	struct MotionSceneLayerReference {
		// Checked identity token only. Owner is compared with this evaluator; stale addresses are never dereferenced.
		const void* Owner = nullptr;
		std::uint64_t RootSelectionSerial = 0;
		std::uint64_t GraphGeneration = 0;
		std::uint64_t ContextIdentity = 0;
		std::uint64_t ContextSelectionSerial = 0;
		std::uint32_t NativeOrdinal = 0;
		std::uint32_t GlobalOrdinal = 0;
		std::uint32_t NodeIdentifier = 0;
	};

	struct MotionSceneDrawState {
		const MotionStaticSourceBinding* Source = nullptr;
		const MotionComposedLayer* Numeric = nullptr;
		const MotionClipRectangle* IndividualClip = nullptr;
		const MotionPlayerRenderControls* Render = nullptr;
		MotionProjectId RequestingProject;
		float RootPriority = 0;
		MotionMeshSubdivision Subdivision;
	};

	struct MotionSceneEvaluationServices {
		// The task's actual constructor-owned stream, distinct from the manager generator.
		// Required only when this root has genuine kind4 emission.
		MotionPlayerRandom* PlayerRandom = nullptr;
		MotionSelectedLayerSources::ReadRegisteredAsset ReadRegisteredAsset;
		// Actual immutable preparation owner outlives every selected child and this evaluator.
		// Preserve the native child HalveRgbForClearBlendNibble preparation policy.
		std::function<const PreparedMotionTimeline&(const ResolvedMotionTimeline&)> PrepareChildTimeline;
		// Runtime path transfers a lease into the actual child; externally owned fixture preparations keep the borrowed path.
		std::function<MotionRuntimeSelection(const ResolvedMotionTimeline&)> PrepareOwnedChildTimeline;
		// Checks the real containing render-context queue before any row, packet or selected-tree mutation.
		std::function<void()> RequireUnqueued;
		// Appends these actual published row/packet borrows to the real containing context.
		std::function<void(const MotionSelectedMeshMaskPackets&, std::span<const MotionStencilRowState* const>)> AppendDraw;
		std::function<void(const MotionPlayerEvent&)> DeliverEvent;
		std::size_t MaxContexts = 512;
		std::size_t MaxDepth = 32;
	};

	class MotionSceneEvaluation {
	public:
		// Root update remains the actual task's existing clock/event owner. This object supplies its concrete phases.
		// Actual selected nodes, commands/root/render, registrations, loaded/prepared assets and runtime enclose it.
		// Destroy this evaluator before selected nodes, then retire selected nodes before their immutable preparations.
		MotionSceneEvaluation(MotionSelectedLayerNodes& selected, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, MotionAssetCatalog& catalog, MotionManagerRuntime& runtime, MotionPlayerUpdate& actual_root_update, MotionSceneEvaluationServices services);
		MotionSceneEvaluation(MotionSelectedLayerNodes& selected, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, MotionAssetCatalog& catalog, MotionManagerRuntime& runtime, MotionPlayerUpdate& actual_root_update, MotionSceneEvaluationServices services, const MotionSceneEvaluation& source);
		~MotionSceneEvaluation() noexcept;
		MotionSceneEvaluation(const MotionSceneEvaluation&) = delete;
		MotionSceneEvaluation& operator=(const MotionSceneEvaluation&) = delete;
		MotionSceneEvaluation(MotionSceneEvaluation&&) = delete;
		MotionSceneEvaluation& operator=(MotionSceneEvaluation&&) = delete;

		[[nodiscard]] std::vector<MotionJoinedLayer> CaptureJoin();
		void RestoreJoin(std::vector<MotionJoinedLayer> layers);
		void PrepareFrames(MotionFrameTraversal traversal, const MotionPlayerFrameContext& frame);
		void Compose(const MotionPlayerFrameContext& frame);
		void Publish(const MotionPlayerFrameContext& frame);
		void Draw();
		// Root uses genuine Update(), not diagnostic UpdateChild(0); child phases use actual scaled parent delta.
		void Update();
		void Progress(float elapsed_ticks);
		void PostUpdate();
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool UsesRuntime(const MotionManagerRuntime& actual_runtime) const noexcept;
		[[nodiscard]] const MotionSelectedMeshMaskPackets& Packets() const;
		[[nodiscard]] std::span<const MotionStencilRowState* const> GlobalRows() const;
		// Exact Packets().Index(row) order, including nonglobal mask members. Borrows expire at the next phase/graph mutation.
		[[nodiscard]] std::span<const MotionStencilRowState* const> PacketRows() const;
		[[nodiscard]] MotionSceneDrawState DrawState(const MotionStencilRowState& row) const;
		[[nodiscard]] std::optional<MotionSceneLayerReference> FindLayer(std::string_view name) const;
		// The actual child commands select a current catalogue subtree; no containing/sibling names leak into its lookup.
		[[nodiscard]] std::optional<MotionSceneLayerReference> FindLayer(std::string_view name, const MotionPlayerCommands& actual_player) const;
		// Script numeric getters borrow the physical player's cell allocation, not an ephemeral selected-tree view.
		[[nodiscard]] MotionNumericBorrow BorrowNumeric(const MotionSceneLayerReference& reference) const;
		// Reconstructs a current selected view only for the exact still-live physical cell; no resource/name lookup.
		[[nodiscard]] MotionSceneLayerReference ResolveNumeric(const MotionNumericBorrow& reference) const;
		[[nodiscard]] MotionShapeSnapshot Shape(const MotionSceneLayerReference& reference) const;
		// Queries retained shape owners without advancing selected frames or requiring an unqueued draw.
		[[nodiscard]] bool Contains(const MotionSceneLayerReference& reference, resource::MotionPoint point) const;
		[[nodiscard]] std::uint32_t LayerType(const MotionSceneLayerReference& reference) const;
		// Immediate same-player children in native ordinal order; never includes another selected player's tree.
		[[nodiscard]] std::vector<MotionSceneLayerReference> LayerChildren(const MotionSceneLayerReference& reference) const;
		// Checked borrow of the actual kind3 player; no task/resource retain. Expires on its genuine retirement.
		[[nodiscard]] const MotionChildConstructionState& LayerChild(const MotionSceneLayerReference& reference) const;
		// Native Getter left/top read retained numeric coordinates independently of deformed shape bounds.
		[[nodiscard]] std::array<float, 3> LayerCoordinate(const MotionSceneLayerReference& reference) const;
		[[nodiscard]] MotionLayerVisibility LayerVisibility(const MotionSceneLayerReference& reference) const;
		// Checked completed numeric borrow; caller must not mutate or retire this evaluator while reading.
		[[nodiscard]] const MotionComposedLayer& LayerNumeric(const MotionSceneLayerReference& reference) const;
		[[nodiscard]] MotionLayerOverrides LayerOverrides(const MotionSceneLayerReference& reference) const;
		void SetLayerOverrides(const MotionSceneLayerReference& reference, const MotionLayerOverrides& overrides, bool force_dirty = false);
		[[nodiscard]] std::uint64_t GraphGeneration() const noexcept;
		[[nodiscard]] bool AllPlaying(const MotionPlayerCommands& actual_player) const;

	private:
		friend class MotionScenePlayer;
		// Real root replacement retires old phases before old selected nodes while retaining owning JOIN children.
		void RetireRootForRebuild();
		void RebuildRoot(MotionSelectedLayerNodes& selected, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
