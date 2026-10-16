#include "sgre/audio/pcm_output_ring.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <thread>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::audio::PcmOutputRingDescription SmallRing(std::uint32_t channels = 2) {
	sgre::audio::PcmOutputRingDescription description;
	description.CapacityFrames = 8;
	description.MaximumTransferFrames = 8;
	description.Channels = channels;
	return description;
}

static void TestWrappedStereoAndExactBits() {
	sgre::audio::PcmOutputRing ring(SmallRing());
	const std::array<float, 12> first{1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6};
	Require(ring.Write(first) == sgre::audio::PcmOutputRingStatus::Ready, "first exact stereo submission");
	std::array<float, 8> prefix{};
	const auto before = ring.Read(prefix);
	Require(before.CopiedFrames == 4 && before.SilentFrames == 0 && prefix[6] == 4 && prefix[7] == -4, "interleaving and consumer frame positions");
	const std::array<float, 8> second{7, -7, 8, -8, 9, -9, 0.0f, -0.0f};
	Require(ring.Write(second) == sgre::audio::PcmOutputRingStatus::Ready, "wrap submission");
	std::array<float, 16> output{};
	const auto result = ring.Read(output);
	const std::array<float, 16> expected{5, -5, 6, -6, 7, -7, 8, -8, 9, -9, 0.0f, -0.0f, 0, 0, 0, 0};
	for (std::size_t index = 0; index < output.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(output[index]) == std::bit_cast<std::uint32_t>(expected[index]), "ring changed queued PCM bits or zero tail");
	}
	Require(result.Status == sgre::audio::PcmOutputRingStatus::Starved && result.CopiedFrames == 6 && result.SilentFrames == 2 && ring.BufferedFrames() == 0, "starvation is explicit and never invents consumed source frames");
}

static void TestOverflowFinishAndInvalidExtents() {
	sgre::audio::PcmOutputRing ring(SmallRing());
	std::array<float, 16> full{};
	full.fill(.25f);
	Require(ring.Write(full) == sgre::audio::PcmOutputRingStatus::Ready && ring.WritableFrames() == 0, "full ring accounting");
	const std::array<float, 2> extra{.5f, -.5f};
	Require(ring.Write(extra) == sgre::audio::PcmOutputRingStatus::Starved && ring.BufferedFrames() == 8, "overflow rejects the whole submission without overwrite");
	std::array<float, 3> invalid{7, 8, 9};
	Require(ring.Write(invalid) == sgre::audio::PcmOutputRingStatus::InvalidExtent && ring.Read(invalid).Status == sgre::audio::PcmOutputRingStatus::InvalidExtent && invalid[0] == 7 && ring.BufferedFrames() == 8, "partial frames never consume or mutate output");
	ring.Finish();
	Require(ring.Write(extra) == sgre::audio::PcmOutputRingStatus::Finished, "finished producer never accepts new PCM");
	Require(ring.Read(full).CopiedFrames == 8, "finish retains already queued tail");
	const auto drained = ring.Read(full);
	Require(drained.Status == sgre::audio::PcmOutputRingStatus::Drained && drained.CopiedFrames == 0 && drained.SilentFrames == 8, "EOF differs from starvation after all software frames drain");
	for (const auto sample : full) {
		Require(sample == 0, "drained output is zero coverage");
	}
}

static void TestConcurrentSpscFrames() {
	sgre::audio::PcmOutputRingDescription description;
	description.CapacityFrames = 1024;
	description.MaximumTransferFrames = 128;
	description.Channels = 2;
	sgre::audio::PcmOutputRing ring(description);
	constexpr std::size_t kFrames = 65536;
	std::atomic<bool> failed = false;
	std::thread consumer([&] {
		std::size_t received = 0;
		std::array<float, 94> output{};
		while (received < kFrames) {
			const auto read = ring.Read(output);
			for (std::size_t frame = 0; frame < read.CopiedFrames; ++frame) {
				const auto expected = static_cast<float>(received + frame + 1);
				if (output[frame * 2] != expected || output[frame * 2 + 1] != -expected) {
					failed.store(true);
				}
			}
			for (std::size_t sample = read.CopiedFrames * 2; sample < output.size(); ++sample) {
				if (output[sample] != 0) {
					failed.store(true);
				}
			}
			received += read.CopiedFrames;
			if (read.CopiedFrames == 0) {
				std::this_thread::yield();
			}
		}
	});
	std::array<float, 128> input{};
	for (std::size_t start = 0; start < kFrames; start += 64) {
		for (std::size_t frame = 0; frame < 64; ++frame) {
			input[frame * 2] = static_cast<float>(start + frame + 1);
			input[frame * 2 + 1] = -input[frame * 2];
		}
		while (ring.Write(input) != sgre::audio::PcmOutputRingStatus::Ready) {
			std::this_thread::yield();
		}
	}
	ring.Finish();
	consumer.join();
	Require(!failed.load() && ring.BufferedFrames() == 0, "concurrent producer/consumer changed order, channel or sample identity");
}

int main() {
	try {
		TestWrappedStereoAndExactBits();
		TestOverflowFinishAndInvalidExtents();
		TestConcurrentSpscFrames();
		std::cout << "bounded PCM ring exact frames/starvation/drain/SPSC passed; no OS audio owner\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
