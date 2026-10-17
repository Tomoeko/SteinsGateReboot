#pragma once

#include "sgre/audio/pcm_output_submission.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sgre::platform {
	enum class MacPcmDeliveryMode {
		None,
		CallbackEvents
	};

	struct MacPcmOutputDescription {
		std::uint32_t SamplesPerSecond = 48000;
		std::uint32_t Channels = 2;
		std::uint32_t MaximumCallbackFrames = 4096;
		std::size_t CapacityFrames = 8192;
		std::size_t TargetBufferedFrames = 4096;
		// Unity preserves raw PCM. Games may opt into the separate authored level/peak policy.
		float OutputGain = 1;
		// Existing callers remain unchanged until a real owner explicitly drains callback receipts.
		MacPcmDeliveryMode DeliveryMode = MacPcmDeliveryMode::None;
		std::size_t DeliveryCapacity = 256;
	};

	struct MacPcmOutputSnapshot {
		std::uint64_t SubmittedFrames = 0;
		std::uint64_t RequestedFrames = 0;
		std::uint64_t CopiedFrames = 0;
		std::uint64_t SilentFrames = 0;
		std::size_t BufferedFrames = 0;
		std::uint32_t DeviceIdentity = 0;
		double DeviceSamplesPerSecond = 0;
		std::uint32_t DeviceChannels = 0;
		bool Started = false;
		bool InputFinished = false;
	};

	// Explicit audible owner. Headless callers must never construct this class.
	// The serialized producer supplies already mixed interleaved Float32 PCM; no decoder/scene callbacks enter the device thread.
	class MacPcmOutput {
	public:
		explicit MacPcmOutput(MacPcmOutputDescription description);
		~MacPcmOutput() noexcept;
		MacPcmOutput(const MacPcmOutput&) = delete;
		MacPcmOutput& operator=(const MacPcmOutput&) = delete;
		[[nodiscard]] MacPcmOutputDescription Description() const noexcept;
		[[nodiscard]] std::size_t FramesToTarget();
		audio::PcmOutputSubmission Submit(std::span<const float> interleaved);
		// Start requires a real prefilled target. Construction/Submit never start the device.
		void Start();
		// Authored post-limiter master control, covering both game Sound and Movie audio.
		void SetMasterVolume(float linear_volume);
		void FinishInput();
		// This observes the software ring, not final DAC/device latency or native XAudio2 drain parity.
		[[nodiscard]] MacPcmOutputSnapshot Snapshot();
		// Only explicit CallbackEvents mode admits this sole consumer; records are software delivery, not DAC completion.
		[[nodiscard]] std::optional<audio::PcmOutputDeliveryReceipt> ReadDelivery();
		void RequireSubmission(const audio::PcmOutputSubmission& submission);
		void RequireDelivery(const audio::PcmOutputDeliveryReceipt& delivery);
		[[nodiscard]] std::int64_t ContinuousClockFrequency();
		void Close();

	private:
		friend class media::MoviePcmDelivery;
		void RequireMovieAdmission(const media::MoviePcmDelivery* owner);
		void BindMovie(const media::MoviePcmDelivery* owner) noexcept;
		[[nodiscard]] audio::PcmOutputSubmission SubmitMovie(const media::MoviePcmDelivery* owner, std::span<const float> interleaved);
		[[nodiscard]] std::optional<audio::PcmOutputDeliveryReceipt> ReadMovieDelivery(const media::MoviePcmDelivery* owner);
		void CloseMovie(const media::MoviePcmDelivery* owner);
		[[nodiscard]] bool MovieRetired() const noexcept;
		void ReleaseMovie(const media::MoviePcmDelivery* owner) noexcept;
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::platform
