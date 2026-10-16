#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_player_update.hpp"

#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_shape_owner.hpp"
#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace sgre::scene {
	static void RequireClonedLayerIdentity(std::span<const MotionSelectedLayerNode> nodes, std::span<const MotionSelectedLayerNode> source_nodes, std::uint32_t layer, bool distinct_selected, bool distinct_identifier_pool) {
		if (!distinct_selected || layer >= nodes.size() || layer >= source_nodes.size() || nodes[layer].Raw != source_nodes[layer].Raw || nodes[layer].Prepared != source_nodes[layer].Prepared || nodes[layer].NativeOrdinal != source_nodes[layer].NativeOrdinal || (!distinct_identifier_pool && nodes[layer].Identifier == source_nodes[layer].Identifier)) {
			throw std::runtime_error("motion cloned layer state requires its independent actual selected node and shared immutable preparation");
		}
	}

	MotionLayerMeshLocalOwner::MotionLayerMeshLocalOwner(MotionSelectedLayerNodes& selected, std::uint32_t layer, const MotionLayerMeshLocalOwner& source)
	    : MotionLayerMeshLocalOwner(selected, layer) {
		source.RequireLive();
		RequireClonedLayerIdentity(selected.m_Nodes, source.m_Selected.m_Nodes, layer, &selected != &source.m_Selected, &selected.m_Identifiers != &source.m_Selected.m_Identifiers);
		if (source.m_Layer != layer) {
			throw std::runtime_error("motion mesh clone differs from the actual selected source ordinal");
		}
		m_Slots = source.m_Slots;
		m_OwnPatch = source.m_OwnPatch;
		m_CurrentLattice = source.m_CurrentLattice;
		m_PixelOrigin = source.m_PixelOrigin;
		m_PhysicalOrigin = source.m_PhysicalOrigin;
		m_OverrideLatched = source.m_OverrideLatched;
		m_PendingWrite = source.m_PendingWrite;
		m_EvaluatedFrame = source.m_EvaluatedFrame;
		m_EvaluatedPosition = source.m_EvaluatedPosition;
		m_Evaluated = source.m_Evaluated;
	}

	MotionLayerShapeOwner::MotionLayerShapeOwner(MotionSelectedLayerNodes& selected, std::uint32_t layer, const MotionLayerShapeOwner& source)
	    : MotionLayerShapeOwner(selected, layer) {
		source.RequireOwner();
		RequireClonedLayerIdentity(selected.m_Nodes, source.m_Selected.m_Nodes, layer, &selected != &source.m_Selected, &selected.m_Identifiers != &source.m_Selected.m_Identifiers);
		if (source.m_Layer != layer || source.m_Snapshot.Type != m_Snapshot.Type) {
			throw std::runtime_error("motion shape clone differs from its actual source ordinal/type");
		}
		m_Snapshot = source.m_Snapshot;
		m_CompositionSerial = source.m_CompositionSerial;
		// The new context binds its own copied composition; no source phase pointer escapes this constructor.
		m_Composition = nullptr;
	}

	MotionStencilMaskOwner::MotionStencilMaskOwner(const MotionStencilMaskOwner& source)
	    : MotionStencilMaskOwner(source.m_Layer, source.m_Prepared, source.m_Resource) {
		m_Controls = source.m_Controls;
		m_FrameIndices = source.m_FrameIndices;
		m_Evaluation = source.m_Evaluation;
		m_Summary = source.m_Summary;
		m_SummaryPublished = source.m_SummaryPublished;
		m_EvaluatedFrameIndex = source.m_EvaluatedFrameIndex;
		m_EvaluatedPosition = source.m_EvaluatedPosition;
		// Native clone rebinds actual member identities to the new selected tree after constructing its nodes.
		m_Bound = false;
	}
	void MotionPlayerUpdate::CopyForClone(const MotionPlayerUpdate& source, MotionManagerRuntime& runtime) {
		if (source.m_Updating || source.m_Delivering || m_Updating || m_Delivering) {
			throw std::runtime_error("motion update clone requires idle detached event delivery");
		}
		m_Prepared = source.m_Prepared;
		for (auto& prepared : m_Prepared) {
			if (!prepared.Owner.IsEmpty()) {
				prepared.Owner = runtime.ClonePreparation(prepared.Owner).Owner;
			}
		}
		m_PendingEvents = source.m_PendingEvents;
		m_DeliveringEvents = source.m_DeliveringEvents;
		m_PendingPreparationOwners = source.m_PendingPreparationOwners;
		m_DeliveringPreparationOwners = source.m_DeliveringPreparationOwners;
		for (auto* owners : {&m_PendingPreparationOwners, &m_DeliveringPreparationOwners}) {
			for (auto& owner : *owners) {
				if (!owner.IsEmpty()) {
					owner = runtime.ClonePreparation(owner).Owner;
				}
			}
		}
		m_PreparedTagCount = source.m_PreparedTagCount;
		m_TagIndex = source.m_TagIndex;
		m_HasParticlePhase = source.m_HasParticlePhase;
	}
} // namespace sgre::scene
