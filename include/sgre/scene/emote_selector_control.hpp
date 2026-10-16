#pragma once

#include "sgre/resource/emote_selectors.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

namespace sgre::scene {

	// The containing Emote variable/controller owner encloses each borrowed selector.
	// Native EPVariableIO reads current controller state and sets it; a read is not a reset.
	class EmoteVariableIo {
	public:
		virtual ~EmoteVariableIo() noexcept = default;
		[[nodiscard]] virtual float Read(std::string_view identifier) const = 0;
		virtual void Set(std::string_view identifier, float value, float duration, float easing) = 0;
	};

	struct EmoteSelectorTransitionState {
		std::size_t Queued = 0;
		float ReciprocalDuration = 0;
		float Progress = 0;
		bool Active = false;
	};

	class EmoteSelectorControl {
	public:
		// Native 2d79e0 copies the definition, then applies selection zero with duration/easing zero.
		EmoteSelectorControl(resource::EmoteSelectorDefinition definition, EmoteVariableIo& variables);
		EmoteSelectorControl(const EmoteSelectorControl&) = delete;
		EmoteSelectorControl& operator=(const EmoteSelectorControl&) = delete;
		EmoteSelectorControl(EmoteSelectorControl&&) = delete;
		EmoteSelectorControl& operator=(EmoteSelectorControl&&) = delete;
		// Native2d7df0 application helper; Set owns the separate native queued setter policy.
		// Each nonempty option performs Read then Set; Immediate zeroes that option's duration only.
		void RequireCanAssignState(const EmoteSelectorControl& source) const;
		// Native2d81d0 imports the owned FIFO without invoking VariableIo or replacing its definition.
		void AssignState(const EmoteSelectorControl& source);
		void Apply(std::int32_t selection, float duration, float easing);
		// Native2d8110 retains the float selection until the actual update starts this transition.
		void Set(float selection, float duration, float easing, bool append = false);
		// Native2d7fb0 advances each newly started queued job using this same actual delta.
		void Update(float elapsed_ticks);
		void FinishTransitions();
		[[nodiscard]] EmoteSelectorTransitionState TransitionState() const noexcept;
		[[nodiscard]] std::int32_t Selection() const noexcept;
		[[nodiscard]] const resource::EmoteSelectorDefinition& Definition() const noexcept;

	private:
		resource::EmoteSelectorDefinition m_Definition;
		EmoteVariableIo& m_Variables;
		std::int32_t m_Selection = 0;
		struct PendingTransition {
			float Selection = 0;
			float Duration = 0;
			float Easing = 0;
		};
		std::deque<PendingTransition> m_Pending;
		float m_ReciprocalDuration = 0;
		float m_Progress = 0;
		bool m_Transitioning = false;
		bool m_Applying = false;
		bool m_Updating = false;
	};

} // namespace sgre::scene
