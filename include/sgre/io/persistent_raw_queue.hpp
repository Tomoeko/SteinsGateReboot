#pragma once

#include "sgre/core/background_calls.hpp"
#include "sgre/io/pending_raw_reads.hpp"
#include "sgre/resource/resource_cache.hpp"
#include "sgre/scene/common_process_tasks.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace sgre::io {

	class PersistentRawReadQueue {
	public:
		using ReadOperation = PendingRawReadRequests::ReadOperation;
		// Actual persistent ResourceManager reader: +108=1, +173=0, lane2; supplied common owner is genuine.
		// Cache, processor and background manager enclose Close. Inputs have completed native mount/path preparation.
		PersistentRawReadQueue(resource::ResourceCache& cache, scene::CommonProcessTasks& processor, core::BackgroundCallManager& background, RawReadQueueLimits limits = {});
		// Supplied read must perform destination.ReadSelected and return its actual completion; captured owners enclose Close.
		PersistentRawReadQueue(resource::ResourceCache& cache, scene::CommonProcessTasks& processor, core::BackgroundCallManager& background, ReadOperation actual_read, RawReadQueueLimits limits = {});
		~PersistentRawReadQueue() noexcept;
		PersistentRawReadQueue(const PersistentRawReadQueue&) = delete;
		PersistentRawReadQueue& operator=(const PersistentRawReadQueue&) = delete;
		PersistentRawReadQueue(PersistentRawReadQueue&&) = delete;
		PersistentRawReadQueue& operator=(PersistentRawReadQueue&&) = delete;

		[[nodiscard]] RawReadRequestHandle QueuePrepared(resource::CachedResourceHandle resource, std::string request_filename, PreparedRawFileRead source);
		[[nodiscard]] bool CancelPending(RawReadRequestHandle request);
		// Real ClearCache boundary cancels only matching pending pointers; selected work remains outside this list.
		[[nodiscard]] bool CancelPendingResource(resource::CachedResourceHandle resource);
		// Native independent4d7930 resort: actual Loading references, stable positive before zero, regardless+108.
		void ResortPending();
		[[nodiscard]] std::size_t PendingCount() const;
		[[nodiscard]] std::size_t WritePendingOrder(std::span<RawReadRequestHandle> output) const;
		// Only the prepared persistent raw phase; mount bootstrap and synchronous/one-shot modes stay unsupported.
		void UpdatePreparedRequests();
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		void RequireCanClose() const;
		// Cancel actual queued nodes, fence selected worker borrows, then detach the actual cache/queue relationship.
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::io
