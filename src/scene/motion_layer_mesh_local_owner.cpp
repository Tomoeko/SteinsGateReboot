#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "motion_native_compared_mesh.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"
#include "sgre/scene/motion_deformation.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::scene {
	static constexpr std::uint32_t kDecodedMeshMask = 0x02000000;

	static std::uint32_t RawMeshFrameMask(const resource::MotionFrame& frame) {
		const auto* value = frame.Content.Find("mask");
		if (value != nullptr) {
			if (const auto* integer = std::get_if<std::int64_t>(&value->Data); integer != nullptr && *integer >= std::numeric_limits<std::int32_t>::min() && *integer <= std::numeric_limits<std::uint32_t>::max()) {
				return static_cast<std::uint32_t>(*integer);
			}
			if (const auto* boolean = std::get_if<bool>(&value->Data)) {
				return *boolean ? 1u : 0u;
			}
			if (const auto* real = std::get_if<double>(&value->Data); real != nullptr && std::isfinite(*real) && *real >= std::numeric_limits<std::int32_t>::min() && *real <= std::numeric_limits<std::int32_t>::max()) {
				return static_cast<std::uint32_t>(static_cast<std::int32_t>(*real));
			}
		}

		throw std::runtime_error("motion local mesh frame requires its actual bounded numeric mask");
	}

	static void RequireMatchingMeshFrame(const resource::MotionFrame& raw, const PreparedMotionFrame& prepared, const resource::MotionResource& resource) {
		if (raw.Type == 0) {
			return;
		}
		if (RawMeshFrameMask(raw) != prepared.Attributes.Mask) {
			throw std::runtime_error("motion local mesh frame mask differs from its actual raw preparation");
		}
		if ((prepared.Attributes.Mask & kDecodedMeshMask) == 0) {
			return;
		}

		const auto* mesh = raw.Content.Find("mesh");
		if (mesh == nullptr || mesh->Find("cc") == nullptr || mesh->Find("bp") == nullptr) {
			throw std::runtime_error("motion local mesh payload requires actual mesh/cc/bp metadata");
		}
		const auto actual = resource::ReadMotionDeformation(*mesh, resource);
		const auto& expected = prepared.Attributes.Deformation;
		if (actual.Patch.has_value() != expected.Patch.has_value() || actual.EasingIndex != expected.EasingIndex) {
			throw std::runtime_error("motion local mesh payload differs from its actual raw preparation");
		}
		if (actual.Patch) {
			for (std::size_t index = 0; index < actual.Patch->Controls.size(); ++index) {
				const auto& point = actual.Patch->Controls[index];
				const auto& wanted = expected.Patch->Controls[index];
				if (std::bit_cast<std::uint32_t>(point.X) != std::bit_cast<std::uint32_t>(wanted.X) || std::bit_cast<std::uint32_t>(point.Y) != std::bit_cast<std::uint32_t>(wanted.Y)) {
					throw std::runtime_error("motion local mesh control differs from its actual raw preparation");
				}
			}
		}
	}

	MotionLayerMeshLocalOwner::MotionLayerMeshLocalOwner(MotionSelectedLayerNodes& selected, std::uint32_t layer)
	    : m_Selected(selected), m_Layer(layer) {
		if (layer >= selected.m_Nodes.size()) {
			throw std::runtime_error("motion local mesh owner requires its actual selected construction cell");
		}

		const auto& node = selected.m_Nodes[layer];
		if (node.Raw == nullptr || node.Prepared != &selected.m_Preparation.Layers[layer] || node.Raw->Type != 0 || node.Prepared->Type != 0 || node.Prepared->MeshTransform != 1 || node.Prepared->MeshCombinator || node.Raw->Frames.size() != node.Prepared->Frames.size()) {
			throw std::runtime_error("motion local mesh owner requires actual matching ordinary kind0 bicubic preparation");
		}
		const auto& selection = selected.m_Commands.SelectedProject();
		if (!selection || selection->Timeline != &selected.m_Timeline || !selected.m_Commands.Manager().Contains(selection->Project)) {
			throw std::runtime_error("motion local mesh owner requires its actual registered selected resource");
		}
		m_CurrentLattice.Controls.reserve(20);
		for (std::size_t index = 0; index < node.Prepared->Frames.size(); ++index) {
			const auto& frame = node.Prepared->Frames[index];
			RequireMatchingMeshFrame(node.Raw->Frames[index], frame, selected.m_Commands.Manager().Resource(selection->Project));
			if (frame.Attributes.Deformation.EasingIndex && *frame.Attributes.Deformation.EasingIndex >= selected.m_Commands.Manager().Resource(selection->Project).Easing.size()) {
				throw std::runtime_error("motion local mesh easing exceeds the actual registered resource");
			}
		}
	}

	void MotionLayerMeshLocalOwner::RequireLive() const {
		const auto nodes = m_Selected.Nodes();
		if (!m_Selected.m_Published || m_Selected.m_Mutating || m_Layer >= nodes.size() || nodes[m_Layer].Prepared != &m_Selected.m_Preparation.Layers[m_Layer] || &m_Selected.MeshLocal(m_Layer) != this) {
			throw std::runtime_error("motion local mesh phase requires its actual live exclusive selected owner");
		}
	}

	void MotionLayerMeshLocalOwner::PrepareFrame(std::size_t slot, std::size_t frame) {
		RequireLive();
		const auto& prepared = m_Selected.m_Preparation.Layers[m_Layer];
		if (slot >= m_Slots.size() || frame >= prepared.Frames.size()) {
			throw std::runtime_error("motion local mesh decoded slot/frame exceeds actual preparation");
		}

		auto& decoded = m_Slots[slot];
		decoded.FrameIndex = frame;
		const auto& input = prepared.Frames[frame];
		// Actual512e40 only clears and decodes the mesh descriptor for an explicit mesh bit.
		if (!input.Hidden && (input.Attributes.Mask & kDecodedMeshMask) != 0) {
			decoded.Deformation = input.Attributes.Deformation;
		}
	}

	void MotionLayerMeshLocalOwner::ApplyLocalEvaluation(const MotionLocalFrameState& state, float position, const MotionLayerEvaluationCache& evaluation, bool changed) {
		RequireLive();
		if (!state.Initialized || state.CurrentSlot >= m_Slots.size() || m_Slots[state.CurrentSlot].FrameIndex != state.CurrentIndex || m_Slots[1 - state.CurrentSlot].FrameIndex != state.CurrentIndex + 1 || !evaluation.Sample || !std::isfinite(position) || !std::isfinite(evaluation.Ratio)) {
			throw std::runtime_error("motion local mesh evaluation requires its actual adjacent physical slots and completed common numeric cache");
		}

		const auto& prepared = m_Selected.m_Preparation.Layers[m_Layer];
		const auto& current = prepared.Frames[state.CurrentIndex];
		const auto& following = prepared.Frames[state.CurrentIndex + 1];
		if (current.Hidden != evaluation.Sample->Hidden || position < current.Time || position > m_Selected.m_Timeline.LastTime) {
			throw std::runtime_error("motion local mesh evaluation differs from its actual selected numeric frame");
		}
		if (changed && !current.Hidden) {
			const auto& first = m_Slots[state.CurrentSlot].Deformation;
			m_OwnPatch = first.Patch;
			if (current.Tween && !following.Hidden && std::abs(evaluation.Ratio) >= std::numeric_limits<float>::epsilon()) {
				const auto& selection = *m_Selected.m_Commands.SelectedProject();
				const auto& resource = m_Selected.m_Commands.Manager().Resource(selection.Project);
				const auto* easing = first.EasingIndex ? &resource.Easing.at(*first.EasingIndex) : nullptr;
				m_OwnPatch = InterpolateMotionBezierPatch(first.Patch, m_Slots[1 - state.CurrentSlot].Deformation.Patch, evaluation.Ratio, easing);
			}
		}

		if (changed && !current.Hidden && !m_OverrideLatched) {
			m_CurrentLattice.EncodedDegree = m_OwnPatch ? std::uint8_t{0x33} : std::uint8_t{0};
			if (m_OwnPatch) {
				m_CurrentLattice.Controls.assign(m_OwnPatch->Controls.begin(), m_OwnPatch->Controls.end());
			} else {
				m_CurrentLattice.Controls.clear();
			}
		}

		m_EvaluatedFrame = state.CurrentIndex;
		m_EvaluatedPosition = position;
		m_Evaluated = true;
	}

	void MotionLayerMeshLocalOwner::AssignOrigins(const resource::MotionPoint& pixel_origin, const resource::MotionPoint& physical_origin) {
		if (&m_Selected.MutableUidMesh(m_Layer) != this) {
			throw std::runtime_error("motion mesh origins differ from the actual exclusive UID owner");
		}
		if (!std::isfinite(pixel_origin.X) || !std::isfinite(pixel_origin.Y) || !std::isfinite(physical_origin.X) || !std::isfinite(physical_origin.Y)) {
			throw std::runtime_error("motion mesh origins require finite actual input pairs");
		}

		m_PixelOrigin = pixel_origin;
		m_PhysicalOrigin = physical_origin;
		m_OverrideLatched = true;
	}

	resource::MotionPoint MotionLayerMeshLocalOwner::PixelOrigin() const {
		RequireLive();

		return m_PixelOrigin;
	}

	resource::MotionPoint MotionLayerMeshLocalOwner::PhysicalOrigin() const {
		RequireLive();

		return m_PhysicalOrigin;
	}

	bool MotionLayerMeshLocalOwner::AssignLattice(const resource::MotionBezierLattice* lattice, bool compare) {
		// The native UID callback can write during per-node construction, before publication.
		if (&m_Selected.MutableUidMesh(m_Layer) != this) {
			throw std::runtime_error("motion mesh assignment differs from the actual exclusive UID owner");
		}
		if (lattice != nullptr) {
			ValidateMotionBezierLattice(*lattice);
			const auto horizontal = lattice->EncodedDegree & 15;
			const auto vertical = lattice->EncodedDegree >> 4;
			if (lattice->EncodedDegree != 0 && (horizontal < 1 || horizontal > 6 || vertical < 1 || vertical > 6)) {
				throw std::runtime_error("motion mesh UID lattice requires its recovered degree1..6 geometry consumer");
			}
		}
		auto candidate = m_CurrentLattice;
		if (!AssignMotionBezierLattice(candidate, lattice, compare)) {
			return false;
		}
		if (m_Selected.m_LocalNumericOwner != nullptr) {
			m_Selected.m_LocalNumericOwner->MarkMeshDirty(m_Layer);
		}
		m_CurrentLattice = std::move(candidate);
		m_OverrideLatched = true;
		m_PendingWrite = true;

		return true;
	}

	bool MotionLayerMeshLocalOwner::AssignComparedNativeOutput(const resource::MotionBezierLattice& lattice) {
		if (&m_Selected.MutableUidMesh(m_Layer) != this) {
			throw std::runtime_error("native compared mesh differs from the actual exclusive UID owner");
		}
		auto candidate = m_CurrentLattice;
		if (!AssignNativeComparedMotionBezierLattice(candidate, lattice)) {
			return false;
		}
		if (m_Selected.m_LocalNumericOwner != nullptr) {
			m_Selected.m_LocalNumericOwner->MarkMeshDirty(m_Layer);
		}
		m_CurrentLattice = std::move(candidate);
		m_OverrideLatched = true;
		m_PendingWrite = true;
		return true;
	}

	const resource::MotionBezierLattice& MotionLayerMeshLocalOwner::CurrentLattice() const {
		RequireLive();

		return m_CurrentLattice;
	}

	bool MotionLayerMeshLocalOwner::OverrideLatched() const {
		RequireLive();

		return m_OverrideLatched;
	}

	bool MotionLayerMeshLocalOwner::PendingWrite() const {
		RequireLive();

		return m_PendingWrite;
	}

	void MotionLayerMeshLocalOwner::CompleteComposition() {
		RequireLive();
		m_PendingWrite = false;
	}

	const MotionMeshDecodedSlot& MotionLayerMeshLocalOwner::Slot(std::size_t slot) const {
		RequireLive();
		if (slot >= m_Slots.size() || !m_Slots[slot].FrameIndex) {
			throw std::runtime_error("motion local mesh slot has no genuine decoded frame");
		}

		return m_Slots[slot];
	}

	const std::optional<resource::MotionBezierPatch>& MotionLayerMeshLocalOwner::OwnPatch() const {
		RequireLive();
		if (!m_Evaluated) {
			throw std::runtime_error("motion local mesh controls require genuine selected numeric evaluation");
		}

		return m_OwnPatch;
	}

	std::size_t MotionLayerMeshLocalOwner::EvaluatedFrameIndex() const {
		static_cast<void>(OwnPatch());

		return m_EvaluatedFrame;
	}

	float MotionLayerMeshLocalOwner::EvaluatedPosition() const {
		static_cast<void>(OwnPatch());

		return m_EvaluatedPosition;
	}

} // namespace sgre::scene
