#include "sgre/script/sound_bindings.hpp"

#include "voice_query_fixture.hpp"

#include <array>
#include <bit>
#include <limits>
#include <memory>
#include <variant>

static std::array<CueVector, 2> Vectors(std::uint8_t invalid_mask) {
	std::array<CueVector, 2> result{CueVector{"tone", 2}, CueVector{"long-cue-name-20bytes", 2}};
	for (auto& vector : result) {
		vector.InvalidChannelMask = invalid_mask;
	}

	return result;
}

class ActualVoiceQueryFixture {
public:
	ActualVoiceQueryFixture(const std::filesystem::path& directory, std::uint8_t active_mask = 3, bool paused = false, std::uint8_t invalid_mask = 0) : Definitions(Vectors(invalid_mask)), Runtime(directory, Definitions), Control(Runtime.Cache, Runtime.Mounts, Runtime.Processor, "win", "synthetic-seed", false) {
		Runtime.LoadArchive("beta", Definitions);
		Require(Control.Load("configuration.psb"), "actual query owner must request its worker-produced configuration");
		for (std::size_t visit = 0; visit < 8 && !Control.GetLoaded("configuration.psb"); ++visit) {
			Control.AdvancePostLoop();
		}
		for (const auto name : {"alpha", "beta"}) {
			Require(Control.LoadArchive(name), "actual query owner must request its declared archive");
			for (std::size_t visit = 0; visit < 8 && !Control.GetArchiveLoaded(name); ++visit) {
				Control.AdvancePostLoop();
			}
			Require(Control.GetArchiveLoaded(name), "actual archive metadata must finish before source preparation");
		}
		const std::array<std::string_view, 3> archives{"alpha", "beta", "alpha"};
		const std::array<std::string_view, 3> names{"tone", "tone", "long-cue-name-20bytes"};
		for (std::size_t index = 0; index < Ids.size(); ++index) {
			sgre::audio::BufferedSoundCueRequest request;
			request.ArchiveName = archives[index];
			request.CueName = names[index];
			request.StartPaused = paused;
			sgre::audio::SoundMixParameters parameters;
			parameters.UseSourcePan = true;
			const auto prepared = Control.BufferedCues().Prepare(request, parameters, 2);
			const auto status = invalid_mask == 0 ? BufferedSoundCuePreparationStatus::Prepared : invalid_mask == 3 ? BufferedSoundCuePreparationStatus::FailedSources
			                                                                                                        : BufferedSoundCuePreparationStatus::PartiallyPrepared;
			Require(prepared.Cue && prepared.Status == status, "actual query fixture source result differs from its independent format inputs");
			Ids[index] = *prepared.Cue;
			if (invalid_mask == 3) {
				// A following real preparation sweeps a fully failed logical cue; query this one before that next phase.
				break;
			}
		}
		for (std::size_t index = 0; index < Ids.size() && invalid_mask != 3; ++index) {
			for (std::size_t channel = 0; channel < 2; ++channel) {
				if ((active_mask & (std::uint8_t{1} << channel)) == 0 && (invalid_mask & (std::uint8_t{1} << channel)) == 0) {
					std::array<float, 4> output{};
					Require(Control.BufferedCues().ConsumeChannelFrames(Ids[index], channel, output) == 4, "query fixture must complete its actual independent PCM source");
					Require(output == std::array<float, 4>{0.25f, 0.5f, -0.5f, 0}, "completed source samples differ from the hand-authored PCM values");
				}
			}
		}
		Bindings = std::make_unique<sgre::script::ScriptSoundBindings>(Vm, Control);
		Class = Bindings->SoundClass();
	}

	~ActualVoiceQueryFixture() noexcept {
		try {
			Bindings.reset();
			Class = {};
			Vm.Close();
			Control.Close();
		} catch (...) {
			std::terminate();
		}
	}

	[[nodiscard]] sgre::script::SquirrelValue Invoke(std::string_view method, std::span<const sgre::script::SquirrelValue> selector) {
		std::vector<sgre::script::SquirrelValue> arguments{Class};
		arguments.insert(arguments.end(), selector.begin(), selector.end());
		const auto closure = Vm.GetSlot(Class, sgre::script::SquirrelString(std::string(method)));

		return Vm.Invoke(closure, arguments).Value;
	}

	std::array<CueVector, 2> Definitions;
	ActualCueRuntime Runtime;
	sgre::audio::SoundResourceControl Control;
	sgre::script::SquirrelVm Vm;
	std::unique_ptr<sgre::script::ScriptSoundBindings> Bindings;
	sgre::script::SquirrelValue Class;
	std::array<sgre::audio::BufferedSoundCueId, 3> Ids;
};

static std::uint8_t Byte(std::istream& stream) {
	const auto value = stream.get();
	Require(value != std::char_traits<char>::eof(), "voice query native corpus is truncated");

	return static_cast<std::uint8_t>(value);
}

static std::uint32_t Word(std::istream& stream) {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		result |= static_cast<std::uint32_t>(Byte(stream)) << shift;
	}

	return result;
}

static std::uint32_t MapNativeId(std::uint32_t native_id, const ActualVoiceQueryFixture& fixture) {
	const std::array<std::uint32_t, 3> native_ids{7, 11, 19};
	const auto found = std::find(native_ids.begin(), native_ids.end(), native_id);

	return found == native_ids.end() ? native_id : fixture.Ids[static_cast<std::size_t>(found - native_ids.begin())].Value;
}

static void TestOriginalQueryCorpus(const std::filesystem::path& directory, const std::filesystem::path& corpus_path) {
	std::ifstream corpus(corpus_path, std::ios::binary);
	Require(corpus.is_open() && Word(corpus) == 0x31595156 && Word(corpus) == 255, "voice query corpus header differs");
	std::unique_ptr<ActualVoiceQueryFixture> fixture;
	std::uint8_t prior_mask = 255;
	std::size_t compared = 0;
	for (std::size_t record = 0; record < 255; ++record) {
		const auto method = Byte(corpus);
		const auto service_metadata = Byte(corpus);
		const auto active_mask = Byte(corpus);
		const auto count = Byte(corpus);
		Require(method <= 1 && service_metadata <= 3 && active_mask <= 3 && count >= 1 && count <= 2, "voice query corpus exceeds the independent domain");
		std::vector<sgre::script::SquirrelValue> arguments;
		for (std::uint8_t index = 0; index < count; ++index) {
			const auto kind = Byte(corpus);
			if (kind == 0) {
				arguments.emplace_back(std::bit_cast<std::int32_t>(Word(corpus)));
			} else {
				Require(kind == 1, "voice query corpus selector type is unknown");
				const auto length = Byte(corpus);
				Require(length < 128, "voice query corpus string exceeds its explicit bound");
				std::string text;
				for (std::uint8_t character = 0; character < length; ++character) {
					text.push_back(static_cast<char>(Byte(corpus)));
				}
				arguments.emplace_back(sgre::script::SquirrelString(std::move(text)));
			}
		}
		const auto native_selected = Word(corpus);
		const auto native_result = Word(corpus);
		if (method == 1 && service_metadata != 2) {
			// Absent/opaque engine metadata belongs to the native witness; no such fake readiness exists in this CPU owner.
			continue;
		}
		if (!fixture || active_mask != prior_mask) {
			fixture.reset();
			fixture = std::make_unique<ActualVoiceQueryFixture>(directory, active_mask);
			prior_mask = active_mask;
		}
		for (auto& argument : arguments) {
			if (auto* integer = std::get_if<std::int32_t>(&argument)) {
				*integer = std::bit_cast<std::int32_t>(MapNativeId(std::bit_cast<std::uint32_t>(*integer), *fixture));
			}
		}
		const auto result = fixture->Invoke(method == 0 ? "getVoicePronounceId" : "getVoicePlaying", arguments);
		if (method == 0) {
			Require(native_result == native_selected && std::holds_alternative<std::int32_t>(result) && std::bit_cast<std::uint32_t>(std::get<std::int32_t>(result)) == MapNativeId(native_result, *fixture), "actual selected 32-bit ID scalar differs from the native query projection");
		} else {
			Require(std::holds_alternative<bool>(result) && std::get<bool>(result) == (native_result != 0), "actual source callback ANY query differs from the native projection");
		}
		++compared;
	}
	Require(compared == 75 && corpus.peek() == std::char_traits<char>::eof(), "voice query projection count or exact corpus end differs");
}

static void TestRealCallbackAndIdPhases(const std::filesystem::path& directory) {
	ActualVoiceQueryFixture fixture(directory, 3, true);
	const std::array<sgre::script::SquirrelValue, 1> name{sgre::script::SquirrelString("tone")};
	const std::array<sgre::script::SquirrelValue, 1> id{std::bit_cast<std::int32_t>(fixture.Ids[0].Value)};
	const auto before = fixture.Control.BufferedCues().InspectChannel(fixture.Ids[0], 0);
	Require(!before.State.Samples.Running && before.State.Samples.CallbackActive, "paused actual source must remain queued and callback-active");
	Require(std::get<bool>(fixture.Invoke("getVoicePlaying", name)), "paused genuine callback must still report playing");
	Require(std::get<std::int32_t>(fixture.Invoke("getVoicePronounceId", name)) == std::get<std::int32_t>(id[0]), "name query must select the first actual active-list cue");
	const auto after = fixture.Control.BufferedCues().InspectChannel(fixture.Ids[0], 0);
	Require(after.State.Samples.ConsumedFrames == before.State.Samples.ConsumedFrames && after.State.Samples.CursorFrame == before.State.Samples.CursorFrame && after.State.Samples.CallbackActive, "query mutated or consumed actual source storage");
	static_cast<void>(fixture.Invoke("resumeVoice", id));
	for (std::size_t channel = 0; channel < 2; ++channel) {
		std::array<float, 4> output{};
		Require(fixture.Control.BufferedCues().ConsumeChannelFrames(fixture.Ids[0], channel, output) == 4, "actual resumed source did not complete its real PCM frames");
	}
	Require(!std::get<bool>(fixture.Invoke("getVoicePlaying", id)), "completed callbacks must report false before cue ID retirement");
	Require(std::get<std::int32_t>(fixture.Invoke("getVoicePronounceId", name)) == std::get<std::int32_t>(id[0]), "completed active-list identity must survive until postloop");
	fixture.Control.AdvancePostLoop();
	Require(std::get<std::int32_t>(fixture.Invoke("getVoicePronounceId", name)) == std::bit_cast<std::int32_t>(fixture.Ids[1].Value), "postloop retirement must expose the next genuine matching cue");
	Require(std::get<std::int32_t>(fixture.Invoke("getVoicePronounceId", id)) == std::get<std::int32_t>(id[0]) && !std::get<bool>(fixture.Invoke("getVoicePlaying", id)), "integer ID query must echo even after its actual cue retires");
}

static void TestPartialAndFailedSourceOwners(const std::filesystem::path& directory) {
	for (const auto invalid_mask : {std::uint8_t{1}, std::uint8_t{3}}) {
		ActualVoiceQueryFixture fixture(directory, 3, false, invalid_mask);
		const std::array<sgre::script::SquirrelValue, 1> name{sgre::script::SquirrelString("tone")};
		Require(std::get<bool>(fixture.Invoke("getVoicePlaying", name)) == (invalid_mask == 1), "actual query must use ANY surviving decoded callback rather than cue counts or successful configuration");
		Require(std::get<std::int32_t>(fixture.Invoke("getVoicePronounceId", name)) == std::bit_cast<std::int32_t>(fixture.Ids[0].Value), "failed/partial actual logical cue still has its native-style ID before postloop");
		static_cast<void>(fixture.Invoke("stopVoice", name));
		Require(!fixture.Control.BufferedCues().IsPlaying(fixture.Ids[0]) && fixture.Control.BufferedCues().RetiringCount() == 0, "stop+flush must complete real callbacks before retired cue release");
	}
}

static void TestStrictArgumentsAndBorrowLifetimes(const std::filesystem::path& directory) {
	ActualVoiceQueryFixture fixture(directory);
	const auto before = fixture.Control.BufferedCues().InspectChannel(fixture.Ids[0], 0).State.Samples;
	for (const auto& value : std::array<sgre::script::SquirrelValue, 3>{std::monostate{}, true, 7.0f}) {
		const std::array<sgre::script::SquirrelValue, 1> arguments{value};
		for (const auto method : {"getVoicePlaying", "getVoicePronounceId"}) {
			RequireFailure([&] {
				static_cast<void>(fixture.Invoke(method, arguments));
			},
			    "invalid argument");
		}
	}
	const std::array<sgre::script::SquirrelValue, 2> invalid_pair{std::int32_t{7}, sgre::script::SquirrelString("tone")};
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke("getVoicePlaying", invalid_pair));
	},
	    "invalid argument");
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke("getVoicePronounceId", {}));
	},
	    "invalid argument");
	const auto after = fixture.Control.BufferedCues().InspectChannel(fixture.Ids[0], 0).State.Samples;
	Require(before.CursorFrame == after.CursorFrame && before.ConsumedFrames == after.ConsumedFrames && after.CallbackActive, "invalid query mutated actual callback/sample state");
	const std::array<sgre::script::SquirrelValue, 1> name{sgre::script::SquirrelString("tone")};
	auto& actual_cues = fixture.Control.BufferedCues();
	fixture.Control.Close();
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke("getVoicePlaying", name));
	},
	    "actual live serialized receiving owner");
	RequireFailure([&] {
		static_cast<void>(actual_cues.IsPlaying(fixture.Ids[0]));
	},
	    "live serialized source/resource owner");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "usage: sound_voice_query_tests owned_fixture_directory native_query_corpus");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directory(directory);
		TestOriginalQueryCorpus(directory, argv[2]);
		TestRealCallbackAndIdPhases(directory);
		TestPartialAndFailedSourceOwners(directory);
		TestStrictArgumentsAndBorrowLifetimes(directory);
		std::cout << "4 actual query owner groups and 75 original scalar/callback projections passed; no native engine/device readiness\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
