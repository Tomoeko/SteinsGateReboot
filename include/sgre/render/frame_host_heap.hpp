#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::render {

	class FrameHostHeap;

	class FrameHostAllocation {
	public:
		// Borrows a real heap page until that physical frame is recycled or its owner closes.
		// All writes finish before the containing frame fence permits Begin to recycle their storage.
		[[nodiscard]] std::span<std::byte> Bytes() const;
		// Exact live allocation ownership, without creating another page or owner pin.
		[[nodiscard]] bool BelongsTo(const FrameHostHeap& actual_heap) const;

	private:
		friend class FrameHostHeap;
		struct Lifetime;
		std::weak_ptr<Lifetime> m_Lifetime;
		std::size_t m_FrameIndex = 0;
		std::uint64_t m_PageIdentity = 0;
		std::size_t m_Offset = 0;
		std::size_t m_Count = 0;
	};

	struct FrameHostHeapLimits {
		std::size_t MaximumBytes = 16 * 1024 * 1024;
		std::size_t MaximumPages = 256;
	};

	class FrameHostHeap {
	public:
		// Actual backend descriptor/frame ownership supplies the positive physical count; no OS/GPU factory is supplied.
		explicit FrameHostHeap(std::size_t physical_frame_count, FrameHostHeapLimits limits = {});
		~FrameHostHeap() noexcept;
		FrameHostHeap(const FrameHostHeap&) = delete;
		FrameHostHeap& operator=(const FrameHostHeap&) = delete;
		// Begin increments the native uint64 counter, clears that physical entry byte and retires its host pages.
		// The containing worker/draw fence enforces the borrowed-span lifetime before this owner-thread operation.
		void Begin();
		// Allocation is mutex serialized; requested alignment0 selects original pool default16.
		// Native alignment applies to the page-relative used offset, not a promise about absolute pointer alignment.
		[[nodiscard]] FrameHostAllocation Allocate(std::size_t byte_count, std::size_t alignment = 0);
		void End();
		[[nodiscard]] std::uint64_t FrameCounter() const;
		[[nodiscard]] std::size_t PhysicalFrame() const;
		// Only this observed byte is recovered here; it is not named GPU readiness or upload completion.
		[[nodiscard]] std::uint8_t ResourceEntryByte() const;
		[[nodiscard]] std::uint64_t CaptureUsedBytes();
		[[nodiscard]] std::uint64_t CachedUsedBytes() const;
		[[nodiscard]] std::size_t CurrentPageCount() const;
		void Close();

	private:
		friend class FrameHostAllocation;
#if defined(SGRE_FRAME_HOST_HEAP_FIXTURE)
		friend class FrameHostHeapFixtureBridge;
		void SetWrappedCounterForFixture();
#endif
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
		std::shared_ptr<FrameHostAllocation::Lifetime> m_Lifetime;
	};

} // namespace sgre::render
