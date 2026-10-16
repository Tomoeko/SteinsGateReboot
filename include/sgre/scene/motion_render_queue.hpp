#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace sgre::scene {

	struct MotionRenderOrder {
		// Native command fields +0x28/+0x0c; their coordinate semantics remain unrecovered.
		float Primary = 0;
		float Secondary = 0;
		std::uint32_t Ordinal = 0;
		std::uint64_t CommandIdentity = 0;
	};

	using MotionRenderOrderReference = std::reference_wrapper<MotionRenderOrder>;

	[[nodiscard]] bool MotionRenderOrderBefore(const MotionRenderOrder& left, const MotionRenderOrder& right) noexcept;

	class MotionRenderQueue {
	public:
		using Submission = std::function<void(std::span<const MotionRenderOrderReference>)>;
		explicit MotionRenderQueue(std::size_t capacity = 65536);
		MotionRenderQueue(const MotionRenderQueue&) = delete;
		MotionRenderQueue& operator=(const MotionRenderQueue&) = delete;
		MotionRenderQueue(MotionRenderQueue&&) = delete;
		MotionRenderQueue& operator=(MotionRenderQueue&&) = delete;
		// Keep borrowed storage alive and its keys unchanged until flush or clear; flush assigns Ordinal.
		void Append(MotionRenderOrder& command);
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] std::size_t Capacity() const noexcept;
		void Clear();
		// Submission sees the sorted live queue; success resets size afterward, failure preserves it.
		void Flush(const Submission& submit);

	private:
		std::size_t m_Capacity = 0;
		std::vector<MotionRenderOrderReference> m_Commands;
		bool m_Flushing = false;
	};

} // namespace sgre::scene
