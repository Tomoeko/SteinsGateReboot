#include "sgre/scene/motion_player_update.hpp"

#include "sgre/scene/motion_parameter_values.hpp"
#include "sgre/scene/motion_timeline_variables.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace sgre::scene {

	static std::string_view NativeEventString(std::string_view value) {
		return value.substr(0, value.find('\0'));
	}

	static const resource::PsbValue& RequireTagField(const resource::PsbValue& content, std::string_view name) {
		const auto* value = content.Find(name);
		if (value == nullptr) {
			throw std::runtime_error("motion tag requires content field " + std::string(name));
		}

		return *value;
	}

	static bool ReadTagBoolean(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return *integer != 0;
		}

		if (const auto* real = std::get_if<double>(&value.Data)) {
			return *real != 0;
		}

		throw std::runtime_error("motion tag boolean conversion requires a boolean or number");
	}

	static void RequireNestedPlayerPhase(const std::vector<resource::MotionLayer>& layers, bool has_particle_phase, std::size_t depth = 0) {
		if (depth > 128) {
			throw std::runtime_error("motion update layer depth exceeds its bound");
		}

		for (const auto& layer : layers) {
			if (layer.Type == 4 && !has_particle_phase) {
				throw std::runtime_error("motion update requires the unrecovered nested player phase");
			}

			RequireNestedPlayerPhase(layer.Children, has_particle_phase, depth + 1);
		}
	}

	MotionPlayerUpdate::MotionPlayerUpdate(MotionPlayerCommands& commands, MotionPlayerUpdateServices services, MotionPlayerUpdateLimits limits)
	    : m_Commands(commands), m_Services(std::move(services)), m_Limits(limits) {
		if (!m_Services.ReadFrameDelta || !m_Services.PrepareFrames || !m_Services.ComposeLayers || !m_Services.PublishDrawPackets || !m_Services.Draw || !m_Services.DeliverEvent) {
			throw std::runtime_error("motion player update requires actual manager, frame, composition, draw publication, draw and event services");
		}

		if (limits.MaxPreparedTimelines == 0 || limits.MaxPreparedTimelines > 256 || limits.MaxTags == 0 || limits.MaxTags > 65536 || limits.MaxPendingEvents == 0 || limits.MaxPendingEvents > 4096 || limits.MaxLoopWraps == 0 || limits.MaxLoopWraps > 65536) {
			throw std::runtime_error("motion player update limits exceed their bounded scope");
		}

		m_Prepared.reserve(limits.MaxPreparedTimelines);
		m_PendingEvents.reserve(limits.MaxPendingEvents);
		m_DeliveringEvents.reserve(limits.MaxPendingEvents);
		m_PendingPreparationOwners.reserve(kMaximumMotionPreparationOwners);
		m_DeliveringPreparationOwners.reserve(kMaximumMotionPreparationOwners);
	}

	void MotionPlayerUpdate::PrepareTimeline(const resource::MotionTimeline& timeline) {
		PrepareTimeline(timeline, {}, false);
	}

	void MotionPlayerUpdate::PrepareCurrentTimeline(const resource::MotionTimeline& timeline, MotionPreparationLease owner) {
		PrepareTimeline(timeline, std::move(owner), true);
	}

	void MotionPlayerUpdate::PrepareTimeline(const resource::MotionTimeline& timeline, MotionPreparationLease owner, bool current_only) {
		if (m_Updating || m_Delivering) {
			throw std::runtime_error("motion tag preparation is forbidden during update or event delivery");
		}

		if (!owner.IsEmpty() && !owner.Owns(m_Commands.Manager(), timeline)) {
			throw std::runtime_error("motion current tag preparation requires its exact manager/timeline lease");
		}
		if (!current_only) {
			for (const auto& prepared : m_Prepared) {
				if (prepared.Timeline == &timeline) {
					return;
				}
			}
		}

		if ((!current_only && m_Prepared.size() == m_Limits.MaxPreparedTimelines) || timeline.Tags.size() > m_Limits.MaxTags - (current_only ? 0 : m_PreparedTagCount)) {
			throw std::runtime_error("motion tag preparation exceeds timeline or aggregate tag limits");
		}

		if (timeline.Kind != resource::MotionTimelineKind::Frames || !std::isfinite(timeline.LastTime) || !std::isfinite(timeline.LoopTime) || timeline.LastTime < 0 || timeline.LoopTime < -1 || (timeline.LoopTime >= 0 && timeline.LoopTime >= timeline.LastTime)) {
			throw std::runtime_error("motion update requires an ordinary finite timeline with a positive loop span");
		}

		if (!std::holds_alternative<std::monostate>(timeline.Stream.Data)) {
			throw std::runtime_error("motion update requires unrecovered packed stream playback");
		}
		if (!std::holds_alternative<std::monostate>(timeline.Parameterization.Data)) {
			const auto ranges = ReadMotionParameterRanges(timeline);
			const auto index = ReadMotionParameterRecordIndex(timeline.Parameterization);
			if (index < 0 || static_cast<std::size_t>(index) >= ranges.size()) {
				throw std::runtime_error("motion update timeline parameter exceeds its actual ordered parameter definitions");
			}
		}

		if (timeline.Priority.size() < 2 || timeline.Priority.size() > 1000000 || timeline.Priority.front().Time != 0 || timeline.Priority.back().Time < timeline.LastTime) {
			throw std::runtime_error("motion update priority requires an origin and terminal frame within bounded preparation");
		}

		RequireNestedPlayerPhase(timeline.Layers, m_HasParticlePhase);
		PreparedTimeline prepared;
		prepared.Timeline = &timeline;
		prepared.Owner = std::move(owner);
		if (!timeline.Tags.empty()) {
			if (timeline.Tags.size() < 2 || timeline.Tags.front().Time != 0 || timeline.Tags.back().Time < timeline.LastTime) {
				throw std::runtime_error("motion tag track requires zero origin and a terminal frame at or beyond lastTime");
			}

			prepared.Tags.reserve(timeline.Tags.size());
			for (const auto& frame : timeline.Tags) {
				if (!std::isfinite(frame.Time) || frame.Time < 0 || (!prepared.Tags.empty() && frame.Time < prepared.Tags.back().Time)) {
					throw std::runtime_error("motion tag track time is nonfinite or out of order");
				}

				PreparedTag tag;
				tag.Time = frame.Time;
				tag.Type = frame.Type;
				if (frame.Type == 1) {
					tag.Align = ReadTagBoolean(RequireTagField(frame.Content, "align"));
					tag.Sync = ReadTagBoolean(RequireTagField(frame.Content, "sync"));
					const auto* action = std::get_if<std::string>(&RequireTagField(frame.Content, "action").Data);
					if (action == nullptr || action->size() > kMaximumMotionCommandNameBytes) {
						throw std::runtime_error("motion tag action requires a bounded string");
					}

					tag.Action = NativeEventString(*action);
				}

				prepared.Tags.push_back(tag);
			}
		}

		if (current_only) {
			m_Prepared.clear();
			m_PreparedTagCount = 0;
		}
		m_PreparedTagCount += prepared.Tags.size();
		m_Prepared.push_back(std::move(prepared));
	}

	const MotionPlayerUpdate::PreparedTimeline* MotionPlayerUpdate::FindPreparedTimeline() const {
		if (m_Commands.m_CachedTimeline == nullptr) {
			return nullptr;
		}

		for (const auto& prepared : m_Prepared) {
			if (prepared.Timeline == m_Commands.m_CachedTimeline) {
				return &prepared;
			}
		}

		throw std::runtime_error("selected motion timeline must be prepared before update");
	}

	MotionPlayerFrameContext MotionPlayerUpdate::Context() const {
		MotionPlayerFrameContext context;
		context.Timeline = m_Commands.m_CachedTimeline;
		context.Clock = m_Commands.ClockState();

		return context;
	}

	bool MotionPlayerUpdate::IsBlocked() const noexcept {
		const auto& state = m_Commands.ClockState();

		return state.SyncWaiting || state.AlignedThisUpdate;
	}

	void MotionPlayerUpdate::EnqueueEvent(MotionPlayerEvent event) {
		if (m_PendingEvents.size() == m_Limits.MaxPendingEvents) {
			throw std::runtime_error("motion pending event count exceeds its bound");
		}

		for (const auto& prepared : m_Prepared) {
			if (prepared.Timeline == m_Commands.CachedTimeline()) {
				RetainEventPreparation(prepared.Owner);
				break;
			}
		}
		m_PendingEvents.push_back(event);
	}

	void MotionPlayerUpdate::RetainEventPreparation(const MotionPreparationLease& owner) {
		if (owner.IsEmpty() || std::any_of(m_PendingPreparationOwners.begin(), m_PendingPreparationOwners.end(), [&](const auto& retained) {
			    return retained.SameOwner(owner);
		    })) {
			return;
		}
		if (m_PendingPreparationOwners.size() == kMaximumMotionPreparationOwners) {
			throw std::runtime_error("motion queued raw-string events exceed their bounded live preparation owners");
		}
		m_PendingPreparationOwners.push_back(owner);
	}

	void MotionPlayerUpdate::EnqueueAction(std::string_view layer, std::string_view action) {
		if (layer.size() > kMaximumMotionCommandNameBytes || action.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion event string exceeds its byte bound");
		}

		MotionPlayerEvent event;
		event.Layer = NativeEventString(layer);
		event.Action = NativeEventString(action);
		EnqueueEvent(event);
	}

	void MotionPlayerUpdate::ProcessTag(const PreparedTag& tag, bool seek) {
		if (tag.Type != 1) {
			return;
		}

		auto& state = m_Commands.m_Clock.m_State;
		if (tag.Align && (!seek || state.EvaluationTime == tag.Time)) {
			state.TickCount = tag.Time;
			state.EvaluationTime = tag.Time;
			state.AlignedThisUpdate = true;
		}

		if (tag.Sync) {
			state.TickCount = tag.Time;
			state.EvaluationTime = tag.Time;
			state.SyncWaiting = true;
			state.Playing = true;
			MotionPlayerEvent event;
			event.Kind = MotionPlayerEventKind::Sync;
			EnqueueEvent(event);
		}

		if (!tag.Action.empty()) {
			EnqueueAction({}, tag.Action);
		}
	}

	void MotionPlayerUpdate::Traverse(MotionFrameTraversal traversal) {
		const auto* prepared = FindPreparedTimeline();
		if (prepared == nullptr) {
			throw std::runtime_error("motion traversal requires a cached ordinary timeline");
		}

		const auto& tags = prepared->Tags;
		const auto& state = m_Commands.ClockState();
		if (!tags.empty() && traversal != MotionFrameTraversal::ParameterOnly) {
			if (traversal == MotionFrameTraversal::Seek) {
				bool located = false;
				for (std::size_t index = 0; index < tags.size(); ++index) {
					if (tags[index].Time == state.EvaluationTime || tags[index].Time > state.EvaluationTime) {
						m_TagIndex = tags[index].Time == state.EvaluationTime ? index : index - 1;
						located = true;
						break;
					}
				}

				if (!located) {
					throw std::runtime_error("motion tag seek is beyond its prepared terminal domain");
				}

				if (m_TagIndex > tags.size() - 2) {
					m_TagIndex = tags.size() - 2;
				}

				ProcessTag(tags[m_TagIndex], true);
			} else if (traversal == MotionFrameTraversal::Forward) {
				while (m_TagIndex < tags.size() - 2 && state.EvaluationTime >= tags[m_TagIndex + 1].Time) {
					++m_TagIndex;
					ProcessTag(tags[m_TagIndex], false);
				}
			} else {
				while (tags[m_TagIndex].Time > state.EvaluationTime) {
					if (m_TagIndex == 0) {
						throw std::runtime_error("motion reverse tags are before their prepared origin");
					}

					--m_TagIndex;
					ProcessTag(tags[m_TagIndex], false);
				}
			}
		}

		if (m_Commands.m_TimelineVariables) {
			m_Commands.m_TimelineVariables->Select(traversal, state.EvaluationTime);
		}
		m_Services.PrepareFrames(traversal, Context());
	}

	void MotionPlayerUpdate::WrapForward() {
		auto& state = m_Commands.m_Clock.m_State;
		state.EvaluationTime = state.LastTime;
		Traverse(MotionFrameTraversal::Forward);
		if (IsBlocked()) {
			return;
		}

		state.EvaluationTime = state.LoopTime;
		Traverse(MotionFrameTraversal::Seek);
		if (IsBlocked()) {
			return;
		}

		std::size_t wraps = 0;
		while (state.TickCount >= state.LastTime) {
			const auto next = (state.TickCount - state.LastTime) + state.LoopTime;
			if (++wraps > m_Limits.MaxLoopWraps || !std::isfinite(next) || next >= state.TickCount) {
				throw std::runtime_error("motion forward loop exceeds its bounded progressing span");
			}

			state.TickCount = next;
		}

		state.EvaluationTime = state.TickCount;
		Traverse(MotionFrameTraversal::Forward);
	}

	void MotionPlayerUpdate::WrapReverse() {
		auto& state = m_Commands.m_Clock.m_State;
		state.EvaluationTime = state.LoopTime;
		Traverse(MotionFrameTraversal::Reverse);
		if (IsBlocked()) {
			return;
		}

		state.EvaluationTime = state.LastTime;
		Traverse(MotionFrameTraversal::Seek);
		if (IsBlocked()) {
			return;
		}

		std::size_t wraps = 0;
		while (state.TickCount < state.LoopTime) {
			const auto next = (state.TickCount - state.LoopTime) + state.LastTime;
			if (++wraps > m_Limits.MaxLoopWraps || !std::isfinite(next) || next <= state.TickCount) {
				throw std::runtime_error("motion reverse loop exceeds its bounded progressing span");
			}

			state.TickCount = next;
		}

		state.EvaluationTime = state.TickCount;
		Traverse(MotionFrameTraversal::Reverse);
	}

	void MotionPlayerUpdate::AdvanceParameterizedTimeline(const MotionParameterCell& parameter) {
		if (!std::isfinite(parameter.Position)) {
			throw std::runtime_error("motion timeline parameter position lies outside its finite supported domain");
		}

		auto& state = m_Commands.m_Clock.m_State;
		if (state.InitialTagDispatchPending) {
			state.TickCount = parameter.Position;
			state.EvaluationTime = parameter.Position;
			state.InitialTagDispatchPending = false;
			Traverse(MotionFrameTraversal::Seek);

			return;
		}

		if (parameter.Position > state.EvaluationTime) {
			state.TickCount = parameter.Position;
			state.EvaluationTime = parameter.Position;
			Traverse(MotionFrameTraversal::Forward);
		} else if (parameter.Position < state.EvaluationTime) {
			state.TickCount = parameter.Position;
			state.EvaluationTime = parameter.Position;
			Traverse(MotionFrameTraversal::Reverse);
		} else {
			// Native5200bc preserves both clock words when the live parameter has not advanced.
			Traverse(MotionFrameTraversal::ParameterOnly);
		}
	}

	void MotionPlayerUpdate::AdvanceTimeline() {
		auto& state = m_Commands.m_Clock.m_State;
		const auto* parameters = m_Commands.m_CachedTimeline == nullptr ? nullptr : m_Commands.ParameterValues().CurrentTimelineParameters(*m_Commands.m_CachedTimeline);
		if (parameters != nullptr) {
			const auto* parameter = parameters->TimelineParameter();
			if (parameter != nullptr) {
				// The bound scalar branch precedes ordinary playing/sync gates in native51ffd0.
				AdvanceParameterizedTimeline(*parameter);

				return;
			}
		}

		if (!state.InitialTagDispatchPending && !state.Playing) {
			if (parameters != nullptr && !parameters->Entries().empty()) {
				Traverse(MotionFrameTraversal::ParameterOnly);
			}

			return;
		}
		if (state.SyncWaiting) {
			return;
		}

		if (state.InitialTagDispatchPending) {
			state.InitialTagDispatchPending = false;
			if (state.DeltaTicks < 0 && state.TickCount == 0) {
				state.TickCount = state.LastTime;
				state.EvaluationTime = state.LastTime;
			}

			TraverseInitialPosition();
			if (IsBlocked()) {
				return;
			}
		}

		const auto tick_assigned = state.TickAssigned;
		if (!tick_assigned) {
			state.TickCount += state.DeltaTicks;
			if (!std::isfinite(state.TickCount)) {
				throw std::runtime_error("motion tick accumulation is nonfinite");
			}

			state.EvaluationTime = state.LastTime > state.TickCount ? state.TickCount : state.LastTime;
		}

		if (state.DeltaTicks < 0) {
			if (state.TickCount < 0 || state.TickCount < state.LoopTime) {
				if (state.LoopTime >= 0) {
					WrapReverse();
				} else {
					state.TickCount = 0;
					state.EvaluationTime = 0;
					state.Playing = false;
					if (!tick_assigned) {
						Traverse(MotionFrameTraversal::Reverse);
					}
				}
			} else if (!tick_assigned) {
				Traverse(MotionFrameTraversal::Reverse);
			}
		} else if (state.TickCount >= state.LastTime) {
			state.EvaluationTime = state.LastTime;
			if (state.LoopTime >= 0) {
				WrapForward();
			} else {
				state.Playing = false;
				if (!tick_assigned) {
					Traverse(MotionFrameTraversal::Forward);
				}
			}
		} else if (!tick_assigned) {
			Traverse(MotionFrameTraversal::Forward);
		}
	}

	void MotionPlayerUpdate::TraverseInitialPosition() {
		auto& state = m_Commands.m_Clock.m_State;
		if (!state.InitialBoundaryTraversalPending) {
			Traverse(MotionFrameTraversal::Seek);

			return;
		}

		state.InitialBoundaryTraversalPending = false;
		if (state.DeltaTicks < 0 && state.TickCount >= state.LastTime) {
			Traverse(MotionFrameTraversal::Seek);

			return;
		}

		const auto assigned_evaluation = state.EvaluationTime;
		state.EvaluationTime = state.DeltaTicks < 0 ? state.LastTime : 0;
		Traverse(MotionFrameTraversal::Seek);
		if (IsBlocked()) {
			return;
		}

		state.EvaluationTime = assigned_evaluation;
		Traverse(state.DeltaTicks < 0 ? MotionFrameTraversal::Reverse : MotionFrameTraversal::Forward);
	}

	void MotionPlayerUpdate::Update() {
		RunUpdate(std::nullopt);
	}

	void MotionPlayerUpdate::Progress(float elapsed_ticks) {
		RunUpdate(elapsed_ticks);
	}

	void MotionPlayerUpdate::RunUpdate(std::optional<float> explicit_delta) {
		if (m_Updating || m_Delivering) {
			throw std::runtime_error("recursive motion player update is unsupported");
		}

		m_Updating = true;
		try {
			const auto elapsed_ticks = explicit_delta ? *explicit_delta : m_Services.ReadFrameDelta();
			if (!std::isfinite(elapsed_ticks) || !std::isfinite(m_Commands.ClockState().Speed)) {
				throw std::runtime_error("motion update requires finite frame delta and speed");
			}

			if (m_Commands.ProcessDelayedPlay().SkipRemainingUpdate) {
				m_Updating = false;

				return;
			}

			AdvanceAndCompose(elapsed_ticks);
			m_Services.PublishDrawPackets(Context());
			m_Updating = false;
		} catch (...) {
			m_Updating = false;
			throw;
		}

		PostUpdate();
	}

	void MotionPlayerUpdate::AdvanceAndCompose(float elapsed_ticks) {
		if (!std::isfinite(elapsed_ticks) || !std::isfinite(m_Commands.ClockState().Speed)) {
			throw std::runtime_error("motion update requires finite frame delta and speed");
		}

		static_cast<void>(FindPreparedTimeline());
		auto& state = m_Commands.m_Clock.m_State;
		state.DeltaTicks = elapsed_ticks * state.Speed;
		state.AlignedThisUpdate = false;
		if (!std::isfinite(state.DeltaTicks) || !std::isfinite(state.TickCount) || !std::isfinite(state.EvaluationTime)) {
			throw std::runtime_error("motion update clock lies outside its finite domain");
		}

		AdvanceTimeline();
		if (m_Commands.m_TimelineVariables) {
			m_Commands.m_TimelineVariables->Apply(state.EvaluationTime);
		}
		m_Services.ComposeLayers(Context());
		state.TickAssigned = false;
		state.InitialCompositionPending = false;
	}

	void MotionPlayerUpdate::UpdateChild(float parent_delta_ticks) {
		if (m_Updating || m_Delivering) {
			throw std::runtime_error("recursive motion child update is unsupported");
		}

		m_Updating = true;
		try {
			AdvanceAndCompose(parent_delta_ticks);
			m_Updating = false;
		} catch (...) {
			m_Updating = false;
			throw;
		}
	}

	void MotionPlayerUpdate::PrependChildEvents(MotionPlayerUpdate& child) {
		if (&child == this || m_Delivering || child.m_Updating || child.m_Delivering) {
			throw std::runtime_error("motion child event transfer requires distinct idle child and a parent outside delivery");
		}

		if (child.m_PendingEvents.size() > m_Limits.MaxPendingEvents - m_PendingEvents.size()) {
			throw std::runtime_error("motion child events exceed the parent's bounded queue");
		}

		std::size_t extra = 0;
		for (const auto& owner : child.m_PendingPreparationOwners) {
			if (std::none_of(m_PendingPreparationOwners.begin(), m_PendingPreparationOwners.end(), [&](const auto& retained) {
				    return retained.SameOwner(owner);
			    })) {
				++extra;
			}
		}
		if (extra > kMaximumMotionPreparationOwners - m_PendingPreparationOwners.size()) {
			throw std::runtime_error("motion child event transfer exceeds its bounded live preparation owners");
		}
		for (const auto& owner : child.m_PendingPreparationOwners) {
			RetainEventPreparation(owner);
		}
		m_PendingEvents.insert(m_PendingEvents.begin(), child.m_PendingEvents.begin(), child.m_PendingEvents.end());
		child.m_PendingEvents.clear();
		child.m_PendingPreparationOwners.clear();
	}

	void MotionPlayerUpdate::PostUpdate() {
		if (m_Delivering || m_Updating) {
			throw std::runtime_error("recursive motion event delivery is unsupported");
		}

		if (m_PendingEvents.empty()) {
			return;
		}

		m_PendingEvents.swap(m_DeliveringEvents);
		m_PendingPreparationOwners.swap(m_DeliveringPreparationOwners);
		m_Delivering = true;
		try {
			for (const auto& event : m_DeliveringEvents) {
				m_Services.DeliverEvent(event);
			}
		} catch (...) {
			m_DeliveringEvents.clear();
			m_DeliveringPreparationOwners.clear();
			m_Delivering = false;
			throw;
		}

		m_DeliveringEvents.clear();
		m_DeliveringPreparationOwners.clear();
		m_Delivering = false;
	}

	void MotionPlayerUpdate::Draw() {
		m_Services.Draw();
	}

	std::size_t MotionPlayerUpdate::PendingEventCount() const noexcept {
		return m_PendingEvents.size();
	}

	std::size_t MotionPlayerUpdate::TagIndex() const noexcept {
		return m_TagIndex;
	}

} // namespace sgre::scene
