#include "sgre/scene/motion_child_initial_timing.hpp"

#include "sgre/scene/motion_layer_parameters.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static bool AssignInitialTick(MotionPlayerCommands& child, float parent_position, float parent_delta, bool parent_tick_assigned, const PreparedMotionFrame& current, const MotionChildFrameControl& control, std::size_t maximum_loop_wraps) {
		if (!child.ClockState().Playing || !child.ClockState().TickAssigned) {
			return false;
		}
		if (current.Hidden || current.Attributes.Source.empty() || current.Attributes.Source.front() == '\0') {
			throw std::runtime_error("motion child initial tick requires an actual present selected character frame");
		}
		if (maximum_loop_wraps == 0 || maximum_loop_wraps > 65536 || !std::isfinite(parent_position) || !std::isfinite(parent_delta) || !std::isfinite(current.Time) || !std::isfinite(control.TimeOffset)) {
			throw std::runtime_error("motion child initial tick requires actual finite selected timing and bounded wraps");
		}

		auto tick = (parent_position - current.Time) + control.TimeOffset;
		const auto& state = child.ClockState();
		if (!std::isfinite(tick) || !std::isfinite(state.LastTime) || !std::isfinite(state.LoopTime)) {
			throw std::runtime_error("motion child initial tick lies outside its finite domain");
		}
		if (parent_delta < 0 && state.LoopTime >= 0) {
			std::size_t wraps = 0;
			while (tick >= state.LastTime) {
				const auto next = (tick - state.LastTime) + state.LoopTime;
				if (++wraps > maximum_loop_wraps || !std::isfinite(next) || next >= tick) {
					throw std::runtime_error("motion child initial reverse tick requires a bounded progressing loop span");
				}

				tick = next;
			}
		}

		child.SetTickCount(tick);
		if (!parent_tick_assigned) {
			child.RequestInitialBoundaryTraversal();
		}

		return true;
	}

	bool AssignMotionChildInitialTick(MotionPlayerCommands& child, const MotionTimelineClockState& parent, const PreparedMotionFrame& current, const MotionChildFrameControl& control, std::size_t maximum_loop_wraps) {
		return AssignInitialTick(child, parent.TickCount, parent.DeltaTicks, parent.TickAssigned, current, control, maximum_loop_wraps);
	}

	bool AssignMotionChildInitialTick(MotionPlayerCommands& child, const MotionLayerParameterBindings& parameters, std::uint32_t layer, const PreparedMotionFrame& current, const MotionChildFrameControl& control, std::size_t maximum_loop_wraps) {
		const auto& parent = parameters.Commands().ClockState();
		const auto& raw = parameters.Layer(layer);
		if (raw.Type != 3 || raw.Frames.empty()) {
			throw std::runtime_error("motion bound child initial time requires its actual selected kind3 record");
		}

		const auto position = parameters.Parameter(layer) == nullptr ? parent.TickCount : parameters.Position(layer);

		return AssignInitialTick(child, position, parent.DeltaTicks, parent.TickAssigned, current, control, maximum_loop_wraps);
	}

} // namespace sgre::scene
