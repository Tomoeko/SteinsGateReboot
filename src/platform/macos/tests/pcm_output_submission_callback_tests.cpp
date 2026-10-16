#include "pcm_output_callback.hpp"

#include "sgre/audio/pcm_output_submission.hpp"
#include "sgre/platform/host_performance_counter.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::audio::PcmOutputRingDescription Description() {
	sgre::audio::PcmOutputRingDescription description;
	description.CapacityFrames = 8;
	description.MaximumTransferFrames = 8;
	description.Channels = 2;

	return description;
}

static AudioBufferList Output(std::span<float> samples) {
	AudioBufferList output{};
	output.mNumberBuffers = 1;
	output.mBuffers[0].mNumberChannels = 2;
	output.mBuffers[0].mDataByteSize = static_cast<UInt32>(samples.size_bytes());
	output.mBuffers[0].mData = samples.data();

	return output;
}

int main() {
	try {
		const sgre::platform::HostPerformanceCounter counter;
		sgre::platform::PcmDeviceCallbackState state(Description(), true, 8);
		sgre::audio::PcmOutputSubmissionOwner producer(state.Ring, &*state.Deliveries, state.Enabled, state.ActiveCallbacks, counter.Frequency());
		const std::array<float, 6> first{1, -1, .75f, -.75f, .5f, -.5f};
		const auto submitted = producer.Submit(first);
		Require(submitted.FirstMasterFrame() == 0 && submitted.Frames() == 3 && !producer.ReadDelivery(), "production ring write is genuine but cannot report a callback delivery");
		std::array<float, 4> samples{};
		auto output = Output(samples);
		AudioUnitRenderActionFlags flags = 0;
		state.Enabled.store(true);
		Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 2, &output) == noErr, "actual production callback must consume the exact submitted master ring");
		const auto partial = producer.ReadDelivery();
		Require(partial && partial->Observation().CopiedFrames == 2 && partial->Observation().SilentFrames == 0 && partial->Observation().FirstMasterFrame == submitted.FirstMasterFrame() && samples[0] == first[0] && samples[3] == first[3], "first private receipt must name real callback-copied samples without changing PCM");
		producer.RequireDelivery(*partial);
		const auto expected = sgre::audio::PcmContinuousTime100ns(partial->Observation().CallbackObserved, counter.Frequency());
		Require(partial->CallbackContinuous100ns() == expected && !partial->Observation().RenderTimestamp.HostTimeValid, "actual continuous callback ticks use the captured real frequency with no fabricated absolute render timestamp");
		const std::array<float, 4> second{.25f, -.25f, .125f, -.125f};
		const auto next = producer.Submit(second);
		Require(next.FirstMasterFrame() == 3 && next.Sequence() == submitted.Sequence() + 1, "second actual submission must follow the first produced interval, not the device request counter");
		producer.FinishInput();
		std::array<float, 8> tail{};
		output = Output(tail);
		Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 4, &output) == noErr, "actual callback drains the queued tail after producer finish");
		const auto final = producer.ReadDelivery();
		Require(final && final->Observation().FirstMasterFrame == 2 && final->Observation().CopiedFrames == 3 && final->Observation().SilentFrames == 1 && producer.DeliveredFrames() == 5 && tail[0] == first[4] && tail[2] == second[0] && tail[5] == second[3] && tail[6] == 0 && tail[7] == 0, "actual callback identity must bridge submission boundaries while excluding silent underflow");
		Require(state.Failure.load() == sgre::platform::PcmDeviceCallbackFailure::None && state.ActiveCallbacks.load() == 0, "callback first-failure and actual idle accounting remain intact");
		state.Enabled.store(false);
		producer.Close();
		std::cout << "actual callback plus successful submission owner passed; no AudioUnit/device construction/start\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
