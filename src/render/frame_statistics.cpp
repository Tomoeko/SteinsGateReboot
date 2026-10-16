#include "sgre/render/frame_statistics.hpp"

namespace sgre::render {
	RenderFrameStatistics::RenderFrameStatistics(RenderFrameCounters current, RenderFrameCounters previous) : m_Current(current), m_Previous(previous) {
	}

	void RenderFrameStatistics::BeginFrame() noexcept {
		m_Previous = m_Current;
		m_Current = {};
	}

	void RenderFrameStatistics::RecordDraw(std::uint32_t submitted_elements) noexcept {
		++m_Current.DrawOperations;
		m_Current.SubmittedElements += submitted_elements;
	}

	RenderFrameCounters RenderFrameStatistics::Current() const noexcept {
		return m_Current;
	}

	RenderFrameCounters RenderFrameStatistics::Previous() const noexcept {
		return m_Previous;
	}
} // namespace sgre::render
