#include "sgre/scene/motion_timeline_variables.hpp"

#include "sgre/scene/motion_clone_resource.hpp"

#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_player_update.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {
	static constexpr std::size_t kMaximumTimelineVariableTracks = 4096;
	static constexpr std::size_t kMaximumTimelineVariableFrames = 1000000;
	static constexpr std::size_t kMaximumTimelineVariableNameBytes = 8 * 1024 * 1024;

	static const resource::PsbValue& VariableField(const resource::PsbValue& object, std::string_view name) {
		const auto* value = object.Find(name);
		if (value == nullptr) {
			throw std::runtime_error("motion timeline variable requires field " + std::string(name));
		}

		return *value;
	}

	static float VariableNumber(const resource::PsbValue& value) {
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		const auto* real = std::get_if<double>(&value.Data);
		const auto* boolean = std::get_if<bool>(&value.Data);
		double number = std::numeric_limits<double>::quiet_NaN();
		if (integer != nullptr) {
			number = static_cast<double>(*integer);
		} else if (real != nullptr) {
			number = *real;
		} else if (boolean != nullptr) {
			number = static_cast<double>(*boolean);
		}
		if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
			throw std::runtime_error("motion timeline variable requires a finite native float");
		}

		return static_cast<float>(number);
	}

	static std::string_view VariableText(const resource::PsbValue& value) {
		const auto* text = std::get_if<std::string>(&value.Data);
		if (text == nullptr || text->size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion timeline variable requires a bounded actual label/scope");
		}

		return std::string_view(*text).substr(0, text->find('\0'));
	}

	static const resource::MotionTimeline& VariableTimeline(const ResolvedMotionTimeline& selection) {
		if (selection.Timeline == nullptr) {
			throw std::runtime_error("motion timeline variable preparation requires its actual resolved timeline");
		}

		return *selection.Timeline;
	}

	MotionTimelineVariables::MotionTimelineVariables(MotionPlayerCommands& commands, const ResolvedMotionTimeline& selection, std::uint64_t serial)
	    : m_Commands(commands), m_Timeline(VariableTimeline(selection)), m_Resource(commands.Manager().Resource(selection.Project)), m_Project(selection.Project), m_SelectionSerial(serial) {
		const auto& metadata = m_Timeline.Variables;
		if (std::holds_alternative<std::monostate>(metadata.Data) || (std::get_if<resource::PsbObject>(&metadata.Data) != nullptr && std::get<resource::PsbObject>(metadata.Data).empty())) {
			return;
		}
		const auto* tracks = std::get_if<resource::PsbArray>(&metadata.Data);
		if (tracks == nullptr || tracks->size() > kMaximumTimelineVariableTracks) {
			throw std::runtime_error("motion timeline variables require their bounded serialized array owner");
		}

		std::size_t frame_count = 0;
		std::size_t name_bytes = 0;
		m_Tracks.reserve(tracks->size());
		for (const auto& raw : *tracks) {
			const auto label = VariableText(VariableField(raw, "label"));
			const auto scope = VariableText(VariableField(raw, "scope"));
			if (label.size() > kMaximumMotionCommandNameBytes - scope.size() || (!scope.empty() && label.size() + scope.size() > kMaximumMotionCommandNameBytes - 2)) {
				throw std::runtime_error("motion timeline variable canonical name exceeds its actual key bound");
			}
			Track track;
			track.Name = scope.empty() ? std::string(label) : std::string(scope) + "::" + std::string(label);
			if (track.Name.size() + 1 > kMaximumTimelineVariableNameBytes - name_bytes) {
				throw std::runtime_error("motion timeline variable names exceed their aggregate owned-byte bound");
			}
			name_bytes += track.Name.size() + 1;
			const auto* frames = std::get_if<resource::PsbArray>(&VariableField(raw, "frameList").Data);
			if (frames == nullptr || frames->size() < 2 || frames->size() > kMaximumTimelineVariableFrames - frame_count) {
				throw std::runtime_error("motion timeline variable requires two bounded physical frame slots");
			}
			frame_count += frames->size();
			track.Frames.reserve(frames->size());
			for (const auto& raw_frame : *frames) {
				Frame frame;
				frame.Time = VariableNumber(VariableField(raw_frame, "time"));
				if (frame.Time < 0 || (track.Frames.empty() ? frame.Time != 0 : frame.Time <= track.Frames.back().Time)) {
					throw std::runtime_error("motion timeline variable times require a zero origin and increasing bounded keys");
				}
				const auto type = ReadMotionParameterRecordIndex(VariableField(raw_frame, "type"));
				if (type != 0 && type != 2 && type != 3) {
					throw std::runtime_error("motion timeline variable frame type requires recovered hidden/static/tween controls");
				}
				frame.Hidden = type == 0;
				frame.Tween = type == 3;
				if (!frame.Hidden) {
					const auto& content = VariableField(raw_frame, "content");
					frame.Value = VariableNumber(VariableField(content, "value"));
					frame.Interval = std::bit_cast<std::uint32_t>(ReadMotionParameterRecordIndex(VariableField(content, "interval")));
					if (const auto* easing = content.Find("easing"); easing != nullptr && !std::holds_alternative<std::monostate>(easing->Data)) {
						const auto index = std::bit_cast<std::uint32_t>(ReadMotionParameterRecordIndex(*easing));
						if (index >= m_Resource.Easing.size()) {
							throw std::runtime_error("motion timeline variable easing exceeds its actual resource owner");
						}
						frame.Easing = index;
					}
				}
				track.Frames.push_back(frame);
			}
			m_Tracks.push_back(std::move(track));
		}
	}

	MotionTimelineVariables::MotionTimelineVariables(MotionPlayerCommands& commands, const MotionTimelineVariables& source)
	    : m_Commands(commands), m_Timeline(source.m_Timeline), m_Resource(source.m_Resource), m_Project(ResolveMotionCloneProject(commands.Manager(), source.m_Commands.Manager(), source.m_Project)), m_SelectionSerial(source.m_SelectionSerial), m_Tracks(source.m_Tracks), m_Applied(source.m_Applied), m_AppliedPosition(source.m_AppliedPosition), m_AppliedParameterSerial(source.m_AppliedParameterSerial) {
		source.RequireLive();
	}

	bool MotionTimelineVariables::Uses(const MotionPlayerCommands& commands, const resource::MotionTimeline& timeline) const noexcept {
		return &commands == &m_Commands && &timeline == &m_Timeline && commands.SelectionSerial() == m_SelectionSerial;
	}

	bool MotionTimelineVariables::Empty() const noexcept {
		return m_Tracks.empty();
	}

	std::size_t MotionTimelineVariables::Count() const noexcept {
		return m_Tracks.size();
	}

	void MotionTimelineVariables::RequireLive() const {
		if (!Uses(m_Commands, m_Timeline) || m_Commands.CachedTimeline() != &m_Timeline || !m_Commands.Manager().Contains(m_Project) || &m_Commands.Manager().Resource(m_Project) != &m_Resource) {
			throw std::runtime_error("motion timeline variables require their exact live current-player/resource owner");
		}
	}

	const MotionTimelineVariableState& MotionTimelineVariables::State(std::size_t track) const {
		RequireLive();
		if (track >= m_Tracks.size()) {
			throw std::runtime_error("motion timeline variable state index exceeds its actual records");
		}

		return m_Tracks[track].State;
	}

	void MotionTimelineVariables::Decode(Track& track, std::size_t slot, std::size_t frame) {
		const auto& source = track.Frames[frame];
		auto& cell = track.State.Cells[slot];
		cell.FrameIndex = frame;
		cell.Time = source.Time;
		cell.Hidden = source.Hidden;
		if (!source.Hidden) {
			cell.Interval = source.Interval;
			cell.Tween = source.Tween;
			cell.Value = source.Value;
			cell.Easing = source.Easing;
		}
	}

	void MotionTimelineVariables::Select(MotionFrameTraversal traversal, float position) {
		RequireLive();
		m_Applied = false;
		if (!std::isfinite(position) || position < 0) {
			throw std::runtime_error("motion timeline variable cursor requires finite nonnegative actual time");
		}
		if (traversal == MotionFrameTraversal::ParameterOnly) {
			return;
		}
		for (auto& track : m_Tracks) {
			auto& state = track.State;
			if (traversal == MotionFrameTraversal::Seek) {
				std::size_t index = 0;
				for (std::size_t candidate = 0; candidate < track.Frames.size(); ++candidate) {
					if (track.Frames[candidate].Time >= position) {
						index = std::min(track.Frames[candidate].Time == position ? candidate : candidate - 1, track.Frames.size() - 2);
						break;
					}
				}
				// Native511640 retains its initial zero if time is beyond every variable key.
				state.CurrentSlot = 0;
				Decode(track, 0, index);
				Decode(track, 1, index + 1);
				state.Initialized = true;
				continue;
			}
			if (!state.Initialized || (traversal != MotionFrameTraversal::Forward && traversal != MotionFrameTraversal::Reverse)) {
				throw std::runtime_error("motion timeline variable cursor requires its genuine initial seek");
			}
			std::array<std::optional<std::size_t>, 2> pending;
			if (traversal == MotionFrameTraversal::Forward) {
				while (state.Cells[state.CurrentSlot].FrameIndex < track.Frames.size() - 2 && position >= state.Cells[1 - state.CurrentSlot].Time) {
					state.CurrentSlot = 1 - state.CurrentSlot;
					const auto next = state.Cells[state.CurrentSlot].FrameIndex + 1;
					auto& cell = state.Cells[1 - state.CurrentSlot];
					cell.FrameIndex = next;
					cell.Time = track.Frames[next].Time;
					pending[1 - state.CurrentSlot] = next;
				}
			} else {
				while (position < state.Cells[state.CurrentSlot].Time) {
					const auto previous = state.Cells[state.CurrentSlot].FrameIndex;
					if (previous == 0) {
						throw std::runtime_error("motion timeline variable reverse cursor precedes its origin");
					}
					state.CurrentSlot = 1 - state.CurrentSlot;
					auto& cell = state.Cells[state.CurrentSlot];
					cell.FrameIndex = previous - 1;
					cell.Time = track.Frames[previous - 1].Time;
					pending[state.CurrentSlot] = previous - 1;
				}
			}
			for (std::size_t slot = 0; slot < pending.size(); ++slot) {
				if (pending[slot]) {
					Decode(track, slot, *pending[slot]);
				}
			}
		}
	}

	void MotionTimelineVariables::Apply(float position) {
		RequireLive();
		m_Applied = false;
		if (!std::isfinite(position) || position < 0) {
			throw std::runtime_error("motion timeline variable application requires finite nonnegative time");
		}
		for (auto& track : m_Tracks) {
			auto& state = track.State;
			const auto& current = state.Cells[state.CurrentSlot];
			const auto& following = state.Cells[1 - state.CurrentSlot];
			if (current.Hidden) {
				continue;
			}
			if (!state.Initialized) {
				throw std::runtime_error("motion timeline variable application lacks its actual decoded slots");
			}
			float value = current.Value;
			if (current.Tween && !following.Hidden && current.Value != following.Value) {
				float elapsed = position - current.Time;
				if (current.Interval != 0) {
					const float steps = elapsed / static_cast<float>(current.Interval);
					if (!(steps >= -2147483648.0f && steps < 2147483648.0f)) {
						throw std::runtime_error("motion timeline variable interval exceeds native truncating arithmetic");
					}
					const auto quantized = static_cast<std::uint32_t>(static_cast<std::int32_t>(steps)) * current.Interval;
					elapsed = static_cast<float>(static_cast<std::uint64_t>(quantized));
				}
				float ratio = elapsed / (following.Time - current.Time);
				if (current.Easing) {
					ratio = resource::EvaluateEasing(m_Resource.Easing[*current.Easing], ratio);
				}
				value = (1.0f - ratio) * current.Value + ratio * following.Value;
			}
			if (!std::isfinite(value)) {
				throw std::runtime_error("motion timeline variable numeric result is nonfinite");
			}
			state.Value = value;
			// Actual50e530 calls51f4f0 flags0, before numeric evaluation but after all player cursors.
			m_Commands.ParameterValues().WriteLocal(track.Name, value, 0);
		}
		const auto* parameters = m_Commands.ParameterValues().CurrentTimelineParameters(m_Timeline);
		m_AppliedParameterSerial = parameters == nullptr ? 0 : parameters->ValueSerial();
		m_AppliedPosition = position;
		m_Applied = true;
	}

	std::uint64_t MotionTimelineVariables::RequireApplied(float position) const {
		RequireLive();
		const auto* parameters = m_Commands.ParameterValues().CurrentTimelineParameters(m_Timeline);
		const auto serial = parameters == nullptr ? 0 : parameters->ValueSerial();
		if (!m_Applied || m_AppliedPosition != position || serial != m_AppliedParameterSerial) {
			throw std::runtime_error("motion variable numeric phase requires its exact applied player clock and current parameter receipt");
		}

		return serial;
	}
} // namespace sgre::scene
