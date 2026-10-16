#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::platform {
	struct MacPcmOutputDescription {
		std::uint32_t SamplesPerSecond = 48000;
		std::uint32_t Channels = 2;
		std::uint32_t MaximumCallbackFrames = 4096;
		std::size_t CapacityFrames = 8192;
		std::size_t TargetBufferedFrames = 4096;
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
		void Submit(std::span<const float> interleaved);
		// Start requires a real prefilled target. Construction/Submit never start the device.
		void Start();
		void FinishInput();
		// This observes the software ring, not final DAC/device latency or native XAudio2 drain parity.
		[[nodiscard]] MacPcmOutputSnapshot Snapshot();
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::platform
