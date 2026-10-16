#include "sgre/audio/voice_allocation.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::audio::SoundSlotCandidate Candidate(std::uint32_t id, float priority, std::span<const std::uint8_t> slots) {
	sgre::audio::SoundSlotCandidate cue;
	cue.CueId = id;
	cue.Type = sgre::audio::SoundVoiceType::Buffered8;
	cue.Priority = priority;
	cue.Slots = slots;

	return cue;
}

static void TestSelectionAndEviction() {
	std::array<bool, 8> free = {false, false, true, false, false, false, true, false};
	const std::array<std::uint8_t, 3> first_slots = {4, 1, 7};
	const std::array<std::uint8_t, 2> second_slots = {3, 5};
	const std::array<sgre::audio::SoundSlotCandidate, 2> active = {Candidate(101, 2.0f, first_slots), Candidate(102, 1.0f, second_slots)};
	const auto direct = sgre::audio::SelectSoundVoiceSlots(sgre::audio::SoundVoiceType::Buffered8, 2, 0, free, active);
	Require(direct.Accepted && direct.SlotCount == 2 && direct.Slots[0] == 2 && direct.Slots[1] == 6 && direct.EvictionCount == 0, "free slots were not selected in ascending order");
	free[2] = false;
	free[6] = false;
	const auto partial = sgre::audio::SelectSoundVoiceSlots(sgre::audio::SoundVoiceType::Buffered8, 1, 2, free, active);
	Require(partial.Accepted && partial.SlotCount == 3 && partial.Slots[0] == 4 && partial.Slots[1] == 1 && partial.Slots[2] == 7 && partial.EvictionCount == 1 && partial.EvictedCues[0] == 101, "whole-cue eviction did not retain its channel order or unused tail");
	const auto protected_first = sgre::audio::SelectSoundVoiceSlots(sgre::audio::SoundVoiceType::Buffered8, 2, 1, free, active);
	Require(protected_first.Accepted && protected_first.Slots[0] == 3 && protected_first.Slots[1] == 5 && protected_first.EvictedCues[0] == 102, "a higher-priority cue was evicted");
	const auto failed = sgre::audio::SelectSoundVoiceSlots(sgre::audio::SoundVoiceType::Buffered8, 6, 2, free, active);
	Require(!failed.Accepted && failed.SlotCount == 0 && failed.EvictionCount == 2 && failed.EvictedCues[0] == 101 && failed.EvictedCues[1] == 102, "failed native allocation rolled back its ordered evictions");
	const auto oversized = sgre::audio::SelectSoundVoiceSlots(sgre::audio::SoundVoiceType::Buffered8, 9, 2, free, active);
	Require(!oversized.Accepted && oversized.EvictionCount == 2, "request above pool capacity did not preserve native failed-allocation effects");
	free[1] = true;
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::SelectSoundVoiceSlots(sgre::audio::SoundVoiceType::Buffered8, 1, 2, free, active));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "incoherent active/free slot overlap was accepted");
}

static std::vector<std::uint8_t> ReadFile(const char* path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	const auto length = stream.tellg();
	if (!stream || length < 0 || length > 2 * 1024 * 1024) {
		throw std::runtime_error("could not open a bounded sound slot vector file");
	}

	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!stream) {
		throw std::runtime_error("sound slot vector read failed");
	}

	return bytes;
}

class VectorReader {
public:
	explicit VectorReader(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
	}

	std::uint32_t Integer(std::size_t width) {
		if (width > 4 || m_Position > m_Bytes.size() || m_Bytes.size() - m_Position < width) {
			throw std::runtime_error("native sound slot vector is truncated");
		}

		std::uint32_t result = 0;
		for (std::size_t index = 0; index < width; ++index) {
			result |= static_cast<std::uint32_t>(m_Bytes[m_Position++]) << (8 * index);
		}

		return result;
	}

	float Number() {
		return std::bit_cast<float>(Integer(4));
	}

	bool Complete() const noexcept {
		return m_Position == m_Bytes.size();
	}

private:
	std::span<const std::uint8_t> m_Bytes;
	std::size_t m_Position = 0;
};

static void CompareNative(const char* input_path, const char* native_path) {
	const auto inputs = ReadFile(input_path);
	const auto native = ReadFile(native_path);
	VectorReader reader(inputs);
	VectorReader expected(native);
	std::size_t cases = 0;
	while (!reader.Complete()) {
		const auto type = static_cast<sgre::audio::SoundVoiceType>(reader.Integer(1));
		const auto requested = reader.Integer(1);
		const auto count = reader.Integer(1);
		static_cast<void>(reader.Integer(1));
		const auto free_mask = reader.Integer(4);
		const auto priority = reader.Number();
		const std::array<std::size_t, 3> capacities = {32, 8, 1};
		Require(static_cast<std::size_t>(type) < capacities.size() && count <= 41, "native sound slot vector shape exceeds its bound");
		std::array<bool, 32> free = {};
		for (std::size_t index = 0; index < capacities[static_cast<std::size_t>(type)]; ++index) {
			free[index] = ((free_mask >> index) & 1) != 0;
		}

		std::array<std::array<std::uint8_t, 32>, 41> owned_slots = {};
		std::array<sgre::audio::SoundSlotCandidate, 41> active;
		for (std::size_t index = 0; index < count; ++index) {
			auto& cue = active[index];
			cue.CueId = reader.Integer(4);
			cue.Type = static_cast<sgre::audio::SoundVoiceType>(reader.Integer(1));
			const auto slots = reader.Integer(1);
			Require(slots <= owned_slots[index].size(), "native sound cue vector exceeds its slot bound");
			static_cast<void>(reader.Integer(2));
			cue.Priority = reader.Number();
			for (std::size_t slot = 0; slot < slots; ++slot) {
				owned_slots[index][slot] = static_cast<std::uint8_t>(reader.Integer(1));
			}

			cue.Slots = std::span(owned_slots[index]).first(slots);
		}

		const auto plan = sgre::audio::SelectSoundVoiceSlots(type, requested, priority, std::span(free).first(capacities[static_cast<std::size_t>(type)]), std::span(active).first(count));
		Require(plan.Accepted == (expected.Integer(1) != 0) && plan.SlotCount == expected.Integer(1) && plan.EvictionCount == expected.Integer(1), "sound slot acceptance or counts differ from original instructions");
		static_cast<void>(expected.Integer(1));
		for (std::size_t index = 0; index < plan.SlotCount; ++index) {
			Require(plan.Slots[index] == expected.Integer(1), "sound slot order differs from original instructions");
		}

		for (std::size_t index = 0; index < plan.EvictionCount; ++index) {
			Require(plan.EvictedCues[index] == expected.Integer(4), "sound cue eviction order differs from original instructions");
		}

		++cases;
	}

	Require(expected.Complete(), "native sound slot output has trailing records");
	std::cout << "Original sound allocation cases: " << cases << '\n';
}

int main(int argc, char** argv) {
	try {
		TestSelectionAndEviction();
		if (argc == 3) {
			CompareNative(argv[1], argv[2]);
		} else {
			Require(argc == 1, "expected zero arguments or two native vector paths");
		}

		std::cout << "Sound voice allocation tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
