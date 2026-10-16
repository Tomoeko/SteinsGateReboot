#pragma once

#include "sgre/io/raw_read_destination.hpp"
#include "sgre/io/resource_file_read.hpp"
#include "sgre/resource/resource_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace sgre::io {
	class PersistentRawReadQueue;
	class FileReadProcessor;

	struct RawReadRequestHandle {
		std::uint32_t Id = 0;
		const void* Owner = nullptr;
		bool operator==(const RawReadRequestHandle&) const = default;
	};

	struct RawReadQueueLimits {
		std::size_t MaxRequests = 1024;
		std::size_t MaxNameBytes = 4096;
	};

	// Actual pending records/cache coupling, separate from the containing reader's common/serial owner.
	class PendingRawReadRequests {
	public:
		using ReadOperation = std::function<FileReadCompletion(RawFileReadDestination&, const PreparedRawFileRead&)>;
		~PendingRawReadRequests() noexcept;
		PendingRawReadRequests(const PendingRawReadRequests&) = delete;
		PendingRawReadRequests& operator=(const PendingRawReadRequests&) = delete;
		PendingRawReadRequests(PendingRawReadRequests&&) = delete;
		PendingRawReadRequests& operator=(PendingRawReadRequests&&) = delete;
		[[nodiscard]] RawReadRequestHandle QueuePrepared(resource::CachedResourceHandle resource, std::string request_filename, PreparedRawFileRead source);
		[[nodiscard]] RawReadRequestHandle QueuePreparedResource(resource::CachedResourceHandle resource, std::string request_filename, PreparedResourceFileRead source);
		[[nodiscard]] bool CancelPending(RawReadRequestHandle request);
		[[nodiscard]] bool CancelPendingResource(resource::CachedResourceHandle resource);
		void ResortPending();
		[[nodiscard]] std::size_t PendingCount() const;
		[[nodiscard]] std::size_t WritePendingOrder(std::span<RawReadRequestHandle> output) const;
		[[nodiscard]] bool IsClosed() const noexcept;

	private:
		friend class PersistentRawReadQueue;
		friend class FileReadProcessor;
		friend class sgre::resource::ResourceCache;
		PendingRawReadRequests(resource::ResourceCache& cache, ReadOperation actual_read, RawReadQueueLimits limits);
		[[nodiscard]] bool UsesCache(const resource::ResourceCache& cache) const noexcept;
		void BindWorkerOwner() noexcept;
		void Run();
		void RequireOpen() const;
		[[nodiscard]] bool CanBeginClose() const noexcept;
		[[nodiscard]] bool HasFailed() const noexcept;
		void BeginClose();
		// Only the concrete reader/facade can finish this after fencing every real serial/background callback.
		void CompleteCloseAfterWorkerFence();
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::io
