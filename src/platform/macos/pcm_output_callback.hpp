#pragma once

#include "sgre/audio/pcm_output_delivery.hpp"
#include "sgre/audio/pcm_output_ring.hpp"

#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sgre::platform {
	enum class PcmDeviceCallbackFailure : std::uint32_t {
		None,
		BufferGeometry,
		ConcurrentCallback,
		RingTransfer,
		CounterOverflow,
		DeliveryQueue
	};

	// Unit disposal and callback quiescence precede state destruction. The callback owns no shared_ptr.
	struct PcmDeviceCallbackState {
		explicit PcmDeviceCallbackState(audio::PcmOutputRingDescription description, bool capture_deliveries = false, std::size_t delivery_capacity = 256) : Ring(description) {
			if (capture_deliveries) {
				Deliveries.emplace(delivery_capacity);
			}
		}
		audio::PcmOutputRing Ring;
		std::optional<audio::PcmOutputDeliveryQueue> Deliveries;
		std::uint64_t DeliverySequence = 0;
		std::atomic<bool> Enabled = false;
		std::atomic_flag Rendering = ATOMIC_FLAG_INIT;
		std::atomic<std::uint32_t> ActiveCallbacks = 0;
		std::atomic<PcmDeviceCallbackFailure> Failure = PcmDeviceCallbackFailure::None;
		std::atomic<std::uint64_t> RequestedFrames = 0;
		std::atomic<std::uint64_t> CopiedFrames = 0;
		std::atomic<std::uint64_t> SilentFrames = 0;
	};

	[[nodiscard]] OSStatus RenderPcmDeviceOutput(void* refcon, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* timestamp, UInt32 bus, UInt32 frames, AudioBufferList* output) noexcept;
} // namespace sgre::platform
