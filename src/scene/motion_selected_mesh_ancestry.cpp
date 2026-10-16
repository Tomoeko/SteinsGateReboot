#include "sgre/scene/motion_selected_mesh_ancestry.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"
#include "sgre/scene/motion_child_root.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_grid_draw.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_layer_parameters.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_particle_layer_state.hpp"
#include "sgre/scene/motion_selected_layer_sources.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumSelectedDeformationLayers = 65536;

	MotionSelectedMeshAncestry::MotionSelectedMeshAncestry(MotionSelectedLayerNodes& selected, MotionSelectedLayerSources& sources, const MotionManagerRuntime& manager, const MotionSelectedMeshAncestry* containing)
	    : MotionSelectedMeshAncestry(selected, sources, manager, containing, false) {
	}

	bool MotionSelectedMeshAncestry::RequireClone(const MotionSelectedLayerNodes& selected, const MotionSelectedMeshAncestry& source, const MotionSelectedMeshAncestry* containing) {
		source.RequireOwners();
		if (&selected == &source.m_Selected || &selected.m_Timeline != &source.m_Selected.m_Timeline || &selected.m_Preparation != &source.m_Selected.m_Preparation || (containing == nullptr) != (source.m_Containing == nullptr)) {
			throw std::runtime_error("motion copied ancestry requires independent actual selected owners and identical immutable frames");
		}

		return containing != nullptr;
	}

	MotionSelectedMeshAncestry::MotionSelectedMeshAncestry(MotionSelectedLayerNodes& selected, MotionSelectedLayerSources& sources, const MotionManagerRuntime& manager, const MotionSelectedMeshAncestry* containing, const MotionSelectedMeshAncestry& source)
	    : MotionSelectedMeshAncestry(selected, sources, manager, containing, RequireClone(selected, source, containing)) {
	}

	MotionSelectedMeshAncestry::MotionSelectedMeshAncestry(MotionSelectedLayerNodes& selected, MotionSelectedLayerSources& sources, const MotionManagerRuntime& manager, const MotionSelectedMeshAncestry* containing, bool copied_root_pending)
	    : m_Selected(selected), m_Sources(sources), m_Manager(manager), m_Containing(containing), m_ClonedRootTransferPending(copied_root_pending) {
		RequireOwners();
		const auto nodes = selected.Nodes();
		if (selected.m_DeformationOwner != nullptr || nodes.size() > kMaximumSelectedDeformationLayers) {
			throw std::runtime_error("motion mixed mesh ancestry requires its exclusive bounded actual selected tree");
		}
		m_States.resize(nodes.size());
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& node = nodes[index];
			const auto& prepared = *node.Prepared;
			if (node.Prepared != &selected.m_Preparation.Layers[index] || node.Parent != selected.m_Preparation.Parents[index] || (node.Parent && *node.Parent >= index) || (prepared.Type != 0 && prepared.Type != 1 && prepared.Type != 2 && prepared.Type != 3 && prepared.Type != 4 && prepared.Type != 7 && prepared.Type != 12) || prepared.MeshTransform > 1 || prepared.MeshCombinator || (prepared.MeshSyncChildMask & ~(prepared.Type == 0 && prepared.MeshTransform == 1 ? std::uint32_t{9} : std::uint32_t{8})) != 0 || (prepared.MeshTransform == 1 && prepared.Type != 0)) {
				throw std::runtime_error("motion mixed ancestry requires every actual supported raw/prepared kind and mesh owner");
			}
			auto& state = m_States[index];
			state.m_Owner = this;
			state.m_Node = &node;
			state.m_Child = prepared.Type == 3 ? &selected.Child(static_cast<std::uint32_t>(index)) : nullptr;
			state.m_Parent = node.Parent ? &m_States[*node.Parent] : nullptr;
		}
		selected.m_DeformationOwner = this;
	}

	MotionSelectedMeshAncestry::~MotionSelectedMeshAncestry() noexcept {
		if (m_Selected.m_Published && !m_Selected.m_Mutating) {
			for (const auto& state : m_States) {
				if (state.m_Child != nullptr && state.m_Child->m_DeformationTransferOwner == this) {
					state.m_Child->m_DeformationTransferOwner = nullptr;
					state.m_Child->m_InheritedDeformation = nullptr;
				}
			}
		}
		if (m_Selected.m_DeformationOwner == this) {
			m_Selected.m_DeformationOwner = nullptr;
		}
	}

	void MotionSelectedMeshAncestry::RequireOwners() const {
		const auto& selection = m_Selected.m_Commands.SelectedProject();
		if (!selection || !m_Selected.m_Published || m_Selected.m_Mutating || (m_Selected.m_DeformationOwner != nullptr && m_Selected.m_DeformationOwner != this) || m_Selected.m_SourceOwner != &m_Sources || &m_Manager.Manager() != &m_Selected.m_Commands.Manager() || m_Sources.RequestingProject() != selection->Project) {
			throw std::runtime_error("motion mixed ancestry requires its exact live selected registered owners");
		}
		if (m_Containing == nullptr) {
			if (m_Selected.m_EnclosingChild != nullptr || m_Selected.m_EnclosingParticle != nullptr || m_Selected.m_Root.MatrixMode() != MotionRootMatrixMode::Recomputed) {
				throw std::runtime_error("motion mixed ancestry root requires its actual containing child producer");
			}
		} else if (m_ClonedRootTransferPending) {
			m_Containing->RequireOwners();
			if (m_Selected.m_EnclosingParticle != nullptr) {
				const auto& particle = *m_Selected.m_EnclosingParticle;
				if (m_Selected.m_Root.MatrixMode() != MotionRootMatrixMode::Recomputed || particle.m_DeformationTransferOwner != m_Containing || !m_Containing->m_Selected.OwnsParticleSelection(particle.m_DeformationTransferLayer, m_Selected)) {
					throw std::runtime_error("motion dormant copied particle root requires its actual independent containing owner");
				}
			} else {
				const auto* child = m_Selected.m_EnclosingChild;
				if (child == nullptr || m_Selected.m_Root.MatrixMode() != MotionRootMatrixMode::SuppliedByParent || child->m_DeformationTransferOwner != m_Containing || m_Containing->m_Selected.ChildNodes(child->m_DeformationTransferLayer) != &m_Selected) {
					throw std::runtime_error("motion dormant copied child root requires its actual independent containing owner");
				}
			}
		} else if (m_Selected.m_EnclosingParticle != nullptr) {
			m_Containing->RequireParticleBorrow(*m_Selected.m_EnclosingParticle, m_Selected);
		} else {
			const auto* child = m_Selected.m_EnclosingChild;
			if (child == nullptr || child->m_DeformationTransferOwner != m_Containing || m_Selected.m_Root.MatrixMode() != MotionRootMatrixMode::SuppliedByParent || m_Containing->m_Selected.ChildNodes(child->m_DeformationTransferLayer) != &m_Selected) {
				throw std::runtime_error("motion mixed ancestry requires this exact selected containing child transfer");
			}
			m_Containing->RequireChildBorrow(*child);
		}
	}

	bool MotionSelectedMeshAncestry::CombinedChainReaches(const MotionSelectedDeformationState& state, const MotionSelectedDeformationState& ancestor) const {
		const auto* current = &state;
		while (current->m_Node->Prepared->MeshCombine && current->m_Parent != nullptr) {
			current = current->m_Parent;
			if (current == &ancestor) {
				return true;
			}
		}

		return false;
	}

	void MotionSelectedMeshAncestry::Compose(MotionLayerComposition& composition) {
		m_Composed = false;
		// Parent521BF0 has replaced every dormant root link before this child numeric phase.
		m_ClonedRootTransferPending = false;
		RequireOwners();
		const auto* local = m_Selected.m_LocalNumericOwner;
		const auto numeric = composition.Layers();
		if (local == nullptr || composition.m_SelectedOwner != &m_Selected || composition.m_LocalFrames != local || !composition.m_ConditionalTrace || composition.m_ComposedSelectionSerial != local->m_SelectionSerial || numeric.size() != m_States.size()) {
			throw std::runtime_error("motion mixed ancestry requires its actual completed current numeric/local phase");
		}
		m_RootAncestor = nullptr;
		if (m_Selected.m_EnclosingChild != nullptr) {
			m_RootAncestor = m_Selected.m_EnclosingChild->InheritedDeformation();
		} else if (m_Selected.m_EnclosingParticle != nullptr) {
			m_RootAncestor = m_Selected.m_EnclosingParticle->m_InheritedDeformation;
		}
		const auto projection = m_Manager.DepthProjection();
		if (!std::isfinite(projection)) {
			throw std::runtime_error("motion mixed ancestry requires its finite actual manager projection");
		}

		for (std::size_t index = 0; index < m_States.size(); ++index) {
			auto& state = m_States[index];
			const auto layer = static_cast<std::uint32_t>(index);
			const auto current = local->State(layer).CurrentIndex;
			const auto& prepared = *state.m_Node->Prepared;
			if (current >= prepared.Frames.size() || numeric[index].FrameHidden != prepared.Frames[current].Hidden) {
				throw std::runtime_error("motion mixed ancestry raw current frame differs from actual numeric selection");
			}
			const auto* parent = state.m_Parent;
			const auto* ancestor = m_RootAncestor;
			if (parent != nullptr) {
				ancestor = parent->m_Export || parent->m_Stop ? parent : parent->m_Ancestor;
			}
			if (ancestor != nullptr && ancestor->m_Owner != this) {
				static_cast<void>(ancestor->Node());
			}
			state.m_Changed = ancestor != state.m_Ancestor || composition.Changed(layer) || (ancestor != nullptr && ancestor->m_Changed);
			state.m_Ancestor = ancestor;
			state.m_Stop = (prepared.InheritMask & 0x02000000u) == 0 && ancestor != nullptr;
			state.m_Active = state.m_Stop && CombinedChainReaches(state, *ancestor);
			state.m_Export = false;
			if (state.m_Changed) {
				state.m_Anchor.X = numeric[index].Hierarchy.Attributes.Coordinate[0];
				state.m_Anchor.Y = numeric[index].Hierarchy.Attributes.Coordinate[1];
				if (prepared.MeshTransform == 1) {
					const auto& mesh = m_Selected.MeshLocal(layer);
					if (mesh.OverrideLatched()) {
						const auto origin = mesh.PixelOrigin();
						const auto& transform = numeric[index].Hierarchy.Transform;
						const auto x_from_y = origin.Y * transform.XY;
						const auto x_from_x = origin.X * transform.XX;
						const auto y_from_y = origin.Y * transform.YY;
						const auto y_from_x = origin.X * transform.YX;
						state.m_Anchor.X = (x_from_y + x_from_x) + state.m_Anchor.X;
						state.m_Anchor.Y = (y_from_y + y_from_x) + state.m_Anchor.Y;
					}
				}
			}

			const MotionStaticSourceState* source = nullptr;
			if (prepared.Type == 0 || prepared.Type == 12) {
				source = &m_Sources.State(layer);
			}
			state.m_OwnLattice.EncodedDegree = 0;
			state.m_OwnLattice.Controls.clear();
			if (prepared.MeshTransform == 1) {
				const auto& mesh = m_Selected.MeshLocal(layer);
				if (mesh.EvaluatedFrameIndex() != current || mesh.EvaluatedPosition() != local->Position(static_cast<std::uint32_t>(index))) {
					throw std::runtime_error("motion mixed mesh patch differs from its actual local/current owner");
				}
				state.m_OwnLattice = mesh.CurrentLattice();
				if (!prepared.Frames[current].Hidden && state.m_OwnLattice.EncodedDegree != 0 && source != nullptr && source->Ready) {
					state.m_Active = true;
					state.m_Export = (prepared.MeshSyncChildMask & 8) != 0;
				}
			}
			if (state.m_Changed && !prepared.Frames[current].Hidden && source != nullptr && source->Ready) {
				MotionDeformationSourceGeometry geometry;
				if (source->External) {
					const auto& external = *source->External;
					if (!external.Installed || external.Reference == nullptr || source->Binding || source->Texture || source->Blank || m_Selected.m_Commands.Manager().ExternalTextures().Get(external.Slot).External != external.Reference) {
						throw std::runtime_error("motion external geometry requires its exact live descriptor and shared table slot");
					}
					static_cast<void>(external.Reference->Read());
					geometry.External = external;
					MotionBlankSourceRectangle rectangle;
					rectangle.Width = external.Width;
					rectangle.Height = external.Height;
					rectangle.OriginX = external.Origin.X;
					rectangle.OriginY = external.Origin.Y;
					geometry.Quad = EvaluateMotionBlankQuad(numeric[index].Hierarchy, rectangle, projection);
					geometry.Width = static_cast<std::uint32_t>(external.Width);
					geometry.Height = static_cast<std::uint32_t>(external.Height);
					geometry.Origin.X = external.Origin.X + numeric[index].Hierarchy.Attributes.OriginX;
					geometry.Origin.Y = external.Origin.Y + numeric[index].Hierarchy.Attributes.OriginY;
					if (prepared.MeshTransform == 1) {
						geometry.Space = MotionBlankDeformationSpace(numeric[index].Hierarchy, rectangle, projection);
					}
				} else if (source->Blank) {
					geometry.Blank = *source->Blank;
					geometry.BlankSource = &m_Sources.Source(layer);
					if (geometry.Blank->Width <= 0 || geometry.Blank->Height <= 0 || source->Binding || source->Texture) {
						throw std::runtime_error("motion mixed blank geometry requires its actual positive descriptor without an atlas owner");
					}
					geometry.Quad = EvaluateMotionBlankQuad(numeric[index].Hierarchy, *geometry.Blank, projection);
					geometry.Width = static_cast<std::uint32_t>(geometry.Blank->Width);
					geometry.Height = static_cast<std::uint32_t>(geometry.Blank->Height);
					geometry.Origin.X = geometry.Blank->OriginX + numeric[index].Hierarchy.Attributes.OriginX;
					geometry.Origin.Y = geometry.Blank->OriginY + numeric[index].Hierarchy.Attributes.OriginY;
					if (prepared.MeshTransform == 1) {
						geometry.Space = MotionBlankDeformationSpace(numeric[index].Hierarchy, *geometry.Blank, projection);
					}
				} else {
					if (!source->Binding || !source->Texture || source->Binding->Source == nullptr || source->Binding->Icon == nullptr) {
						throw std::runtime_error("motion mixed source geometry requires its actual ordinary rectangle owner");
					}

					const bool retained_icon_mesh = !std::holds_alternative<std::monostate>(source->Binding->Icon->Mesh.Data);
					if (retained_icon_mesh && prepared.Type != 0 && prepared.Type != 12) {
						throw std::runtime_error("motion mixed icon mesh requires its unrecovered direct or polygon geometry owner");
					}

					// Native520d80 uses rectangle/grid geometry for ordinary and kind12 mask icons even with a retained mesh atom; polygon helpers are separate.
					geometry.Source = source->Binding->Source;
					geometry.Icon = source->Binding->Icon;
					geometry.Quad = EvaluateMotionSpriteQuad(numeric[index].Hierarchy, *geometry.Icon, projection);
					geometry.Width = static_cast<std::uint32_t>(geometry.Icon->Width);
					geometry.Height = static_cast<std::uint32_t>(geometry.Icon->Height);
					geometry.Origin.X = geometry.Icon->OriginX + numeric[index].Hierarchy.Attributes.OriginX;
					geometry.Origin.Y = geometry.Icon->OriginY + numeric[index].Hierarchy.Attributes.OriginY;
					if (prepared.MeshTransform == 1) {
						geometry.Space = MotionSpriteDeformationSpace(numeric[index].Hierarchy, *geometry.Icon, projection);
					}
				}
				state.m_Source = geometry;
			}
			state.m_CombinedLattice = state.m_OwnLattice;
			const auto* combined = &state;
			while (combined->m_Node->Prepared->MeshCombine && combined->m_Parent != nullptr) {
				combined = combined->m_Parent;
				if (combined->m_Active) {
					state.m_CombinedLattice = CombineMotionBezierLattices(state.m_CombinedLattice, combined->m_CombinedLattice);
					break;
				}
			}
			state.m_World.reset();
			if (state.m_Active) {
				if (state.m_CombinedLattice.EncodedDegree == 0 || !state.m_Source || !state.m_Source->Space) {
					throw std::runtime_error("motion active mixed deformation lacks its genuine retained patch/source space");
				}
				state.m_World = PrepareMotionDeformation(state.m_CombinedLattice, *state.m_Source->Space);
			}
		}
		m_Composition = &composition;
		m_CompositionSerial = composition.m_CompositionSerial;
		m_ContainingSerial = m_Containing == nullptr ? 0 : m_Containing->m_AncestrySerial;
		if (m_AncestrySerial == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("motion mixed ancestry preparation serial exhausted");
		}
		++m_AncestrySerial;
		m_Composed = true;
	}

	void MotionSelectedMeshAncestry::RequirePhase() const {
		RequireOwners();
		const auto* local = m_Selected.m_LocalNumericOwner;
		if (!m_Composed || m_Composition == nullptr || m_CompositionSerial != m_Composition->m_CompositionSerial || local == nullptr || m_Composition->m_LocalFrames != local || !local->m_Selected || local->m_Traversing || m_Composition->m_ComposedSelectionSerial != local->m_SelectionSerial || m_Composition->Layers().size() != m_States.size() || (m_Containing != nullptr && m_ContainingSerial != m_Containing->m_AncestrySerial)) {
			throw std::runtime_error("motion mixed deformation borrow requires its current actual completed ancestry phase");
		}
	}

	void MotionSelectedMeshAncestry::RequireLive(const MotionSelectedDeformationState& state) const {
		RequirePhase();
		if (state.m_Owner != this || state.m_Node == nullptr || state.m_Node->NativeOrdinal == 0) {
			throw std::runtime_error("motion mixed deformation borrow requires its actual selected ancestry cell");
		}

		const auto index = state.m_Node->NativeOrdinal - 1;
		if (index >= m_States.size() || &m_States[index] != &state || &m_Selected.Nodes()[index] != state.m_Node) {
			throw std::runtime_error("motion mixed deformation borrow differs from its actual selected cell");
		}
	}

	const MotionSelectedDeformationState& MotionSelectedMeshAncestry::State(std::uint32_t layer) const {
		if (layer >= m_States.size()) {
			throw std::runtime_error("motion mixed deformation index exceeds its actual selected cells");
		}
		RequireLive(m_States[layer]);

		return m_States[layer];
	}

	void MotionSelectedMeshAncestry::BindChildDeformation(std::uint32_t layer, MotionChildConstructionState& child) const {
		const auto& state = State(layer);
		if (state.m_Node->Prepared->Type != 3 || &m_Selected.Child(layer) != &child || child.Root().MatrixMode() != MotionRootMatrixMode::SuppliedByParent) {
			throw std::runtime_error("motion deformation transfer requires this exact actual selected kind3 child");
		}
		const auto& slots = m_Selected.ChildSlots(layer);
		const auto& frame = slots.CurrentFrame();
		const auto& names = child.Commands().Names();
		const auto* local = m_Selected.m_LocalNumericOwner;
		if (local == nullptr || slots.State().FrameIndices[slots.State().CurrentSlot] != local->State(layer).CurrentIndex || &frame != &state.m_Node->Prepared->Frames[local->State(layer).CurrentIndex]) {
			throw std::runtime_error("motion child deformation transfer requires its actual current selected control/command target");
		}
		const auto* selected_child = m_Selected.ChildNodes(layer);
		const bool empty_frame = frame.Hidden || frame.Attributes.Source.empty() || frame.Attributes.Source.front() == '\0';
		// Native521bf0 still transfers the real separate root after initial525880 lookup failure.
		const bool empty_child = (empty_frame || slots.HasInitialUnselectedTarget()) && (selected_child == nullptr || selected_child->Nodes().empty()) && names.CurrentMotion.empty() && names.RequestedMotion.empty();
		if (!empty_child && (empty_frame || !child.Commands().SelectedProject() || child.Commands().CachedTimeline() != child.Commands().SelectedProject()->Timeline || names.CurrentCharacter != frame.Attributes.Source.substr(0, frame.Attributes.Source.find('\0')) || (names.CurrentMotion != frame.Attributes.Icon.substr(0, frame.Attributes.Icon.find('\0')) && !slots.HasClonedSelectedTarget()))) {
			throw std::runtime_error("motion child deformation transfer requires its actual current selected control/command target");
		}
		const auto* parameters = local->ParameterBindings();
		const auto flags = parameters == nullptr ? 0 : parameters->ControlFlags(layer);
		// Native521bf0 transfers borrows on unchanged frames even after reset left only the genuine separate root.
		if (!empty_child && (m_Composition->Changed(layer) || flags != 0)) {
			BindMotionChildRoot(child.Root(), *state.m_Node->Prepared, frame, m_Composition->Layers()[layer], slots.CurrentControl(), std::bit_cast<std::int32_t>(child.RenderControls().ColorFilter));
		}
		child.m_DeformationTransferOwner = this;
		child.m_DeformationTransferLayer = layer;
		child.m_DeformationTransferSerial = m_AncestrySerial;
		child.m_InheritedDeformation = state.m_Stop ? &state : state.m_Ancestor;
	}

	void MotionSelectedMeshAncestry::BindParticleDeformation(std::uint32_t layer, MotionParticlePlayerState& child, const MotionSelectedLayerNodes& selected) const {
		const auto& state = State(layer);
		if (state.m_Node->Prepared->Type != 4 || child.Root().MatrixMode() != MotionRootMatrixMode::Recomputed || !m_Selected.OwnsParticleSelection(layer, selected) || selected.m_EnclosingParticle != &child) {
			throw std::runtime_error("motion particle deformation transfer requires its actual emitted selected owner");
		}
		child.m_DeformationTransferOwner = this;
		child.m_DeformationTransferLayer = layer;
		child.m_DeformationTransferSerial = m_AncestrySerial;
		child.m_InheritedDeformation = state.m_Stop ? &state : state.m_Ancestor;
	}

	void MotionSelectedMeshAncestry::RequireParticleBorrow(const MotionParticlePlayerState& child, const MotionSelectedLayerNodes& selected) const {
		const auto& state = State(child.m_DeformationTransferLayer);
		if (child.m_DeformationTransferOwner != this || child.m_DeformationTransferSerial != m_AncestrySerial || state.m_Node->Prepared->Type != 4 || !m_Selected.OwnsParticleSelection(child.m_DeformationTransferLayer, selected) || selected.m_EnclosingParticle != &child || child.m_InheritedDeformation != (state.m_Stop ? &state : state.m_Ancestor) || child.Root().MatrixMode() != MotionRootMatrixMode::Recomputed) {
			throw std::runtime_error("motion particle deformation borrow differs from its actual current transfer");
		}
	}

	void MotionSelectedMeshAncestry::RequireChildBorrow(const MotionChildConstructionState& child) const {
		const auto& state = State(child.m_DeformationTransferLayer);
		if (child.m_DeformationTransferOwner != this || child.m_DeformationTransferSerial != m_AncestrySerial || state.m_Node->Prepared->Type != 3 || &m_Selected.Child(child.m_DeformationTransferLayer) != &child || child.m_InheritedDeformation != (state.m_Stop ? &state : state.m_Ancestor)) {
			throw std::runtime_error("motion child deformation borrow differs from its exact current containing kind3 transfer");
		}
	}

	const MotionSelectedLayerNode& MotionSelectedDeformationState::Node() const {
		if (m_Owner == nullptr) {
			throw std::runtime_error("motion deformation state has no actual selected owner");
		}
		m_Owner->RequireLive(*this);

		return *m_Node;
	}

	const MotionSelectedDeformationState* MotionSelectedDeformationState::Ancestor() const {
		static_cast<void>(Node());

		return m_Ancestor;
	}

	bool MotionSelectedDeformationState::Active() const {
		static_cast<void>(Node());

		return m_Active;
	}

	bool MotionSelectedDeformationState::ExportsToChildren() const {
		static_cast<void>(Node());

		return m_Export;
	}

	bool MotionSelectedDeformationState::StopsInheritance() const {
		static_cast<void>(Node());

		return m_Stop;
	}

	bool MotionSelectedDeformationState::Changed() const {
		static_cast<void>(Node());

		return m_Changed;
	}

	resource::MotionPoint MotionSelectedDeformationState::Anchor() const {
		static_cast<void>(Node());

		return m_Anchor;
	}

	const MotionDeformationSourceGeometry* MotionSelectedDeformationState::SourceGeometry() const {
		static_cast<void>(Node());

		return m_Source ? &*m_Source : nullptr;
	}

	const PreparedMotionDeformation* MotionSelectedDeformationState::World() const {
		static_cast<void>(Node());

		return m_World ? &*m_World : nullptr;
	}

} // namespace sgre::scene
