#pragma once

#include <cstdint>

namespace sgre::render {
	struct RenderFrameCounters {
		std::uint32_t DrawOperations = 0;
		std::uint32_t SubmittedElements = 0;
		bool operator==(const RenderFrameCounters&) const = default;
	};

	class RenderFrameStatistics {
	public:
		// Supply the containing renderer's initialized counters; native constructor defaults are unproved.
		explicit RenderFrameStatistics(RenderFrameCounters current, RenderFrameCounters previous);
		// Native graphics preloop copies the completed frame, then clears the two current words.
		void BeginFrame() noexcept;
		// One logical native render operation may contain several device draws. Record its argument count once.
		void RecordDraw(std::uint32_t submitted_elements) noexcept;
		[[nodiscard]] RenderFrameCounters Current() const noexcept;
		[[nodiscard]] RenderFrameCounters Previous() const noexcept;

	private:
		RenderFrameCounters m_Current;
		RenderFrameCounters m_Previous;
	};
} // namespace sgre::render
