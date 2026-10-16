#include "sgre/scene/motion_layer_composition.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"
#include "sgre/scene/motion_deformation.hpp"
#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_selected_layer_sources.hpp"
#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::uint32_t kSkipCompositionAncestor = 0x400000;
	static constexpr std::uint32_t kRootMeshInheritance = 0x02000000;
	static constexpr std::uint32_t kSupportedCompositionInheritance = 0x7fc | kSkipCompositionAncestor;
	static constexpr std::size_t kMaximumCompositionAncestorDepth = 128;

	static bool CanResolveRootMeshInheritance(const PreparedMotionLayer& layer, bool has_parent, MotionRootMatrixMode root_matrix_mode) {
		// 520d80 resolves no deformation ancestor through the ordinary plain root.
		return layer.Type == 0 && layer.MeshTransform == 0 && layer.CoordinatePlane == 0 && layer.StencilType == 0 && !has_parent && root_matrix_mode == MotionRootMatrixMode::Recomputed;
	}

	static void RequireFiniteComposition(const MotionLayerSample& sample, const MotionLayerMeshLocalOwner* mesh = nullptr) {
		for (const auto component : sample.Coordinate) {
			if (!std::isfinite(component)) {
				throw std::runtime_error("motion layer composition requires finite local coordinates");
			}
		}

		if (!std::isfinite(sample.Angle) || !std::isfinite(sample.ZoomX) || !std::isfinite(sample.ZoomY) || !std::isfinite(sample.ShearX) || !std::isfinite(sample.ShearY) || sample.Opacity > 255 || sample.UnresolvedMask != 0 || (sample.Deformation.has_value() && mesh == nullptr)) {
			throw std::runtime_error("motion layer composition requires finite resolved ordinary local attributes without deformation");
		}

		if (mesh != nullptr) {
			const auto& patch = mesh->OwnPatch();
			if (sample.Deformation.has_value() != patch.has_value()) {
				throw std::runtime_error("motion mesh composition requires its actual local retained patch");
			}
			if (patch) {
				for (std::size_t index = 0; index < patch->Controls.size(); ++index) {
					const auto& actual = sample.Deformation->Controls[index];
					const auto& retained = patch->Controls[index];
					if (!std::isfinite(actual.X) || !std::isfinite(actual.Y) || std::bit_cast<std::uint32_t>(actual.X) != std::bit_cast<std::uint32_t>(retained.X) || std::bit_cast<std::uint32_t>(actual.Y) != std::bit_cast<std::uint32_t>(retained.Y)) {
						throw std::runtime_error("motion mesh numeric controls differ from their genuine local owner");
					}
				}
			}
		}
	}

	static void SynchronizeChildCoordinates(MotionLayerSample& local, const PreparedMotionLayer& parent, const PreparedMotionFrame& current, const MotionLayerMeshLocalOwner& mesh, const MotionStaticSourceState& source) {
		const auto& lattice = mesh.CurrentLattice();
		if (lattice.EncodedDegree == 0 || !source.Ready) {
			return;
		}

		float width = 0;
		float height = 0;
		resource::MotionPoint origin;
		if (source.External) {
			if (!source.External->Installed || source.External->Width <= 0 || source.External->Height <= 0) {
				throw std::runtime_error("motion child coordinate synchronization requires its live external source descriptor");
			}
			width = static_cast<float>(source.External->Width);
			height = static_cast<float>(source.External->Height);
			origin = source.External->Origin;
		} else if (source.Blank) {
			if (source.Blank->Width <= 0 || source.Blank->Height <= 0 || source.Binding || source.Texture) {
				throw std::runtime_error("motion child coordinate synchronization requires its actual positive blank descriptor");
			}
			width = static_cast<float>(source.Blank->Width);
			height = static_cast<float>(source.Blank->Height);
			origin.X = source.Blank->OriginX;
			origin.Y = source.Blank->OriginY;
		} else {
			if (!source.Binding || !source.Texture || source.Binding->Source == nullptr || source.Binding->Icon == nullptr) {
				throw std::runtime_error("motion child coordinate synchronization requires its actual registered source rectangle");
			}
			const auto& icon = *source.Binding->Icon;
			if (!std::isfinite(icon.Width) || !std::isfinite(icon.Height) || icon.Width < 1 || icon.Height < 1 || icon.Width >= 2147483648.0f || icon.Height >= 2147483648.0f) {
				throw std::runtime_error("motion child coordinate synchronization requires bounded positive signed source dimensions");
			}
			width = static_cast<float>(static_cast<std::int32_t>(icon.Width));
			height = static_cast<float>(static_cast<std::int32_t>(icon.Height));
			origin.X = icon.OriginX;
			origin.Y = icon.OriginY;
		}

		// Native50cc1d/50f240 maps the child's overridden local coordinates through the parent's own patch before ordinary inheritance.
		const float origin_x = current.Attributes.OriginX + origin.X;
		const float origin_y = current.Attributes.OriginY + origin.Y;
		const std::size_t mapped_axis = parent.CoordinatePlane == 0 ? 1 : 2;
		resource::MotionPoint normalized;
		normalized.X = (origin_x + local.Coordinate[0]) / width;
		normalized.Y = (origin_y + local.Coordinate[mapped_axis]) / height;
		const auto mapped = EvaluateMotionBezierLattice(lattice, normalized);
		local.Coordinate[0] = width * mapped.X - origin_x;
		local.Coordinate[mapped_axis] = height * mapped.Y - origin_y;
		if (!std::isfinite(local.Coordinate[0]) || !std::isfinite(local.Coordinate[mapped_axis])) {
			throw std::runtime_error("motion child coordinate synchronization exceeds finite local coordinates");
		}
	}

	static void RequireOverrides(const MotionLayerOverrides& overrides) {
		for (const auto component : overrides.Coordinate) {
			if (!std::isfinite(component)) {
				throw std::runtime_error("motion layer override coordinates must be finite");
			}
		}

		if (!std::isfinite(overrides.Angle) || !std::isfinite(overrides.ZoomX) || !std::isfinite(overrides.ZoomY) || !std::isfinite(overrides.ShearX) || !std::isfinite(overrides.ShearY) || overrides.Opacity > 255) {
			throw std::runtime_error("motion layer overrides require finite transforms and byte opacity");
		}
	}

	static MotionLayerSample ApplyOverrides(const MotionLayerSample& sample, const MotionLayerOverrides& overrides, const MotionLayerMeshLocalOwner* mesh) {
		auto local = sample;
		local.FlipX = local.FlipX != overrides.FlipX;
		local.FlipY = local.FlipY != overrides.FlipY;
		local.Angle += overrides.Angle;
		local.ZoomX *= overrides.ZoomX;
		local.ZoomY *= overrides.ZoomY;
		local.ShearX += overrides.ShearX;
		local.ShearY += overrides.ShearY;
		local.Opacity = local.Opacity * overrides.Opacity / 255;
		for (std::size_t component = 0; component < local.Coordinate.size(); ++component) {
			local.Coordinate[component] += overrides.Coordinate[component];
		}

		RequireFiniteComposition(local, mesh);

		return local;
	}

	MotionLayerComposition::MotionLayerComposition(const PreparedMotionTimeline& timeline, MotionRootScene& root, bool normalize_against_root)
	    : MotionLayerComposition(timeline, root, normalize_against_root, nullptr) {
	}

	MotionLayerComposition::MotionLayerComposition(MotionSelectedLayerNodes& selected, MotionRootScene& root, bool normalize_against_root)
	    : MotionLayerComposition(selected.m_Preparation, root, normalize_against_root, &selected) {
	}

	MotionLayerComposition::MotionLayerComposition(const PreparedMotionTimeline& timeline, MotionRootScene& root, bool normalize_against_root, MotionSelectedLayerNodes* selected)
	    : m_Timeline(timeline), m_Root(root), m_SelectedOwner(selected), m_DeformationOwner(selected == nullptr ? nullptr : selected->m_DeformationOwner), m_NormalizeAgainstRoot(normalize_against_root) {
		RequireSelectedOwner();
		if (m_SelectedOwner != nullptr && m_SelectedOwner->m_EnclosingChild != nullptr && m_NormalizeAgainstRoot != !m_SelectedOwner->m_EnclosingChild->IndependentLayerInherit()) {
			throw std::runtime_error("motion child numeric normalization requires its actual constructor inheritance policy");
		}
		if (timeline.Layers.size() > 1000000 || timeline.Parents.size() != timeline.Layers.size()) {
			throw std::runtime_error("motion layer composition hierarchy count or parents exceed their prepared bound");
		}

		for (std::size_t index = 0; index < timeline.Layers.size(); ++index) {
			const auto& layer = timeline.Layers[index];
			const bool selected_mesh = m_SelectedOwner != nullptr && layer.Type == 0 && layer.MeshTransform == 1 && !layer.MeshCombinator && (layer.MeshSyncChildMask & ~std::uint32_t{9}) == 0;
			const bool selected_ancestry = m_SelectedOwner != nullptr && m_SelectedOwner->m_DeformationOwner != nullptr;
			const auto supported_inheritance = kSupportedCompositionInheritance | (selected_mesh || selected_ancestry ? kRootMeshInheritance : 0) | (CanResolveRootMeshInheritance(layer, timeline.Parents[index].has_value(), root.MatrixMode()) ? kRootMeshInheritance : 0);
			if ((layer.Type != 0 && layer.Type != 2 && !(m_SelectedOwner != nullptr && (layer.Type == 1 || layer.Type == 3 || layer.Type == 4 || layer.Type == 7 || layer.Type == 12))) || (layer.Type == 2 && layer.StencilType != 0) || (layer.MeshTransform != 0 && !selected_mesh) || layer.CoordinatePlane > 1 || (layer.InheritMask & ~supported_inheritance) != 0 || (timeline.Parents[index].has_value() && *timeline.Parents[index] >= index)) {
				throw std::runtime_error("motion layer composition requires ordinary sprite/hierarchy layers without hierarchy stencil, recovered inheritance and parents preceding children");
			}

			auto ancestor = timeline.Parents[index];
			std::size_t depth = 0;
			while (ancestor.has_value()) {
				if (++depth > kMaximumCompositionAncestorDepth) {
					throw std::runtime_error("motion layer composition ancestor depth exceeds its bounded preparation");
				}

				ancestor = timeline.Parents[*ancestor];
			}
		}

		m_Overrides.resize(timeline.Layers.size());
		m_Layers.resize(timeline.Layers.size());
		for (auto& layer : m_Layers) {
			// Native50dca0 initializes evaluated +355/+356 before the first numeric phase.
			layer.Active = true;
			layer.Visible = true;
		}
		m_OverrideDirty.resize(timeline.Layers.size(), true);
		m_Changes.resize(timeline.Layers.size());
		if (selected != nullptr) {
			if (selected->m_CompositionOwner != nullptr) {
				throw std::runtime_error("motion selected numeric composition requires its exact exclusive owner");
			}
			selected->m_CompositionOwner = this;
		}
	}

	MotionLayerComposition::~MotionLayerComposition() noexcept {
		if (m_SelectedOwner != nullptr && m_SelectedOwner->m_CompositionOwner == this) {
			m_SelectedOwner->m_CompositionOwner = nullptr;
		}
	}

	const MotionLocalFrames& MotionLayerComposition::LocalFrames() const {
		RequireSelectedOwner();
		if (!m_ConditionalTrace || m_LocalFrames == nullptr || m_ComposedSelectionSerial != m_LocalFrames->m_SelectionSerial || Layers().size() != m_Layers.size()) {
			throw std::runtime_error("motion numeric local borrow requires its actual completed current conditional composition");
		}
		static_cast<void>(m_LocalFrames->ParameterBindings());

		return *m_LocalFrames;
	}

	void MotionLayerComposition::RequireSelectedOwner(bool require_current_parameters) const {
		if (require_current_parameters && m_LocalFrames != nullptr && m_ConditionalTrace) {
			m_LocalFrames->RequireCurrentParameters();
		}
		if (m_SelectedOwner != nullptr && (!m_SelectedOwner->m_Published || m_SelectedOwner->m_Mutating || (m_DeformationOwner != nullptr && m_SelectedOwner->m_DeformationOwner != m_DeformationOwner) || &m_SelectedOwner->m_Preparation != &m_Timeline || &m_SelectedOwner->m_Root != &m_Root || m_SelectedOwner->Nodes().size() != m_Timeline.Layers.size())) {
			throw std::runtime_error("motion numeric composition requires its actual live selected preparation and containing root");
		}
	}

	const MotionLayerMeshLocalOwner* MotionLayerComposition::RequireMeshOwner(std::size_t layer) const {
		if (m_Timeline.Layers[layer].MeshTransform == 0) {
			return nullptr;
		}

		if (m_SelectedOwner == nullptr || m_LocalFrames == nullptr || m_LocalFrames->m_SelectedOwner != m_SelectedOwner) {
			throw std::runtime_error("motion mesh numeric composition requires its actual exclusive selected local owner");
		}
		const auto& mesh = m_SelectedOwner->MeshLocal(static_cast<std::uint32_t>(layer));
		if (mesh.EvaluatedFrameIndex() != m_LocalFrames->State(static_cast<std::uint32_t>(layer)).CurrentIndex || mesh.EvaluatedPosition() != m_LocalFrames->Position(static_cast<std::uint32_t>(layer))) {
			throw std::runtime_error("motion mesh numeric composition requires its actual current local evaluation");
		}

		return &mesh;
	}

	void MotionLayerComposition::SetOverrides(std::uint32_t layer, const MotionLayerOverrides& overrides) {
		RequireSelectedOwner();
		if (layer >= m_Overrides.size()) {
			throw std::runtime_error("motion layer override index exceeds selected preparation");
		}

		RequireOverrides(overrides);
		m_Overrides[layer] = overrides;
		m_OverrideDirty[layer] = true;
	}

	const MotionLayerOverrides& MotionLayerComposition::Overrides(std::uint32_t layer) const {
		RequireSelectedOwner();
		if (layer >= m_Overrides.size()) {
			throw std::runtime_error("motion layer override index exceeds selected preparation");
		}

		return m_Overrides[layer];
	}

	std::optional<std::uint32_t> MotionLayerComposition::EffectiveParent(std::size_t layer) const {
		auto parent = m_Timeline.Parents[layer];
		while (parent.has_value() && (m_Timeline.Layers[*parent].InheritMask & kSkipCompositionAncestor) != 0) {
			parent = m_Timeline.Parents[*parent];
		}

		return parent;
	}

	void MotionLayerComposition::Compose(std::span<const MotionLayerSample> local_samples, bool tick_assigned) {
		m_ConditionalTrace = false;
		RequireSelectedOwner();
		if (m_SelectedOwner != nullptr) {
			throw std::runtime_error("motion selected numeric composition requires its actual local frame owner");
		}
		if (local_samples.size() != m_Layers.size()) {
			throw std::runtime_error("motion layer composition requires every actual prepared local sample");
		}

		for (const auto& sample : local_samples) {
			RequireFiniteComposition(sample);
		}

		// Failed composition cannot publish a mixture of the previous and partially updated frame.
		m_Composed = false;
		m_Root.Compose(tick_assigned);
		for (std::size_t index = 0; index < m_Layers.size(); ++index) {
			ComposeLayer(index, local_samples[index], tick_assigned);
			m_OverrideDirty[index] = false;
		}

		m_Composed = true;
	}

	void MotionLayerComposition::ComposeLayer(std::size_t index, const MotionLayerSample& sample, bool tick_assigned) {
		auto& output = m_Layers[index];
		const auto& root = m_Root.Frame().Hierarchy;
		const auto previous = output.Hierarchy.Attributes.Coordinate;
		output.EffectiveParent = EffectiveParent(index);
		const auto& parent = output.EffectiveParent.has_value() ? m_Layers[*output.EffectiveParent].Hierarchy : root;
		const auto parent_visible = output.EffectiveParent.has_value() ? m_Layers[*output.EffectiveParent].Visible : !root.Attributes.Hidden;
		const auto& overrides = m_Overrides[index];
		auto local = sample.Hidden ? sample : ApplyOverrides(sample, overrides, RequireMeshOwner(index));
		if (!sample.Hidden && output.EffectiveParent && m_Layers[*output.EffectiveParent].Active) {
			const auto parent_index = *output.EffectiveParent;
			const auto& prepared_parent = m_Timeline.Layers[parent_index];
			if (prepared_parent.MeshTransform == 1 && (prepared_parent.MeshSyncChildMask & 1) != 0) {
				const auto* mesh = RequireMeshOwner(parent_index);
				if (m_SelectedOwner->m_SourceOwner == nullptr || (prepared_parent.MeshSyncChildMask & ~std::uint32_t{9}) != 0) {
					throw std::runtime_error("motion child coordinate synchronization requires its actual source owner and recovered coordinate-only flags");
				}
				const auto current = m_LocalFrames->State(parent_index).CurrentIndex;
				SynchronizeChildCoordinates(local, prepared_parent, prepared_parent.Frames.at(current), *mesh, m_SelectedOwner->m_SourceOwner->State(parent_index));
			}
		}
		output.Hierarchy = ComposeMotionHierarchyLayer(m_Timeline.Layers[index], local, parent, root, m_NormalizeAgainstRoot);
		output.Hierarchy.UnresolvedInheritMask &= ~kSkipCompositionAncestor;
		if (CanResolveRootMeshInheritance(m_Timeline.Layers[index], m_Timeline.Parents[index].has_value(), m_Root.MatrixMode())) {
			// The mask remains in the preparation; only this known inactive output field is resolved.
			output.Hierarchy.UnresolvedInheritMask &= ~kRootMeshInheritance;
		}
		output.FrameHidden = sample.Hidden;
		output.Visible = overrides.Visible && parent_visible;
		const auto immediate = m_Timeline.Parents[index];
		if (!sample.Hidden && m_SelectedOwner != nullptr && immediate.has_value() && m_Timeline.Layers[*immediate].Type == 12) {
			const auto& mask = m_SelectedOwner->StencilMask(*immediate);
			if (m_LocalFrames == nullptr || mask.EvaluatedPosition() != m_LocalFrames->Position(*immediate) || mask.EvaluatedFrameIndex() != m_LocalFrames->State(*immediate).CurrentIndex) {
				throw std::runtime_error("motion numeric visibility requires its actual current raw-parent stencil summary");
			}
			const auto& summary = mask.Summary();
			const float extent = summary.Scale + summary.Offset;
			output.Visible = output.Visible && (!summary.Enabled || !(std::numeric_limits<float>::epsilon() > extent));
		}
		output.Active = !sample.Hidden && overrides.Enabled && output.Visible && (output.Hierarchy.ModulationColor & 255) != 0;
		output.Hierarchy.Attributes.Hidden = !output.Active;
		for (std::size_t component = 0; component < output.Displacement.size(); ++component) {
			output.Displacement[component] = tick_assigned ? 0 : output.Hierarchy.Attributes.Coordinate[component] - previous[component];
		}
	}

	void MotionLayerComposition::Compose(MotionLocalFrames& local_frames, bool tick_assigned, bool force_all) {
		m_ConditionalTrace = false;
		m_Composed = false;
		RequireSelectedOwner();
		if (m_SelectedOwner != local_frames.m_SelectedOwner) {
			throw std::runtime_error("motion conditional numeric composition requires its exact selected local owner");
		}
		if (m_CompositionSerial == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("motion numeric composition serial exhausted its owner lifetime");
		}
		m_LocalFrames = &local_frames;
		if (&local_frames.Preparation() != &m_Timeline) {
			throw std::runtime_error("motion conditional composition requires its actual selected preparation owner");
		}

		local_frames.PrepareVariableNumerics();
		const auto root_changed = m_Root.Overrides().Dirty;
		m_Root.Compose(tick_assigned);
		std::fill(m_Changes.begin(), m_Changes.end(), false);
		for (std::size_t index = 0; index < m_Layers.size(); ++index) {
			const auto effective = EffectiveParent(index);
			const auto immediate = m_Timeline.Parents[index];
			const auto parent_changed = effective.has_value() ? m_Changes[*effective] : root_changed;
			const auto immediate_changed = immediate.has_value() ? m_Changes[*immediate] : root_changed;
			const auto force = force_all || m_OverrideDirty[index] || parent_changed || immediate_changed;
			const auto previous = m_Layers[index].Hierarchy.Attributes.Coordinate;
			const auto layer = static_cast<std::uint32_t>(index);
			if (local_frames.Evaluate(layer, force)) {
				const auto& sample = *local_frames.Evaluation(layer).Sample;
				RequireFiniteComposition(sample, RequireMeshOwner(index));
				ComposeLayer(index, sample, tick_assigned);
				m_OverrideDirty[index] = false;
				m_Changes[index] = !sample.Hidden || parent_changed || local_frames.State(layer).FrameDirty;
			}

			if (m_SelectedOwner != nullptr && m_SelectedOwner->m_ExternalSources[index].Dirty) {
				m_Changes[index] = true;
			}

			for (std::size_t component = 0; component < m_Layers[index].Displacement.size(); ++component) {
				m_Layers[index].Displacement[component] = tick_assigned ? 0 : m_Layers[index].Hierarchy.Attributes.Coordinate[component] - previous[component];
			}
		}

		if (m_SelectedOwner != nullptr && m_SelectedOwner->m_ContainingSelected != nullptr) {
			const auto& containing = *m_SelectedOwner->m_ContainingSelected;
			const auto* parent = containing.m_LocalNumericOwner;
			const auto* composition = containing.m_CompositionOwner;
			if (parent == nullptr || composition == nullptr || &composition->LocalFrames() != parent) {
				throw std::runtime_error("motion child numeric composition requires its exact current containing numeric phase");
			}
			// Native524910 still composes when51ffd0 retains stopped/sync-waiting cursors. Tie this completed work to the current parent.
			local_frames.m_ContainingPreparationSerial = parent->m_SelectionSerial;
		}
		m_ComposedSelectionSerial = local_frames.m_SelectionSerial;
		++m_CompositionSerial;
		m_Composed = true;
		m_ConditionalTrace = true;
	}

	std::span<const MotionComposedLayer> MotionLayerComposition::Layers() const noexcept {
		if (!m_Composed || (m_SelectedOwner != nullptr && (!m_SelectedOwner->m_Published || m_SelectedOwner->m_Mutating || (m_DeformationOwner != nullptr && m_SelectedOwner->m_DeformationOwner != m_DeformationOwner) || (m_ConditionalTrace && m_SelectedOwner->m_LocalNumericOwner != m_LocalFrames))) || (m_ConditionalTrace && (m_LocalFrames == nullptr || !m_LocalFrames->ParameterValuesCurrent() || !m_LocalFrames->m_Selected || m_LocalFrames->m_Traversing || m_ComposedSelectionSerial != m_LocalFrames->m_SelectionSerial))) {
			return {};
		}

		return m_Layers;
	}

	std::array<float, 3> MotionLayerComposition::RetainedCoordinate(std::uint32_t layer) const {
		RequireSelectedOwner(false);
		if (layer >= m_Layers.size() || (m_SelectedOwner != nullptr && m_SelectedOwner->m_CompositionOwner != this)) {
			throw std::runtime_error("motion retained numeric coordinate requires its exact live owner and selected layer");
		}
		return m_Layers[layer].Hierarchy.Attributes.Coordinate;
	}

	MotionLayerVisibility MotionLayerComposition::RetainedVisibility(std::uint32_t layer) const {
		RequireSelectedOwner(false);
		if (layer >= m_Layers.size() || (m_SelectedOwner != nullptr && m_SelectedOwner->m_CompositionOwner != this)) {
			throw std::runtime_error("motion retained visibility requires its exact live owner and selected layer");
		}
		return {m_Layers[layer].Active, m_Layers[layer].Visible};
	}

	bool MotionLayerComposition::Changed(std::uint32_t layer) const {
		RequireSelectedOwner();
		if (!m_Composed || !m_ConditionalTrace || (m_SelectedOwner != nullptr && m_SelectedOwner->m_LocalNumericOwner != m_LocalFrames) || m_LocalFrames == nullptr || !m_LocalFrames->ParameterValuesCurrent() || !m_LocalFrames->m_Selected || m_LocalFrames->m_Traversing || m_ComposedSelectionSerial != m_LocalFrames->m_SelectionSerial || layer >= m_Changes.size()) {
			throw std::runtime_error("motion geometry changed trace requires successful actual conditional composition");
		}

		return m_Changes[layer];
	}

} // namespace sgre::scene
