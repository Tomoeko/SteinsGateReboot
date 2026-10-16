#include "sgre/io/persistent_raw_queue.hpp"

#include "sgre/io/file_read_serial.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::io {

	static FileReadCompletion ReadActualPreparedRaw(RawFileReadDestination& destination, const PreparedRawFileRead& source) {
		return destination.ReadSelected(source);
	}

	static resource::ResourceCache& RequireExclusiveReader(resource::ResourceCache& cache, scene::CommonProcessTasks& processor) {
		if (!processor.CanClear() || processor.Serial().Count() != 0 || processor.Serial().Status() != scene::TaskStatus::Active) {
			throw std::runtime_error("persistent raw reader requires its actual exclusive empty active common owner");
		}

		return cache;
	}

	class PersistentRawReadQueue::Implementation {
	public:
		Implementation(resource::ResourceCache& cache, scene::CommonProcessTasks& processor, core::BackgroundCallManager& background, ReadOperation actual_read, RawReadQueueLimits limits) : Requests(RequireExclusiveReader(cache, processor), std::move(actual_read), limits), Processor(processor), Serial(processor, background, [this]() {
			                                                                                                                                                                                      Requests.Run();
		                                                                                                                                                                                      }) {
			Requests.BindWorkerOwner();
		}

		PendingRawReadRequests Requests;
		scene::CommonProcessTasks& Processor;
		FileReadSerialAttachment Serial;
	};

	PersistentRawReadQueue::PersistentRawReadQueue(resource::ResourceCache& cache, scene::CommonProcessTasks& processor, core::BackgroundCallManager& background, RawReadQueueLimits limits) : PersistentRawReadQueue(cache, processor, background, ReadActualPreparedRaw, limits) {
	}

	PersistentRawReadQueue::PersistentRawReadQueue(resource::ResourceCache& cache, scene::CommonProcessTasks& processor, core::BackgroundCallManager& background, ReadOperation actual_read, RawReadQueueLimits limits) : m_Implementation(std::make_unique<Implementation>(cache, processor, background, std::move(actual_read), limits)) {
	}

	PersistentRawReadQueue::~PersistentRawReadQueue() noexcept {
		try {
			Close();
		} catch (...) {
			if (!IsClosed()) {
				std::terminate();
			}
		}
	}

	RawReadRequestHandle PersistentRawReadQueue::QueuePrepared(resource::CachedResourceHandle resource, std::string request_filename, PreparedRawFileRead source) {
		return m_Implementation->Requests.QueuePrepared(resource, std::move(request_filename), std::move(source));
	}

	bool PersistentRawReadQueue::CancelPending(RawReadRequestHandle request) {
		return m_Implementation->Requests.CancelPending(request);
	}

	bool PersistentRawReadQueue::CancelPendingResource(resource::CachedResourceHandle resource) {
		return m_Implementation->Requests.CancelPendingResource(resource);
	}

	void PersistentRawReadQueue::ResortPending() {
		m_Implementation->Requests.ResortPending();
	}

	std::size_t PersistentRawReadQueue::PendingCount() const {
		return m_Implementation->Requests.PendingCount();
	}

	std::size_t PersistentRawReadQueue::WritePendingOrder(std::span<RawReadRequestHandle> output) const {
		return m_Implementation->Requests.WritePendingOrder(output);
	}

	void PersistentRawReadQueue::UpdatePreparedRequests() {
		auto& owner = *m_Implementation;
		owner.Requests.RequireOpen();
		owner.Processor.Update([&owner]() {
			if (owner.Requests.PendingCount() != 0) {
				static_cast<void>(owner.Serial.AppendReadLoop());
			}
		});
	}

	bool PersistentRawReadQueue::CanClose() const noexcept {
		const auto& owner = *m_Implementation;

		return owner.Requests.CanBeginClose() && (owner.Requests.IsClosed() || owner.Serial.CanClose());
	}

	bool PersistentRawReadQueue::IsClosed() const noexcept {
		return m_Implementation->Requests.IsClosed();
	}

	void PersistentRawReadQueue::RequireCanClose() const {
		if (!CanClose()) {
			throw std::runtime_error("persistent raw queue retirement requires its actual main/common/serial owner boundary");
		}
	}

	void PersistentRawReadQueue::Close() {
		RequireCanClose();
		if (IsClosed()) {
			return;
		}

		auto& owner = *m_Implementation;
		owner.Requests.BeginClose();
		std::exception_ptr failure;
		try {
			owner.Serial.Close();
		} catch (...) {
			if (!owner.Serial.IsClosed()) {
				throw;
			}

			failure = std::current_exception();
		}

		if (owner.Requests.HasFailed()) {
			if (failure) {
				std::rethrow_exception(failure);
			}

			throw std::runtime_error("failed persistent raw read provider retains its actual receiving owners");
		}

		owner.Requests.CompleteCloseAfterWorkerFence();
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::io
