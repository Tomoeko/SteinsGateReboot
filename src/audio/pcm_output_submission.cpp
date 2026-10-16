#include "sgre/audio/pcm_output_submission.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::audio {
	class PcmOutputBufferIdentity {
	};

	PcmOutputSubmission::PcmOutputSubmission(std::weak_ptr<const PcmOutputBufferIdentity> owner, std::uint64_t sequence, std::uint64_t first, std::uint32_t frames) noexcept : m_Owner(std::move(owner)), m_Sequence(sequence), m_First(first), m_Frames(frames) {
	}
	std::uint64_t PcmOutputSubmission::Sequence() const noexcept {
		return m_Sequence;
	}
	std::uint64_t PcmOutputSubmission::FirstMasterFrame() const noexcept {
		return m_First;
	}
	std::uint32_t PcmOutputSubmission::Frames() const noexcept {
		return m_Frames;
	}
	PcmOutputDeliveryReceipt::PcmOutputDeliveryReceipt(std::weak_ptr<const PcmOutputBufferIdentity> owner, PcmOutputDelivery record, std::int64_t time100ns) noexcept : m_Owner(std::move(owner)), m_Record(record), m_Time100ns(time100ns) {
	}
	const PcmOutputDelivery& PcmOutputDeliveryReceipt::Observation() const noexcept {
		return m_Record;
	}
	std::int64_t PcmOutputDeliveryReceipt::CallbackContinuous100ns() const noexcept {
		return m_Time100ns;
	}

	PcmOutputSubmissionOwner::PcmOutputSubmissionOwner(PcmOutputRing& ring, PcmOutputDeliveryQueue* deliveries, const std::atomic<bool>& consumer_enabled, const std::atomic<std::uint32_t>& active_callbacks, std::int64_t actual_continuous_frequency) : m_Ring(ring), m_Deliveries(deliveries), m_ConsumerEnabled(consumer_enabled), m_ActiveCallbacks(active_callbacks), m_ContinuousFrequency(actual_continuous_frequency) {
		if (consumer_enabled.load(std::memory_order_acquire) || active_callbacks.load(std::memory_order_acquire) != 0 || ring.BufferedFrames() != 0 || (deliveries != nullptr && actual_continuous_frequency <= 0)) {
			throw std::runtime_error("PCM submission owner requires its actual fresh idle buffer and valid callback clock");
		}
		m_Identity = std::make_shared<const PcmOutputBufferIdentity>();
	}

	void PcmOutputSubmissionOwner::RequireOwner() const {
		if (m_Closed || std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("PCM submission requires its actual live serialized buffer owner");
		}
		if (m_Failure) {
			std::rethrow_exception(m_Failure);
		}
	}

	PcmOutputSubmissionOwner::~PcmOutputSubmissionOwner() noexcept {
		if (m_MovieOwner != nullptr) {
			std::terminate();
		}
	}
	void PcmOutputSubmissionOwner::RequireUnbound() const {
		RequireOwner();
		if (m_MovieOwner != nullptr) {
			throw std::runtime_error("PCM output is exclusively bound to its actual Movie delivery owner");
		}
	}
	void PcmOutputSubmissionOwner::RequireMovieAdmission(const media::MoviePcmDelivery* owner) const {
		RequireUnbound();
		if (owner == nullptr || m_Deliveries == nullptr || m_Finished || m_Submitted != 0 || m_ConsumerEnabled.load(std::memory_order_acquire) || m_ActiveCallbacks.load(std::memory_order_acquire) != 0) {
			throw std::runtime_error("Movie delivery binding requires its actual fresh receipt-enabled idle PCM output");
		}
	}
	void PcmOutputSubmissionOwner::BindMovie(const media::MoviePcmDelivery* owner) noexcept {
		m_MovieOwner = owner;
	}
	void PcmOutputSubmissionOwner::RequireMovie(const media::MoviePcmDelivery* owner) const {
		RequireOwner();
		if (owner == nullptr || m_MovieOwner != owner) {
			throw std::runtime_error("Movie delivery requires its exact containing PCM producer binding");
		}
	}
	void PcmOutputSubmissionOwner::ReleaseMovie(const media::MoviePcmDelivery* owner) noexcept {
		if (std::this_thread::get_id() != m_OwnerThread || m_MovieOwner != owner || !m_Closed) {
			std::terminate();
		}
		m_MovieOwner = nullptr;
	}
	PcmOutputSubmission PcmOutputSubmissionOwner::SubmitMovie(const media::MoviePcmDelivery* owner, std::span<const float> interleaved) {
		RequireMovie(owner);
		return SubmitActual(interleaved);
	}
	std::optional<PcmOutputDeliveryReceipt> PcmOutputSubmissionOwner::ReadMovieDelivery(const media::MoviePcmDelivery* owner) {
		RequireMovie(owner);
		return ReadActualDelivery();
	}
	PcmOutputSubmission PcmOutputSubmissionOwner::Submit(std::span<const float> interleaved) {
		RequireUnbound();
		return SubmitActual(interleaved);
	}
	PcmOutputSubmission PcmOutputSubmissionOwner::SubmitActual(std::span<const float> interleaved) {
		RequireOwner();
		const auto description = m_Ring.Description();
		if (m_Finished || interleaved.size() % description.Channels != 0 || interleaved.size() / description.Channels > description.MaximumTransferFrames || !std::all_of(interleaved.begin(), interleaved.end(), [](float sample) {
			    return std::isfinite(sample);
		    })) {
			throw std::runtime_error("PCM submission requires bounded finite whole master frames before input finish");
		}
		const auto count = interleaved.size() / description.Channels;
		if (count > std::numeric_limits<std::uint64_t>::max() - m_Submitted || m_SubmissionSequence == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("PCM submission identity exceeds its actual master sequence domain");
		}
		PcmOutputSubmission submission(m_Identity, m_SubmissionSequence + 1, m_Submitted, static_cast<std::uint32_t>(count));
		if (m_Ring.Write(interleaved) != PcmOutputRingStatus::Ready) {
			throw std::runtime_error("actual PCM output ring rejected the bounded master submission");
		}
		m_Submitted += count;
		++m_SubmissionSequence;

		return submission;
	}

	std::optional<PcmOutputDeliveryReceipt> PcmOutputSubmissionOwner::ReadDelivery() {
		RequireUnbound();
		return ReadActualDelivery();
	}
	std::optional<PcmOutputDeliveryReceipt> PcmOutputSubmissionOwner::ReadActualDelivery() {
		RequireOwner();
		if (m_Deliveries == nullptr) {
			throw std::runtime_error("PCM delivery identity requires its actual captured callback queue");
		}
		PcmOutputDelivery record;
		if (!m_Deliveries->TryRead(record)) {
			return std::nullopt;
		}
		try {
			if (m_DeliverySequence == std::numeric_limits<std::uint64_t>::max() || record.Sequence != m_DeliverySequence + 1 || record.FirstMasterFrame != m_Delivered || m_Delivered > m_Submitted || record.CopiedFrames > m_Submitted - m_Delivered || record.CallbackObserved.Value < m_LastCallbackTick) {
				throw std::runtime_error("PCM callback receipt lost its actual contiguous submitted interval, sequence or continuous epoch");
			}
			const auto time100ns = PcmContinuousTime100ns(record.CallbackObserved, m_ContinuousFrequency);
			PcmOutputDeliveryReceipt receipt(m_Identity, record, time100ns);
			m_Delivered += record.CopiedFrames;
			m_DeliverySequence = record.Sequence;
			m_LastCallbackTick = record.CallbackObserved.Value;

			return receipt;
		} catch (...) {
			m_Failure = std::current_exception();
			throw;
		}
	}

	void PcmOutputSubmissionOwner::RequireSubmission(const PcmOutputSubmission& submission) const {
		RequireOwner();
		if (submission.m_Owner.lock() != m_Identity || submission.m_Sequence == 0 || submission.m_Sequence > m_SubmissionSequence || submission.m_First > m_Submitted || submission.m_Frames > m_Submitted - submission.m_First) {
			throw std::runtime_error("PCM submission does not belong to this actual successful output owner");
		}
	}
	void PcmOutputSubmissionOwner::RequireDelivery(const PcmOutputDeliveryReceipt& delivery) const {
		RequireOwner();
		if (delivery.m_Owner.lock() != m_Identity || delivery.m_Record.Sequence == 0 || delivery.m_Record.Sequence > m_DeliverySequence) {
			throw std::runtime_error("PCM delivery does not belong to this actual callback-drain owner");
		}
	}
	std::uint64_t PcmOutputSubmissionOwner::SubmittedFrames() const {
		RequireOwner();

		return m_Submitted;
	}
	std::uint64_t PcmOutputSubmissionOwner::DeliveredFrames() const {
		RequireOwner();

		return m_Delivered;
	}
	void PcmOutputSubmissionOwner::FinishInput() {
		RequireUnbound();
		m_Ring.Finish();
		m_Finished = true;
	}
	void PcmOutputSubmissionOwner::Close() {
		if (m_MovieOwner != nullptr) {
			throw std::runtime_error("PCM output must retire through its actual Movie delivery binding");
		}
		CloseActual();
	}
	void PcmOutputSubmissionOwner::CloseMovie(const media::MoviePcmDelivery* owner) {
		if (owner == nullptr || owner != m_MovieOwner) {
			throw std::runtime_error("PCM retirement requires its exact Movie delivery binding");
		}
		CloseActual();
	}
	void PcmOutputSubmissionOwner::CloseActual() {
		if (m_Closed) {
			return;
		}
		if (std::this_thread::get_id() != m_OwnerThread || m_ConsumerEnabled.load(std::memory_order_acquire) || m_ActiveCallbacks.load(std::memory_order_acquire) != 0) {
			throw std::runtime_error("PCM submission identity retirement requires its actual disabled idle callback observation");
		}
		m_Identity.reset();
		m_Closed = true;
	}
} // namespace sgre::audio
