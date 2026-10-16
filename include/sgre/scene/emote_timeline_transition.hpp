#pragma once

namespace sgre::scene {
	// Native EPTransitionControl's scalar, noncyclic domain used by active timeline tracks and blend ratios.
	class EmoteTimelineTransition {
	public:
		void Set(float target, float duration = 0, float easing = 0);
		// Native2d7500 finishes only a changing control, retaining its timing/start/target words.
		void FinishTransitions() noexcept;
		[[nodiscard]] float Advance(float elapsed_ticks);
		[[nodiscard]] float Current() const noexcept;
		[[nodiscard]] bool Changing() const noexcept;

	private:
		float m_Current = 0;
		float m_Start = 0;
		float m_Target = 0;
		float m_Duration = 0;
		float m_Elapsed = 0;
		float m_Easing = 0;
		bool m_Changing = false;
	};
} // namespace sgre::scene
