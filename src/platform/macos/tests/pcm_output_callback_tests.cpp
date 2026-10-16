#include "pcm_output_callback.hpp"

#include "sgre/platform/macos_pcm_output.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::audio::PcmOutputRingDescription CallbackDescription() {
	sgre::audio::PcmOutputRingDescription result;
	result.CapacityFrames = 8;
	result.MaximumTransferFrames = 8;
	result.Channels = 2;
	return result;
}

static AudioBufferList Output(std::span<float> samples, std::uint32_t channels = 2) {
	AudioBufferList result{};
	result.mNumberBuffers = 1;
	result.mBuffers[0].mNumberChannels = channels;
	result.mBuffers[0].mDataByteSize = static_cast<UInt32>(samples.size_bytes());
	result.mBuffers[0].mData = samples.data();
	return result;
}

static void TestActualCallbackPcmAndStarvation() {
	sgre::platform::PcmDeviceCallbackState state(CallbackDescription());
	const std::array<float, 6> source{.125f, -.25f, .5f, -.75f, 1.25f, -1.5f};
	Require(state.Ring.Write(source) == sgre::audio::PcmOutputRingStatus::Ready, "callback source submission");
	std::array<float, 10> guarded{};
	guarded.fill(97);
	auto output = Output(std::span(guarded).subspan(1, 8));
	AudioUnitRenderActionFlags flags = 0;
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 4, &output) == noErr && state.Ring.BufferedFrames() == 3, "disabled callback does not consume real input");
	Require((flags & kAudioUnitRenderAction_OutputIsSilence) != 0 && guarded[0] == 97 && guarded[9] == 97, "disabled callback silence and extents");
	state.Enabled.store(true);
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 4, &output) == noErr, "real callback transfer");
	for (std::size_t sample = 0; sample < source.size(); ++sample) {
		Require(guarded[sample + 1] == source[sample], "callback changed float samples/interleaving/gain");
	}
	Require(guarded[7] == 0 && guarded[8] == 0 && guarded[0] == 97 && guarded[9] == 97, "callback fills only missing software frames with zero");
	Require(state.CopiedFrames.load() == 3 && state.SilentFrames.load() == 1 && state.RequestedFrames.load() == 4 && state.ActiveCallbacks.load() == 0 && (flags & kAudioUnitRenderAction_OutputIsSilence) == 0, "callback accounting and mixed silence flag");
	state.Ring.Finish();
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 4, &output) == noErr && (flags & kAudioUnitRenderAction_OutputIsSilence) != 0, "drained callback remains explicit silence");
}

static void TestInvalidAndConcurrentCallback() {
	sgre::platform::PcmDeviceCallbackState state(CallbackDescription());
	state.Enabled.store(true);
	std::array<float, 8> samples{};
	samples.fill(37);
	auto output = Output(samples, 1);
	AudioUnitRenderActionFlags flags = 0;
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 4, &output) != noErr && state.Failure.load() == sgre::platform::PcmDeviceCallbackFailure::BufferGeometry && samples[0] == 37, "invalid device extent is rejected before write");
	state.Failure.store(sgre::platform::PcmDeviceCallbackFailure::None);
	output = Output(samples);
	state.Rendering.test_and_set();
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 4, &output) != noErr && state.Failure.load() == sgre::platform::PcmDeviceCallbackFailure::ConcurrentCallback, "concurrent callbacks never become multiple ring consumers");
	state.Rendering.clear();
	Require(state.ActiveCallbacks.load() == 0 && samples[0] == 0 && state.RequestedFrames.load() == 0, "rejected concurrent callback releases active count and emits bounded silence");
}

static void TestDefaultTargetRepeatedMaximumPulls() {
	const sgre::platform::MacPcmOutputDescription description;
	Require(description.Channels == 2 && description.MaximumCallbackFrames == 4096 && description.TargetBufferedFrames >= description.MaximumCallbackFrames, "default owner target must cover one complete admitted maximum pull");
	sgre::audio::PcmOutputRingDescription ring;
	ring.Channels = description.Channels;
	ring.CapacityFrames = description.CapacityFrames;
	ring.MaximumTransferFrames = description.MaximumCallbackFrames;
	sgre::platform::PcmDeviceCallbackState state(ring);
	state.Enabled.store(true);
	std::array<float, 128> producer{};
	std::array<float, 8192> device{};
	std::size_t produced_frames = 0;
	std::size_t consumed_frames = 0;
	AudioUnitRenderActionFlags flags = 0;
	for (std::size_t pull = 0; pull < 24; ++pull) {
		const auto buffered = state.Ring.BufferedFrames();
		Require(buffered <= description.TargetBufferedFrames, "producer observes its own bounded target");
		auto remaining = description.TargetBufferedFrames - buffered;
		while (remaining != 0) {
			const auto chunk = std::min(remaining, producer.size() / description.Channels);
			for (std::size_t frame = 0; frame < chunk; ++frame) {
				const auto value = static_cast<float>(produced_frames + frame + 1);
				producer[frame * 2] = value;
				producer[frame * 2 + 1] = -value;
			}
			Require(state.Ring.Write(std::span(producer).first(chunk * description.Channels)) == sgre::audio::PcmOutputRingStatus::Ready, "producer-to-target refill must be complete");
			produced_frames += chunk;
			remaining -= chunk;
		}
		auto output = Output(device);
		Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, description.MaximumCallbackFrames, &output) == noErr, "actual production callback accepts its full maximum extent");
		for (std::size_t frame = 0; frame < description.MaximumCallbackFrames; ++frame) {
			const auto expected = static_cast<float>(consumed_frames + frame + 1);
			Require(device[frame * 2] == expected && device[frame * 2 + 1] == -expected, "repeated maximum callbacks must not add systematic zero gaps or slow source progress");
		}
		consumed_frames += description.MaximumCallbackFrames;
	}
	Require(state.CopiedFrames.load() == consumed_frames && state.RequestedFrames.load() == consumed_frames && state.SilentFrames.load() == 0 && state.ActiveCallbacks.load() == 0, "producer/callback accounting remains exact after repeated wrapped maximum pulls");
}

static void TestAuthoredMasteringAtFinalCallback() {
	sgre::platform::PcmDeviceCallbackState state(CallbackDescription());
	state.Leveler.emplace(48000, 2, 2);
	state.Enabled.store(true);
	Require(state.Ring.Write(std::array<float, 4>{0.125f, -0.25f, 2, -1}) == sgre::audio::PcmOutputRingStatus::Ready, "already mixed source/movie data enters the same software ring");
	std::array<float, 6> samples{};
	auto output = Output(samples);
	AudioUnitRenderActionFlags flags = 0;
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 3, &output) == noErr, "authored output policy must run through the production callback without creating a device");
	Require(samples[0] == 0.25f && samples[1] == -0.5f && samples[2] == 0.98f && samples[3] == -0.49f && samples[4] == 0 && samples[5] == 0, "callback must level the final mix once, link stereo peaks and retain missing-frame silence");
	Require(state.CopiedFrames.load() == 2 && state.SilentFrames.load() == 1, "leveling must preserve actual frame accounting and delivery clocks");
	Require(state.Ring.Write(std::array<float, 2>{std::numeric_limits<float>::quiet_NaN(), 1}) == sgre::audio::PcmOutputRingStatus::Ready, "invalid sample fixture enters the real producer ring");
	Require(sgre::platform::RenderPcmDeviceOutput(&state, &flags, nullptr, 0, 3, &output) != noErr && state.Failure.load() == sgre::platform::PcmDeviceCallbackFailure::InvalidSamples && std::all_of(samples.begin(), samples.end(), [](float sample) {
		return sample == 0;
	}),
	    "callback must silence invalid samples and report failure without throwing across the OS boundary");
}

int main() {
	try {
		TestDefaultTargetRepeatedMaximumPulls();
		TestActualCallbackPcmAndStarvation();
		TestInvalidAndConcurrentCallback();
		TestAuthoredMasteringAtFinalCallback();
		std::cout << "production AudioUnit callback CPU contract passed; no AudioUnit/device was created or started\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
