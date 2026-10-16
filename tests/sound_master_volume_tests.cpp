#include "sgre/script/sound_bindings.hpp"

#include "voice_control_fixture.hpp"

#include <cmath>
#include <limits>

static sgre::script::SquirrelValue Invoke(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& sound, std::string_view method, std::span<const sgre::script::SquirrelValue> values = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{sound};
	arguments.insert(arguments.end(), values.begin(), values.end());

	return vm.Invoke(vm.GetSlot(sound, sgre::script::SquirrelString(std::string(method))), arguments).Value;
}

static void TestMasterVolume(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> vectors{CueVector{}};
	ActualCueRuntime runtime(directory, vectors);
	sgre::audio::SoundResourceControl control(runtime.Cache, runtime.Mounts, runtime.Processor, "win", "synthetic-seed", false);
	Require(control.Load("configuration.psb"), "master fixture requires a genuine configuration reference");
	for (std::size_t step = 0; step < 8 && !control.GetLoaded("configuration.psb"); ++step) {
		control.AdvancePostLoop();
	}
	Require(control.LoadArchive("alpha"), "master fixture requires the real archive declaration");
	for (std::size_t step = 0; step < 8 && !control.GetArchiveLoaded("alpha"); ++step) {
		control.AdvancePostLoop();
	}
	sgre::audio::BufferedSoundCueRequest request;
	request.ArchiveName = "alpha";
	request.CueName = "tone";
	sgre::audio::SoundMixParameters parameters;
	parameters.VoiceVolume = 0.5f;
	control.SetGroupVolume("", 0.5f);
	parameters.UseSourcePan = true;
	const auto prepared = control.BufferedCues().Prepare(request, parameters, 2);
	Require(prepared.Cue && prepared.Status == BufferedSoundCuePreparationStatus::Prepared, "master fixture requires real decoded PCM source ownership");

	sgre::script::SquirrelVm vm;
	{
		sgre::script::ScriptSoundBindings bindings(vm, control);
		const auto sound = bindings.SoundClass();
		Require(std::get<float>(Invoke(vm, sound, "getMasterVolume")) == 1.0f, "native master must begin at unity");
		for (const auto setting : {0.0f, 0.5f, -1.0f, 2.0f, 0.0f, 1.0f}) {
			const std::array<sgre::script::SquirrelValue, 1> argument{setting};
			Require(std::get<float>(Invoke(vm, sound, "setMasterVolume", argument)) == setting, "native master setter must preserve the physical argument result");
			const auto expected = setting == 0.5f ? 1.0f / 3.0f : setting == 2.0f ? 3.0f
			                                                                      : setting;
			Require(std::abs(control.MasterVolume() - expected) < 0.000001f, "numeric master setting differs from native loudness vectors");
			const auto channel = control.BufferedCues().InspectChannel(*prepared.Cue, 0);
			Require(channel.Mix && std::abs(channel.Mix->Matrix[0] - expected * 0.03125f) < 0.000001f && channel.State.Samples.RemainingBufferFrames == 4, "master change must recompute actual live channel factors without consuming samples");
			Require(std::abs(std::get<float>(Invoke(vm, sound, "getMasterVolume")) - setting) < 0.000001f, "master getter must invert native loudness conversion");
		}
		for (const auto& invalid : std::vector<sgre::script::SquirrelValue>{true, std::monostate{}, std::numeric_limits<float>::max(), vm.CreateArray()}) {
			const std::array<sgre::script::SquirrelValue, 1> argument{invalid};
			RequireFailure([&] {
				static_cast<void>(Invoke(vm, sound, "setMasterVolume", argument));
			},
			    "invalid argument");
			Require(control.MasterVolume() == 1.0f, "invalid master input must preserve real gain");
		}
		Require(std::get<bool>(Invoke(vm, sound, "getLoudnessVolumeEnabled")), "native loudness flag begins enabled");
		const std::vector<std::pair<sgre::script::SquirrelValue, bool>> truth{{false, false}, {true, true}, {std::monostate{}, false}, {std::int32_t{0}, false}, {std::int32_t{-1}, true}, {0.0f, false}, {-0.0f, false}, {0.5f, true}, {std::numeric_limits<float>::quiet_NaN(), true}, {sgre::script::SquirrelString(""), true}, {vm.CreateArray(), true}};
		for (const auto& [value, expected] : truth) {
			const std::array<sgre::script::SquirrelValue, 1> argument{value};
			Require(std::holds_alternative<std::monostate>(Invoke(vm, sound, "setLoudnessVolumeEnabled", argument)) && std::get<bool>(Invoke(vm, sound, "getLoudnessVolumeEnabled")) == expected, "captured Boolean setter must use native scalar truth and return zero values");
		}
		const std::array<sgre::script::SquirrelValue, 1> disabled{false};
		static_cast<void>(Invoke(vm, sound, "setLoudnessVolumeEnabled", disabled));
		const std::array<sgre::script::SquirrelValue, 1> half{0.5f};
		static_cast<void>(Invoke(vm, sound, "setMasterVolume", half));
		Require(control.MasterVolume() == 0.5f && std::get<float>(Invoke(vm, sound, "getMasterVolume")) == 0.5f, "disabled loudness must retain linear numeric master settings");
		const std::array<sgre::script::SquirrelValue, 1> enabled{true};
		static_cast<void>(Invoke(vm, sound, "setLoudnessVolumeEnabled", enabled));
		Require(control.MasterVolume() == 0.5f, "changing conversion policy must not rewrite retained linear gains");
		const std::array<sgre::script::SquirrelValue, 1> unity{1.0f};
		static_cast<void>(Invoke(vm, sound, "setMasterVolume", unity));
		RequireFailure([&] {
			static_cast<void>(Invoke(vm, sound, "setMasterVolume"));
		},
		    "invalid argument");
		const std::array<sgre::script::SquirrelValue, 1> negative{std::int32_t{-1}};
		Require(std::get<std::int32_t>(Invoke(vm, sound, "setMasterVolume", negative)) == -1, "master integer argument result must retain its original type");
		std::array<float, 2> samples{};
		Require(control.BufferedCues().ConsumeMixedChannelFrames(*prepared.Cue, 0, samples) == 1 && samples[0] == -0.0078125f && samples[1] == -0.0234375f, "actual PCM consumption must use the newly applied signed master matrix");
	}
	vm.Close();
	control.Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "master volume fixture requires an owned output directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestMasterVolume(directory);
		std::cout << "Sound master volume: genuine VM, numeric units, zero recovery and live PCM matrices passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
