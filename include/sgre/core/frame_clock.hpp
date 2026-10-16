#pragma once

#include <cstdint>

namespace sgre::core {

	class FrameClock {
	public:
		explicit FrameClock(std::uint64_t initial_milliseconds = 0);
		[[nodiscard]] static FrameClock NativeLooper(std::uint64_t initial_milliseconds);
		void SetVariableFrame(bool enabled);
		[[nodiscard]] bool VariableFrame() const;
		// The native counter difference wraps at 32 bits; all float operations remain separate.
		[[nodiscard]] std::int32_t Advance(std::uint64_t milliseconds);
		[[nodiscard]] float VariableStep() const;
		[[nodiscard]] float ResidualTicks() const;

	private:
		std::uint64_t m_PreviousMilliseconds = 0;
		float m_VariableStep = 1;
		float m_ResidualTicks = 0;
		bool m_VariableFrame = false;
	};

} // namespace sgre::core
