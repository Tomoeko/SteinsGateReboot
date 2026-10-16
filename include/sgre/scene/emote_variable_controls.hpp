#pragma once

#include "sgre/resource/emote_selectors.hpp"
#include "sgre/scene/emote_blink_control.hpp"
#include "sgre/scene/emote_clamp_control.hpp"
#include "sgre/scene/emote_graph_control.hpp"
#include "sgre/scene/emote_selector_control.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {
	class EmoteTimelineScheduler;

	class MotionPlayerCommands;

	struct EmoteGraphChannelDefinition {
		std::string Name;
		std::int32_t InitialFrame = 0;
		EmoteGraphDefinition Graph;
	};

	struct EmoteMouthDefinition {
		std::string Name;
		std::string TalkName;
		std::int32_t InitialFrame = 0;
	};

	struct EmoteEyeDefinition {
		std::string Name;
		std::string LeftName;
		std::string RightName;
		std::string BlinkName;
		std::int32_t InitialFrame = 0;
		std::int32_t LowerFrame = 0;
		std::int32_t EndFrame = 0;
		EmoteGraphDefinition Graph;
		std::int32_t BlinkParameterIndex = -1;
		EmoteBlinkParameters Blink;
		std::vector<EmoteBlinkParameters> BlinkChoices;
	};

	struct EmoteClampDefinition {
		std::string LeftRight;
		std::string UpDown;
		EmoteClampTransform Transform;
	};

	struct EmoteTimelineCatalogEntry {
		std::string Name;
		bool Difference = false;
	};

	struct EmoteVariableProgram {
		resource::EmoteSelectorMetadata Selectors;
		std::size_t PrimarySelectorCount = 0;
		std::vector<std::string> Transitions;
		std::vector<std::string> Immediate;
		std::vector<std::string> Extensions;
		std::vector<std::string> VariableLabels;
		std::vector<EmoteGraphChannelDefinition> Eyebrows;
		std::vector<EmoteMouthDefinition> Mouths;
		std::vector<EmoteEyeDefinition> Eyes;
		std::vector<EmoteClampDefinition> Clamps;
		std::vector<std::string> InstantVariables;
		std::vector<std::string> StereoVariables;
		std::vector<EmoteTimelineCatalogEntry> TimelineCatalog;
		bool DefaultImmediate = false;
		// An enabled family is never silently replaced with a scalar or omitted during setup.
		std::vector<std::string> UnsupportedFamilies;
	};

	[[nodiscard]] EmoteVariableProgram ReadEmoteVariableProgram(const resource::PsbDocument& document);

	class EmoteVariableControls {
	public:
		// This is the genuine channel/selector publisher component, not the whole MEmote player/task.
		// Actual Commands/selected scene and its queue guard enclose this owner. Motion task/clock/624-word random remain shared.
		EmoteVariableControls(MotionPlayerCommands& actual_commands, std::function<void()> require_mutable, EmoteVariableProgram program, std::function<std::uint32_t()> read_entropy_word = {}, std::function<void(float)> update_properties = {});
		~EmoteVariableControls() noexcept;
		EmoteVariableControls(const EmoteVariableControls&) = delete;
		EmoteVariableControls& operator=(const EmoteVariableControls&) = delete;
		// Known native initialization is observable even while other required families remain open.
		// Full mutation/update still rejects before publishing to Motion until every required family exists.
		[[nodiscard]] std::span<const std::string> MissingFamilies() const noexcept;
		[[nodiscard]] std::optional<float> TransitionValue(std::string_view identifier) const;
		// Native2bc760: bind only dedicated scalar transitions; absent names start at1, other existing routes are skipped.
		[[nodiscard]] bool UsesCommands(const MotionPlayerCommands& actual_commands) const noexcept;
		[[nodiscard]] bool BindSpeedTransition(std::string_view identifier);
		[[nodiscard]] std::optional<float> SpeedTransitionValue(std::string_view identifier) const;
		[[nodiscard]] float Read(std::string_view identifier) const;
		void Set(std::string_view identifier, float value, float duration = 0, float easing = 0);
		// Exact sorted timeline track names suppress only their matching native named controls.
		[[nodiscard]] bool Animating(std::span<const std::string_view> timeline_variables = {}) const;
		void RequireCanAssignState(const EmoteVariableControls& source) const;
		// Native2bc870 matches source controls to the first destination label, without publication.
		void AssignState(const EmoteVariableControls& source);
		// Native Skip finishes named controls before primary properties, then extension-map controls afterward.
		// Neither phase samples graphs/blink, advances time, or republishes cached Motion values.
		void FinishNamedTransitions();
		void FinishExtensionTransitions();
		void SetBlinkEnabled(bool enabled);
		[[nodiscard]] bool BlinkEnabled() const;
		[[nodiscard]] bool IsInstantVariable(std::string_view identifier) const;
		[[nodiscard]] std::span<const std::string> TimelineLabels(bool difference) const;
		// Native channel values are processed/published before the containing plain player's external-delta update.
		// Motion composition/update and event delivery remain the actual containing owner's next explicit phase.
		void Update(float elapsed_ticks);
		void UpdateWithTimelines(float elapsed_ticks, const EmoteTimelineScheduler& actual_timelines);
		// The containing property setter shares native+89 dirty admission with the existing channel loop.
		void MarkPropertiesDirty();
		[[nodiscard]] std::size_t PublishedVariableCount() const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
