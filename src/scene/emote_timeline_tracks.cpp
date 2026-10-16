#include "sgre/scene/emote_timeline_tracks.hpp"

#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {
	static constexpr std::size_t kMaximumTimelineTracks = 65536;
	static constexpr std::size_t kMaximumTimelineKeys = 1000000;
	static constexpr std::size_t kMaximumTimelineNameBytes = 8 * 1024 * 1024;

	static const resource::PsbValue& TrackField(const resource::PsbValue& value, std::string_view name) {
		if (const auto* field = value.Find(name)) {
			return *field;
		}
		throw std::runtime_error("Emote timeline track requires actual metadata field " + std::string(name));
	}

	static const resource::PsbArray& TrackArray(const resource::PsbValue& value, std::string_view context) {
		const auto* array = std::get_if<resource::PsbArray>(&value.Data);
		if (array == nullptr) {
			throw std::runtime_error("Emote timeline track requires actual array " + std::string(context));
		}
		return *array;
	}

	static float TrackNumber(const resource::PsbValue& value, std::string_view context) {
		float number = std::numeric_limits<float>::quiet_NaN();
		if (const auto* real = std::get_if<double>(&value.Data)) {
			if (!std::isfinite(*real) || *real < -std::numeric_limits<float>::max() || *real > std::numeric_limits<float>::max()) {
				throw std::runtime_error("Emote timeline track requires finite numeric " + std::string(context));
			}
			number = static_cast<float>(*real);
		} else if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			number = static_cast<float>(*integer);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			number = *boolean ? 1.0f : 0.0f;
		}
		if (!std::isfinite(number)) {
			throw std::runtime_error("Emote timeline track requires finite numeric " + std::string(context));
		}
		return number;
	}

	static bool HiddenTrackKey(const resource::PsbValue& value) {
		// Native4ca8a0 converts the actual scalar tag to a signed word before comparison with zero.
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			if (*integer < std::numeric_limits<std::int32_t>::min() || *integer > std::numeric_limits<std::int32_t>::max()) {
				throw std::runtime_error("Emote timeline frame type exceeds the native bounded word conversion");
			}
			return *integer == 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data)) {
			// Native4ca8e0 truncates a double directly; rounding it through float changes values just below one.
			if (!std::isfinite(*real) || *real < -2147483648.0 || !(*real < 2147483648.0)) {
				throw std::runtime_error("Emote timeline frame type exceeds the native bounded word conversion");
			}
			return static_cast<std::int32_t>(*real) == 0;
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return !*boolean;
		}
		throw std::runtime_error("Emote timeline track requires native numeric frame type");
	}

	static EmoteTimelineTrackDefinition PrepareTracks(const resource::PsbValue& actual, const EmoteVariableControls& controls) {
		EmoteTimelineTrackDefinition result;
		result.LoopBegin = TrackNumber(TrackField(actual, "loopBegin"), "loopBegin");
		result.LoopEnd = TrackNumber(TrackField(actual, "loopEnd"), "loopEnd");
		result.LastTime = TrackNumber(TrackField(actual, "lastTime"), "lastTime");
		const auto& variables = TrackArray(TrackField(actual, "variableList"), "variableList");
		if (variables.size() > kMaximumTimelineTracks) {
			throw std::runtime_error("Emote timeline exceeds bounded actual track count");
		}
		std::size_t key_count = 0;
		std::size_t name_bytes = 0;
		float maximum_time = 0;
		result.Tracks.reserve(variables.size());
		for (const auto& variable : variables) {
			const auto* name = std::get_if<std::string>(&TrackField(variable, "label").Data);
			if (name == nullptr || name->size() > kMaximumMotionCommandNameBytes || name->size() >= kMaximumTimelineNameBytes - name_bytes) {
				throw std::runtime_error("Emote timeline track name exceeds its native bounded string domain");
			}
			name_bytes += name->size() + 1;
			EmoteTimelineTrack track;
			track.Name = name->substr(0, name->find('\0'));
			track.Instant = controls.IsInstantVariable(track.Name);
			const auto& frames = TrackArray(TrackField(variable, "frameList"), "frameList");
			if (frames.empty() || frames.size() >= kMaximumTimelineKeys - key_count) {
				throw std::runtime_error("Emote timeline requires nonempty bounded actual key tracks");
			}
			track.Keys.reserve(frames.size() + 1);
			float previous = 0;
			for (const auto& frame : frames) {
				EmoteTimelineKey key;
				key.Time = TrackNumber(TrackField(frame, "time"), "frame time");
				if (key.Time < previous) {
					throw std::runtime_error("Emote timeline requires ordered nonnegative actual key times");
				}
				previous = key.Time;
				maximum_time = std::max(key.Time, maximum_time);
				key.Hidden = HiddenTrackKey(TrackField(frame, "type"));
				if (!key.Hidden) {
					const auto& content = TrackField(frame, "content");
					key.Value = TrackNumber(TrackField(content, "value"), "frame value");
					key.Easing = TrackNumber(TrackField(content, "easing"), "frame easing");
				}
				track.Keys.push_back(key);
			}
			// Native2c4b8c suppresses equal consecutive interior values except the key owning loopBegin.
			for (std::size_t index = 1; index + 1 < track.Keys.size(); ++index) {
				auto& key = track.Keys[index];
				if (!track.Keys[index - 1].Hidden && !key.Hidden && track.Keys[index - 1].Value == key.Value && (result.LoopBegin < key.Time || !(track.Keys[index + 1].Time > result.LoopBegin))) {
					key.Hidden = true;
				}
			}
			if (track.Keys.front().Time > 0) {
				track.Keys.insert(track.Keys.begin(), EmoteTimelineKey{});
			}
			if (track.Keys.size() > 1 && !track.Keys.back().Hidden) {
				throw std::runtime_error("Emote timeline requires an actual hidden terminal key before following-key dispatch");
			}
			key_count += track.Keys.size();
			result.Tracks.push_back(std::move(track));
		}
		if (!(result.LastTime >= 0)) {
			result.LastTime = maximum_time;
		}
		return result;
	}

	EmoteTimelineTracks::EmoteTimelineTracks(EmoteVariableControls& controls, const resource::PsbValue& actual_definition, std::uint32_t flags)
	    : m_Controls(controls), m_Flags(flags) {
		if ((flags & ~std::uint32_t{7}) != 0) {
			throw std::runtime_error("Emote timeline track flags exceed recovered parallel/instant policy");
		}
		m_Definition = PrepareTracks(actual_definition, controls);
		m_Cursors.resize(m_Definition.Tracks.size());
		m_HasDifference.resize(m_Definition.Tracks.size());
		Reset(flags);
	}

	void EmoteTimelineTracks::Reset(std::uint32_t flags) {
		RequireOperation(0);
		if ((flags & ~std::uint32_t{7}) != 0) {
			throw std::runtime_error("Emote timeline reset requires recovered additive/instant playback policy");
		}
		if ((flags & 2) != 0) {
			m_Difference.resize(m_Definition.Tracks.size());
			m_DifferenceValues.resize(m_Definition.Tracks.size());
			for (std::size_t index = 0; index != m_Definition.Tracks.size(); ++index) {
				if (!m_Definition.Tracks[index].Instant) {
					m_Difference[index].Set(0);
					m_HasDifference[index] = true;
				}
			}
		}
		m_Flags = flags;
		m_Position = 0;
		m_Initialized = false;
		std::fill(m_Cursors.begin(), m_Cursors.end(), 0);
	}

	void EmoteTimelineTracks::RequireCanAssignState(const EmoteTimelineTracks& source) const {
		RequireOperation(m_Position);
		source.RequireOperation(source.m_Position);
		if (!source.m_Initialized || source.m_Definition.Tracks.size() != source.m_HasDifference.size()) {
			throw std::runtime_error("Emote timeline assignment requires its actual prepared source tracks");
		}
		if (!m_Definition.Tracks.empty() && source.m_Definition.Tracks.size() > kMaximumTimelineKeys / m_Definition.Tracks.size()) {
			throw std::runtime_error("Emote timeline assignment exceeds bounded exact-name matching work");
		}
	}

	void EmoteTimelineTracks::AssignState(const EmoteTimelineTracks& source, std::uint32_t flags) {
		RequireCanAssignState(source);
		if ((flags & ~std::uint32_t{7}) != 0) {
			throw std::runtime_error("Emote timeline assignment requires recovered native playback flags");
		}
		if (this == &source) {
			return;
		}
		// Allocate before resetting any retained state. A source transition can exist even after flags2 was cleared.
		auto next_difference = m_Difference;
		next_difference.resize(m_Definition.Tracks.size());
		auto next_present = m_HasDifference;
		if ((flags & 2) != 0) {
			for (std::size_t index = 0; index != m_Definition.Tracks.size(); ++index) {
				if (!m_Definition.Tracks[index].Instant) {
					next_difference[index].Set(0);
					next_present[index] = true;
				}
			}
		}
		for (std::size_t destination = 0; destination != m_Definition.Tracks.size(); ++destination) {
			for (std::size_t origin = 0; origin != source.m_Definition.Tracks.size(); ++origin) {
				if (m_Definition.Tracks[destination].Name != source.m_Definition.Tracks[origin].Name) {
					continue;
				}
				if (source.m_HasDifference[origin]) {
					next_difference[destination] = source.m_Difference[origin];
					next_present[destination] = true;
				}
				break;
			}
		}
		Reset(flags);
		Seek(source.m_Position, false);
		m_Difference.swap(next_difference);
		m_HasDifference.swap(next_present);
	}

	void EmoteTimelineTracks::AdvanceDifference(float elapsed_ticks) {
		RequireOperation(m_Position);
		if ((m_Flags & 2) != 0) {
			for (std::size_t index = 0; index != m_Definition.Tracks.size(); ++index) {
				if (!m_Definition.Tracks[index].Instant) {
					m_DifferenceValues[index] = m_Difference[index].Advance(elapsed_ticks);
				}
			}
		}
	}
	float EmoteTimelineTracks::ApplyDifference(std::string_view name, float base, float blend) const {
		RequireOperation(m_Position);
		if ((m_Flags & 2) != 0) {
			for (std::size_t index = 0; index != m_Definition.Tracks.size(); ++index) {
				const auto& track = m_Definition.Tracks[index];
				if (!track.Instant && track.Name == name) {
					const auto contribution = m_DifferenceValues[index] * blend;
					base += contribution;
				}
			}
		}
		return base;
	}

	void EmoteTimelineTracks::RequireOperation(float position) const {
		if (m_Operating || !m_Valid) {
			throw std::runtime_error("Emote timeline track phase is reentered or invalid after failed dispatch");
		}
		if (!std::isfinite(position) || position < 0) {
			throw std::runtime_error("Emote timeline track position must be finite and nonnegative");
		}
	}

	bool EmoteTimelineTracks::Skipped(std::size_t track) const noexcept {
		return (m_Flags & 4) != 0 && m_Definition.Tracks[track].Instant;
	}

	void EmoteTimelineTracks::Dispatch(std::size_t track, std::size_t key, float position) {
		const auto& selected = m_Definition.Tracks[track];
		if (key + 1 >= selected.Keys.size() || selected.Keys[key].Hidden) {
			throw std::runtime_error("Emote timeline dispatch requires its actual visible key and following boundary");
		}
		const float remaining = (selected.Keys[key + 1].Time - position) - 1.0f;
		const float duration = remaining >= 0 ? remaining : 0;
		if ((m_Flags & 2) != 0 && !selected.Instant) {
			m_Difference[track].Set(selected.Keys[key].Value, duration, selected.Keys[key].Easing);
		} else {
			m_Controls.Set(selected.Name, selected.Keys[key].Value, duration, selected.Keys[key].Easing);
		}
	}

	void EmoteTimelineTracks::Seek(float position, bool dispatch) {
		RequireOperation(position);
		m_Operating = true;
		try {
			for (std::size_t index = 0; index != m_Definition.Tracks.size(); ++index) {
				if (Skipped(index)) {
					continue;
				}
				const auto& keys = m_Definition.Tracks[index].Keys;
				std::size_t cursor = 0;
				std::optional<std::size_t> visible;
				while (cursor + 1 < keys.size()) {
					if (!keys[cursor].Hidden) {
						visible = cursor;
					}
					if (!(position < keys[cursor].Time) && keys[cursor + 1].Time > position) {
						break;
					}
					++cursor;
				}
				m_Cursors[index] = cursor;
				if (dispatch && visible) {
					Dispatch(index, *visible, position);
				}
			}
			m_Position = position;
			m_Initialized = true;
			m_Operating = false;
		} catch (...) {
			m_Operating = false;
			m_Valid = false;
			throw;
		}
	}

	void EmoteTimelineTracks::CrossTo(float position, bool inclusive) {
		RequireOperation(position);
		if (!m_Initialized || position < m_Position) {
			throw std::runtime_error("Emote timeline crossing requires a real seek and forward position; reverse owner remains unrecovered");
		}
		m_Operating = true;
		try {
			for (std::size_t index = 0; index != m_Definition.Tracks.size(); ++index) {
				if (Skipped(index)) {
					continue;
				}
				const auto& keys = m_Definition.Tracks[index].Keys;
				auto cursor = m_Cursors[index];
				while (cursor + 1 < keys.size() && (inclusive ? !(position < keys[cursor + 1].Time) : position > keys[cursor + 1].Time)) {
					++cursor;
					if (!keys[cursor].Hidden) {
						Dispatch(index, cursor, position);
					}
				}
				m_Cursors[index] = cursor;
			}
			m_Position = position;
			m_Operating = false;
		} catch (...) {
			m_Operating = false;
			m_Valid = false;
			throw;
		}
	}

	const EmoteTimelineTrackDefinition& EmoteTimelineTracks::Definition() const noexcept {
		return m_Definition;
	}
	std::span<const std::size_t> EmoteTimelineTracks::Cursors() const noexcept {
		return m_Cursors;
	}
	float EmoteTimelineTracks::Position() const noexcept {
		return m_Position;
	}
} // namespace sgre::scene
