#include "sgre/scene/motion_layer_stencil_ancestry.hpp"

#include "sgre/scene/motion_particle_player_state.hpp"

#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_selected_layer_sources.hpp"

#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace sgre::scene {
	static constexpr std::size_t kMaximumStencilAncestryLayers = 65536;
	static constexpr std::size_t kMaximumStencilAncestryDepth = 128;

	static std::string_view StencilSourceName(std::string_view text) {
		return text.substr(0, text.find('\0'));
	}

	static bool Kind12StencilActive(const MotionStencilMaskOwner& mask) {
		const auto& summary = mask.Summary();
		if (!summary.Enabled) {
			return true;
		}
		// Actual50d42c widens only Offset to double before subtraction; the final Scale+Offset addition stays float32.
		const auto difference = static_cast<double>(summary.Offset) - 1.0;
		if (difference > -static_cast<double>(std::numeric_limits<float>::epsilon())) {
			return false;
		}

		return !(std::numeric_limits<float>::epsilon() > summary.Offset + summary.Scale);
	}

	MotionLayerStencilAncestry::MotionLayerStencilAncestry(MotionSelectedLayerNodes& selected)
	    : m_Selected(selected), m_Prepared(selected.m_Preparation) {
		const auto nodes = selected.Nodes();
		if (nodes.size() > kMaximumStencilAncestryLayers || nodes.size() != m_Prepared.Layers.size() || nodes.size() != m_Prepared.Parents.size()) {
			throw std::runtime_error("motion stencil ancestry requires its exact bounded live selected tree");
		}
		m_States.resize(nodes.size());
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& node = nodes[index];
			if (node.Prepared != &m_Prepared.Layers[index] || node.Parent != m_Prepared.Parents[index] || (node.Parent && *node.Parent >= index) || (node.Prepared->Type != 0 && node.Prepared->Type != 1 && node.Prepared->Type != 2 && node.Prepared->Type != 3 && node.Prepared->Type != 4 && node.Prepared->Type != 7 && node.Prepared->Type != 12)) {
				throw std::runtime_error("motion stencil ancestry requires its actual supported nodes and preceding raw parents");
			}
			auto parent = node.Parent;
			std::size_t depth = 0;
			while (parent) {
				if (++depth > kMaximumStencilAncestryDepth) {
					throw std::runtime_error("motion stencil ancestry exceeds its selected depth bound");
				}
				parent = nodes[*parent].Parent;
			}
			auto& state = m_States[index];
			state.m_Owner = this;
			state.m_Node = &node;
			if (node.Prepared->Type == 12) {
				state.m_Mask = &selected.StencilMask(static_cast<std::uint32_t>(index));
			}
		}
	}

	void MotionLayerStencilAncestry::Compose(std::span<const MotionComposedLayer> layers, std::span<const std::size_t> current_indices, std::span<const MotionStaticSourceBinding* const> sources, float position, const MotionStencilLayerState* root_stencil) {
		m_Composed = false;
		const auto nodes = m_Selected.Nodes();
		if (nodes.size() != m_States.size() || layers.size() != nodes.size() || current_indices.size() != nodes.size() || sources.size() != nodes.size() || !std::isfinite(position)) {
			throw std::runtime_error("motion stencil ancestry requires every actual live numeric/current/source input");
		}
		if (root_stencil != nullptr) {
			static_cast<void>(root_stencil->Node());
		}
		const MotionStencilLayerState* transferred_root = nullptr;
		if (m_Selected.m_EnclosingChild != nullptr) {
			transferred_root = m_Selected.m_EnclosingChild->InheritedStencil();
		} else if (m_Selected.m_EnclosingParticle != nullptr) {
			transferred_root = m_Selected.m_EnclosingParticle->m_InheritedStencil;
		}
		if (root_stencil != transferred_root) {
			throw std::runtime_error("motion stencil root borrow differs from its exact containing child transfer");
		}

		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& node = nodes[index];
			const auto& prepared = *node.Prepared;
			if (current_indices[index] >= prepared.Frames.size() || layers[index].FrameHidden != prepared.Frames[current_indices[index]].Hidden) {
				throw std::runtime_error("motion stencil ancestry current slot differs from actual numeric frame selection");
			}
			auto& state = m_States[index];
			if (node.Parent) {
				const auto& parent = m_States[*node.Parent];
				state.m_Ancestor = parent.m_Active ? &parent : parent.m_Ancestor;
			} else {
				state.m_Ancestor = root_stencil;
			}
			if (prepared.Type == 12 && (state.m_Mask->EvaluatedFrameIndex() != current_indices[index] || state.m_Mask->EvaluatedPosition() != (m_Selected.m_LocalNumericOwner == nullptr ? position : m_Selected.m_LocalNumericOwner->Position(static_cast<std::uint32_t>(index))))) {
				throw std::runtime_error("motion stencil ancestry requires this exact current mask owner evaluation");
			}
			bool source_ready = true;
			if (prepared.Type == 0 || prepared.Type == 12) {
				if (sources[index] == nullptr) {
					throw std::runtime_error("motion stencil ancestry requires each actual drawing source binding");
				}
				const auto& source = sources[index]->State();
				const auto& frame = prepared.Frames[current_indices[index]];
				if (source.External) {
					const auto& external = *source.External;
					const auto& installed = m_Selected.m_ExternalSources[index].Binding;
					if (!installed || !installed->Installed || !external.Installed || external.Reference == nullptr || external.Table != &m_Selected.m_Commands.Manager().ExternalTextures() || external.Reference != installed->Reference || external.Table != installed->Table || external.Slot != installed->Slot || external.Left != installed->Left || external.Top != installed->Top || external.Width != installed->Width || external.Height != installed->Height || external.Origin.X != installed->Origin.X || external.Origin.Y != installed->Origin.Y || m_Selected.m_SourceOwner == nullptr || &m_Selected.m_SourceOwner->Source(static_cast<std::uint32_t>(index)) != sources[index]) {
						throw std::runtime_error("motion stencil ancestry requires this exact selected external source descriptor and live shared slot");
					}
					const auto shared = external.Table->Get(external.Slot);
					if (shared.External != external.Reference || !source.Ready || !source.Source.empty() || !source.Icon.empty() || source.Binding || source.Texture || source.Blank) {
						throw std::runtime_error("motion stencil ancestry external source differs from its genuine descriptor readiness and cleared authored fields");
					}
				} else if (!frame.Hidden && (source.Source != StencilSourceName(frame.Attributes.Source) || source.Icon != StencilSourceName(frame.Attributes.Icon))) {
					throw std::runtime_error("motion stencil ancestry source names differ from the actual current frame");
				}
				source_ready = source.Ready;
			} else if (sources[index] != nullptr) {
				throw std::runtime_error("motion stencil ancestry structural/child kinds do not consume sprite source bindings");
			}
			state.m_Active = !layers[index].FrameHidden && prepared.StencilType != 0 && layers[index].Active && source_ready;
			if (state.m_Active && prepared.Type == 12) {
				state.m_Active = Kind12StencilActive(*state.m_Mask);
			}
		}
		m_Composed = true;
	}

	void MotionLayerStencilAncestry::RequireLive(const MotionStencilLayerState& state) const {
		const auto nodes = m_Selected.Nodes();
		if (!m_Composed || state.m_Owner != this || state.m_Node == nullptr || nodes.size() != m_States.size() || state.m_Node->NativeOrdinal == 0) {
			throw std::runtime_error("motion stencil link requires its actual live composed selected owner");
		}
		const auto index = state.m_Node->NativeOrdinal - 1;
		if (index >= m_States.size() || &m_States[index] != &state || &nodes[index] != state.m_Node) {
			throw std::runtime_error("motion stencil link identity differs from its actual selected node");
		}
	}

	const MotionStencilLayerState& MotionLayerStencilAncestry::State(std::size_t layer) const {
		if (layer >= m_States.size()) {
			throw std::runtime_error("motion stencil ancestry index exceeds its selected nodes");
		}
		RequireLive(m_States[layer]);

		return m_States[layer];
	}

	void MotionLayerStencilAncestry::BindChildStencil(std::size_t layer, MotionChildConstructionState& child) const {
		const auto& state = State(layer);
		if (state.m_Node->Prepared->Type != 3 || &m_Selected.Child(static_cast<std::uint32_t>(layer)) != &child || child.Root().MatrixMode() != MotionRootMatrixMode::SuppliedByParent) {
			throw std::runtime_error("motion stencil transfer requires this exact exclusive selected kind3 child");
		}
		child.m_InheritedStencil = state.m_Ancestor;
	}

	void MotionLayerStencilAncestry::BindParticleStencil(std::size_t layer, MotionParticlePlayerState& child, const MotionSelectedLayerNodes& selected) const {
		const auto& state = State(layer);
		if (state.m_Node->Prepared->Type != 4 || !m_Selected.OwnsParticleSelection(static_cast<std::uint32_t>(layer), selected) || selected.m_EnclosingParticle != &child || child.Root().MatrixMode() != MotionRootMatrixMode::Recomputed) {
			throw std::runtime_error("motion particle stencil transfer requires its actual emitted child");
		}
		child.m_InheritedStencil = state.m_Ancestor;
	}

	const MotionSelectedLayerNode& MotionStencilLayerState::Node() const {
		if (m_Owner == nullptr) {
			throw std::runtime_error("motion stencil link has no actual composed selected owner");
		}
		m_Owner->RequireLive(*this);

		return *m_Node;
	}

	bool MotionStencilLayerState::Active() const {
		static_cast<void>(Node());

		return m_Active;
	}

	const MotionStencilLayerState* MotionStencilLayerState::Ancestor() const {
		static_cast<void>(Node());

		return m_Ancestor;
	}

	const MotionStencilMaskOwner* MotionStencilLayerState::MaskOwner() const {
		static_cast<void>(Node());

		return m_Mask;
	}

} // namespace sgre::scene
