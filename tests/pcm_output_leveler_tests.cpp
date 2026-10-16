#include "sgre/audio/pcm_output_leveler.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestQuietSignalAndStereoPeaks() {
	sgre::audio::PcmOutputLeveler leveler(48000, 2, 2);
	std::array<float, 6> quiet{0.125f, -0.25f, 0.0f, -0.0f, -0.2f, 0.1f};
	Require(leveler.Process(quiet) && quiet == std::array<float, 6>{0.25f, -0.5f, 0.0f, -0.0f, -0.4f, 0.2f}, "quiet mixed PCM must receive exactly the authored gain without latency or balancing changes");
	std::array<float, 4> transient{2.0f, -0.5f, -3.0f, 0.75f};
	Require(leveler.Process(transient) && std::abs(transient[0] - 0.98f) < 0.000001f && std::abs(transient[2] + 0.98f) < 0.000001f, "immediate mixed peaks must remain within the configured ceiling");
	Require(transient[1] == -transient[0] * 0.25f && transient[3] == -transient[2] * 0.25f, "one shared frame gain must retain stereo image and phase across an overload");
	std::array<float, 2> recovery{0.1f, -0.1f};
	Require(leveler.Process(recovery) && recovery[0] > 0 && recovery[0] < 0.2f && recovery[1] == -recovery[0], "limiter release must preserve linked channels and avoid an immediate gain jump");
}

static void TestContinuousTimeAcrossChunks() {
	std::vector<float> whole(12000);
	for (std::size_t frame = 0; frame != whole.size() / 2; ++frame) {
		const auto sample = static_cast<float>(std::sin(static_cast<double>(frame) * 0.07) * (frame < 100 ? 1.4 : 0.1));
		whole[frame * 2] = sample;
		whole[frame * 2 + 1] = -sample * 0.5f;
	}
	auto chunks = whole;
	sgre::audio::PcmOutputLeveler reference(48000, 2, 2);
	sgre::audio::PcmOutputLeveler sliced(48000, 2, 2);
	Require(reference.Process(whole), "continuous reference signal");
	std::size_t offset = 0;
	while (offset != chunks.size()) {
		const auto count = std::min<std::size_t>(chunks.size() - offset, 74);
		Require(sliced.Process(std::span(chunks).subspan(offset, count)), "bounded callback chunk");
		offset += count;
	}
	Require(whole == chunks, "mastering envelope must remain identical across callback chunk boundaries");
	Require(std::all_of(whole.begin(), whole.end(), [](float sample) {
		return std::isfinite(sample) && std::abs(sample) <= 0.98f;
	}),
	    "finite multibus transients must never exceed the configured digital ceiling");
}

static void TestInvalidInputPreservesState() {
	sgre::audio::PcmOutputLeveler leveler(48000, 2, 2);
	sgre::audio::PcmOutputLeveler reference(48000, 2, 2);
	std::array<float, 2> invalid{2, std::numeric_limits<float>::quiet_NaN()};
	const auto bits = std::bit_cast<std::array<std::uint32_t, 2>>(invalid);
	Require(!leveler.Process(invalid) && std::bit_cast<std::array<std::uint32_t, 2>>(invalid) == bits, "nonfinite input must reject before changing any sample");
	std::array<float, 1> short_frame{2};
	Require(!leveler.Process(short_frame) && short_frame[0] == 2, "partial channel frames must reject before mutation");
	std::array<float, 2> after{0.1f, 0.2f};
	auto expected = after;
	Require(leveler.Process(after) && reference.Process(expected) && after == expected, "failed input must leave the envelope unchanged");
	for (const auto gain : {0.0f, -1.0f, 17.0f, std::numeric_limits<float>::infinity()}) {
		bool rejected = false;
		try {
			sgre::audio::PcmOutputLeveler invalid_leveler(48000, 2, gain);
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected, "invalid output gain must reject at setup");
	}
}

int main() {
	try {
		TestQuietSignalAndStereoPeaks();
		TestContinuousTimeAcrossChunks();
		TestInvalidInputPreservesState();
		std::cout << "PCM output gain, linked peak protection, continuous release and invalid-input contracts passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
