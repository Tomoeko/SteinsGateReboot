#include "sgre/script/sound_bindings.hpp"

#include "sgre/audio/buffered_sound_output.hpp"
#include "sgre/audio/sound_volume.hpp"
#include "group_volume_fixture.hpp"

#include <cmath>
#include <limits>

class ObservedSourceMix final : public sgre::audio::BufferedSoundOutput {
public:
	std::size_t MixChanges = 0;
	std::map<std::uint32_t, sgre::audio::SoundMix> Mixes;

private:
	void ConnectSourceOwner(sgre::audio::BufferedSoundSources&) override {
	}
	void ReleaseSourceOwner(sgre::audio::BufferedSoundSources&) noexcept override {
	}
	void AttachSource(sgre::audio::SoundVoiceAddress, sgre::audio::BufferedSoundSources&) override {
	}
	void RetireSource(sgre::audio::SoundVoiceAddress source) noexcept override {
		Mixes.erase((static_cast<std::uint32_t>(source.Type) << 8) | source.Slot);
	}
	void ApplySourceMix(sgre::audio::SoundVoiceAddress source, const sgre::audio::SoundMix& actual_mix) override {
		Mixes[(static_cast<std::uint32_t>(source.Type) << 8) | source.Slot] = actual_mix;
		++MixChanges;
	}
};

static sgre::script::SquirrelValue Invoke(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& sound, std::string_view method, std::span<const sgre::script::SquirrelValue> values = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{sound};
	arguments.insert(arguments.end(), values.begin(), values.end());

	return vm.Invoke(vm.GetSlot(sound, sgre::script::SquirrelString(std::string(method))), arguments).Value;
}

static void TestParser() {
	using sgre::audio::ParseSoundVolume;
	Require(ParseSoundVolume("2") == 3.0f && ParseSoundVolume("  -2L") == -3.0f && ParseSoundVolume("2", false) == 2.0f, "native volume numeric prefix and loudness flag differ");
	Require(ParseSoundVolume("2v") == 2.0f && ParseSoundVolume("2 l", false) == 3.0f && ParseSoundVolume("-20 dB") == 0.1f, "case-insensitive exact native V/L/DB suffix differs");
	Require(ParseSoundVolume("-96DB") == 0.0f && ParseSoundVolume("0db") == 1.0f && ParseSoundVolume("7DB") == 1.0f, "native decibel bounds differ");
	Require(ParseSoundVolume("2 V ") == 3.0f && ParseSoundVolume("2\tV") == 3.0f && ParseSoundVolume("2ignored") == 3.0f, "unknown complete suffix must follow native setting fallback, not trim or reject");
	Require(ParseSoundVolume("") == 0.0f && ParseSoundVolume("abc") == 0.0f && ParseSoundVolume("+2.0e0V") == 2.0f && ParseSoundVolume("0x1p1V") == 2.0f, "native CRT prefix grammar differs");
	Require(ParseSoundVolume(std::string_view("2V\0L", 5)) == 2.0f && std::signbit(ParseSoundVolume("-0V")), "native NUL boundary or signed zero changed");
	Require(std::isinf(ParseSoundVolume("1e1000V")) && std::isnan(ParseSoundVolume("NaNV")), "native nonfinite numeric values were silently replaced");
	RequireFailure([] {
		static_cast<void>(ParseSoundVolume(std::string(65537, '1')));
	},
	    "bounded numeric/unit");
}

static void TestGroupOwner(const std::filesystem::path& directory) {
	CueVector grouped;
	grouped.Grouped = true;
	CueVector ungrouped;
	ungrouped.Name = "other";
	const std::array<CueVector, 2> vectors{grouped, ungrouped};
	ActualCueRuntime runtime(directory, vectors);
	ObservedSourceMix output;
	sgre::audio::SoundResourceControl control(runtime.Cache, runtime.Mounts, runtime.Processor, "win", "synthetic-seed", false);
	control.ConnectOutput(output);
	Require(control.Load("configuration.psb"), "group fixture requires a real configuration owner");
	for (std::size_t step = 0; step < 8 && !control.GetLoaded("configuration.psb"); ++step) {
		control.AdvancePostLoop();
	}
	Require(control.LoadArchive("alpha"), "group fixture requires a real archive declaration");
	for (std::size_t step = 0; step < 8 && !control.GetArchiveLoaded("alpha"); ++step) {
		control.AdvancePostLoop();
	}

	sgre::script::SquirrelVm vm;
	{
		sgre::script::ScriptSoundBindings bindings(vm, control);
		const auto sound = bindings.SoundClass();
		const auto set = [&](sgre::script::SquirrelValue key, sgre::script::SquirrelValue value) {
			const std::array<sgre::script::SquirrelValue, 2> arguments{key, value};

			return Invoke(vm, sound, "setGroupVolume", arguments);
		};
		const auto get = [&](sgre::script::SquirrelValue key) {
			const std::array<sgre::script::SquirrelValue, 1> arguments{key};

			return std::get<float>(Invoke(vm, sound, "getGroupVolume", arguments));
		};
		Require(get(sgre::script::SquirrelString("shared")) == 1.0f && get(std::int32_t{8}) == 1.0f, "native absent group and getter tostring must resolve actual unity");
		Require(std::get<float>(set(sgre::script::SquirrelString("shared"), 0.5f)) == 0.5f && std::abs(control.GroupVolume("shared") - 1.0f / 3.0f) < 0.000001f, "numeric group setting must enter the actual owner in linear units");

		sgre::audio::BufferedSoundCueRequest request;
		request.ArchiveName = "alpha";
		request.CueName = "tone";
		sgre::audio::SoundMixParameters parameters;
		parameters.VoiceVolume = 0.5f;
		parameters.GroupVolume = 99.0f;
		parameters.ArchiveVolume = 0.5f;
		parameters.MasterVolume = 0.5f;
		parameters.MuteVolume = 0.5f;
		parameters.UseSourcePan = true;
		const auto first = control.BufferedCues().Prepare(request, parameters, 2);
		request.CueName = "other";
		const auto other = control.BufferedCues().Prepare(request, parameters, 2);
		Require(first.Cue && other.Cue && first.Status == BufferedSoundCuePreparationStatus::Prepared && other.Status == BufferedSoundCuePreparationStatus::Prepared, "group fixture requires real independent decoded sources");
		const auto inspect = [&](sgre::audio::BufferedSoundCueId id) {
			const auto channel = control.BufferedCues().InspectChannel(id, 0);
			Require(channel.Mix.has_value(), "group fixture requires a real retained source matrix");
			const auto key = (static_cast<std::uint32_t>(channel.Source.Type) << 8) | channel.Source.Slot;
			Require(output.Mixes.at(key).Matrix == channel.Mix->Matrix && channel.State.Samples.RemainingBufferFrames == 4, "group factor must reach actual connected source output without consuming samples");

			return *channel.Mix;
		};
		Require(std::abs(inspect(*first.Cue).Matrix[0] - 0.0078125f / 3.0f) < 0.000001f && inspect(*other.Cue).Matrix[0] == 0.0078125f, "future cue preparation must use its definition group map/default1, preserving all other factors");
		const auto before_no_change = output.MixChanges;
		static_cast<void>(set(sgre::script::SquirrelString("shared"), 0.5f));
		Require(output.MixChanges == before_no_change, "same native group gain must not update source output");
		static_cast<void>(set(sgre::script::SquirrelString("Shared"), 0.0f));
		Require(output.MixChanges == before_no_change && control.GroupVolume("shared") != 0.0f, "group lookup must preserve exact case and active cue membership");
		static_cast<void>(set(sgre::script::SquirrelString("shared"), 0.0f));
		Require(inspect(*first.Cue).Matrix[0] == 0.0f && inspect(*other.Cue).Matrix[0] == 0.0078125f, "native group mute must change only exact active matches");
		Require(std::get<std::int32_t>(set(sgre::script::SquirrelString("shared"), std::int32_t{-1})) == -1 && inspect(*first.Cue).Matrix[0] == -0.0078125f, "signed group gain must recover from zero without dividing an old matrix");
		static_cast<void>(set(sgre::script::SquirrelString(std::string("shared\0tail", 11)), 1.0f));
		Require(control.GroupVolume("shared") == 1.0f && inspect(*first.Cue).Matrix[0] == 0.0078125f && get(sgre::script::SquirrelString("shared")) == 1.0f, "native unit reset/NUL group boundary must restore actual unity");

		const sgre::script::SquirrelValue text = sgre::script::SquirrelString("0.5V");
		const auto returned = set(sgre::script::SquirrelString("shared"), text);
		Require(std::get<sgre::script::SquirrelString>(returned).SharesIdentity(std::get<sgre::script::SquirrelString>(text)) && std::abs(control.GroupVolume("shared") - 0.5f) < 0.000001f, "string units must preserve the physical return cell and inverse/forward conversion order");
		for (const auto& invalid : std::vector<sgre::script::SquirrelValue>{true, std::monostate{}, std::numeric_limits<float>::max(), vm.CreateArray()}) {
			RequireFailure([&] {
				static_cast<void>(set(sgre::script::SquirrelString("shared"), invalid));
			},
			    "invalid argument");
		}
		RequireFailure([&] {
			static_cast<void>(set(std::int32_t{1}, 1.0f));
		},
		    "invalid argument");
		RequireFailure([&] {
			static_cast<void>(Invoke(vm, sound, "setGroupVolume"));
		},
		    "invalid argument");

		control.SetGroupVolume("shared", -1.0f);
		std::array<float, 2> samples{};
		Require(control.BufferedCues().ConsumeMixedChannelFrames(*first.Cue, 0, samples) == 1 && samples[0] == -0.001953125f && samples[1] == -0.005859375f, "actual PCM output must consume the newly applied group matrix");
		Require(control.BufferedCues().Stop(*first.Cue), "real group cue must enter retirement");
		const auto before_retired = output.MixChanges;
		control.SetGroupVolume("shared", 0.25f);
		Require(output.MixChanges == before_retired && control.BufferedCues().RetiringCount() == 1, "native group setter must not rewrite already retiring payloads");
		control.BufferedCues().CommitAndRetire();
		request.CueName = "tone";
		const auto later = control.BufferedCues().Prepare(request, parameters, 2);
		Require(later.Cue && inspect(*later.Cue).Matrix[0] == 0.001953125f, "group map must survive cue retirement and initialize a later real source");
		control.SetGroupVolume("shared", 1.0f);
		Require(inspect(*later.Cue).Matrix[0] == 0.0078125f, "erased group override must restore an existing active source");
	}
	vm.Close();
	control.Close();
	RequireFailure([&] {
		static_cast<void>(control.GroupVolume("shared"));
	},
	    "live serialized");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "group volume fixture requires an owned output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestParser();
		TestGroupOwner(directory);
		std::cout << "Sound group volume: genuine group lookup, active/future source matrices, VM units and PCM passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
