#include "pcm_output_callback.hpp"

#include <algorithm>
#include <limits>
#include <span>

namespace sgre::platform {
	static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
	static_assert(std::atomic<PcmDeviceCallbackFailure>::is_always_lock_free);

	static OSStatus FailPcmDeviceCallback(PcmDeviceCallbackState& state, PcmDeviceCallbackFailure failure) noexcept {
		auto expected = PcmDeviceCallbackFailure::None;
		state.Failure.compare_exchange_strong(expected, failure, std::memory_order_relaxed);
		return kAudio_ParamError;
	}

	OSStatus RenderPcmDeviceOutput(void* refcon, AudioUnitRenderActionFlags* flags, const AudioTimeStamp*, UInt32 bus, UInt32 frames, AudioBufferList* output) noexcept {
		if (refcon == nullptr) {
			return kAudio_ParamError;
		}
		auto& state = *static_cast<PcmDeviceCallbackState*>(refcon);
		state.ActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
		const auto finish = [&](OSStatus status) noexcept {
			state.ActiveCallbacks.fetch_sub(1, std::memory_order_release);
			return status;
		};
		const auto description = state.Ring.Description();
		const auto sample_count = static_cast<std::size_t>(frames) * description.Channels;
		if (flags == nullptr || output == nullptr || bus != 0 || frames > description.MaximumTransferFrames || output->mNumberBuffers != 1 || output->mBuffers[0].mNumberChannels != description.Channels || output->mBuffers[0].mData == nullptr || output->mBuffers[0].mDataByteSize < sample_count * sizeof(float)) {
			return finish(FailPcmDeviceCallback(state, PcmDeviceCallbackFailure::BufferGeometry));
		}
		auto samples = std::span(static_cast<float*>(output->mBuffers[0].mData), sample_count);
		if (!state.Enabled.load(std::memory_order_acquire)) {
			std::fill(samples.begin(), samples.end(), 0.0f);
			*flags |= kAudioUnitRenderAction_OutputIsSilence;
			return finish(noErr);
		}
		if (state.Rendering.test_and_set(std::memory_order_acquire)) {
			std::fill(samples.begin(), samples.end(), 0.0f);
			*flags |= kAudioUnitRenderAction_OutputIsSilence;
			return finish(FailPcmDeviceCallback(state, PcmDeviceCallbackFailure::ConcurrentCallback));
		}
		const auto read = state.Ring.Read(samples);
		OSStatus status = noErr;
		const auto requested = state.RequestedFrames.load(std::memory_order_relaxed);
		const auto copied = state.CopiedFrames.load(std::memory_order_relaxed);
		const auto silent = state.SilentFrames.load(std::memory_order_relaxed);
		if (read.Status == audio::PcmOutputRingStatus::InvalidExtent) {
			status = FailPcmDeviceCallback(state, PcmDeviceCallbackFailure::RingTransfer);
		} else if (frames > std::numeric_limits<std::uint64_t>::max() - requested || read.CopiedFrames > std::numeric_limits<std::uint64_t>::max() - copied || read.SilentFrames > std::numeric_limits<std::uint64_t>::max() - silent) {
			status = FailPcmDeviceCallback(state, PcmDeviceCallbackFailure::CounterOverflow);
		} else {
			state.RequestedFrames.store(requested + frames, std::memory_order_relaxed);
			state.CopiedFrames.store(copied + read.CopiedFrames, std::memory_order_relaxed);
			state.SilentFrames.store(silent + read.SilentFrames, std::memory_order_relaxed);
		}
		if (read.CopiedFrames == 0) {
			*flags |= kAudioUnitRenderAction_OutputIsSilence;
		} else {
			*flags &= ~static_cast<AudioUnitRenderActionFlags>(kAudioUnitRenderAction_OutputIsSilence);
		}
		state.Rendering.clear(std::memory_order_release);
		return finish(status);
	}
} // namespace sgre::platform
