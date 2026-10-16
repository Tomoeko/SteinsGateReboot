#pragma once

#include <optional>

namespace sgre::scene {
	// Native EPRotateControl, with the containing owner's default nonappend request policy.
	class EmoteRotationTransition {
	public:
		void Set(float radians, float duration = 0, float easing = 0);
		[[nodiscard]] float Advance(float elapsed_ticks);
		[[nodiscard]] float Current() const noexcept;

	private:
		struct Request {
			float Target = 0;
			float Duration = 0;
			float Easing = 0;
		};
		[[nodiscard]] float AdvanceCurrent(float elapsed_ticks);
		std::optional<Request> m_Pending;
		float m_Current = 0;
		float m_Start = 0;
		float m_Target = 0;
		float m_Easing = 0;
		float m_Progress = 0;
		float m_ReciprocalDuration = 0;
		bool m_Active = false;
	};
} // namespace sgre::scene
