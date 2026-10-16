#include "sgre/io/pending_raw_reads.hpp"

#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::io {

	static std::string_view NativeReadSortName(std::string_view filename) {
		const auto occurrence = filename.find(".m");
		if (filename.size() >= 2 && occurrence == filename.size() - 2) {
			return filename.substr(0, filename.size() - 2);
		}

		return filename;
	}

	struct PersistentRawRequest {
		RawReadRequestHandle Handle;
		resource::CachedResourceHandle Resource;
		std::string Filename;
		PreparedRawFileRead Source;
		RawFileReadDestination* Destination = nullptr;
		PreparedResourceFileRead ResourceSource;
		ResourceFileReadDestination* ResourceDestination = nullptr;
	};

	class PendingRawReadRequests::Implementation {
	public:
		Implementation(resource::ResourceCache& cache, ReadOperation actual_read, RawReadQueueLimits limits) : Cache(cache), Read(std::move(actual_read)), Limits(limits), OwnerThread(std::this_thread::get_id()) {
			if (!Read || limits.MaxRequests == 0 || limits.MaxRequests > 65536 || limits.MaxNameBytes == 0 || limits.MaxNameBytes > 65536) {
				throw std::runtime_error("persistent raw read queue requires bounded positive request/name limits");
			}

			Pending.reserve(limits.MaxRequests);
			OrderScratch.reserve(limits.MaxRequests);
			ReferenceScratch.resize(limits.MaxRequests);
		}

		resource::ResourceCache& Cache;
		ReadOperation Read;
		RawReadQueueLimits Limits;
		std::thread::id OwnerThread;
		std::mutex Mutex;
		std::vector<std::unique_ptr<PersistentRawRequest>> Pending;
		std::vector<std::unique_ptr<PersistentRawRequest>> OrderScratch;
		std::vector<bool> ReferenceScratch;
		std::uint32_t NextId = 0;
		std::size_t AdmittedThisLoop = 0;
		bool Running = false;
		bool Closing = false;
		std::atomic<bool> Closed = false;
		std::atomic<bool> Failed = false;
		bool WorkerBound = false;

		void RequireOwnerThread() const {
			if (std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("persistent raw queue mutation requires the actual processor receiving thread");
			}
		}

		void RequireOpen() const {
			RequireOwnerThread();
			if (Closed.load(std::memory_order_acquire) || Closing || Failed.load(std::memory_order_acquire)) {
				throw std::runtime_error("persistent raw queue requires its actual open owner");
			}
		}

		void ValidatePendingSortReferencesLocked() {
			for (std::size_t index = 0; index < Pending.size(); ++index) {
				const auto& request = *Pending[index];
				const auto info = Cache.Info(request.Resource);
				if (info.Residency != resource::ResourceResidency::Loading || info.References > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || Cache.Name(request.Resource) != NativeReadSortName(request.Filename)) {
					throw std::runtime_error("native read sorter requires each actual Loading name and supported signed reference count");
				}

				ReferenceScratch[index] = info.References != 0;
			}
		}

		void ResortLocked() {
			ValidatePendingSortReferencesLocked();

			// Native comparator is only positive-reference group before nonpositive, stable within both groups.
			for (const bool referenced : {true, false}) {
				for (std::size_t index = 0; index < Pending.size(); ++index) {
					if (ReferenceScratch[index] == referenced) {
						OrderScratch.push_back(std::move(Pending[index]));
					}
				}
			}

			Pending.swap(OrderScratch);
			OrderScratch.clear();
		}

		std::unique_ptr<PersistentRawRequest> TakeNext() {
			std::lock_guard lock(Mutex);
			if (Pending.empty() || Closing) {
				return {};
			}

			auto request = std::move(Pending.front());
			if (request->ResourceDestination) {
				request->ResourceDestination->Select();
			} else {
				request->Destination->Select();
			}
			Pending.erase(Pending.begin());

			return request;
		}

		void Run() {
			{
				std::lock_guard lock(Mutex);
				if (Closing) {
					return;
				}
				if (Running) {
					throw std::runtime_error("actual persistent raw read loop cannot overlap itself");
				}

				Running = true;
				AdmittedThisLoop = Pending.size();
			}

			try {
				auto current = TakeNext();
				while (current) {
					std::unique_ptr<PersistentRawRequest> next;
					if (current->ResourceDestination) {
						const bool physical = current->ResourceDestination->ReadPhysicalSelected(current->ResourceSource);
						if (physical) {
							next = TakeNext();
						}

						static_cast<void>(current->ResourceDestination->PrepareSelectedObject(current->ResourceSource));
						current->ResourceDestination->PublishSelected();
						if (!physical) {
							next = TakeNext();
						}
					} else {
						const auto completion = Read(*current->Destination, current->Source);
						if (!current->Destination->m_ReadPrepared || completion != current->Destination->m_Result.Completion) {
							throw std::runtime_error("actual raw read service did not return its prepared selected completion");
						}

						if (completion == FileReadCompletion::Succeeded) {
							next = TakeNext();
							current->Destination->PublishSelected();
						} else {
							current->Destination->PublishSelected();
							next = TakeNext();
						}
					}

					// No destination access follows publication; deleting the actual request ends its filename/source ownership.
					current.reset();
					current = std::move(next);
				}
			} catch (...) {
				Failed.store(true, std::memory_order_release);
				std::lock_guard lock(Mutex);
				Running = false;
				AdmittedThisLoop = 0;
				throw;
			}

			std::lock_guard lock(Mutex);
			Running = false;
			AdmittedThisLoop = 0;
		}

		bool CancelAt(std::size_t index) {
			const bool cancelled = Pending[index]->ResourceDestination ? Pending[index]->ResourceDestination->CancelQueued() : Pending[index]->Destination->CancelQueued();
			if (!cancelled) {
				throw std::runtime_error("actual pending node lost its queued receiving destination");
			}

			Pending.erase(Pending.begin() + static_cast<std::ptrdiff_t>(index));

			return true;
		}
	};

	PendingRawReadRequests::PendingRawReadRequests(resource::ResourceCache& cache, ReadOperation actual_read, RawReadQueueLimits limits) : m_Implementation(std::make_unique<Implementation>(cache, std::move(actual_read), limits)) {
		cache.AttachRawReadQueue(*this);
	}

	bool PendingRawReadRequests::UsesCache(const resource::ResourceCache& cache) const noexcept {
		return &m_Implementation->Cache == &cache;
	}

	PendingRawReadRequests::~PendingRawReadRequests() noexcept {
		if (IsClosed()) {
			return;
		}
		if (m_Implementation->WorkerBound) {
			std::terminate();
		}

		// Constructor failure before worker publication has no outstanding callback/receiver borrow.
		try {
			BeginClose();
			CompleteCloseAfterWorkerFence();
		} catch (...) {
			std::terminate();
		}
	}

	RawReadRequestHandle PendingRawReadRequests::QueuePrepared(resource::CachedResourceHandle resource, std::string request_filename, PreparedRawFileRead source) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		std::lock_guard lock(owner.Mutex);
		const auto info = owner.Cache.Info(resource);
		if (request_filename.empty() || request_filename.size() > owner.Limits.MaxNameBytes || request_filename.find('\0') != std::string::npos || source.Filename.empty() || info.Residency != resource::ResourceResidency::Loading || info.Completion != resource::ResourceCompletion::Pending || info.References > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || owner.Cache.Name(resource) != NativeReadSortName(request_filename)) {
			throw std::runtime_error("prepared raw queue request requires its exact native Loading identity and physical source");
		}
		if (owner.Pending.size() == owner.Limits.MaxRequests || (owner.Running && owner.AdmittedThisLoop == owner.Limits.MaxRequests) || owner.NextId == std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("persistent raw queue request/pass/id budget is exhausted");
		}

		auto request = std::make_unique<PersistentRawRequest>();
		request->Handle = {owner.NextId, &owner};
		request->Resource = resource;
		request->Filename = std::move(request_filename);
		request->Source = std::move(source);
		request->Destination = &owner.Cache.CreateRawReadDestination(resource);
		request->Destination->MarkQueued();
		const auto handle = request->Handle;
		owner.Pending.push_back(std::move(request));
		++owner.NextId;
		if (owner.Running) {
			++owner.AdmittedThisLoop;
		}

		owner.ResortLocked();

		return handle;
	}

	RawReadRequestHandle PendingRawReadRequests::QueuePreparedResource(resource::CachedResourceHandle resource, std::string request_filename, PreparedResourceFileRead source) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		std::lock_guard lock(owner.Mutex);
		const auto info = owner.Cache.Info(resource);
		if (request_filename.empty() || request_filename.size() > owner.Limits.MaxNameBytes || request_filename.find('\0') != std::string::npos || (!source.MountedSource && source.Region.Filename.empty()) || (source.MountedSource && (!source.Region.Filename.empty() || source.Region.Offset != 0 || source.Region.Length != 0 || source.Region.MaxBytes == 0 || source.Region.MaxBytes > 160 * 1024 * 1024)) || source.OriginalFilename != request_filename || (source.Format.Kind != ResourceFileKind::Binary && source.Format.Kind != ResourceFileKind::Psb) || info.Residency != resource::ResourceResidency::Loading || info.Completion != resource::ResourceCompletion::Pending || info.References > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || owner.Cache.Name(resource) != NativeReadSortName(request_filename)) {
			throw std::runtime_error("typed queue request requires its exact native Loading identity and actual physical or mounted source");
		}
		if (owner.Pending.size() == owner.Limits.MaxRequests || (owner.Running && owner.AdmittedThisLoop == owner.Limits.MaxRequests) || owner.NextId == std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("persistent raw queue request/pass/id budget is exhausted");
		}

		// Reject an existing pending node outside the signed native sorter domain before publishing a new borrow.
		owner.ValidatePendingSortReferencesLocked();

		auto request = std::make_unique<PersistentRawRequest>();
		request->Handle = {owner.NextId, &owner};
		request->Resource = resource;
		request->Filename = std::move(request_filename);
		const auto format = source.Format;
		request->ResourceSource = std::move(source);
		request->ResourceDestination = &owner.Cache.CreateResourceReadDestination(resource, format);
		request->ResourceDestination->MarkQueued();
		const auto handle = request->Handle;
		owner.Pending.push_back(std::move(request));
		++owner.NextId;
		if (owner.Running) {
			++owner.AdmittedThisLoop;
		}

		owner.ResortLocked();

		return handle;
	}

	bool PendingRawReadRequests::CancelPending(RawReadRequestHandle request) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		if (request.Owner != &owner) {
			throw std::runtime_error("raw read request handle belongs to another actual processor");
		}

		std::lock_guard lock(owner.Mutex);
		for (std::size_t index = 0; index < owner.Pending.size(); ++index) {
			if (owner.Pending[index]->Handle == request) {
				return owner.CancelAt(index);
			}
		}

		return false;
	}

	bool PendingRawReadRequests::CancelPendingResource(resource::CachedResourceHandle resource) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		std::lock_guard lock(owner.Mutex);
		for (std::size_t index = 0; index < owner.Pending.size(); ++index) {
			if (owner.Pending[index]->Resource == resource) {
				return owner.CancelAt(index);
			}
		}

		return false;
	}

	void PendingRawReadRequests::ResortPending() {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		std::lock_guard lock(owner.Mutex);
		owner.ResortLocked();
	}

	std::size_t PendingRawReadRequests::PendingCount() const {
		auto& owner = *m_Implementation;
		std::lock_guard lock(owner.Mutex);

		return owner.Pending.size();
	}

	std::size_t PendingRawReadRequests::WritePendingOrder(std::span<RawReadRequestHandle> output) const {
		auto& owner = *m_Implementation;
		std::lock_guard lock(owner.Mutex);
		if (output.size() < owner.Pending.size()) {
			throw std::runtime_error("raw pending order output is smaller than its actual pointer list");
		}

		for (std::size_t index = 0; index < owner.Pending.size(); ++index) {
			output[index] = owner.Pending[index]->Handle;
		}

		return owner.Pending.size();
	}

	void PendingRawReadRequests::BindWorkerOwner() noexcept {
		m_Implementation->WorkerBound = true;
	}

	void PendingRawReadRequests::Run() {
		m_Implementation->Run();
	}

	void PendingRawReadRequests::RequireOpen() const {
		m_Implementation->RequireOpen();
	}

	bool PendingRawReadRequests::CanBeginClose() const noexcept {
		const auto& owner = *m_Implementation;

		return std::this_thread::get_id() == owner.OwnerThread && !HasFailed();
	}

	bool PendingRawReadRequests::HasFailed() const noexcept {
		return m_Implementation->Failed.load(std::memory_order_acquire);
	}

	bool PendingRawReadRequests::IsClosed() const noexcept {
		return m_Implementation->Closed.load(std::memory_order_acquire);
	}

	void PendingRawReadRequests::BeginClose() {
		if (!CanBeginClose()) {
			throw std::runtime_error("raw pending owner retirement requires its main thread and supported provider");
		}

		auto& owner = *m_Implementation;
		std::lock_guard lock(owner.Mutex);
		owner.Closing = true;
		while (!owner.Pending.empty()) {
			owner.CancelAt(0);
		}
	}

	void PendingRawReadRequests::CompleteCloseAfterWorkerFence() {
		if (!CanBeginClose()) {
			throw std::runtime_error("failed persistent raw read provider retains its actual receiving owners");
		}

		auto& owner = *m_Implementation;
		{
			std::lock_guard lock(owner.Mutex);
			if (!owner.Closing || owner.Running || !owner.Pending.empty()) {
				throw std::runtime_error("raw pending owner must complete its concrete worker fence before detachment");
			}
		}

		owner.Cache.DetachRawReadQueue(*this);
		owner.Closed.store(true, std::memory_order_release);
		owner.Read = {};
	}

} // namespace sgre::io
