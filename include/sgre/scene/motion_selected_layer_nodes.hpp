#pragma once

#include "sgre/scene/motion_alternate_textures.hpp"
#include "sgre/scene/motion_child_frame_slots.hpp"
#include "sgre/scene/motion_layer_identifiers.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace sgre::scene {
	class MotionStencilMaskOwner;
	class MotionParticleLayerState;
	class MotionParticlePlayerState;
	class MotionLayerShapeOwner;
	class MotionLayerUidReference;
	class MotionPlayerLayerUids;
	class MotionTimelineParameters;
	class MotionLayerParameterBindings;
	class MotionLayerComposition;
	class MotionLocalFrames;
	class MotionLayerMeshLocalOwner;
	class MotionSelectedLayerSources;
	class MotionSelectedMeshGeometry;
	class MotionSelectedMeshAncestry;
	class MotionSelectedMeshVertices;

	struct MotionSelectedLayerNode {
		// Native ordinal includes the separate root at zero; the public layer index does not.
		std::uint32_t NativeOrdinal = 0;
		std::uint32_t Identifier = 0;
		std::int32_t RelativeParent = 0;
		std::optional<std::uint32_t> Parent;
		// This is the actual +400 row ordinal, not a fabricated packet or a node/task identifier.
		std::optional<std::uint32_t> DrawRow;
		std::optional<std::uint32_t> ObjectTrianglePriority;
		// Actual node+43 is outgoing JOIN eligibility, retained even while snapshot producers are unsupported.
		bool JoinTarget = false;
		// Actual510650 marks matched type0/3 nodes for mask publication even if currently hidden.
		bool CompositeMask = false;
		const resource::MotionLayer* Raw = nullptr;
		const PreparedMotionLayer* Prepared = nullptr;
	};

	class MotionSelectedLayerNodes {
	public:
		// The actual enclosing player, immutable raw/prepared resources, manager ID pool and service owners outlive this tree.
		// No manager/resource/VM pin is added. Actual ultimate-player UID, mesh and parameter owners retain their registrations; kind12 owns its recovered mask binding and control summary.
		MotionSelectedLayerNodes(const MotionPlayerCommands& commands, const MotionRootScene& root, const MotionPlayerRenderControls& render, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots);
		MotionSelectedLayerNodes(MotionChildConstructionState& owner, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots);
		MotionSelectedLayerNodes(MotionParticlePlayerState& owner, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots);
		// Native504cf0 reconstructs independent selected ownership without selecting or decoding a frame.
		MotionSelectedLayerNodes(const MotionPlayerCommands& commands, const MotionRootScene& root, const MotionPlayerRenderControls& render, const MotionSelectedLayerNodes& source, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots);
		MotionSelectedLayerNodes(MotionChildConstructionState& owner, const MotionSelectedLayerNodes& source, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots);
		MotionSelectedLayerNodes(MotionParticlePlayerState& owner, const MotionSelectedLayerNodes& source, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots);
		~MotionSelectedLayerNodes() noexcept;
		MotionSelectedLayerNodes(const MotionSelectedLayerNodes&) = delete;
		MotionSelectedLayerNodes& operator=(const MotionSelectedLayerNodes&) = delete;
		MotionSelectedLayerNodes(MotionSelectedLayerNodes&&) = delete;
		MotionSelectedLayerNodes& operator=(MotionSelectedLayerNodes&&) = delete;

		[[nodiscard]] std::span<const MotionSelectedLayerNode> Nodes() const noexcept;
		// Native +318 stores flattened ordinals in forward construction order; child update uses reverse traversal later.
		[[nodiscard]] std::span<const std::uint32_t> ChildOrdinals() const noexcept;
		[[nodiscard]] std::span<const std::uint32_t> ShapeOrdinals() const noexcept;
		[[nodiscard]] std::span<const std::uint32_t> ParticleOrdinals() const noexcept;
		[[nodiscard]] MotionParticleLayerState& Particles(std::uint32_t layer);
		[[nodiscard]] const MotionParticleLayerState& Particles(std::uint32_t layer) const;
		[[nodiscard]] bool OwnsParticleSelection(std::uint32_t layer, const MotionSelectedLayerNodes& selected) const;
		[[nodiscard]] bool OwnsParticleSelection(std::uint32_t layer, std::size_t occurrence, const MotionSelectedLayerNodes& selected) const;
		[[nodiscard]] std::uint32_t SpriteCount() const noexcept;
		[[nodiscard]] std::uint32_t KindMask() const noexcept;
		[[nodiscard]] std::uint32_t DrawRowCount() const noexcept;
		[[nodiscard]] MotionLayerMeshLocalOwner& MeshLocal(std::uint32_t layer);
		[[nodiscard]] const MotionLayerMeshLocalOwner& MeshLocal(std::uint32_t layer) const;
		[[nodiscard]] MotionStencilMaskOwner& StencilMask(std::uint32_t layer);
		[[nodiscard]] const MotionStencilMaskOwner& StencilMask(std::uint32_t layer) const;
		[[nodiscard]] MotionLayerShapeOwner& Shape(std::uint32_t layer);
		[[nodiscard]] const MotionLayerShapeOwner& Shape(std::uint32_t layer) const;
		[[nodiscard]] MotionChildConstructionState& Child(std::uint32_t layer);
		[[nodiscard]] const MotionChildConstructionState& Child(std::uint32_t layer) const;
		[[nodiscard]] MotionChildFrameSlots& ChildSlots(std::uint32_t layer);
		[[nodiscard]] const MotionChildFrameSlots& ChildSlots(std::uint32_t layer) const;
		// The real command selector must have selected this exact child timeline before its selected nodes are rebuilt.
		// Preparation and child action-queue service remain borrowed; this is not a replacement Play or UpdateChild operation.
		void RebuildChildNodes(std::uint32_t layer, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionChildFrameSlotServices child_slots);
		[[nodiscard]] MotionSelectedLayerNodes* ChildNodes(std::uint32_t layer);
		// Native reset stops first, retires actual selected descendants, then clears current/requested motion names.
		void ResetChildMotion(std::uint32_t layer);
		// Returns each parent ID before retiring that node's selected child tree and exclusive child state.
		// The enclosing player's separate root survives. Repeated retirement is harmless.
		void Retire();
		// Native50fd90 preserves the player's actual ordered parameter records; caller owns them until replacement/retirement.
		[[nodiscard]] std::unique_ptr<MotionTimelineParameters> ClearLayers();

	private:
		friend class MotionSceneEvaluation;
		friend class MotionStencilMaskOwner;
		friend class MotionPlayerParameterValues;
		friend class MotionLayerShapeOwner;
		friend class MotionAlternateTextures;
		friend class MotionLayerUidReference;
		friend class MotionPlayerLayerUids;
		friend class MotionLocalFrames;
		friend class MotionSelectedLayerSources;
		friend class MotionSelectedMeshGeometry;
		friend class MotionSelectedMeshAncestry;
		friend class MotionSelectedMeshVertices;
		friend class MotionLayerMeshLocalOwner;
		friend class MotionLayerComposition;
		friend class MotionLayerStencilAncestry;
		friend class MotionStencilRowPolicies;
		friend class MotionSelectedQuadGeometry;
		struct Owner;
		using JoinedChildOwners = std::pair<std::unique_ptr<MotionChildConstructionState>, std::unique_ptr<MotionSelectedLayerNodes>>;
		void UnbindJoinedDescriptors() const noexcept;
		void RebindJoinedDescriptors() const;
		[[nodiscard]] JoinedChildOwners ExtractJoinedChild(std::uint32_t layer);
		void InstallJoinedChild(std::uint32_t layer, JoinedChildOwners& owners, MotionChildFrameSlotServices action_service);
		[[nodiscard]] const MotionPlayerParameterValues& DescriptorChildValues(std::uint32_t layer) const;
		[[nodiscard]] MotionLayerMeshLocalOwner& MutableUidMesh(std::uint32_t layer) const;
		[[nodiscard]] MotionLayerShapeOwner* ShapeOwner(std::uint32_t layer) const noexcept;
		MotionSelectedLayerNodes(const MotionPlayerCommands& commands, const MotionRootScene& root, const MotionPlayerRenderControls& render, MotionChildConstructionState* owner, MotionParticlePlayerState* particle, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots, std::size_t owner_depth);
		MotionSelectedLayerNodes(const MotionPlayerCommands& commands, const MotionRootScene& root, const MotionPlayerRenderControls& render, MotionChildConstructionState* owner, MotionParticlePlayerState* particle, const MotionSelectedLayerNodes& source, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots);
		void CloneOwners(const MotionSelectedLayerNodes& source);
		void ValidateLayers(const std::vector<resource::MotionLayer>& layers, std::optional<std::uint32_t> parent, std::size_t depth, std::size_t& index, std::size_t& frames) const;
		void ConstructLayers(const std::vector<resource::MotionLayer>& layers, std::optional<std::uint32_t> parent, std::size_t& index);
		void QueueAction(std::string_view label, std::string_view action);
		void FinalizeNodes();
		void Retire(bool preserve_parameters);
		[[nodiscard]] Owner& ReadChildOwner(std::uint32_t layer);
		[[nodiscard]] const Owner& ReadChildOwner(std::uint32_t layer) const;
		const MotionPlayerCommands& m_Commands;
		const MotionRootScene& m_Root;
		const MotionPlayerRenderControls& m_Render;
		MotionChildConstructionState* m_EnclosingChild = nullptr;
		MotionParticlePlayerState* m_EnclosingParticle = nullptr;
		MotionSelectedLayerNodes* m_ContainingSelected = nullptr;
		std::uint64_t m_CommandSelectionSerial = 0;
		const resource::MotionTimeline& m_Timeline;
		const PreparedMotionTimeline& m_Preparation;
		MotionLayerIdentifierPool& m_Identifiers;
		MotionChildConstructionServices m_Construction;
		MotionChildFrameSlotServices m_Slots;
		std::size_t m_OwnerDepth = 0;
		std::unique_ptr<MotionTimelineParameters> m_Parameters;
		std::unique_ptr<MotionLayerParameterBindings> m_ParameterBindings;
		MotionLayerComposition* m_CompositionOwner = nullptr;
		MotionLocalFrames* m_LocalNumericOwner = nullptr;
		MotionSelectedLayerSources* m_SourceOwner = nullptr;
		MotionSelectedMeshGeometry* m_MeshGeometryOwner = nullptr;
		MotionSelectedMeshAncestry* m_DeformationOwner = nullptr;
		MotionSelectedMeshVertices* m_VertexOwner = nullptr;
		std::vector<MotionSelectedLayerNode> m_Nodes;
		// Native UID callbacks mutate only these actual node source descriptors, including during finalization.
		mutable std::vector<MotionExternalSourceCell> m_ExternalSources;
		std::vector<std::unique_ptr<Owner>> m_Owners;
		std::vector<std::uint32_t> m_ChildOrdinals;
		std::vector<std::uint32_t> m_ParticleOrdinals;
		std::vector<std::uint32_t> m_ShapeOrdinals;
		std::uint32_t m_SpriteCount = 0;
		std::uint32_t m_KindMask = 0;
		std::uint32_t m_DrawRowCount = 0;
		// Only actual per-node Added notification admits native construction-time mesh writes.
		std::optional<std::uint32_t> m_FinalizingUidOrdinal;
		bool m_Published = false;
		bool m_Mutating = false;
	};

} // namespace sgre::scene
