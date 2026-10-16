#include "sgre/scene/motion_preparation_service.hpp"

#include <deque>
#include <exception>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::scene {

	class MotionPreparationService::Implementation {
	public:
		Implementation(TextureCache& cache, MotionPreparationLimits limits)
		    : Cache(cache), Limits(limits) {
			if (limits.MaxPendingJobs == 0 || limits.MaxPendingJobs > 1024 || limits.MaxQueuedBytes == 0 || limits.MaxQueuedBytes > 160 * 1024 * 1024 || limits.MaxLiveDocuments == 0 || limits.MaxLiveDocuments > 65536) {
				throw std::runtime_error("motion texture preparation service requires bounded pending bytes/jobs and document owners");
			}

			Retiring.reserve(limits.MaxLiveDocuments);
		}

		struct Job {
			std::vector<std::uint8_t> Bytes;
			std::promise<std::shared_ptr<const MotionReceivingDocument>> Result;
		};

		TextureCache& Cache;
		MotionPreparationLimits Limits;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		mutable std::mutex Mutex;
		std::deque<std::unique_ptr<Job>> Pending;
		std::vector<std::unique_ptr<const MotionReceivingDocument>> Retiring;
		std::size_t QueuedBytes = 0;
		std::size_t LiveDocuments = 0;
		bool Calling = false;
		bool Stopping = false;
		bool Closed = false;

		void RequireOwner() const {
			if (std::this_thread::get_id() != OwnerThread || Closed || Calling) {
				throw std::runtime_error("motion texture preparation mutation requires its actual live owner outside platform callbacks");
			}
		}

		class Call {
		public:
			explicit Call(Implementation& owner)
			    : Owner(owner) {
				Owner.RequireOwner();
				Owner.Calling = true;
			}

			~Call() {
				Owner.Calling = false;
			}

		private:
			Implementation& Owner;
		};

		void ReleaseDocument(const MotionReceivingDocument* raw_document) noexcept {
			std::unique_ptr<const MotionReceivingDocument> document(raw_document);
			if (std::this_thread::get_id() == OwnerThread) {
				const bool was_calling = Calling;
				Calling = true;
				document.reset();
				Calling = was_calling;
				const std::lock_guard lock(Mutex);
				--LiveDocuments;
				return;
			}

			// Capacity was reserved before document admission; an off-thread final pin never touches the texture cache.
			const std::lock_guard lock(Mutex);
			Retiring.push_back(std::move(document));
		}

		std::shared_ptr<const MotionReceivingDocument> PrepareOwner(const std::shared_ptr<Implementation>& self, std::vector<std::uint8_t> bytes) {
			{
				const std::lock_guard lock(Mutex);
				if (Stopping || LiveDocuments == Limits.MaxLiveDocuments) {
					throw std::runtime_error("motion texture preparation is stopped or its live-document capacity is exhausted");
				}
			}

			auto document = std::make_unique<const MotionReceivingDocument>(Cache, std::move(bytes));
			{
				const std::lock_guard lock(Mutex);
				++LiveDocuments;
			}

			auto release = [self](const MotionReceivingDocument* value) noexcept {
				self->ReleaseDocument(value);
			};
			std::unique_ptr<const MotionReceivingDocument, decltype(release)> scoped(document.release(), std::move(release));

			return std::shared_ptr<const MotionReceivingDocument>(std::move(scoped));
		}

		void RetirePending() {
			RequireOwner();
			for (;;) {
				std::unique_ptr<const MotionReceivingDocument> document;
				{
					const std::lock_guard lock(Mutex);
					if (Retiring.empty()) {
						break;
					}
					document = std::move(Retiring.front());
					Retiring.erase(Retiring.begin());
				}

				const Call call(*this);
				document.reset();
				const std::lock_guard lock(Mutex);
				--LiveDocuments;
			}
		}
	};

	MotionPreparationService::MotionPreparationService(TextureCache& cache, MotionPreparationLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(cache, limits)) {
	}

	MotionPreparationService::~MotionPreparationService() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	std::shared_ptr<const MotionReceivingDocument> MotionPreparationService::Prepare(std::vector<std::uint8_t> decoded_psb) {
		auto owner = m_Implementation;
		if (std::this_thread::get_id() == owner->OwnerThread) {
			const Implementation::Call call(*owner);

			return owner->PrepareOwner(owner, std::move(decoded_psb));
		}

		auto job = std::make_unique<Implementation::Job>();
		job->Bytes = std::move(decoded_psb);
		auto result = job->Result.get_future();
		{
			const std::lock_guard lock(owner->Mutex);
			if (owner->Stopping || owner->Closed || owner->Pending.size() == owner->Limits.MaxPendingJobs || job->Bytes.size() > owner->Limits.MaxQueuedBytes - owner->QueuedBytes) {
				throw std::runtime_error("motion texture preparation queue is closed or exceeds its pending job/byte bound");
			}

			const auto byte_count = job->Bytes.size();
			owner->Pending.push_back(std::move(job));
			owner->QueuedBytes += byte_count;
		}

		return result.get();
	}

	std::size_t MotionPreparationService::PreparePending(std::size_t max_jobs) {
		auto owner = m_Implementation;
		owner->RequireOwner();
		if (max_jobs == 0 || max_jobs > owner->Limits.MaxPendingJobs) {
			throw std::runtime_error("motion texture preparation pump requires a bounded positive job count");
		}

		owner->RetirePending();
		std::size_t completed = 0;
		while (completed < max_jobs) {
			std::unique_ptr<Implementation::Job> job;
			{
				const std::lock_guard lock(owner->Mutex);
				if (owner->Pending.empty()) {
					break;
				}

				job = std::move(owner->Pending.front());
				owner->Pending.pop_front();
				owner->QueuedBytes -= job->Bytes.size();
			}

			const Implementation::Call call(*owner);
			try {
				job->Result.set_value(owner->PrepareOwner(owner, std::move(job->Bytes)));
			} catch (...) {
				job->Result.set_exception(std::current_exception());
			}
			++completed;
		}

		return completed;
	}

	void MotionPreparationService::RetirePending() {
		m_Implementation->RetirePending();
	}

	std::size_t MotionPreparationService::PendingCount() const {
		const std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->Pending.size();
	}

	std::size_t MotionPreparationService::LiveDocumentCount() const {
		const std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->LiveDocuments;
	}

	TextureCache& MotionPreparationService::Cache() {
		m_Implementation->RequireOwner();

		return m_Implementation->Cache;
	}

	bool MotionPreparationService::IsOwnerThread() const noexcept {
		return std::this_thread::get_id() == m_Implementation->OwnerThread;
	}

	bool MotionPreparationService::CanClose() const noexcept {
		if (!IsOwnerThread() || m_Implementation->Calling) {
			return false;
		}
		const std::lock_guard lock(m_Implementation->Mutex);

		return m_Implementation->LiveDocuments == m_Implementation->Retiring.size();
	}

	void MotionPreparationService::Stop() {
		auto owner = m_Implementation;
		owner->RequireOwner();
		std::deque<std::unique_ptr<Implementation::Job>> pending;
		{
			const std::lock_guard lock(owner->Mutex);
			owner->Stopping = true;
			pending.swap(owner->Pending);
			owner->QueuedBytes = 0;
		}

		const auto failure = std::make_exception_ptr(std::runtime_error("motion texture preparation job cancelled before actual texture production"));
		for (auto& job : pending) {
			job->Result.set_exception(failure);
		}
	}

	void MotionPreparationService::Close() {
		auto owner = m_Implementation;
		if (owner->Closed && IsOwnerThread()) {
			return;
		}
		if (!CanClose()) {
			throw std::runtime_error("motion texture preparation close requires all actual returned document/provider pins to expire");
		}

		owner->RetirePending();
		Stop();
		const std::lock_guard lock(owner->Mutex);
		owner->Closed = true;
	}

} // namespace sgre::scene
