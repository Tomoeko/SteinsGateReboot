#include "sgre/render/frame_host_heap.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::render {

	static constexpr std::size_t kHostPageUnit = 65536;
	static constexpr std::size_t kMaximumPhysicalFrames = 64;
	static constexpr std::size_t kMaximumHeapBytes = 64 * 1024 * 1024;
	static constexpr std::size_t kMaximumHeapPages = 1024;

	struct HostPage {
		std::uint64_t Identity = 0;
		std::vector<std::byte> Storage;
		std::size_t Used = 0;
	};

	struct HostPhysicalFrame {
		std::vector<HostPage> Pages;
		std::uint8_t ResourceEntryByte = 0;
	};

	struct FrameHostAllocation::Lifetime {
		mutable std::mutex Mutex;
		FrameHostHeap::Implementation* Receiver = nullptr;
	};

	class FrameHostHeap::Implementation {
	public:
		Implementation(std::size_t count, FrameHostHeapLimits limits) : Limits(limits), Frames(count) {
		}

		void RequireLive() const {
			if (Closed) {
				throw std::runtime_error("frame host heap has closed");
			}
		}

		void RequireOwner() const {
			RequireLive();
			if (std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("frame host heap phase requires its actual containing owner thread");
			}
		}

		[[nodiscard]] std::size_t Current() const {
			return static_cast<std::size_t>(Frame % Frames.size());
		}

		FrameHostHeapLimits Limits;
		std::vector<HostPhysicalFrame> Frames;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::uint64_t Frame = 0;
		std::uint64_t NextIdentity = 1;
		std::uint64_t Cached = 0;
		std::size_t TotalBytes = 0;
		std::size_t TotalPages = 0;
		bool Closed = false;
	};

	FrameHostHeap::FrameHostHeap(std::size_t physical_frame_count, FrameHostHeapLimits limits) {
		if (physical_frame_count == 0 || physical_frame_count > kMaximumPhysicalFrames || limits.MaximumBytes < kHostPageUnit || limits.MaximumBytes > kMaximumHeapBytes || limits.MaximumPages == 0 || limits.MaximumPages > kMaximumHeapPages) {
			throw std::runtime_error("frame host heap requires a positive bounded physical count and host page budget");
		}
		m_Implementation = std::make_unique<Implementation>(physical_frame_count, limits);
		m_Lifetime = std::make_shared<FrameHostAllocation::Lifetime>();
		m_Lifetime->Receiver = m_Implementation.get();
	}

	FrameHostHeap::~FrameHostHeap() noexcept {
		const std::lock_guard lock(m_Lifetime->Mutex);
		m_Lifetime->Receiver = nullptr;
	}

#if defined(SGRE_FRAME_HOST_HEAP_FIXTURE)
	void FrameHostHeap::SetWrappedCounterForFixture() {
		const std::lock_guard lock(m_Lifetime->Mutex);
		auto& state = *m_Implementation;
		state.RequireOwner();
		if (state.Frame != 0 || state.TotalPages != 0) {
			throw std::runtime_error("wrapped native counter fixture requires a fresh actual owner");
		}
		state.Frame = std::numeric_limits<std::uint64_t>::max();
	}
#endif

	void FrameHostHeap::Begin() {
		const std::lock_guard lock(m_Lifetime->Mutex);
		auto& state = *m_Implementation;
		state.RequireOwner();
		++state.Frame;
		auto& frame = state.Frames[state.Current()];
		frame.ResourceEntryByte = 0;
		for (const auto& page : frame.Pages) {
			state.TotalBytes -= page.Storage.size();
		}
		state.TotalPages -= frame.Pages.size();
		frame.Pages.clear();
	}

	FrameHostAllocation FrameHostHeap::Allocate(std::size_t byte_count, std::size_t alignment) {
		const std::lock_guard lock(m_Lifetime->Mutex);
		auto& state = *m_Implementation;
		state.RequireLive();
		if (alignment == 0) {
			alignment = 16;
		}
		if (alignment > kHostPageUnit || (alignment & (alignment - 1)) != 0 || byte_count > state.Limits.MaximumBytes) {
			throw std::runtime_error("frame host allocation exceeds recovered bounded size/alignment domain");
		}

		const auto frame_index = state.Current();
		auto& frame = state.Frames[frame_index];
		HostPage* selected = nullptr;
		std::size_t offset = 0;
		for (auto& page : frame.Pages) {
			if (page.Used > std::numeric_limits<std::size_t>::max() - (alignment - 1)) {
				throw std::runtime_error("frame host relative alignment overflows");
			}
			const auto aligned = (page.Used + alignment - 1) & ~(alignment - 1);
			if (aligned <= page.Storage.size() && byte_count <= page.Storage.size() - aligned) {
				selected = &page;
				offset = aligned;
				break;
			}
		}
		if (selected == nullptr) {
			const auto minimum = std::max(byte_count, kHostPageUnit);
			if (minimum > std::numeric_limits<std::size_t>::max() - (kHostPageUnit - 1)) {
				throw std::runtime_error("frame host page rounding overflows");
			}
			const auto capacity = (minimum + kHostPageUnit - 1) & ~(kHostPageUnit - 1);
			if (state.TotalPages == state.Limits.MaximumPages || state.TotalBytes > state.Limits.MaximumBytes || capacity > state.Limits.MaximumBytes - state.TotalBytes || state.NextIdentity == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("frame host page allocation exceeds its actual owner budget or identity domain");
			}
			HostPage page;
			page.Identity = state.NextIdentity;
			page.Storage.resize(capacity);
			frame.Pages.insert(frame.Pages.begin(), std::move(page));
			++state.NextIdentity;
			++state.TotalPages;
			state.TotalBytes += capacity;
			selected = &frame.Pages.front();
		}
		selected->Used = offset + byte_count;
		FrameHostAllocation allocation;
		allocation.m_Lifetime = m_Lifetime;
		allocation.m_FrameIndex = frame_index;
		allocation.m_PageIdentity = selected->Identity;
		allocation.m_Offset = offset;
		allocation.m_Count = byte_count;

		return allocation;
	}

	std::span<std::byte> FrameHostAllocation::Bytes() const {
		const auto lifetime = m_Lifetime.lock();
		if (!lifetime) {
			throw std::runtime_error("frame host allocation owner has retired");
		}
		const std::lock_guard lock(lifetime->Mutex);
		if (lifetime->Receiver == nullptr || lifetime->Receiver->Closed || m_FrameIndex >= lifetime->Receiver->Frames.size()) {
			throw std::runtime_error("frame host allocation owner has closed");
		}
		auto& pages = lifetime->Receiver->Frames[m_FrameIndex].Pages;
		const auto found = std::find_if(pages.begin(), pages.end(), [&](const auto& page) {
			return page.Identity == m_PageIdentity;
		});
		if (found == pages.end() || m_Offset > found->Used || m_Count > found->Used - m_Offset) {
			throw std::runtime_error("frame host allocation physical page has recycled");
		}

		return std::span<std::byte>(found->Storage).subspan(m_Offset, m_Count);
	}

	bool FrameHostAllocation::BelongsTo(const FrameHostHeap& actual_heap) const {
		const auto lifetime = m_Lifetime.lock();
		if (!lifetime || lifetime != actual_heap.m_Lifetime) {
			return false;
		}
		static_cast<void>(Bytes());

		return true;
	}

	void FrameHostHeap::End() {
		const std::lock_guard lock(m_Lifetime->Mutex);
		auto& state = *m_Implementation;
		state.RequireOwner();
		state.Frames[state.Current()].ResourceEntryByte = 1;
	}

	std::uint64_t FrameHostHeap::FrameCounter() const {
		const std::lock_guard lock(m_Lifetime->Mutex);
		m_Implementation->RequireLive();

		return m_Implementation->Frame;
	}

	std::size_t FrameHostHeap::PhysicalFrame() const {
		const std::lock_guard lock(m_Lifetime->Mutex);
		m_Implementation->RequireLive();

		return m_Implementation->Current();
	}

	std::uint8_t FrameHostHeap::ResourceEntryByte() const {
		const std::lock_guard lock(m_Lifetime->Mutex);
		m_Implementation->RequireLive();

		return m_Implementation->Frames[m_Implementation->Current()].ResourceEntryByte;
	}

	std::uint64_t FrameHostHeap::CaptureUsedBytes() {
		const std::lock_guard lock(m_Lifetime->Mutex);
		auto& state = *m_Implementation;
		state.RequireLive();
		std::uint64_t used = 0;
		for (const auto& page : state.Frames[state.Current()].Pages) {
			used += static_cast<std::uint64_t>(page.Used);
		}
		state.Cached = used;

		return used;
	}

	std::uint64_t FrameHostHeap::CachedUsedBytes() const {
		const std::lock_guard lock(m_Lifetime->Mutex);
		m_Implementation->RequireLive();

		return m_Implementation->Cached;
	}

	std::size_t FrameHostHeap::CurrentPageCount() const {
		const std::lock_guard lock(m_Lifetime->Mutex);
		m_Implementation->RequireLive();

		return m_Implementation->Frames[m_Implementation->Current()].Pages.size();
	}

	void FrameHostHeap::Close() {
		const std::lock_guard lock(m_Lifetime->Mutex);
		auto& state = *m_Implementation;
		if (state.Closed) {
			return;
		}
		state.RequireOwner();
		state.Frames.clear();
		state.TotalBytes = 0;
		state.TotalPages = 0;
		state.Closed = true;
		m_Lifetime->Receiver = nullptr;
	}

} // namespace sgre::render
