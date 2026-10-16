#include "sgre/audio/voice_uniqueness.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::audio::SoundUniqueCandidate Candidate(std::uint32_t id, float priority, std::string_view archive, std::string_view key) {
	sgre::audio::SoundUniqueCandidate cue;
	cue.CueId = id;
	cue.Priority = priority;
	cue.Archive = archive;
	cue.Key = key;

	return cue;
}

static void TestExactScopeAndPartialFailure() {
	const std::array<sgre::audio::SoundUniqueCandidate, 4> active = {Candidate(1, 0, "Other", "same"), Candidate(2, 1, "Archive", "Same"), Candidate(3, 1, "Archive", "same"), Candidate(4, 4, "Archive", "same")};
	const auto rejected = sgre::audio::ResolveSoundUniqueness("Archive", "same", 2, active);
	Require(!rejected.Accepted && rejected.EvictionCount == 1 && rejected.EvictedCues[0] == 3, "archive uniqueness lost exact spelling, active order, or prior eviction on rejection");
	const auto global = sgre::audio::ResolveSoundUniqueness("Archive", "same", 4, active, false);
	Require(global.Accepted && global.EvictionCount == 3 && global.EvictedCues[0] == 1 && global.EvictedCues[1] == 3 && global.EvictedCues[2] == 4, "global uniqueness or equal-priority replacement differs");
	const auto empty = sgre::audio::ResolveSoundUniqueness("Archive", "", 0, active, false);
	Require(empty.Accepted && empty.EvictionCount == 0, "empty uniqueness unexpectedly replaced a cue");
}

static std::vector<std::uint8_t> ReadFile(const char* path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	const auto length = stream.tellg();
	if (!stream || length < 0 || length > 2 * 1024 * 1024) {
		throw std::runtime_error("could not open a bounded sound uniqueness vector file");
	}

	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!stream) {
		throw std::runtime_error("sound uniqueness vector read failed");
	}

	return bytes;
}

class VectorReader {
public:
	explicit VectorReader(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
	}

	std::uint32_t Integer(std::size_t width) {
		if (width > 4 || m_Position > m_Bytes.size() || m_Bytes.size() - m_Position < width) {
			throw std::runtime_error("native sound uniqueness vector is truncated");
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

	std::string Text(std::size_t length) {
		if (m_Position > m_Bytes.size() || m_Bytes.size() - m_Position < length) {
			throw std::runtime_error("native sound uniqueness string is truncated");
		}

		std::string text;
		for (std::size_t index = 0; index < length; ++index) {
			text.push_back(static_cast<char>(m_Bytes[m_Position++]));
		}

		return text;
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
		const auto archive_length = reader.Integer(1);
		const auto key_length = reader.Integer(1);
		const bool limit_to_archive = reader.Integer(1) != 0;
		const auto count = reader.Integer(1);
		const auto priority = reader.Number();
		const auto archive = reader.Text(archive_length);
		const auto key = reader.Text(key_length);
		Require(count <= 41, "native sound uniqueness vector exceeds its bound");
		std::array<sgre::audio::SoundUniqueCandidate, 41> active;
		std::array<std::string, 41> owned_archives;
		std::array<std::string, 41> owned_keys;
		for (std::size_t index = 0; index < count; ++index) {
			auto& cue = active[index];
			cue.CueId = reader.Integer(4);
			const auto cue_archive_length = reader.Integer(1);
			const auto cue_key_length = reader.Integer(1);
			static_cast<void>(reader.Integer(2));
			cue.Priority = reader.Number();
			owned_archives[index] = reader.Text(cue_archive_length);
			owned_keys[index] = reader.Text(cue_key_length);
			cue.Archive = owned_archives[index];
			cue.Key = owned_keys[index];
		}

		const auto plan = sgre::audio::ResolveSoundUniqueness(archive, key, priority, std::span(active).first(count), limit_to_archive);
		Require(plan.Accepted == (expected.Integer(1) != 0) && plan.EvictionCount == expected.Integer(1), "sound uniqueness acceptance or counts differ from original instructions");
		static_cast<void>(expected.Integer(2));
		for (std::size_t index = 0; index < plan.EvictionCount; ++index) {
			Require(plan.EvictedCues[index] == expected.Integer(4), "sound uniqueness eviction order differs from original instructions");
		}

		++cases;
	}

	Require(expected.Complete(), "native sound uniqueness output has trailing records");
	std::cout << "Original sound uniqueness cases: " << cases << '\n';
}

int main(int argc, char** argv) {
	try {
		TestExactScopeAndPartialFailure();
		if (argc == 3) {
			CompareNative(argv[1], argv[2]);
		} else {
			Require(argc == 1, "expected zero arguments or two native vector paths");
		}

		std::cout << "Sound voice uniqueness tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
