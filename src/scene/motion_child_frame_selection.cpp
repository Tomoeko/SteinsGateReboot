#include "sgre/scene/motion_child_frame_selection.hpp"

#include "sgre/scene/motion_child_initial_timing.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_layer_parameters.hpp"
#include "sgre/scene/motion_local_frames.hpp"

#include <stdexcept>

namespace sgre::scene {

	MotionChildFrameSelection SelectMotionChildFrameCommands(MotionChildFrameSlots& slots, const MotionTimelineClockState& parent, bool node_changed, std::uint32_t parent_parameter_flags, const std::function<void()>& retire_selected_tree) {
		if (parent_parameter_flags != 0 || (slots.ParameterBindings() != nullptr && slots.ParameterBindings()->Parameter(slots.LayerIndex()) != nullptr)) {
			throw std::runtime_error("motion child frame selection requires its genuine parameter timing/flag producer");
		}
		const auto& state = slots.State();
		if (!node_changed) {
			return MotionChildFrameSelection::ContinueUpdate;
		}

		auto& commands = slots.Child().Commands();
		const auto& frame = slots.CurrentFrame();
		if (frame.Hidden || frame.Attributes.Source.empty() || frame.Attributes.Source.front() == '\0') {
			commands.ResetChildMotion(retire_selected_tree);

			return MotionChildFrameSelection::Reset;
		}

		if (state.FrameDirty) {
			const auto& control = slots.CurrentControl();
			commands.SetCharacter(frame.Attributes.Source, MotionCharacterMode::Ordinary);
			commands.Play(frame.Attributes.Icon, control.Flags);
			slots.RecordCommandSelection();
			static_cast<void>(AssignMotionChildInitialTick(commands, parent, frame, control));
		}

		return MotionChildFrameSelection::BindRoot;
	}

	MotionChildFrameSelection SelectMotionChildFrameCommands(MotionChildFrameSlots& slots, const MotionLayerComposition& composition, const std::function<void()>& retire_selected_tree) {
		const auto* parameters = slots.ParameterBindings();
		const auto layer = slots.LayerIndex();
		if (parameters == nullptr || composition.LocalFrames().ParameterBindings() != parameters || &parameters->Commands() != &slots.Child().ImmediateParentCommands()) {
			throw std::runtime_error("motion child selection requires its exact containing parameter/local/numeric owners");
		}
		const auto& state = slots.State();
		const auto flags = parameters->ControlFlags(layer);
		if (!composition.Changed(layer) && flags == 0) {
			return MotionChildFrameSelection::ContinueUpdate;
		}

		auto& commands = slots.Child().Commands();
		const auto& frame = slots.CurrentFrame();
		if (frame.Hidden || frame.Attributes.Source.empty() || frame.Attributes.Source.front() == '\0') {
			commands.ResetChildMotion(retire_selected_tree);

			return MotionChildFrameSelection::Reset;
		}
		if (state.FrameDirty || (flags & 5) != 0) {
			const auto& control = slots.CurrentControl();
			const auto previous_selection = commands.SelectionSerial();
			commands.SetCharacter(frame.Attributes.Source, MotionCharacterMode::Ordinary);
			commands.Play(frame.Attributes.Icon, static_cast<MotionPlayFlags>(static_cast<std::uint32_t>(control.Flags) | flags));
			slots.RecordCommandSelection();
			static_cast<void>(AssignMotionChildInitialTick(commands, *parameters, layer, frame, control));
			if (commands.SelectionSerial() != previous_selection) {
				retire_selected_tree();
			}
		}

		return MotionChildFrameSelection::BindRoot;
	}

} // namespace sgre::scene
