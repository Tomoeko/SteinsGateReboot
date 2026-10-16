#include "sgre/scene/motion_local_frames.hpp"

#include "sgre/scene/motion_particle_layer_state.hpp"

#include "sgre/scene/motion_timeline_variables.hpp"

#include "sgre/scene/motion_shape_owner.hpp"

#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_layer_parameters.hpp"
#include "sgre/scene/motion_selected_mesh_vertices.hpp"

#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::scene {

	struct MotionLocalTraversalScope {
		bool& Active;

		explicit MotionLocalTraversalScope(bool& active)
		    : Active(active) {
			if (active) {
				throw std::runtime_error("motion local frame traversal cannot reenter its required services");
			}

			active = true;
		}

		~MotionLocalTraversalScope() {
			Active = false;
		}
	};

	static bool LocalBoolean(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return *integer != 0;
		}

		if (const auto* real = std::get_if<double>(&value.Data)) {
			return *real != 0;
		}

		throw std::runtime_error("motion local layer boolean property requires actual numeric metadata");
	}

	static std::string_view LocalEventString(std::string_view text) {
		return text.substr(0, text.find('\0'));
	}

	static bool ZeroHierarchyStencil(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return !*boolean;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return *integer == 0;
		}

		if (const auto* real = std::get_if<double>(&value.Data)) {
			return std::isfinite(*real) && *real > -1 && *real < 1;
		}

		return false;
	}

	static bool EmptyLocalMetadata(const resource::PsbValue& value) {
		if (const auto* array = std::get_if<resource::PsbArray>(&value.Data)) {
			return array->empty();
		}

		if (const auto* object = std::get_if<resource::PsbObject>(&value.Data)) {
			return object->empty();
		}

		return std::holds_alternative<std::monostate>(value.Data);
	}

	MotionLocalFrames::MotionLocalFrames(const resource::MotionTimeline& timeline, const PreparedMotionTimeline& prepared, MotionLocalFrameServices services)
	    : MotionLocalFrames(timeline, prepared, nullptr, std::move(services)) {
	}

	MotionLocalFrames::MotionLocalFrames(const resource::MotionTimeline& timeline, const PreparedMotionTimeline& prepared, const resource::MotionResource& resource, MotionLocalFrameServices services)
	    : MotionLocalFrames(timeline, prepared, &resource, std::move(services)) {
	}

	MotionLocalFrames::MotionLocalFrames(MotionSelectedLayerNodes& selected, const resource::MotionResource& resource, MotionLocalFrameServices services)
	    : MotionLocalFrames(selected.m_Timeline, selected.m_Preparation, &resource, std::move(services), &selected) {
	}

	MotionLocalFrames::MotionLocalFrames(const resource::MotionTimeline& timeline, const PreparedMotionTimeline& prepared, const resource::MotionResource* resource, MotionLocalFrameServices services, MotionSelectedLayerNodes* selected)
	    : m_Timeline(timeline), m_Prepared(prepared), m_Resource(resource), m_SelectedOwner(selected), m_Services(std::move(services)) {
		if (!m_Services.BindSource || !m_Services.EnqueueAction) {
			throw std::runtime_error("motion local frames require actual source binding and action queue services");
		}

		if (timeline.Kind != resource::MotionTimelineKind::Frames || !std::isfinite(timeline.LastTime) || timeline.LastTime < 0 || (!EmptyLocalMetadata(timeline.Variables) && (selected == nullptr || selected->m_Commands.TimelineVariables() == nullptr || !selected->m_Commands.TimelineVariables()->Uses(selected->m_Commands, timeline))) || (!std::holds_alternative<std::monostate>(timeline.Parameterization.Data) && selected == nullptr) || !std::holds_alternative<std::monostate>(timeline.Stream.Data) || prepared.Layers.size() > 1000000 || prepared.LastTime != timeline.LastTime) {
			throw std::runtime_error("motion local frames require ordinary tracks without variable/parameterized/packed stream preparation");
		}

		RequireSelectedOwner();
		if (!std::holds_alternative<std::monostate>(timeline.Parameterization.Data) && (m_SelectedOwner == nullptr || m_SelectedOwner->m_Parameters == nullptr || m_SelectedOwner->m_Commands.ParameterValues().CurrentTimelineParameters(timeline) != m_SelectedOwner->m_Parameters.get() || m_SelectedOwner->m_Parameters->TimelineParameter() == nullptr)) {
			throw std::runtime_error("motion selected whole-timeline local frames require their exact live registered controller");
		}
		if (m_SelectedOwner != nullptr && (m_SelectedOwner->m_ParameterBindings == nullptr || m_SelectedOwner->m_ParameterBindings->Count() != prepared.Layers.size())) {
			throw std::runtime_error("motion selected local numeric phase requires its genuine parameter producer");
		}

		m_Layers.reserve(prepared.Layers.size());
		AddLayers(timeline.Layers, 0);
		if (m_Layers.size() != prepared.Layers.size()) {
			throw std::runtime_error("motion local frame hierarchy differs from its actual preparation");
		}

		m_Samples.resize(prepared.Layers.size());
		if (m_SelectedOwner != nullptr) {
			m_SelectedOwner->m_LocalNumericOwner = this;
		}
	}

	MotionLocalFrames::~MotionLocalFrames() noexcept {
		if (m_SelectedOwner != nullptr && m_SelectedOwner->m_LocalNumericOwner == this) {
			m_SelectedOwner->m_LocalNumericOwner = nullptr;
		}
	}

	void MotionLocalFrames::AddLayers(const std::vector<resource::MotionLayer>& layers, std::size_t depth) {
		if (depth > 128) {
			throw std::runtime_error("motion local frame hierarchy depth exceeds its bound");
		}

		for (const auto& source : layers) {
			if (m_Layers.size() >= m_Prepared.Layers.size()) {
				throw std::runtime_error("motion local frame hierarchy exceeds actual prepared storage");
			}

			const auto& prepared = m_Prepared.Layers[m_Layers.size()];
			if (m_SelectedOwner != nullptr && (m_SelectedOwner->Nodes()[m_Layers.size()].Raw != &source || m_SelectedOwner->Nodes()[m_Layers.size()].Prepared != &prepared)) {
				throw std::runtime_error("motion local numeric phase requires exact selected raw layer identities");
			}
			if ((source.Type != 0 && source.Type != 2 && !(m_SelectedOwner != nullptr && (source.Type == 1 || source.Type == 3 || source.Type == 4 || source.Type == 7 || source.Type == 12))) || prepared.Type != source.Type || (prepared.MeshTransform != 0 && !(m_SelectedOwner != nullptr && source.Type == 0 && prepared.MeshTransform == 1)) || (source.Type == 2 && prepared.StencilType != 0) || source.Label != prepared.Label || source.Frames.size() != prepared.Frames.size() || source.Frames.size() < 2 || source.Frames.front().Time != 0 || source.Frames.size() > 1000000 || source.Label.size() > kMaximumMotionCommandNameBytes) {
				throw std::runtime_error("motion local frames require matching bounded sprite/hierarchy preparation with at least two sorted frames; hierarchy stencil requires its actual owner");
			}

			for (const auto& [name, value] : source.Properties) {
				if (source.Type == 2 && name == "stencilType" && !ZeroHierarchyStencil(value)) {
					throw std::runtime_error("motion hierarchy stencil requires actual non-drawing numeric metadata and the recovered stencil owner");
				}

				if (name == "groundCorrection" && LocalBoolean(value)) {
					throw std::runtime_error("motion local frames require the unrecovered ground correction owner");
				}

				if (name == "parameterize" && !std::holds_alternative<std::monostate>(value.Data) && m_SelectedOwner == nullptr) {
					throw std::runtime_error("motion local frames require the unrecovered per-layer parameter phase");
				}
			}

			if (source.Frames.size() > 1000000 - m_FrameCount) {
				throw std::runtime_error("motion local frames exceed their aggregate prepared frame bound");
			}

			m_FrameCount += source.Frames.size();
			Layer layer;
			layer.Label = LocalEventString(source.Label);
			layer.Actions.reserve(source.Frames.size());
			float previous = 0;
			for (std::size_t index = 0; index < source.Frames.size(); ++index) {
				const auto& raw = source.Frames[index];
				const auto& frame = prepared.Frames[index];
				if (!std::isfinite(raw.Time) || raw.Time < previous || raw.Time != frame.Time || (raw.Type != 0 && raw.Type != 2 && raw.Type != 3) || frame.Hidden != (raw.Type == 0) || frame.Tween != (raw.Type == 3) || (frame.Tween && m_Resource == nullptr)) {
					throw std::runtime_error("motion local frames require finite matching static/hidden/tween frames; tween requires its actual curve resource and type1 remains unsupported");
				}

				if (frame.Tween && index + 1 < prepared.Frames.size() && !prepared.Frames[index + 1].Hidden && prepared.Frames[index + 1].Time <= frame.Time) {
					throw std::runtime_error("motion selected tween requires a positive adjacent duration");
				}

				Action action;
				if (!frame.Hidden) {
					if (source.Type == 0 || source.Type == 12) {
						for (const auto name : {"src", "icon"}) {
							const auto* value = raw.Content.Find(name);
							if (value == nullptr || !std::holds_alternative<std::string>(value->Data)) {
								throw std::runtime_error("motion ordinary sprite frame requires actual source/icon strings");
							}
						}
					}

					if ((frame.Attributes.UnresolvedMask & ~(std::uint32_t{0x40000} | (source.Type == 3 && m_SelectedOwner != nullptr ? 0x80000u : 0u) | (source.Type == 4 && m_SelectedOwner != nullptr ? 0x100000u : 0u) | (m_SelectedOwner != nullptr ? 0x20000000u : 0u))) != 0) {
						throw std::runtime_error("motion local frame contains unrecovered attribute behavior");
					}

					if ((frame.Attributes.Mask & 0x40000) != 0) {
						const auto* value = raw.Content.Find("act");
						const auto* text = value == nullptr ? nullptr : std::get_if<std::string>(&value->Data);
						if (text == nullptr || text->size() > kMaximumMotionCommandNameBytes) {
							throw std::runtime_error("motion frame action flag requires bounded act text");
						}

						action.Text = LocalEventString(*text);
						action.Present = true;
					}
				}

				layer.Actions.push_back(action);
				previous = raw.Time;
			}

			m_Layers.push_back(std::move(layer));
			AddLayers(source.Children, depth + 1);
		}
	}

	void MotionLocalFrames::QueueAction(std::size_t layer) {
		const auto& selected = m_Layers[layer];
		const auto& action = selected.Actions[selected.State.CurrentIndex];
		if (action.Present) {
			m_Services.EnqueueAction(selected.Label, action.Text);
		}
	}

	void MotionLocalFrames::BindSource(std::size_t layer) {
		// Native seek/crossing dispatch mask 0x1801 has no binding phase for structural kinds1/2/7 or the separately controlled kind3.
		if (m_Prepared.Layers[layer].Type == 1 || m_Prepared.Layers[layer].Type == 2 || m_Prepared.Layers[layer].Type == 3 || m_Prepared.Layers[layer].Type == 4 || m_Prepared.Layers[layer].Type == 7) {
			return;
		}

		const auto& frames = m_Prepared.Layers[layer].Frames;
		const auto index = m_Layers[layer].State.CurrentIndex;
		// This cache consumes only incoming Hidden/Source/Icon, which JOIN never restores.
		// Keep its retained frame pointer and string views in the immutable preparation owner.
		m_Services.BindSource(static_cast<std::uint32_t>(layer), frames[index], frames[index + 1]);
	}

	void MotionLocalFrames::RequireSelectedOwner() const {
		if (m_SelectedOwner == nullptr) {
			return;
		}

		if (m_SelectedOwner->m_LocalNumericOwner != nullptr && m_SelectedOwner->m_LocalNumericOwner != this) {
			throw std::runtime_error("motion selected local numeric cache belongs to its exact exclusive owner");
		}

		const auto& selection = m_SelectedOwner->m_Commands.SelectedProject();
		if (!selection || m_SelectedOwner->m_Commands.CachedTimeline() != &m_Timeline || selection->Timeline != &m_Timeline || !m_SelectedOwner->m_Commands.Manager().Contains(selection->Project) || &m_SelectedOwner->m_Commands.Manager().Resource(selection->Project) != m_Resource) {
			throw std::runtime_error("motion local numeric phase requires its actual registered selected resource");
		}

		const auto nodes = m_SelectedOwner->Nodes();
		if (!m_SelectedOwner->m_Published || m_SelectedOwner->m_Mutating || nodes.size() != m_Prepared.Layers.size() || &m_SelectedOwner->m_Timeline != &m_Timeline || &m_SelectedOwner->m_Preparation != &m_Prepared) {
			throw std::runtime_error("motion local numeric phase requires its actual live selected raw/prepared owner");
		}
	}

	void MotionLocalFrames::DecodeSlot(std::size_t layer, std::size_t slot, std::size_t frame) {
		auto& selected = m_Layers.at(layer);
		if (selected.JoinedFrame && selected.JoinedSlot == slot) {
			selected.JoinedDecoded.reset();
			selected.JoinedFrame.reset();
		}
		if (m_Prepared.Layers[layer].MeshTransform == 1) {
			m_SelectedOwner->MeshLocal(static_cast<std::uint32_t>(layer)).PrepareFrame(slot, frame);
		}
		if (m_Prepared.Layers[layer].Type == 4) {
			m_SelectedOwner->Particles(static_cast<std::uint32_t>(layer)).Frames().PrepareFrame(slot, frame);
		}
		if (m_Prepared.Layers[layer].Type == 12) {
			m_SelectedOwner->StencilMask(static_cast<std::uint32_t>(layer)).PrepareFrame(slot, frame);
		}
	}

	std::uint32_t MotionLocalFrames::CurrentMask(std::uint32_t layer) const {
		static_cast<void>(Evaluation(layer));
		const auto& selected = m_Layers[layer];
		return selected.JoinedFrame == selected.State.CurrentIndex ? selected.JoinedDecoded->Attributes.Mask : m_Prepared.Layers[layer].Frames[selected.State.CurrentIndex].Attributes.Mask;
	}

	void MotionLocalFrames::RestoreJoin(std::uint32_t layer, const MotionJoinedLayer& saved) {
		RequireSelectedOwner();
		RequireCurrentParameters();
		if (!m_Selected || m_Traversing || layer >= m_Layers.size() || saved.Kind != m_Prepared.Layers[layer].Type || (saved.Kind != 0 && saved.Kind != 2 && saved.Kind != 3 && saved.Kind != 12) || m_Prepared.Layers[layer].MeshTransform != 0) {
			throw std::runtime_error("motion JOIN numeric restore requires the exact supported selected current slot");
		}
		auto& selected = m_Layers[layer];
		const auto& incoming = m_Prepared.Layers[layer].Frames[selected.State.CurrentIndex];
		if (incoming.Hidden) {
			return;
		}
		if (!saved.Numeric.Hidden) {
			selected.JoinedDecoded = incoming;
			selected.JoinedFrame = selected.State.CurrentIndex;
			selected.JoinedSlot = selected.State.CurrentSlot;
			auto& target = selected.JoinedDecoded->Attributes;
			const auto& source = saved.Numeric;
			target.Mask = saved.Mask;
			target.BlendMode = source.BlendMode;
			target.OriginX = source.OriginX;
			target.OriginY = source.OriginY;
			target.Colors = source.Colors;
			target.Coordinate = source.Coordinate;
			target.Opacity = source.Opacity;
			target.UniformColor = source.UniformColor;
			target.UsesDefaultColor = source.UsesDefaultColor;
			target.FlipX = source.FlipX;
			target.FlipY = source.FlipY;
			target.Angle = source.Angle;
			target.ZoomX = source.ZoomX;
			target.ZoomY = source.ZoomY;
			target.ShearX = source.ShearX;
			target.ShearY = source.ShearY;
			selected.State.FrameDirty = true;
		}
		// Native51e720 calls515d40 after restoring the actual current numeric slot.
		const MotionLocalTraversalScope source_phase(m_Traversing);
		BindSource(layer);
	}

	void MotionLocalFrames::Traverse(MotionFrameTraversal traversal, float position) {
		Select(traversal, position);
		for (std::size_t layer = 0; layer < m_Layers.size(); ++layer) {
			static_cast<void>(Evaluate(static_cast<std::uint32_t>(layer), true));
		}

		m_Published = true;
	}

	void MotionLocalFrames::Select(MotionFrameTraversal traversal, float position) {
		MotionLocalTraversalScope traversal_scope(m_Traversing);
		RequireSelectedOwner();
		m_Published = false;
		m_Selected = false;
		if (m_SelectionSerial == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("motion local selection serial exhausted its owner lifetime");
		}
		++m_SelectionSerial;
		if (!std::isfinite(position) || position < 0 || position > m_Timeline.LastTime || (traversal != MotionFrameTraversal::Seek && traversal != MotionFrameTraversal::Forward && traversal != MotionFrameTraversal::Reverse && traversal != MotionFrameTraversal::ParameterOnly)) {
			throw std::runtime_error("motion local frame traversal is outside its ordinary domain");
		}

		if (m_SelectedOwner != nullptr && m_SelectedOwner->m_ContainingSelected != nullptr) {
			const auto* parent = m_SelectedOwner->m_ContainingSelected->m_LocalNumericOwner;
			if (parent == nullptr || !parent->m_Selected || parent->m_Traversing) {
				throw std::runtime_error("motion selected child requires its actual containing local preparation before child selection");
			}
			m_ContainingPreparationSerial = parent->m_SelectionSerial;
		}
		m_ParameterValueSerial = m_SelectedOwner == nullptr ? 0 : m_SelectedOwner->m_ParameterBindings->ValueSerial();
		for (std::size_t layer = 0; layer < m_Layers.size(); ++layer) {
			const auto* parameters = m_SelectedOwner == nullptr ? nullptr : m_SelectedOwner->m_ParameterBindings.get();
			const bool parameter = parameters != nullptr && parameters->Parameter(static_cast<std::uint32_t>(layer)) != nullptr;
			const auto layer_position = parameter ? parameters->Position(static_cast<std::uint32_t>(layer)) : position;
			if (!std::isfinite(layer_position) || layer_position < 0) {
				throw std::runtime_error("motion selected parameter position is outside its finite frame origin domain");
			}
			m_Layers[layer].Position = layer_position;
			auto& state = m_Layers[layer].State;
			const auto& prepared = m_Prepared.Layers[layer];
			const auto& frames = prepared.Frames;
			// Native51afb0 retains unbound cursor/decoded slots; numeric evaluation still reads current ordinary time.
			if (traversal == MotionFrameTraversal::ParameterOnly && !parameter) {
				if (!state.Initialized) {
					throw std::runtime_error("motion parameter-only traversal requires its actual initialized retained cursor");
				}

				continue;
			}

			if (prepared.Type == 3) {
				auto& slots = m_SelectedOwner->ChildSlots(static_cast<std::uint32_t>(layer));
				if (parameter) {
					slots.SelectParameter(traversal, position);
				} else {
					slots.Select(traversal, position);
				}
				const auto& child_state = slots.State();
				state.CurrentIndex = child_state.FrameIndices[child_state.CurrentSlot];
				state.CurrentSlot = child_state.CurrentSlot;
				state.Initialized = child_state.Initialized;
				state.FrameDirty = child_state.FrameDirty;
				continue;
			}

			if (traversal == MotionFrameTraversal::Seek) {
				// Native511640 starts the search at zero, including a record position beyond every frame time.
				state.CurrentIndex = 0;
				for (std::size_t index = 0; index < frames.size(); ++index) {
					if (frames[index].Time == layer_position || frames[index].Time > layer_position) {
						state.CurrentIndex = std::min(frames[index].Time == layer_position ? index : index - 1, frames.size() - 2);
						break;
					}
				}

				state.CurrentSlot = 0;
				DecodeSlot(layer, 0, state.CurrentIndex);
				DecodeSlot(layer, 1, state.CurrentIndex + 1);
				state.Initialized = true;
				state.FrameDirty = true;
				BindSource(layer);
				if (frames[state.CurrentIndex].Time == position) {
					QueueAction(layer);
				}
			} else {
				if (!state.Initialized) {
					throw std::runtime_error("motion local frames require initial seek before advancing");
				}

				const auto previous = state.CurrentIndex;
				std::array<std::optional<std::size_t>, 2> pending_decoded_slots;
				if (parameter || traversal == MotionFrameTraversal::Forward) {
					while (state.CurrentIndex < frames.size() - 2 && layer_position >= frames[state.CurrentIndex + 1].Time) {
						++state.CurrentIndex;
						state.CurrentSlot = 1 - state.CurrentSlot;
						if (prepared.MeshTransform == 1 || prepared.Type == 12) {
							pending_decoded_slots[1 - state.CurrentSlot] = state.CurrentIndex + 1;
						} else {
							DecodeSlot(layer, 1 - state.CurrentSlot, state.CurrentIndex + 1);
						}
						if (!parameter) {
							QueueAction(layer);
						}
					}
				}
				if (parameter || traversal == MotionFrameTraversal::Reverse) {
					while (layer_position < frames[state.CurrentIndex].Time) {
						--state.CurrentIndex;
						state.CurrentSlot = 1 - state.CurrentSlot;
						if (prepared.MeshTransform == 1 || prepared.Type == 12) {
							pending_decoded_slots[state.CurrentSlot] = state.CurrentIndex;
						} else {
							DecodeSlot(layer, state.CurrentSlot, state.CurrentIndex);
						}
						if (!parameter) {
							QueueAction(layer);
						}
					}
				}

				// The native cursor assigns every crossed frame before decoding only the final pending physical slots.
				for (std::size_t slot = 0; slot < pending_decoded_slots.size(); ++slot) {
					if (pending_decoded_slots[slot]) {
						DecodeSlot(layer, slot, *pending_decoded_slots[slot]);
					}
				}

				if (previous != state.CurrentIndex) {
					state.FrameDirty = true;
					BindSource(layer);
				}
			}
		}

		m_Position = position;
		m_Selected = true;
	}

	bool MotionLocalFrames::Evaluate(std::uint32_t layer, bool force) {
		RequireSelectedOwner();
		RequireCurrentParameters();
		if (!m_Selected || m_Traversing || layer >= m_Layers.size()) {
			throw std::runtime_error("motion local evaluation requires complete actual selected preparation");
		}

		auto& selected = m_Layers[layer];
		selected.InitialUnselectedFinishedSerial = 0;
		const auto retained_colors = selected.Evaluation.Sample.has_value() ? selected.Evaluation.Sample->Colors : std::array<std::uint32_t, 4>{};
		bool changed = force || selected.State.FrameDirty;
		const auto& prepared = m_Prepared.Layers[layer];
		const auto& current = selected.JoinedFrame == selected.State.CurrentIndex ? *selected.JoinedDecoded : prepared.Frames[selected.State.CurrentIndex];
		const auto& following = selected.JoinedFrame == selected.State.CurrentIndex + 1 ? *selected.JoinedDecoded : prepared.Frames[selected.State.CurrentIndex + 1];
		if (m_Resource != nullptr) {
			const auto* variables = m_SelectedOwner == nullptr ? nullptr : m_SelectedOwner->m_Commands.TimelineVariables();
			if (variables != nullptr && !variables->Empty()) {
				changed = EvaluateCachedMotionVariableLayer(prepared, *m_Resource, selected.Position, selected.State.CurrentIndex, current, following, force, selected.State.FrameDirty, selected.Evaluation, *variables);
			} else {
				changed = EvaluateCachedMotionLayer(prepared, *m_Resource, selected.Position, selected.State.CurrentIndex, current, following, force, selected.State.FrameDirty, selected.Evaluation);
			}
		} else if (changed) {
			selected.Evaluation.Sample = SamplePreparedMotionFrame(current);
		}

		if (!selected.Evaluation.Sample.has_value()) {
			throw std::runtime_error("motion local evaluation cannot suppress its genuine initial selected pose");
		}

		if (selected.Evaluation.Sample->Hidden) {
			// Actual50bc84 returns before updating local corner colors for a hidden frame.
			selected.Evaluation.Sample->Colors = retained_colors;
		}

		if (m_Prepared.Layers[layer].MeshTransform == 1) {
			auto& mesh = m_SelectedOwner->MeshLocal(layer);
			mesh.ApplyLocalEvaluation(selected.State, selected.Position, selected.Evaluation, changed);
			selected.Evaluation.Sample->Deformation = mesh.OwnPatch();
		}

		if (m_Prepared.Layers[layer].Type == 3) {
			auto& slots = m_SelectedOwner->ChildSlots(layer);
			const auto& child_state = slots.State();
			if (child_state.CurrentSlot != selected.State.CurrentSlot || child_state.FrameIndices[child_state.CurrentSlot] != selected.State.CurrentIndex || child_state.FrameIndices[1 - child_state.CurrentSlot] != selected.State.CurrentIndex + 1 || child_state.Initialized != selected.State.Initialized || child_state.FrameDirty != selected.State.FrameDirty || &slots.FollowingFrame() != &m_Prepared.Layers[layer].Frames[selected.State.CurrentIndex + 1]) {
				throw std::runtime_error("motion child numeric sample requires its actual selected physical child cursor tuple");
			}
			if (&slots.CurrentFrame() != &m_Prepared.Layers[layer].Frames[selected.State.CurrentIndex] || &slots.Child() != &m_SelectedOwner->Child(layer)) {
				throw std::runtime_error("motion child local numeric sample requires its actual selected control slots");
			}
			selected.Evaluation.Sample->UnresolvedMask &= ~std::uint32_t{0x80000};
		} else if (m_Prepared.Layers[layer].Type == 4) {
			auto& slots = m_SelectedOwner->Particles(layer).Frames();
			static_cast<void>(slots.Evaluate(*m_Resource, selected.Position, selected.State.CurrentSlot, force, selected.State.FrameDirty));
			if (slots.EvaluatedFrameIndex() != selected.State.CurrentIndex || slots.EvaluatedPosition() != selected.Position) {
				throw std::runtime_error("motion particle numeric sample requires its actual physical cursor tuple");
			}
			selected.Evaluation.Sample->UnresolvedMask &= ~std::uint32_t{0x100000};
		} else if (m_Prepared.Layers[layer].Type == 12) {
			auto& mask = m_SelectedOwner->StencilMask(layer);
			static_cast<void>(mask.Evaluate(*m_Resource, selected.Position, selected.State.CurrentSlot, force, selected.State.FrameDirty));
			if (mask.EvaluatedFrameIndex() != selected.State.CurrentIndex || mask.EvaluatedPosition() != selected.Position) {
				throw std::runtime_error("motion stencil numeric sample requires its actual selected physical mask cursor tuple");
			}
			selected.Evaluation.Sample->UnresolvedMask &= ~std::uint32_t{0x20000000};
		}

		if (m_SelectedOwner != nullptr && m_Prepared.Layers[layer].Type != 12) {
			// Actual512e40 exits before the stc bit dispatch for every non-kind12 node.
			selected.Evaluation.Sample->UnresolvedMask &= ~std::uint32_t{0x20000000};
		}

		selected.Evaluation.Sample->UnresolvedMask &= ~std::uint32_t{0x40000};
		m_Samples[layer] = *selected.Evaluation.Sample;
		selected.EvaluationChanged = changed;

		return changed;
	}

	const MotionLayerEvaluationCache& MotionLocalFrames::Evaluation(std::uint32_t layer) const {
		RequireSelectedOwner();
		RequireCurrentParameters();
		if (!m_Selected || m_Traversing || layer >= m_Layers.size() || !m_Layers[layer].Evaluation.Sample.has_value()) {
			throw std::runtime_error("motion local cache requires a completed selected evaluation");
		}

		return m_Layers[layer].Evaluation;
	}

	bool MotionLocalFrames::EvaluationChanged(std::uint32_t layer) const {
		static_cast<void>(Evaluation(layer));

		return m_Layers[layer].EvaluationChanged;
	}

	void MotionLocalFrames::PrepareVariableNumerics() {
		RequireSelectedOwner();
		if (m_SelectedOwner == nullptr) {
			return;
		}
		const auto* variables = m_SelectedOwner->m_Commands.TimelineVariables();
		if (variables == nullptr || variables->Empty()) {
			return;
		}
		if (!m_Selected || m_Traversing || !variables->Uses(m_SelectedOwner->m_Commands, m_Timeline)) {
			throw std::runtime_error("motion variable numeric preparation requires its actual selected local owner");
		}

		const auto position = m_SelectedOwner->m_Commands.ClockState().EvaluationTime;
		const auto serial = variables->RequireApplied(position);
		// Native50e530 runs after51ffd0: retain decoded cursors, then50bc40 reads the freshly written parameter position.
		for (std::size_t layer = 0; layer < m_Layers.size(); ++layer) {
			const auto* parameters = m_SelectedOwner->m_ParameterBindings.get();
			const auto value = parameters->Parameter(static_cast<std::uint32_t>(layer)) == nullptr ? position : parameters->Position(static_cast<std::uint32_t>(layer));
			if (!std::isfinite(value) || value < 0) {
				throw std::runtime_error("motion timeline variable wrote an unsupported local numeric position");
			}
			m_Layers[layer].Position = value;
		}
		m_Position = position;
		m_ParameterValueSerial = serial;
	}

	bool MotionLocalFrames::ParameterValuesCurrent() const noexcept {
		return m_SelectedOwner == nullptr || (m_SelectedOwner->m_Parameters != nullptr && m_SelectedOwner->m_Parameters->m_ValueSerial == m_ParameterValueSerial && m_SelectedOwner->m_Commands.SelectionSerial() == m_SelectedOwner->m_Parameters->m_SelectionSerial);
	}

	void MotionLocalFrames::RequireCurrentParameters() const {
		if (m_SelectedOwner != nullptr && m_Selected && m_SelectedOwner->m_ParameterBindings->ValueSerial() != m_ParameterValueSerial) {
			throw std::runtime_error("motion local parameter positions changed after their actual containing selection");
		}
	}

	float MotionLocalFrames::Position(std::uint32_t layer) const {
		RequireSelectedOwner();
		RequireCurrentParameters();
		if (!m_Selected || m_Traversing || layer >= m_Layers.size()) {
			throw std::runtime_error("motion layer position requires its actual selected local owner");
		}

		return m_Layers[layer].Position;
	}

	const MotionLayerParameterBindings* MotionLocalFrames::ParameterBindings() const {
		RequireSelectedOwner();
		RequireCurrentParameters();

		return m_SelectedOwner == nullptr ? nullptr : m_SelectedOwner->m_ParameterBindings.get();
	}

	void MotionLocalFrames::RequireFinishPhases() const {
		RequireCurrentParameters();
		if (m_SelectedOwner == nullptr) {
			return;
		}
		bool parameter_frame = false;
		std::size_t depth = 0;
		for (const auto* owner = m_SelectedOwner; owner != nullptr; owner = owner->m_ContainingSelected) {
			if (++depth > 64 || owner->m_Parameters == nullptr) {
				throw std::runtime_error("motion parameter finish requires bounded live actual containing selected owners");
			}
			parameter_frame = parameter_frame || !owner->m_Parameters->Entries().empty();
		}
		if (!parameter_frame) {
			return;
		}
		if (m_SelectedOwner->m_ContainingSelected != nullptr) {
			const auto* parent = m_SelectedOwner->m_ContainingSelected->m_LocalNumericOwner;
			if (parent == nullptr || !parent->m_Selected || parent->m_Traversing || m_ContainingPreparationSerial != parent->m_SelectionSerial) {
				throw std::runtime_error("motion parameter finish requires this exact child's current containing preparation");
			}
		}
		const auto* composition = m_SelectedOwner->m_CompositionOwner;
		if (composition == nullptr || composition->m_LocalFrames != this || !composition->m_ConditionalTrace || composition->m_ComposedSelectionSerial != m_SelectionSerial || composition->Layers().size() != m_Layers.size()) {
			throw std::runtime_error("motion parameter finish requires its actual completed current numeric composition");
		}
		for (std::size_t index = 0; index < m_Layers.size(); ++index) {
			const auto type = m_Prepared.Layers[index].Type;
			if (type == 0 || type == 12) {
				const auto* vertices = m_SelectedOwner->m_VertexOwner;
				if (vertices == nullptr || &vertices->Node(static_cast<std::uint32_t>(index)) != &m_SelectedOwner->Nodes()[index]) {
					throw std::runtime_error("motion parameter finish requires every actual current geometry producer before dirty acknowledgement");
				}
			}
			if (type == 1) {
				m_SelectedOwner->Shape(static_cast<std::uint32_t>(index)).RequireCurrentComposition(*composition);
			}
			if (type == 4) {
				const auto& particles = m_SelectedOwner->Particles(static_cast<std::uint32_t>(index));
				for (const auto& particle : particles.m_Particles) {
					const auto* child = particle->Selected.get();
					if (child == nullptr || child->m_LocalNumericOwner == nullptr || child->m_ContainingSelected != m_SelectedOwner || child->m_LocalNumericOwner->m_ContainingPreparationSerial != m_SelectionSerial || child->m_LocalNumericOwner->m_FinishedSelectionSerial != child->m_LocalNumericOwner->m_SelectionSerial || !child->m_LocalNumericOwner->m_Published) {
						throw std::runtime_error("motion parameter finish requires each actual surviving particle child after current geometry");
					}
					child->m_LocalNumericOwner->RequireFinishPhases();
				}
			}
			if (type == 3 && composition->Layers()[index].Active) {
				const auto& slots = m_SelectedOwner->ChildSlots(static_cast<std::uint32_t>(index));
				const auto& frame = slots.CurrentFrame();
				if (!frame.Hidden && !frame.Attributes.Source.empty() && frame.Attributes.Source.front() != '\0') {
					const auto* child = m_SelectedOwner->ChildNodes(static_cast<std::uint32_t>(index));
					if (child == nullptr && slots.HasInitialUnselectedTarget() && m_Layers[index].InitialUnselectedFinishedSerial == m_SelectionSerial) {
						const auto& actual = m_SelectedOwner->Child(static_cast<std::uint32_t>(index));
						const auto& clock = actual.Commands().ClockState();
						if (actual.Root().Frame().Composed && !actual.Root().Overrides().Dirty && !clock.TickAssigned && !clock.InitialCompositionPending) {
							continue;
						}
					}
					if (child == nullptr || child->m_LocalNumericOwner == nullptr || child->m_ContainingSelected != m_SelectedOwner || child->m_LocalNumericOwner->m_ContainingPreparationSerial != m_SelectionSerial || child->m_LocalNumericOwner->m_FinishedSelectionSerial != child->m_LocalNumericOwner->m_SelectionSerial || !child->m_LocalNumericOwner->m_Published) {
						throw std::runtime_error("motion parameter finish requires its exact selected nonempty child to finish after current child geometry");
					}
					child->m_LocalNumericOwner->RequireFinishPhases();
				}
			}
		}
	}

	void MotionLocalFrames::CompleteInitialUnselectedChild(std::uint32_t layer) {
		RequireSelectedOwner();
		RequireCurrentParameters();
		const auto* composition = m_SelectedOwner == nullptr ? nullptr : m_SelectedOwner->m_CompositionOwner;
		if (!m_Selected || m_Traversing || layer >= m_Layers.size() || m_Prepared.Layers[layer].Type != 3 || composition == nullptr || composition->m_LocalFrames != this || composition->m_ComposedSelectionSerial != m_SelectionSerial || !composition->Layers()[layer].Active || m_SelectedOwner->ChildNodes(layer) != nullptr || !m_SelectedOwner->ChildSlots(layer).HasInitialUnselectedTarget()) {
			throw std::runtime_error("motion unselected child finish requires its exact current containing composition and failed first command");
		}
		const auto& child = m_SelectedOwner->Child(layer);
		const auto& clock = child.Commands().ClockState();
		if (!child.Root().Frame().Composed || child.Root().Overrides().Dirty || clock.TickAssigned || clock.InitialCompositionPending) {
			throw std::runtime_error("motion unselected child finish requires its actual completed separate root and acknowledged update clock");
		}
		// Native524910 completes the real root even when there are no selected layers to acknowledge.
		m_Layers[layer].InitialUnselectedFinishedSerial = m_SelectionSerial;
	}

	const PreparedMotionTimeline& MotionLocalFrames::Preparation() const noexcept {
		return m_Prepared;
	}

	void MotionLocalFrames::MarkMeshDirty(std::uint32_t layer) {
		RequireSelectedOwner();
		if (m_Traversing || layer >= m_Layers.size() || m_Prepared.Layers[layer].MeshTransform != 1) {
			throw std::runtime_error("motion mesh write requires its actual idle numeric cell");
		}
		// Native526df0 writes node+40 while the preceding completed geometry remains usable.
		m_Layers[layer].State.FrameDirty = true;
	}

	void MotionLocalFrames::CompleteComposition() {
		RequireSelectedOwner();
		if (!m_Selected || m_Traversing) {
			throw std::runtime_error("motion local dirty acknowledgement requires complete selected producer phases");
		}

		for (auto& layer : m_Layers) {
			if (!layer.Evaluation.Sample.has_value()) {
				throw std::runtime_error("motion local dirty acknowledgement requires every actual local evaluation");
			}
		}

		RequireFinishPhases();
		for (std::size_t index = 0; index < m_Layers.size(); ++index) {
			if (m_Prepared.Layers[index].Type == 3) {
				m_SelectedOwner->ChildSlots(static_cast<std::uint32_t>(index)).CompleteComposition();
			}
			m_Layers[index].State.FrameDirty = false;
			if (m_SelectedOwner != nullptr) {
				m_SelectedOwner->m_ExternalSources[index].Dirty = false;
			}
		}

		if (m_SelectedOwner != nullptr) {
			m_SelectedOwner->m_Parameters->CompleteFrame();
		}
		// A failed producer/parameter finish retains the pending write for the next genuine update.
		if (m_SelectedOwner != nullptr) {
			for (std::size_t index = 0; index < m_Layers.size(); ++index) {
				if (m_Prepared.Layers[index].MeshTransform == 1) {
					m_SelectedOwner->MeshLocal(static_cast<std::uint32_t>(index)).CompleteComposition();
				}
			}
		}
		m_FinishedSelectionSerial = m_SelectionSerial;
		m_Published = true;
	}

	std::span<const MotionLayerSample> MotionLocalFrames::Samples() const noexcept {
		const bool live = m_SelectedOwner == nullptr || (m_SelectedOwner->m_Published && !m_SelectedOwner->m_Mutating);

		return m_Published && live ? std::span<const MotionLayerSample>{m_Samples} : std::span<const MotionLayerSample>{};
	}

	const MotionLocalFrameState& MotionLocalFrames::State(std::uint32_t layer) const {
		RequireSelectedOwner();
		if (layer >= m_Layers.size()) {
			throw std::runtime_error("motion local frame state index exceeds selected preparation");
		}

		return m_Layers[layer].State;
	}

} // namespace sgre::scene
