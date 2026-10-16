#include "pcm_output_callback.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <mach/mach_time.h>
#include <span>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::audio::PcmOutputRingDescription Description() {
	sgre::audio::PcmOutputRingDescription result;
	result.CapacityFrames = 8;
	result.MaximumTransferFrames = 8;
	result.Channels = 2;

	return result;
}

static AudioBufferList Output(std::span<float> samples) {
	AudioBufferList output{};
	output.mNumberBuffers = 1;
	output.mBuffers[0].mNumberChannels = 2;
	output.mBuffers[0].mDataByteSize = static_cast<UInt32>(samples.size_bytes());
	output.mBuffers[0].mData = samples.data();

	return output;
}

static void TestActualCallbackAndSeparateClocks() {
	sgre::platform::PcmDeviceCallbackState state(Description(), true, 4);
	const std::array<float, 6> source{.25f, -.5f, .75f, -1, 1.25f, -1.5f};
	Require(state.Ring.Write(source) == sgre::audio::PcmOutputRingStatus::Ready, "actual callback input must be retained before delivery");
	std::array<float, 8> samples{};
	auto output = Output(samples);
	AudioUnitRenderActionFlags flags = 0;
	sgre::audio::PcmOutputDelivery delivery;
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 4, &output) == noErr && !state.Deliveries->TryRead(delivery) && state.Ring.BufferedFrames() == 3, "disabled output must not manufacture a delivery event or consume queued PCM");
	AudioTimeStamp timestamp{};
	timestamp.mFlags = kAudioTimeStampHostTimeValid | kAudioTimeStampSampleTimeValid | kAudioTimeStampRateScalarValid;
	timestamp.mHostTime = 123;
	timestamp.mSampleTime = -7.5;
	timestamp.mRateScalar = 1.0001;
	state.Enabled.store(true);
	const auto before = mach_continuous_time();
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, &timestamp, 0, 4, &output) == noErr, "actual callback transfer must accept flagged render metadata");
	const auto after = mach_continuous_time();
	Require(state.Deliveries->TryRead(delivery) && delivery.Sequence == 1 && delivery.FirstMasterFrame == 0 && delivery.CopiedFrames == 3 && delivery.RequestedFrames == 4 && delivery.SilentFrames == 1, "receipt must distinguish copied master frames from requested/silent device frames");
	Require(delivery.CallbackObserved.Value >= before && delivery.CallbackObserved.Value <= after && delivery.RenderTimestamp.HostTimeValid && delivery.RenderTimestamp.HostTime.Value == 123 && delivery.RenderTimestamp.SampleFrameBits == std::bit_cast<std::uint64_t>(-7.5) && delivery.RenderTimestamp.RateScalarBits == std::bit_cast<std::uint64_t>(1.0001), "continuous event observation and separately flagged absolute/sample representations must not share an epoch");
	for (std::size_t sample = 0; sample < source.size(); ++sample) {
		Require(samples[sample] == source[sample], "receipt capture cannot change actual Float32 PCM/gain/interleaving");
	}
	Require(samples[6] == 0 && samples[7] == 0, "underflow tail remains actual silence");
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 4, &output) == noErr && state.Deliveries->TryRead(delivery) && delivery.Sequence == 2 && delivery.FirstMasterFrame == 3 && delivery.CopiedFrames == 0 && delivery.SilentFrames == 4 && !delivery.RenderTimestamp.HostTimeValid && !delivery.RenderTimestamp.SampleTimeValid && !delivery.RenderTimestamp.RateScalarValid, "pure underflow reports no invented source/master progress or timestamp validity");
	Require(state.ActiveCallbacks.load() == 0 && state.Failure.load() == sgre::platform::PcmDeviceCallbackFailure::None, "actual callback and receipt writer must be quiescent after return");
}

static void TestFullReceiptQueueBeforePcmConsumption() {
	sgre::platform::PcmDeviceCallbackState state(Description(), true, 1);
	state.Enabled.store(true);
	const std::array<float, 4> source{1, -1, .5f, -.5f};
	std::array<float, 4> samples{};
	auto output = Output(samples);
	AudioUnitRenderActionFlags flags = 0;
	Require(state.Ring.Write(source) == sgre::audio::PcmOutputRingStatus::Ready && sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 2, &output) == noErr, "first genuine receipt fills its fixed owner queue");
	Require(state.Ring.Write(source) == sgre::audio::PcmOutputRingStatus::Ready, "second PCM submission remains a real pending copy owner");
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 2, &output) != noErr && state.Failure.load() == sgre::platform::PcmDeviceCallbackFailure::DeliveryQueue && state.Ring.BufferedFrames() == 2 && samples[0] == 0 && samples[3] == 0 && state.CopiedFrames.load() == 2, "receipt overflow must latch a precise failure before consuming unreceipted PCM");
	sgre::audio::PcmOutputDelivery receipt;
	Require(state.Deliveries->TryRead(receipt) && receipt.Sequence == 1 && receipt.CopiedFrames == 2 && !state.Deliveries->TryRead(receipt), "a failed second event cannot overwrite the first actual receipt");
	output.mNumberBuffers = 0;
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 2, &output) != noErr && state.Failure.load() == sgre::platform::PcmDeviceCallbackFailure::DeliveryQueue && state.ActiveCallbacks.load() == 0, "subsequent geometry failure must preserve the actual first cause");
}

static void TestTimestampFlagsAndSequenceBoundary() {
	sgre::platform::PcmDeviceCallbackState state(Description(), true, 2);
	state.Enabled.store(true);
	std::array<float, 4> samples{};
	auto output = Output(samples);
	AudioUnitRenderActionFlags flags = 0;
	AudioTimeStamp timestamp{};
	timestamp.mFlags = kAudioTimeStampSampleTimeValid;
	timestamp.mHostTime = 999;
	timestamp.mSampleTime = .25;
	timestamp.mRateScalar = 8;
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, &timestamp, 0, 2, &output) == noErr, "sample-only metadata is a real independent representation");
	sgre::audio::PcmOutputDelivery receipt;
	Require(state.Deliveries->TryRead(receipt) && !receipt.RenderTimestamp.HostTimeValid && receipt.RenderTimestamp.HostTime.Value == 0 && receipt.RenderTimestamp.SampleTimeValid && receipt.RenderTimestamp.SampleFrameBits == std::bit_cast<std::uint64_t>(.25) && !receipt.RenderTimestamp.RateScalarValid && receipt.RenderTimestamp.RateScalarBits == 0, "invalid timestamp representations must not be read or admitted from nonzero stale fields");
	state.DeliverySequence = std::numeric_limits<std::uint64_t>::max();
	const std::array<float, 2> source{1, -1};
	Require(state.Ring.Write(source) == sgre::audio::PcmOutputRingStatus::Ready && sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 2, &output) != noErr && state.Ring.BufferedFrames() == 1 && !state.Deliveries->TryRead(receipt), "event sequence overflow cannot wrap or consume an unidentifiable actual source interval");
}

int main() {
	try {
		TestActualCallbackAndSeparateClocks();
		TestFullReceiptQueueBeforePcmConsumption();
		TestTimestampFlagsAndSequenceBoundary();
		std::cout << "three actual callback receipt groups passed; no AudioUnit/device was created or started\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
