#include "sgre/script/sound_bindings.hpp"

#include "voice_control_fixture.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <variant>

class ActualVoiceControlFixture {
public:
	ActualVoiceControlFixture(const std::filesystem::path& directory, std::uint8_t mask = 0)
	    : Runtime(directory, Vectors), Control(Runtime.Cache, Runtime.Mounts, Runtime.Processor, "win", "synthetic-seed", false) {
		Runtime.LoadArchive("beta", Vectors);
		Require(Control.Load("configuration.psb"), "actual Sound owner must acquire its worker-produced configuration");
		for (std::size_t visit = 0; visit < 8 && !Control.GetLoaded("configuration.psb"); ++visit) {
			Control.AdvancePostLoop();
		}
		for (const auto name : {"alpha", "beta"}) {
			Require(Control.LoadArchive(name), "actual Sound declaration must produce each real archive request");
			for (std::size_t visit = 0; visit < 8 && !Control.GetArchiveLoaded(name); ++visit) {
				Control.AdvancePostLoop();
			}
			Require(Control.GetArchiveLoaded(name), "actual archive metadata must complete before cue preparation");
		}
		Ids[0] = Prepare("alpha", "tone", (mask & 1) != 0);
		Ids[1] = Prepare("beta", "tone", (mask & 1) != 0);
		Ids[2] = Prepare("alpha", "long-cue-name-20bytes", (mask & 1) != 0);
		if ((mask & 2) != 0) {
			Require(Control.PauseAll(), "actual global pause must produce the corpus's All2 reason");
		}
		Bindings = std::make_unique<sgre::script::ScriptSoundBindings>(Vm, Control);
		Class = Bindings->SoundClass();
	}

	~ActualVoiceControlFixture() noexcept {
		try {
			Bindings.reset();
			Class = {};
			Vm.Close();
			Control.Close();
		} catch (...) {
			std::terminate();
		}
	}

	[[nodiscard]] sgre::audio::BufferedSoundCueId Prepare(std::string_view archive, std::string_view name, bool paused = false) {
		sgre::audio::BufferedSoundCueRequest request;
		request.ArchiveName = archive;
		request.CueName = name;
		request.StartPaused = paused;
		sgre::audio::SoundMixParameters parameters;
		parameters.UseSourcePan = true;
		const auto prepared = Control.BufferedCues().Prepare(request, parameters, 2);
		Require(prepared.Cue && prepared.Status == sgre::audio::BufferedSoundCuePreparationStatus::Prepared, "actual voice control requires genuine decoded PCM cue channels");

		return *prepared.Cue;
	}

	[[nodiscard]] sgre::script::SquirrelValue Invoke(std::string_view method, std::span<const sgre::script::SquirrelValue> selector) {
		std::vector<sgre::script::SquirrelValue> arguments{Class};
		arguments.insert(arguments.end(), selector.begin(), selector.end());
		const auto closure = Vm.GetSlot(Class, sgre::script::SquirrelString(std::string(method)));

		return Vm.Invoke(closure, arguments).Value;
	}

	std::array<CueVector, 2> Vectors{CueVector{}, CueVector{"long-cue-name-20bytes"}};
	ActualCueRuntime Runtime;
	sgre::audio::SoundResourceControl Control;
	sgre::script::SquirrelVm Vm;
	std::unique_ptr<sgre::script::ScriptSoundBindings> Bindings;
	sgre::script::SquirrelValue Class;
	std::array<sgre::audio::BufferedSoundCueId, 3> Ids;
};

static void RequireExactLastArgument(const sgre::script::SquirrelValue& result, const sgre::script::SquirrelValue& last) {
	Require(result.index() == last.index(), "voice control must preserve last argument type, never publish a Boolean stand-in");
	if (const auto* integer = std::get_if<std::int32_t>(&last)) {
		Require(std::get<std::int32_t>(result) == *integer, "voice control integer result must preserve the exact selector");
	} else {
		const auto& text = std::get<sgre::script::SquirrelString>(last);
		const auto& returned = std::get<sgre::script::SquirrelString>(result);
		Require(returned.Text() == text.Text() && returned.SharesIdentity(text), "voice control string result must transfer the original full string identity");
	}
}

static std::uint8_t Byte(std::istream& stream) {
	const auto value = stream.get();
	Require(value != std::char_traits<char>::eof(), "voice control native corpus is truncated");

	return static_cast<std::uint8_t>(value);
}

static std::uint32_t Word(std::istream& stream) {
	std::uint32_t value = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		value |= static_cast<std::uint32_t>(Byte(stream)) << shift;
	}

	return value;
}

static void TestOriginalSelectorAndReturnCorpus(const std::filesystem::path& directory, const std::filesystem::path& corpus_path) {
	std::ifstream corpus(corpus_path, std::ios::binary);
	Require(corpus.is_open() && Word(corpus) == 0x31525356 && Word(corpus) == 45, "voice control native corpus header differs");
	const std::array<std::uint32_t, 3> native_ids{7, 11, 19};
	const std::array<std::string_view, 3> methods{"pauseVoice", "resumeVoice", "stopVoice"};
	for (std::uint32_t record = 0; record < 45; ++record) {
		const auto method = Byte(corpus);
		const auto count = Byte(corpus);
		const auto mask = Byte(corpus);
		const auto selected = Byte(corpus);
		Require(method < 3 && count >= 1 && count <= 2 && mask <= 3 && (selected < 3 || selected == 255), "voice corpus exceeds bounded method/selector domain");
		ActualVoiceControlFixture fixture(directory, mask);
		std::vector<sgre::script::SquirrelValue> arguments;
		for (std::uint8_t index = 0; index < count; ++index) {
			const auto kind = Byte(corpus);
			if (kind == 0) {
				const auto raw = Word(corpus);
				const auto found = std::find(native_ids.begin(), native_ids.end(), raw);
				// Native fixture IDs are caller-owned metadata. Portable selectors use the real prepared cue IDs.
				const auto actual = found == native_ids.end() ? raw : fixture.Ids[static_cast<std::size_t>(found - native_ids.begin())].Value;
				arguments.emplace_back(std::bit_cast<std::int32_t>(actual));
			} else {
				Require(kind == 1, "voice corpus selector kind is unsupported");
				const auto length = Byte(corpus);
				Require(length < 128, "voice corpus string exceeds bounded input");
				std::string text;
				for (std::uint8_t character = 0; character < length; ++character) {
					text.push_back(static_cast<char>(Byte(corpus)));
				}
				arguments.emplace_back(sgre::script::SquirrelString(std::move(text)));
			}
		}
		std::array<std::uint8_t, 3> expected_masks;
		for (auto& value : expected_masks) {
			value = Byte(corpus);
		}
		const auto selected_source = selected < 3 ? std::optional(fixture.Control.BufferedCues().InspectChannel(fixture.Ids[selected], 0).Source) : std::nullopt;
		RequireExactLastArgument(fixture.Invoke(methods[method], arguments), arguments.back());
		for (std::size_t cue = 0; cue < fixture.Ids.size(); ++cue) {
			if (method == 2 && cue == selected) {
				Require(!fixture.Control.BufferedCues().InspectSource(*selected_source).Samples.CallbackActive && fixture.Control.BufferedCues().RetiringCount() == 0, "real stopped buffer callback must finish before immediate retired cue release");
				RequireFailure([&]() {
					static_cast<void>(fixture.Control.BufferedCues().Inspect(fixture.Ids[cue]));
				},
				    "no actual active ID");
				continue;
			}
			const auto info = fixture.Control.BufferedCues().Inspect(fixture.Ids[cue]);
			const auto source = fixture.Control.BufferedCues().InspectChannel(fixture.Ids[cue], 0).State;
			Require(info.PauseReasons == expected_masks[cue] && source.Samples.Running == (expected_masks[cue] == 0) && source.Samples.CallbackActive && source.Samples.RemainingBufferFrames == 4 && !source.NativeBufferContextPresent, "actual source ownership/mask differs from native control projection");
		}
	}
	Require(corpus.get() == std::char_traits<char>::eof(), "voice corpus trailing bytes exceed exact scope");
}

static void TestFirstActiveNameAndRetirement(const std::filesystem::path& directory) {
	ActualVoiceControlFixture fixture(directory);
	const auto fourth = fixture.Prepare("alpha", "tone");
	auto& cues = fixture.Control.BufferedCues();
	Require(cues.FindNamedCue("tone")->Value == fixture.Ids[0].Value && cues.FindNamedCue("tone", "beta")->Value == fixture.Ids[1].Value && !cues.FindNamedCue("tone", ""), "selector must preserve active list order and explicit empty archive restriction");
	const std::array<sgre::script::SquirrelValue, 1> name{sgre::script::SquirrelString("tone")};
	RequireExactLastArgument(fixture.Invoke("stopVoice", name), name.back());
	Require(cues.FindNamedCue("tone")->Value == fixture.Ids[1].Value && cues.FindNamedCue("tone", "alpha")->Value == fourth.Value, "retiring the first actual payload must expose the next real active matching cue");
}

static void TestImmediateSamplesAndIndependentCompletion(const std::filesystem::path& directory) {
	ActualVoiceControlFixture fixture(directory);
	auto& cues = fixture.Control.BufferedCues();
	const std::array<sgre::script::SquirrelValue, 1> first{static_cast<std::int32_t>(fixture.Ids[0].Value)};
	std::array<float, 1> sample{};
	Require(cues.ConsumeChannelFrames(fixture.Ids[0], 0, sample) == 1 && sample[0] == 0.25f, "initial actual PCM sample differs");
	RequireExactLastArgument(fixture.Invoke("pauseVoice", first), first.back());
	Require(cues.ConsumeChannelFrames(fixture.Ids[0], 0, sample) == 0, "native voice control must commit actual CPU pause without a postloop");
	RequireExactLastArgument(fixture.Invoke("resumeVoice", first), first.back());
	RequireExactLastArgument(fixture.Invoke("resumeVoice", first), first.back());
	Require(cues.ConsumeChannelFrames(fixture.Ids[0], 0, sample) == 1 && sample[0] == 0.5f, "repeated native resume must preserve real remaining sample position");
	std::array<float, 2> tail{};
	Require(cues.ConsumeChannelFrames(fixture.Ids[0], 0, tail) == 2 && !cues.Inspect(fixture.Ids[0]).CallbackActive, "actual source completion must be measured before retirement");
	const std::array<sgre::script::SquirrelValue, 1> second{static_cast<std::int32_t>(fixture.Ids[1].Value)};
	static_cast<void>(fixture.Invoke("pauseVoice", second));
	Require(cues.ActiveCount() == 3 && cues.FindNamedCue("tone")->Value == fixture.Ids[0].Value, "source commit may not sweep unrelated completed active cues");
	static_cast<void>(fixture.Invoke("stopVoice", second));
	Require(cues.ActiveCount() == 2 && cues.FindNamedCue("tone")->Value == fixture.Ids[0].Value, "native stop retirement poll may not run the containing postloop's active sweep");
	fixture.Control.AdvancePostLoop();
	Require(!cues.FindNamedCue("tone") && cues.ActiveCount() == 1, "actual postloop must perform the still-required completed active cue sweep");
}

static void TestVoiceVolume(const std::filesystem::path& directory) {
	ActualVoiceControlFixture fixture(directory);
	auto& cues = fixture.Control.BufferedCues();
	const auto id = std::bit_cast<std::int32_t>(fixture.Ids[0].Value);
	const auto second_matrix = cues.InspectChannel(fixture.Ids[1], 0).Mix->Matrix;
	for (const auto setting : {0.0f, 0.5f, 1.0f, -1.0f, 2.0f, 0.0f, 1.0f}) {
		const std::array<sgre::script::SquirrelValue, 2> arguments{id, setting};
		const auto result = fixture.Invoke("setVoiceVolume", arguments);
		const auto gain = setting == 0.5f ? 1.0f / 3.0f : setting == 2.0f ? 3.0f
		                                                                  : setting;
		const auto channel = cues.InspectChannel(fixture.Ids[0], 0);
		Require(std::holds_alternative<float>(result) && std::get<float>(result) == setting, "voice volume result must preserve the original physical value cell");
		Require(channel.Mix && std::abs(channel.Mix->Matrix[0] - gain * 0.125f) < 0.000001f && std::abs(channel.Mix->Matrix[1] - gain * 0.375f) < 0.000001f, "per-voice volume must recompute the real decoded source matrix through zero and signed gains");
		Require(channel.State.Samples.RemainingBufferFrames == 4 && cues.InspectChannel(fixture.Ids[1], 0).Mix->Matrix == second_matrix, "volume mutation must preserve sample position and other active cues");
	}
	const sgre::script::SquirrelString archive(std::string("alpha\0suffix", 12));
	const sgre::script::SquirrelString name(std::string("tone\0suffix", 11));
	const sgre::script::SquirrelString linear("0.75V");
	const std::array<sgre::script::SquirrelValue, 3> paired{archive, name, linear};
	const auto result = fixture.Invoke("setVoiceVolume", paired);
	Require(std::get<sgre::script::SquirrelString>(result).SharesIdentity(linear) && cues.InspectChannel(fixture.Ids[0], 0).Mix->Matrix[0] == 0.09375f, "paired C-string selector and explicit volume unit must retain original result identity");
	const sgre::script::SquirrelString weak_target("0.5V");
	const auto weak = fixture.Vm.CreateWeakReference(weak_target);
	const std::array<sgre::script::SquirrelValue, 2> named{sgre::script::SquirrelString("tone"), weak};
	Require(fixture.Invoke("setVoiceVolume", named) == weak && cues.InspectChannel(fixture.Ids[0], 0).Mix->Matrix[0] == 0.0625f, "weak volume must resolve for conversion while retaining its actual physical result");
	for (const auto& invalid : std::vector<sgre::script::SquirrelValue>{true, std::monostate{}, std::numeric_limits<float>::max(), fixture.Vm.CreateArray()}) {
		const std::array<sgre::script::SquirrelValue, 2> arguments{id, invalid};
		RequireFailure([&] {
			static_cast<void>(fixture.Invoke("setVoiceVolume", arguments));
		},
		    "invalid argument");
		Require(cues.InspectChannel(fixture.Ids[0], 0).Mix->Matrix[0] == 0.0625f, "invalid value must not mutate a live source");
	}
	const std::array<sgre::script::SquirrelValue, 2> absent{std::int32_t{-1}, std::int32_t{1}};
	Require(std::get<std::int32_t>(fixture.Invoke("setVoiceVolume", absent)) == 1 && cues.ActiveCount() == 3, "missing native ID must remain a valid no-op with the original value result");
	fixture.Control.SetLoudnessVolumeEnabled(false);
	const std::array<sgre::script::SquirrelValue, 2> half_linear{id, 0.5f};
	static_cast<void>(fixture.Invoke("setVoiceVolume", half_linear));
	Require(cues.InspectChannel(fixture.Ids[0], 0).Mix->Matrix[0] == 0.0625f, "voice numeric gain must use the actual containing loudness policy");
	const std::array<sgre::script::SquirrelValue, 2> explicit_loudness{id, sgre::script::SquirrelString("0.5L")};
	static_cast<void>(fixture.Invoke("setVoiceVolume", explicit_loudness));
	Require(std::abs(cues.InspectChannel(fixture.Ids[0], 0).Mix->Matrix[0] - 0.125f / 3.0f) < 0.000001f, "explicit loudness suffix must retain its conversion with global loudness disabled");
	fixture.Control.SetLoudnessVolumeEnabled(true);
	const std::array<sgre::script::SquirrelValue, 2> negative{id, std::int32_t{-1}};
	Require(std::get<std::int32_t>(fixture.Invoke("setVoiceVolume", negative)) == -1, "signed integer volume must retain original type");
	std::array<float, 2> samples{};
	Require(cues.ConsumeMixedChannelFrames(fixture.Ids[0], 0, samples) == 1 && samples[0] == -0.03125f && samples[1] == -0.09375f, "real PCM consumption must use the immediately updated selected voice matrix");
}

static void TestStrictArgumentsAndUnsupportedReadiness(const std::filesystem::path& directory) {
	ActualVoiceControlFixture fixture(directory);
	const std::vector<std::vector<sgre::script::SquirrelValue>> invalid{{}, {true}, {1.0f}, {std::monostate{}}, {fixture.Vm.CreateArray()}, {sgre::script::SquirrelString("alpha"), std::int32_t{1}}, {std::int32_t{1}, sgre::script::SquirrelString("tone")}, {sgre::script::SquirrelString("alpha"), sgre::script::SquirrelString("tone"), std::int32_t{1}}};
	for (const auto& selector : invalid) {
		RequireFailure([&]() {
			static_cast<void>(fixture.Invoke("pauseVoice", selector));
		},
		    "invalid argument");
		Require(fixture.Control.BufferedCues().ActiveCount() == 3 && fixture.Control.BufferedCues().Inspect(fixture.Ids[0]).PauseReasons == 0, "invalid selector must fail before real cue/source mutation");
	}
	RequireFailure([&]() {
		static_cast<void>(fixture.Control.BufferedCues().ApplyControl(fixture.Ids[0], static_cast<sgre::audio::BufferedSoundCueControl>(255)));
	},
	    "outside the recovered voice");
	const std::array<sgre::script::SquirrelValue, 1> argument{sgre::script::SquirrelString("tone")};
	RequireFailure([&]() {
		static_cast<void>(fixture.Invoke("setVoicePitch", argument));
	},
	    "unrecovered Sound operation");
	RequireFailure([&]() {
		static_cast<void>(fixture.Invoke("playVoice", argument));
	},
	    "actual containing buffered output geometry");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "voice control tests require owned fixture directory and exact native corpus");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestOriginalSelectorAndReturnCorpus(directory, std::filesystem::path(argv[2]));
		TestFirstActiveNameAndRetirement(directory);
		TestImmediateSamplesAndIndependentCompletion(directory);
		TestStrictArgumentsAndUnsupportedReadiness(directory);
		TestVoiceVolume(directory);
		std::cout << "four actual resource/VM voice-control groups;45 native selector/results; decoded samples and source-commit/retired-poll ownership passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
