#include "sgre/audio/voice_schedule.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void AppendWord(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
	for (std::size_t index = 0; index < 4; ++index) {
		bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
	}
}

class RecordingOperations final : public sgre::audio::SoundVoiceOperations {
public:
	std::vector<std::uint8_t> Events;
	std::array<bool, 41> ContextPresent = {};

	void Start(sgre::audio::SoundVoiceAddress voice, std::uint32_t operation_set) override {
		Record(1, voice, operation_set);
	}

	void Stop(sgre::audio::SoundVoiceAddress voice, std::uint32_t operation_set) override {
		Record(2, voice, operation_set);
	}

	void CommitAll() override {
		Record(3, {}, 0);
	}

	bool CurrentBufferContextPresent(sgre::audio::SoundVoiceAddress voice) override {
		Record(4, voice, 0);
		const std::array<std::size_t, 3> offsets = {0, 32, 40};

		return ContextPresent[offsets[static_cast<std::size_t>(voice.Type)] + voice.Slot];
	}

	void Flush(sgre::audio::SoundVoiceAddress voice) override {
		Record(5, voice, 0);
	}

private:
	void Record(std::uint8_t kind, sgre::audio::SoundVoiceAddress voice, std::uint32_t operation_set) {
		Events.push_back(kind);
		Events.push_back(static_cast<std::uint8_t>(voice.Type));
		Events.push_back(voice.Slot);
		Events.push_back(0);
		AppendWord(Events, operation_set);
	}
};

static void TestPauseReasons() {
	sgre::audio::SoundPauseState pause;
	Require(pause.Pause(sgre::audio::SoundPauseReason::Voice), "first pause did not request a stop");
	Require(!pause.Pause(sgre::audio::SoundPauseReason::Group), "nested pause requested another stop");
	Require(!pause.Resume(sgre::audio::SoundPauseReason::Voice) && pause.Reasons() == 4, "voice resume ignored a remaining group pause");
	Require(pause.Resume(sgre::audio::SoundPauseReason::Group) && pause.Reasons() == 0, "group resume differs");
	Require(pause.Pause(sgre::audio::SoundPauseReason::Voice), "voice pause was not restored");
	Require(pause.Resume(sgre::audio::SoundPauseReason::All) && pause.Reasons() == 1, "native global-resume quirk was lost");
	Require(pause.Resume(sgre::audio::SoundPauseReason::Archive) && pause.Reasons() == 1, "native archive-resume quirk was lost");
}

static void TestDeferredLifetime() {
	sgre::audio::SoundVoiceSchedule schedule;
	RecordingOperations operations;
	sgre::audio::SoundVoiceAddress voice;
	voice.Slot = 3;
	schedule.Register(voice);
	schedule.Commit(operations);
	Require(operations.Events.size() == 16 && operations.Events[0] == 1 && operations.Events[8] == 3 && schedule.Inspect(voice).Active && schedule.OperationSet() == 2, "initial deferred start or operation set differs");
	operations.Events.clear();
	schedule.RequestPause(voice);
	schedule.RequestResume(voice);
	schedule.Commit(operations);
	Require(operations.Events.size() == 8 && operations.Events[0] == 3, "cancelled pause produced an extra backend transition");
	operations.Events.clear();
	schedule.RequestStop(voice);
	schedule.Commit(operations);
	Require(operations.Events.size() == 24 && operations.Events[0] == 2 && operations.Events[8] == 3 && operations.Events[16] == 5 && schedule.Inspect(voice).Draining && schedule.Inspect(voice).Active, "stop, commit, flush order or deferred retirement differs");
	operations.Events.clear();
	operations.ContextPresent[3] = true;
	schedule.Commit(operations);
	Require(operations.Events.size() == 16 && operations.Events[0] == 3 && operations.Events[8] == 4 && schedule.Inspect(voice).Active, "a present buffer context was retired");
	operations.Events.clear();
	operations.ContextPresent[3] = false;
	schedule.Commit(operations);
	Require(!schedule.Inspect(voice).Active && !schedule.Inspect(voice).Draining, "null buffer context did not retire the voice");
	schedule.Register(voice);
	schedule.BufferEnded(voice);
	Require(!schedule.Inspect(voice).Active, "native buffer-end callback differs");
	schedule.Remove(voice);
	Require(!schedule.Inspect(voice).Exists, "voice removal did not release its schedule state");
	voice.Type = sgre::audio::SoundVoiceType::Buffered8;
	voice.Slot = 8;
	bool rejected = false;
	try {
		schedule.Register(voice);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "out-of-pool voice was accepted");
}

static std::vector<std::uint8_t> ReadFile(const char* path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	const auto length = stream.tellg();
	if (!stream || length < 0 || length > 2 * 1024 * 1024) {
		throw std::runtime_error("could not open a bounded audio schedule vector file");
	}

	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!stream) {
		throw std::runtime_error("audio schedule vector read failed");
	}

	return bytes;
}

static std::uint64_t Integer(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t width) {
	if (offset > bytes.size() || bytes.size() - offset < width) {
		throw std::runtime_error("audio schedule vector is truncated");
	}

	std::uint64_t value = 0;
	for (std::size_t index = 0; index < width; ++index) {
		value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
	}

	return value;
}

static void CompareNative(const char* input_path, const char* native_path) {
	const auto inputs = ReadFile(input_path);
	const auto native = ReadFile(native_path);
	Require(inputs.size() % 12 == 0, "native audio schedule input extent differs");
	sgre::audio::SoundVoiceSchedule schedule;
	RecordingOperations operations;
	std::vector<std::uint8_t> output;
	for (std::size_t offset = 0; offset < inputs.size(); offset += 12) {
		sgre::audio::SoundVoiceAddress voice;
		voice.Type = static_cast<sgre::audio::SoundVoiceType>(inputs[offset + 1]);
		voice.Slot = inputs[offset + 2];
		const auto context_mask = Integer(inputs, offset + 4, 8);
		for (std::size_t index = 0; index < operations.ContextPresent.size(); ++index) {
			operations.ContextPresent[index] = ((context_mask >> index) & 1) != 0;
		}

		switch (inputs[offset]) {
		case 0:
			schedule.Register(voice);
			break;
		case 1:
			schedule.Remove(voice);
			break;
		case 2:
			schedule.RequestPause(voice);
			break;
		case 3:
			schedule.RequestResume(voice);
			break;
		case 4:
			schedule.RequestStop(voice);
			break;
		case 5:
			schedule.BufferEnded(voice);
			break;
		default:
			throw std::runtime_error("native schedule command is unknown");
		}

		operations.Events.clear();
		schedule.Commit(operations);
		AppendWord(output, static_cast<std::uint32_t>(operations.Events.size() / 8));
		AppendWord(output, schedule.OperationSet());
		output.insert(output.end(), operations.Events.begin(), operations.Events.end());
		for (std::uint8_t type = 0; type < 3; ++type) {
			const std::array<std::uint8_t, 3> capacities = {32, 8, 1};
			for (std::uint8_t slot = 0; slot < capacities[type]; ++slot) {
				voice.Type = static_cast<sgre::audio::SoundVoiceType>(type);
				voice.Slot = slot;
				const auto& state = schedule.Inspect(voice);
				for (const bool field : {state.Exists, state.Active, state.RequestedRunning, state.AppliedRunning, state.PendingFlush, state.Draining}) {
					output.push_back(static_cast<std::uint8_t>(field));
				}
			}
		}
	}

	Require(output == native, "authored sound schedule trace differs from original instructions");
	std::cout << "Original sound schedule steps: " << inputs.size() / 12 << '\n';
}

int main(int argc, char** argv) {
	try {
		TestPauseReasons();
		TestDeferredLifetime();
		if (argc == 3) {
			CompareNative(argv[1], argv[2]);
		} else {
			Require(argc == 1, "expected zero arguments or two native vector paths");
		}

		std::cout << "Sound voice scheduling tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
