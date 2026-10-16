#include "sgre/scene/motion_child_frame_slots.hpp"

#include "sgre/scene/motion_layer_parameters.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace sgre::scene {

	struct MotionChildSlotSelectionScope {
		bool& Selecting;

		explicit MotionChildSlotSelectionScope(bool& selecting)
		    : Selecting(selecting) {
			if (Selecting) {
				throw std::runtime_error("motion child frame selection cannot reenter its required action queue");
			}

			Selecting = true;
		}

		~MotionChildSlotSelectionScope() {
			Selecting = false;
		}
	};

	static std::uint32_t ChildSlotFrameMask(const resource::MotionFrame& frame) {
		const auto* value = frame.Content.Find("mask");
		if (value != nullptr) {
			if (const auto* boolean = std::get_if<bool>(&value->Data)) {
				return *boolean ? 1u : 0u;
			}

			if (const auto* integer = std::get_if<std::int64_t>(&value->Data); integer != nullptr && *integer >= std::numeric_limits<std::int32_t>::min() && *integer <= std::numeric_limits<std::uint32_t>::max()) {
				return static_cast<std::uint32_t>(*integer);
			}

			if (const auto* real = std::get_if<double>(&value->Data); real != nullptr && std::isfinite(*real) && *real >= std::numeric_limits<std::int32_t>::min() && *real <= std::numeric_limits<std::int32_t>::max()) {
				return static_cast<std::uint32_t>(static_cast<std::int32_t>(*real));
			}
		}

		throw std::runtime_error("motion child frame requires its bounded native mask");
	}

	static bool ChildSlotGround(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return *integer != 0;
		}

		if (const auto* real = std::get_if<double>(&value.Data)) {
			return *real != 0;
		}

		throw std::runtime_error("motion child ground flag requires actual numeric metadata");
	}

	static std::string_view ChildSlotString(const resource::PsbValue& content, std::string_view key) {
		const auto* value = content.Find(key);
		const auto* text = value == nullptr ? nullptr : std::get_if<std::string>(&value->Data);
		if (text == nullptr || text->size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion child frame requires bounded " + std::string(key) + " text");
		}

		return std::string_view(*text).substr(0, text->find('\0'));
	}

	MotionChildFrameSlots::MotionChildFrameSlots(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float last_time, MotionChildConstructionState& child, MotionChildFrameSlotServices services)
	    : MotionChildFrameSlots(layer, prepared, last_time, child, std::move(services), nullptr, 0) {
	}

	MotionChildFrameSlots::MotionChildFrameSlots(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float last_time, MotionChildConstructionState& child, MotionChildFrameSlotServices services, const MotionLayerParameterBindings& parameters, std::uint32_t layer_index)
	    : MotionChildFrameSlots(layer, prepared, last_time, child, std::move(services), &parameters, layer_index) {
	}

	MotionChildFrameSlots::MotionChildFrameSlots(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float last_time, MotionChildConstructionState& child, MotionChildFrameSlotServices services, const MotionLayerParameterBindings* parameters, std::uint32_t layer_index)
	    : m_Parameters(parameters), m_LayerIndex(layer_index), m_Layer(layer), m_Prepared(prepared), m_LastTime(last_time), m_Child(&child), m_Services(std::move(services)) {
		if (parameters != nullptr && (&parameters->Layer(layer_index) != &layer || &parameters->Commands() != &child.ImmediateParentCommands())) {
			throw std::runtime_error("motion child parameter slots require their exact raw layer and actual immediate parent records");
		}
		if (!m_Services.EnqueueAction) {
			throw std::runtime_error("motion child frame slots require an actual action queue");
		}

		const bool live_bound_parameter = parameters != nullptr && parameters->Parameter(layer_index) != nullptr;
		if (layer.Type != 3 || prepared.Type != 3 || prepared.MeshTransform != 0 || (prepared.StencilType != 0 && prepared.StencilType != 5) || layer.Label != prepared.Label || layer.Coordinate != prepared.CoordinatePlane || child.Root().Frame().Hierarchy.CoordinatePlane != prepared.CoordinatePlane || child.Root().TransformOrder() != prepared.TransformOrder || !std::isfinite(last_time) || last_time < 0 || layer.Frames.size() < 2 || layer.Frames.size() > kMaximumMotionChildSlotFrames || layer.Frames.size() != prepared.Frames.size() || layer.Frames.front().Time != 0 || (layer.Frames.back().Time < last_time && !live_bound_parameter) || layer.Label.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion child slots require matching bounded ordinary kind3 frames and their genuine constructor configuration");
		}

		for (const auto& [name, value] : layer.Properties) {
			if ((name == "parameterize" && !std::holds_alternative<std::monostate>(value.Data) && parameters == nullptr) || (name == "groundCorrection" && ChildSlotGround(value))) {
				throw std::runtime_error("motion child frame slots require the unrecovered parameter/ground producer for this layer");
			}
		}

		m_Frames.reserve(layer.Frames.size());
		float previous = 0;
		for (std::size_t index = 0; index < layer.Frames.size(); ++index) {
			const auto& raw = layer.Frames[index];
			const auto& frame = prepared.Frames[index];
			if (!std::isfinite(raw.Time) || raw.Time < previous || raw.Time != frame.Time || (raw.Type != 0 && raw.Type != 2 && raw.Type != 3) || frame.Hidden != (raw.Type == 0) || frame.Tween != (raw.Type == 3)) {
				throw std::runtime_error("motion child slots require matching finite static/hidden/tween metadata; type1 remains unsupported");
			}

			Frame value;
			value.Control = PrepareMotionChildFrameControl(layer, raw);
			if (raw.Type != 0) {
				const auto mask = ChildSlotFrameMask(raw);
				if (mask != frame.Attributes.Mask) {
					throw std::runtime_error("motion child frame mask differs from actual preparation");
				}

				static_cast<void>(ChildSlotString(raw.Content, "src"));
				static_cast<void>(ChildSlotString(raw.Content, "icon"));
				if ((mask & 0x40000) != 0) {
					value.Action = ChildSlotString(raw.Content, "act");
					value.ActionPresent = true;
				}
			}

			m_Frames.push_back(value);
			previous = raw.Time;
		}
	}

	MotionChildFrameSlots::MotionChildFrameSlots(const MotionChildFrameSlots& source, MotionChildConstructionState& child, MotionChildFrameSlotServices services, const MotionLayerParameterBindings& parameters)
	    : m_Parameters(&parameters), m_LayerIndex(source.m_LayerIndex), m_Layer(source.m_Layer), m_Prepared(source.m_Prepared), m_LastTime(source.m_LastTime), m_Child(&child), m_Services(std::move(services)), m_Frames(source.m_Frames), m_State(source.m_State), m_InitialUnselectedFrame(source.m_InitialUnselectedFrame), m_PreparedSlots(source.m_PreparedSlots), m_Published(source.m_Published) {
		if (source.m_Selecting || !m_Services.EnqueueAction || &parameters.Layer(m_LayerIndex) != &m_Layer || &parameters.Commands() != &child.ImmediateParentCommands()) {
			throw std::runtime_error("motion child slot clone requires its idle source and new actual parameter/action owners");
		}
		if (source.m_Published && source.m_State.Initialized) {
			const auto& frame = source.CurrentFrame();
			const auto source_name = std::string_view(frame.Attributes.Source).substr(0, frame.Attributes.Source.find('\0'));
			const auto icon = std::string_view(frame.Attributes.Icon).substr(0, frame.Attributes.Icon.find('\0'));
			const auto& commands = source.m_Child->Commands();
			const auto& names = commands.Names();
			if (!frame.Hidden && !source_name.empty() && commands.SelectedProject() && commands.CachedTimeline() != nullptr && commands.SelectedProject()->Timeline == commands.CachedTimeline() && names.CurrentCharacter == source_name && (names.CurrentMotion == icon || source.HasClonedSelectedTarget()) && child.Commands().Names().CurrentMotion.empty() && child.Commands().Names().RequestedMotion == icon && child.Commands().CachedTimeline() == commands.CachedTimeline() && child.Commands().SelectionSerial() == commands.SelectionSerial()) {
				m_ClonedTargetFrame = &frame;
				m_ClonedTargetTimeline = commands.CachedTimeline();
				m_ClonedTargetSelectionSerial = commands.SelectionSerial();
			}
		}
	}

	void MotionChildFrameSlots::Decode(std::size_t slot, std::size_t frame) {
		m_State.FrameIndices[slot] = frame;
		m_PreparedSlots[slot] = false;
	}

	void MotionChildFrameSlots::PrepareSlot(std::size_t slot) {
		if (!m_PreparedSlots[slot]) {
			m_Child->ApplyControlUpdate(slot, m_Frames[m_State.FrameIndices[slot]].Control);
			m_PreparedSlots[slot] = true;
		}
	}

	void MotionChildFrameSlots::QueueCurrentAction() {
		const auto& frame = m_Frames[m_State.FrameIndices[m_State.CurrentSlot]];
		if (frame.ActionPresent) {
			m_Services.EnqueueAction(std::string_view(m_Layer.Label).substr(0, m_Layer.Label.find('\0')), frame.Action);
		}
	}

	const MotionLayerParameterBindings* MotionChildFrameSlots::ParameterBindings() const noexcept {
		return m_Parameters;
	}

	std::uint32_t MotionChildFrameSlots::LayerIndex() const noexcept {
		return m_LayerIndex;
	}

	void MotionChildFrameSlots::Select(MotionFrameTraversal traversal, float position) {
		if (m_Parameters != nullptr && m_Parameters->Parameter(m_LayerIndex) != nullptr) {
			throw std::runtime_error("motion bound child selection requires its actual containing local parameter phase");
		}

		SelectPosition(traversal, position, false, position);
	}

	void MotionChildFrameSlots::SelectParameter(MotionFrameTraversal traversal, float ordinary_position) {
		if (m_Parameters == nullptr || m_Parameters->Parameter(m_LayerIndex) == nullptr) {
			throw std::runtime_error("motion child parameter traversal requires its exact bound record");
		}

		SelectPosition(traversal, m_Parameters->Position(m_LayerIndex), true, ordinary_position);
	}

	void MotionChildFrameSlots::SelectPosition(MotionFrameTraversal traversal, float position, bool parameter, float ordinary_position) {
		MotionChildSlotSelectionScope scope(m_Selecting);
		const auto previous_frame = m_State.Initialized ? std::optional<std::size_t>(m_State.FrameIndices[m_State.CurrentSlot]) : std::nullopt;
		m_Published = false;
		if (!std::isfinite(position) || position < 0 || (!parameter && position > m_LastTime) || (traversal != MotionFrameTraversal::Seek && traversal != MotionFrameTraversal::Forward && traversal != MotionFrameTraversal::Reverse && !(parameter && traversal == MotionFrameTraversal::ParameterOnly))) {
			throw std::runtime_error("motion child frame traversal is outside its ordinary domain");
		}

		if (traversal == MotionFrameTraversal::Seek) {
			std::size_t current = 0;
			for (std::size_t index = 0; index < m_Layer.Frames.size(); ++index) {
				if (m_Layer.Frames[index].Time == position || m_Layer.Frames[index].Time > position) {
					current = std::min(m_Layer.Frames[index].Time == position ? index : index - 1, m_Layer.Frames.size() - 2);
					break;
				}
			}

			m_State.CurrentSlot = 0;
			Decode(0, current);
			PrepareSlot(0);
			Decode(1, current + 1);
			PrepareSlot(1);
			m_State.Initialized = true;
			m_State.FrameDirty = true;
			if (m_Layer.Frames[current].Time == ordinary_position) {
				QueueCurrentAction();
			}
		} else {
			if (!m_State.Initialized) {
				throw std::runtime_error("motion child slots require an initial seek before advancing");
			}

			const auto prior = m_State.FrameIndices[m_State.CurrentSlot];
			auto current = prior;
			if (parameter || traversal == MotionFrameTraversal::Forward) {
				while (current < m_Layer.Frames.size() - 2 && position >= m_Layer.Frames[current + 1].Time) {
					m_State.CurrentSlot = 1 - m_State.CurrentSlot;
					++current;
					Decode(1 - m_State.CurrentSlot, current + 1);
					if (!parameter) {
						QueueCurrentAction();
					}
				}
			}
			if (parameter || traversal == MotionFrameTraversal::Reverse) {
				while (m_Layer.Frames[current].Time > position) {
					m_State.CurrentSlot = 1 - m_State.CurrentSlot;
					--current;
					Decode(m_State.CurrentSlot, current);
					if (!parameter) {
						QueueCurrentAction();
					}
				}
			}

			if (current != prior) {
				m_State.FrameDirty = true;
				PrepareSlot(0);
				PrepareSlot(1);
			}
		}

		if (!previous_frame || *previous_frame != m_State.FrameIndices[m_State.CurrentSlot]) {
			// A physical crossing cannot itself prove a new command result. Keep only dormant clone provenance.
			m_ClonedTargetNeedsCommand = m_ClonedTargetFrame != nullptr;
		}
		m_Published = true;
	}

	void MotionChildFrameSlots::CompleteComposition() {
		static_cast<void>(State());
		m_State.FrameDirty = false;
	}

	MotionChildConstructionState& MotionChildFrameSlots::Child() noexcept {
		return *m_Child;
	}

	const MotionChildFrameSlotState& MotionChildFrameSlots::State() const {
		if (!m_Published) {
			throw std::runtime_error("motion child frame slots have no published selection");
		}

		return m_State;
	}

	const PreparedMotionFrame& MotionChildFrameSlots::CurrentFrame() const {
		const auto& state = State();

		return m_Prepared.Frames[state.FrameIndices[state.CurrentSlot]];
	}

	const PreparedMotionFrame& MotionChildFrameSlots::FollowingFrame() const {
		const auto& state = State();

		return m_Prepared.Frames[state.FrameIndices[1 - state.CurrentSlot]];
	}

	const MotionChildFrameControl& MotionChildFrameSlots::CurrentControl() const {
		return m_Child->Control(State().CurrentSlot);
	}

	bool MotionChildFrameSlots::MatchesInitialUnselectedTarget() const {
		const auto& frame = CurrentFrame();
		const auto source = std::string_view(frame.Attributes.Source).substr(0, frame.Attributes.Source.find('\0'));
		const auto& commands = m_Child->Commands();
		const auto& names = commands.Names();

		return !frame.Hidden && !source.empty() && !commands.SelectedProject() && commands.CachedTimeline() == nullptr && !commands.ClockState().Playing && names.CurrentCharacter == source && names.RequestedCharacter == source && names.CurrentMotion.empty() && names.RequestedMotion.empty() && names.DelayedMotion.empty() && names.DeferredStealthCharacter.empty() && names.DeferredStealthMotion.empty();
	}

	void MotionChildFrameSlots::RecordCommandSelection() {
		m_InitialUnselectedFrame = nullptr;
		const auto* retained_frame = MatchesClonedSelectedOwner() ? m_ClonedTargetFrame : nullptr;
		m_ClonedTargetFrame = nullptr;
		m_ClonedTargetNeedsCommand = false;
		if (retained_frame != nullptr) {
			const auto& current = CurrentFrame();
			const auto source = std::string_view(current.Attributes.Source).substr(0, current.Attributes.Source.find('\0'));
			const auto icon = std::string_view(current.Attributes.Icon).substr(0, current.Attributes.Icon.find('\0'));
			const auto retained_source = std::string_view(retained_frame->Attributes.Source).substr(0, retained_frame->Attributes.Source.find('\0'));
			const auto retained_icon = std::string_view(retained_frame->Attributes.Icon).substr(0, retained_frame->Attributes.Icon.find('\0'));
			if (!current.Hidden && source == retained_source && icon == retained_icon) {
				// Native525880 compares ordinary Play to RequestedMotion and can retain the copied atom without 51bd50.
				m_ClonedTargetFrame = &current;
			}
		}
		if (m_ClonedTargetFrame == nullptr) {
			m_ClonedTargetTimeline = nullptr;
			m_ClonedTargetSelectionSerial = 0;
		}
		if (MatchesInitialUnselectedTarget()) {
			const auto& frame = CurrentFrame();
			const auto source = std::string_view(frame.Attributes.Source).substr(0, frame.Attributes.Source.find('\0'));
			const auto motion = std::string_view(frame.Attributes.Icon).substr(0, frame.Attributes.Icon.find('\0'));
			const auto& commands = m_Child->Commands();
			if (!commands.Manager().ResolveTimeline(source, motion, commands.PreferredProject())) {
				m_InitialUnselectedFrame = &frame;
			}
		}
	}

	bool MotionChildFrameSlots::HasInitialUnselectedTarget() const {
		// New registrations do not replay a clean native child command; retain its actual result until the next command.
		return m_InitialUnselectedFrame == &CurrentFrame() && MatchesInitialUnselectedTarget();
	}

	bool MotionChildFrameSlots::MatchesClonedSelectedOwner() const {
		if (m_ClonedTargetFrame == nullptr || m_ClonedTargetTimeline == nullptr) {
			return false;
		}
		const auto& frame = *m_ClonedTargetFrame;
		const auto source = std::string_view(frame.Attributes.Source).substr(0, frame.Attributes.Source.find('\0'));
		const auto icon = std::string_view(frame.Attributes.Icon).substr(0, frame.Attributes.Icon.find('\0'));
		const auto& commands = m_Child->Commands();
		const auto& names = commands.Names();
		return !frame.Hidden && !source.empty() && commands.SelectedProject() && commands.CachedTimeline() == m_ClonedTargetTimeline && commands.SelectedProject()->Timeline == m_ClonedTargetTimeline && commands.SelectionSerial() == m_ClonedTargetSelectionSerial && names.CurrentCharacter == source && names.CurrentMotion.empty() && names.RequestedMotion == icon;
	}

	bool MotionChildFrameSlots::HasClonedSelectedTarget() const {
		if (!m_Published || !m_State.Initialized || m_ClonedTargetNeedsCommand || m_ClonedTargetFrame == nullptr || m_ClonedTargetFrame != &CurrentFrame()) {
			return false;
		}

		return MatchesClonedSelectedOwner();
	}

	const MotionChildFrameControl& MotionChildFrameSlots::FollowingControl() const {
		return m_Child->Control(1 - State().CurrentSlot);
	}

} // namespace sgre::scene
