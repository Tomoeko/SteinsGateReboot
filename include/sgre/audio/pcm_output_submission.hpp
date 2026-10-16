#pragma once

#include "sgre/audio/pcm_output_delivery.hpp"
#include "sgre/audio/pcm_output_ring.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <thread>

namespace sgre::media {
	class MoviePcmDelivery;
}
namespace sgre::platform {
	class MacPcmOutput;
}

namespace sgre::audio {
	class PcmOutputBufferIdentity;
	class PcmOutputSubmissionOwner;

	// Only a successful real ring write constructs this value. It retains no PCM or output owner.
	class PcmOutputSubmission {
	public:
		[[nodiscard]] std::uint64_t Sequence() const noexcept;
		[[nodiscard]] std::uint64_t FirstMasterFrame() const noexcept;
		[[nodiscard]] std::uint32_t Frames() const noexcept;

	private:
		PcmOutputSubmission(std::weak_ptr<const PcmOutputBufferIdentity> owner, std::uint64_t sequence, std::uint64_t first, std::uint32_t frames) noexcept;
		std::weak_ptr<const PcmOutputBufferIdentity> m_Owner;
		std::uint64_t m_Sequence;
		std::uint64_t m_First;
		std::uint32_t m_Frames;
		friend class PcmOutputSubmissionOwner;
	};

	// Constructed only while draining this actual callback queue, never from an arbitrary public record.
	class PcmOutputDeliveryReceipt {
	public:
		[[nodiscard]] const PcmOutputDelivery& Observation() const noexcept;
		[[nodiscard]] std::int64_t CallbackContinuous100ns() const noexcept;

	private:
		PcmOutputDeliveryReceipt(std::weak_ptr<const PcmOutputBufferIdentity> owner, PcmOutputDelivery record, std::int64_t time100ns) noexcept;
		std::weak_ptr<const PcmOutputBufferIdentity> m_Owner;
		PcmOutputDelivery m_Record;
		std::int64_t m_Time100ns;
		friend class PcmOutputSubmissionOwner;
	};

	// The actual software-output buffer encloses every borrowed ring/queue/flag below. Its serialized
	// producer is the sole writer; the real callback remains the sole ring/receipt producer-consumer peer.
	// This controls PCM submission/delivery identity. It does not certify an OS device or source readiness.
	class PcmOutputSubmissionOwner {
	public:
		PcmOutputSubmissionOwner(PcmOutputRing& ring, PcmOutputDeliveryQueue* deliveries, const std::atomic<bool>& consumer_enabled, const std::atomic<std::uint32_t>& active_callbacks, std::int64_t actual_continuous_frequency);
		~PcmOutputSubmissionOwner() noexcept;
		PcmOutputSubmissionOwner(const PcmOutputSubmissionOwner&) = delete;
		PcmOutputSubmissionOwner& operator=(const PcmOutputSubmissionOwner&) = delete;
		[[nodiscard]] PcmOutputSubmission Submit(std::span<const float> interleaved);
		[[nodiscard]] std::optional<PcmOutputDeliveryReceipt> ReadDelivery();
		void RequireSubmission(const PcmOutputSubmission& submission) const;
		void RequireDelivery(const PcmOutputDeliveryReceipt& delivery) const;
		[[nodiscard]] std::uint64_t SubmittedFrames() const;
		[[nodiscard]] std::uint64_t DeliveredFrames() const;
		void FinishInput();
		// An actual OS owner must dispose/detach and establish quiescence before destroying buffer storage.
		// This narrower close only invalidates software submission identities after the observed idle fence.
		void Close();

	private:
		friend class media::MoviePcmDelivery;
		friend class platform::MacPcmOutput;
		void RequireOwner() const;
		void RequireUnbound() const;
		void RequireMovieAdmission(const media::MoviePcmDelivery* owner) const;
		void BindMovie(const media::MoviePcmDelivery* owner) noexcept;
		void RequireMovie(const media::MoviePcmDelivery* owner) const;
		void ReleaseMovie(const media::MoviePcmDelivery* owner) noexcept;
		[[nodiscard]] PcmOutputSubmission SubmitMovie(const media::MoviePcmDelivery* owner, std::span<const float> interleaved);
		[[nodiscard]] std::optional<PcmOutputDeliveryReceipt> ReadMovieDelivery(const media::MoviePcmDelivery* owner);
		void CloseMovie(const media::MoviePcmDelivery* owner);
		[[nodiscard]] PcmOutputSubmission SubmitActual(std::span<const float> interleaved);
		[[nodiscard]] std::optional<PcmOutputDeliveryReceipt> ReadActualDelivery();
		void CloseActual();
		const media::MoviePcmDelivery* m_MovieOwner = nullptr;
		PcmOutputRing& m_Ring;
		PcmOutputDeliveryQueue* m_Deliveries;
		const std::atomic<bool>& m_ConsumerEnabled;
		const std::atomic<std::uint32_t>& m_ActiveCallbacks;
		const std::int64_t m_ContinuousFrequency;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		std::shared_ptr<const PcmOutputBufferIdentity> m_Identity;
		std::exception_ptr m_Failure;
		std::uint64_t m_Submitted = 0;
		std::uint64_t m_SubmissionSequence = 0;
		std::uint64_t m_Delivered = 0;
		std::uint64_t m_DeliverySequence = 0;
		std::uint64_t m_LastCallbackTick = 0;
		bool m_Finished = false;
		bool m_Closed = false;
	};
} // namespace sgre::audio
