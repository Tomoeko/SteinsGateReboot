#include "sgre/script/sound_bindings.hpp"

#include "sgre/audio/sound_volume.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::script {
	static constexpr std::uint8_t kSoundClassTag = 0;

	enum class SoundStaticOperation : std::uint8_t {
		SetLoudnessVolumeEnabled = 1,
		GetLoudnessVolumeEnabled = 2,
		SetLimitUniqInArchive = 3,
		GetLimitUniqInArchive = 4,
		Load = 5,
		Unload = 6,
		GetLoading = 7,
		GetLoaded = 8,
		LoadArchive = 9,
		UnloadArchive = 10,
		GetArchiveLoading = 11,
		GetArchiveLoaded = 12,
		PlayVoice = 13,
		GetVoicePronounceId = 14,
		GetVoicePlaying = 15,
		StopVoice = 16,
		PauseVoice = 17,
		ResumeVoice = 18,
		SetVoiceVolume = 19,
		SetVoicePitch = 20,
		SetVoicePan = 21,
		SetVoicePriority = 22,
		SetVoiceGroup = 23,
		SetVoiceUniq = 24,
		SetVoiceSourcePan = 25,
		SetGroupVolume = 26,
		GetGroupVolume = 27,
		StopGroup = 28,
		PauseGroup = 29,
		ResumeGroup = 30,
		SetArchiveVolume = 31,
		GetArchiveVolume = 32,
		StopArchive = 33,
		PauseArchive = 34,
		ResumeArchive = 35,
		SetMasterVolume = 36,
		GetMasterVolume = 37,
		SetMute = 38,
		GetMute = 39,
		StopAll = 40,
		PauseAll = 41,
		ResumeAll = 42,
		AnimateVoiceVolume = 43,
		AnimateVoicePitch = 44,
		AnimateVoicePan = 45,
		AnimateGroupVolume = 46,
		AnimateArchiveVolume = 47,
		AnimateMasterVolume = 48,
		ConnectResourceCache = 49,
		DisconnectResourceCache = 50,
	};

	struct SoundStaticMethod {
		const char* Name;
		SoundStaticOperation Operation;
		bool Captured;
	};

	static constexpr std::array<SoundStaticMethod, 50> kSoundStaticMethods{{
	    {"setLoudnessVolumeEnabled", SoundStaticOperation::SetLoudnessVolumeEnabled, true},
	    {"getLoudnessVolumeEnabled", SoundStaticOperation::GetLoudnessVolumeEnabled, true},
	    {"setLimitUniqInArchive", SoundStaticOperation::SetLimitUniqInArchive, true},
	    {"getLimitUniqInArchive", SoundStaticOperation::GetLimitUniqInArchive, true},
	    {"load", SoundStaticOperation::Load, true},
	    {"unload", SoundStaticOperation::Unload, true},
	    {"getLoading", SoundStaticOperation::GetLoading, true},
	    {"getLoaded", SoundStaticOperation::GetLoaded, true},
	    {"loadArchive", SoundStaticOperation::LoadArchive, true},
	    {"unloadArchive", SoundStaticOperation::UnloadArchive, true},
	    {"getArchiveLoading", SoundStaticOperation::GetArchiveLoading, true},
	    {"getArchiveLoaded", SoundStaticOperation::GetArchiveLoaded, true},
	    {"playVoice", SoundStaticOperation::PlayVoice, false},
	    {"getVoicePronounceId", SoundStaticOperation::GetVoicePronounceId, false},
	    {"getVoicePlaying", SoundStaticOperation::GetVoicePlaying, false},
	    {"stopVoice", SoundStaticOperation::StopVoice, false},
	    {"pauseVoice", SoundStaticOperation::PauseVoice, false},
	    {"resumeVoice", SoundStaticOperation::ResumeVoice, false},
	    {"setVoiceVolume", SoundStaticOperation::SetVoiceVolume, false},
	    {"setVoicePitch", SoundStaticOperation::SetVoicePitch, false},
	    {"setVoicePan", SoundStaticOperation::SetVoicePan, false},
	    {"setVoicePriority", SoundStaticOperation::SetVoicePriority, false},
	    {"setVoiceGroup", SoundStaticOperation::SetVoiceGroup, false},
	    {"setVoiceUniq", SoundStaticOperation::SetVoiceUniq, false},
	    {"setVoiceSourcePan", SoundStaticOperation::SetVoiceSourcePan, false},
	    {"setGroupVolume", SoundStaticOperation::SetGroupVolume, false},
	    {"getGroupVolume", SoundStaticOperation::GetGroupVolume, true},
	    {"stopGroup", SoundStaticOperation::StopGroup, true},
	    {"pauseGroup", SoundStaticOperation::PauseGroup, true},
	    {"resumeGroup", SoundStaticOperation::ResumeGroup, true},
	    {"setArchiveVolume", SoundStaticOperation::SetArchiveVolume, false},
	    {"getArchiveVolume", SoundStaticOperation::GetArchiveVolume, true},
	    {"stopArchive", SoundStaticOperation::StopArchive, true},
	    {"pauseArchive", SoundStaticOperation::PauseArchive, true},
	    {"resumeArchive", SoundStaticOperation::ResumeArchive, true},
	    {"setMasterVolume", SoundStaticOperation::SetMasterVolume, false},
	    {"getMasterVolume", SoundStaticOperation::GetMasterVolume, true},
	    {"setMute", SoundStaticOperation::SetMute, true},
	    {"getMute", SoundStaticOperation::GetMute, true},
	    {"stopAll", SoundStaticOperation::StopAll, true},
	    {"pauseAll", SoundStaticOperation::PauseAll, true},
	    {"resumeAll", SoundStaticOperation::ResumeAll, true},
	    {"animateVoiceVolume", SoundStaticOperation::AnimateVoiceVolume, false},
	    {"animateVoicePitch", SoundStaticOperation::AnimateVoicePitch, false},
	    {"animateVoicePan", SoundStaticOperation::AnimateVoicePan, false},
	    {"animateGroupVolume", SoundStaticOperation::AnimateGroupVolume, false},
	    {"animateArchiveVolume", SoundStaticOperation::AnimateArchiveVolume, false},
	    {"animateMasterVolume", SoundStaticOperation::AnimateMasterVolume, false},
	    {"connectResourceCache", SoundStaticOperation::ConnectResourceCache, true},
	    {"disconnectResourceCache", SoundStaticOperation::DisconnectResourceCache, true},
	}};

	static SquirrelValue SoundMethodCapture(SquirrelVm& vm, SoundStaticOperation operation) {
		const std::array<std::uint8_t, 8> payload{1, 0, 0, 0, static_cast<std::uint8_t>(operation), 0, 0, 0};

		return vm.CreateUserData(payload);
	}

	static void RequireSoundMethodCapture(SquirrelVm& vm, SoundStaticOperation operation) {
		const auto capture = vm.NativeStackValue(-1);
		if (vm.Type(capture) != SquirrelValueType::UserData) {
			throw std::runtime_error("Sound callback requires its native userdata method capture");
		}

		const auto bytes = vm.UserDataBytes(capture);
		const std::array<std::uint8_t, 8> expected{1, 0, 0, 0, static_cast<std::uint8_t>(operation), 0, 0, 0};
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("Sound callback userdata capture identity is invalid");
		}
	}

	static std::string SoundArgumentString(SquirrelVm& vm) {
		// Native043a00/043a70 run sq_tostring at stack2, read/pop the temporary, then use strlen.
		const auto input = vm.NativeAllocatedStackValue(2);
		auto text = vm.ToString(input);
		vm.PushNativeValue(SquirrelString(text));
		vm.SetNativeStackSize(vm.NativeStackSize() - 1);
		text.resize(text.find('\0') == std::string::npos ? text.size() : text.find('\0'));

		return text;
	}

	static std::string_view SoundVoiceCString(const SquirrelValue& value) {
		const auto& text = StringText(value);

		return std::string_view(text).substr(0, text.find('\0'));
	}

	static std::optional<audio::BufferedSoundCueId> SoundVoiceArgument(SquirrelVm& vm, const audio::BufferedSoundCues& cues, std::size_t extra_arguments = 0) {
		const auto stack_size = vm.NativeStackSize();
		if (extra_arguments > stack_size) {
			throw std::runtime_error("invalid argument");
		}
		const auto count = stack_size - extra_arguments;
		if (count == 2) {
			const auto value = vm.NativeStackValue(2);
			if (vm.Type(value) == SquirrelValueType::Integer) {
				audio::BufferedSoundCueId id;
				id.Value = static_cast<std::uint32_t>(std::get<std::int32_t>(value));

				return id;
			}
			if (vm.Type(value) == SquirrelValueType::String) {
				return cues.FindNamedCue(SoundVoiceCString(value));
			}
		} else if (count == 3) {
			const auto archive = vm.NativeStackValue(2);
			const auto name = vm.NativeStackValue(3);
			if (vm.Type(archive) == SquirrelValueType::String && vm.Type(name) == SquirrelValueType::String) {
				return cues.FindNamedCue(SoundVoiceCString(name), SoundVoiceCString(archive));
			}
		}

		throw std::runtime_error("invalid argument");
	}

	static SquirrelNativeResult SoundVoiceControl(SquirrelVm& vm, audio::BufferedSoundCues& cues, audio::BufferedSoundCueControl control) {
		if (const auto selected = SoundVoiceArgument(vm, cues)) {
			static_cast<void>(cues.ApplyControl(*selected, control));
		}

		// The original direct callbacks return count1 without pushing: preserve the last physical argument cell.
		return SquirrelNativeResult::StackStatus(1);
	}

	static SquirrelNativeResult SoundVoicePronounceId(SquirrelVm& vm, const audio::BufferedSoundCues& cues) {
		const auto selected = SoundVoiceArgument(vm, cues);
		vm.PushNativeValue(selected ? std::bit_cast<std::int32_t>(selected->Value) : std::int32_t{-1});

		return SquirrelNativeResult::StackStatus(1);
	}

	static SquirrelNativeResult SoundVoicePlaying(SquirrelVm& vm, const audio::BufferedSoundCues& cues) {
		const auto selected = SoundVoiceArgument(vm, cues);
		vm.PushNativeValue(selected && cues.IsPlaying(*selected));

		return SquirrelNativeResult::StackStatus(1);
	}

	static float SoundVolumeArgument(SquirrelVm& vm, std::int32_t index, bool loudness_enabled, bool reject_maximum = true) {
		auto value = vm.NativeStackValue(index);
		if (vm.Type(value) == SquirrelValueType::WeakReference) {
			value = vm.ResolveWeakReference(value);
		}
		float setting = 0;
		if (const auto* number = std::get_if<float>(&value)) {
			setting = *number;
		} else if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			setting = static_cast<float>(*integer);
		} else if (vm.Type(value) == SquirrelValueType::String) {
			setting = audio::SoundVolumeFromLinear(audio::ParseSoundVolume(SoundVoiceCString(value), loudness_enabled), loudness_enabled);
		} else {
			throw std::runtime_error("invalid argument");
		}
		if (reject_maximum && setting == std::numeric_limits<float>::max()) {
			throw std::runtime_error("invalid argument");
		}

		return audio::SoundVolumeToLinear(setting, audio::SoundVolumeUnit::Setting, loudness_enabled);
	}

	static SquirrelNativeResult SoundVoiceVolume(SquirrelVm& vm, audio::SoundResourceControl& sound) {
		// Native389b50 returns the final value's stack index after its strict one/two-cell selector.
		auto& cues = sound.BufferedCues();
		const auto selected = SoundVoiceArgument(vm, cues, 1);
		const auto linear_volume = SoundVolumeArgument(vm, static_cast<std::int32_t>(vm.NativeStackSize()), sound.LoudnessVolumeEnabled());
		if (selected) {
			static_cast<void>(cues.SetVoiceVolume(*selected, linear_volume));
		}

		// Native38d2e0 returns one without pushing, retaining the original physical value cell.
		return SquirrelNativeResult::StackStatus(1);
	}

	static SquirrelNativeResult SoundVoiceVolumeAnimation(SquirrelVm& vm, audio::SoundResourceControl& sound) {
		const auto selected = SoundVoiceArgument(vm, sound.BufferedCues(), 3);
		if (selected && selected->Value != std::numeric_limits<std::uint32_t>::max()) {
			const auto volume_index = static_cast<std::int32_t>(vm.NativeStackSize() - 2);
			const auto gain = SoundVolumeArgument(vm, volume_index, sound.LoudnessVolumeEnabled(), false);
			const auto duration = TryNativeInteger(vm.NativeStackValue(-2));
			const auto curve = TryNativeInteger(vm.NativeStackValue(-1));
			if (!duration || !curve) {
				throw std::runtime_error("invalid argument");
			}
			sound.AnimateVoiceVolume(*selected, gain, *duration, *curve);
		}

		// Direct registry closure has no capture; native count1 transfers the unchanged final curve argument.
		return SquirrelNativeResult::StackStatus(1);
	}

	static SquirrelNativeResult SoundGroupVolumeAnimation(SquirrelVm& vm, audio::SoundResourceControl& sound) {
		if (vm.NativeStackSize() != 5 || vm.Type(vm.NativeStackValue(2)) != SquirrelValueType::String || vm.Type(vm.NativeStackValue(4)) != SquirrelValueType::Integer || vm.Type(vm.NativeStackValue(5)) != SquirrelValueType::Integer) {
			throw std::runtime_error("invalid argument");
		}
		const auto key = vm.NativeStackValue(2);
		const auto gain = SoundVolumeArgument(vm, 3, sound.LoudnessVolumeEnabled());
		sound.AnimateGroupVolume(SoundVoiceCString(key), gain, std::get<std::int32_t>(vm.NativeStackValue(4)), std::get<std::int32_t>(vm.NativeStackValue(5)));

		// Direct registry closure has no capture; native count1 transfers the unchanged final curve argument.
		return SquirrelNativeResult::StackStatus(1);
	}

	static bool SoundBooleanArgument(SquirrelVm& vm) {
		// Native225b00/25a420 tests the original scalar payload without resolving weak references.
		const auto value = vm.NativeAllocatedStackValue(2);
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer != 0;
		}
		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0;
		}

		return true;
	}

	static SquirrelNativeResult SoundMasterVolume(SquirrelVm& vm, audio::SoundResourceControl& sound) {
		if (vm.NativeStackSize() != 2) {
			throw std::runtime_error("invalid argument");
		}

		sound.SetMasterVolume(SoundVolumeArgument(vm, 2, sound.LoudnessVolumeEnabled()));

		// Native38ca50 returns one without pushing; preserve the physical argument and its weakref identity.
		return SquirrelNativeResult::StackStatus(1);
	}

	static SquirrelNativeResult SoundGroupVolume(SquirrelVm& vm, audio::SoundResourceControl& sound) {
		if (vm.NativeStackSize() != 3 || vm.Type(vm.NativeStackValue(2)) != SquirrelValueType::String) {
			throw std::runtime_error("invalid argument");
		}

		const auto group_value = vm.NativeStackValue(2);
		const auto linear_volume = SoundVolumeArgument(vm, 3, sound.LoudnessVolumeEnabled());
		sound.SetGroupVolume(SoundVoiceCString(group_value), linear_volume);

		// Native38c680 returns one without pushing, so the last argument cell is the actual result.
		return SquirrelNativeResult::StackStatus(1);
	}

	static SquirrelValue SoundResolvedValue(SquirrelVm& vm, SquirrelValue value) {
		if (vm.Type(value) == SquirrelValueType::WeakReference) {
			return vm.ResolveWeakReference(value);
		}

		return value;
	}

	static float SoundOptionFloat(SquirrelVm& vm, SquirrelValue value) {
		value = SoundResolvedValue(vm, std::move(value));
		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}

		throw std::runtime_error("invalid argument: Sound option requires a number");
	}

	static std::optional<SquirrelValue> FindSoundOption(SquirrelVm& vm, const SquirrelValue& object, std::string_view name) {
		const SquirrelString key{std::string(name)};
		// Native2229d0 follows explicit delegate slots with raw flag1; it does not call _get.
		if (!vm.HasSlot(object, key)) {
			return {};
		}

		return vm.ReadSlot(object, key);
	}

	static void ReadSoundPan(SquirrelVm& vm, const SquirrelValue& object, audio::SoundPan& pan) {
		static constexpr std::array<std::string_view, 8> names{"l", "r", "c", "lfe", "sl", "sr", "sbl", "sbr"};
		const auto resolved = SoundResolvedValue(vm, object);
		for (std::size_t index = 0; index != names.size(); ++index) {
			if (index < 2) {
				pan.Destinations[index] = SoundOptionFloat(vm, vm.ReadSlot(resolved, SquirrelString(std::string(names[index]))));
			} else if (const auto value = FindSoundOption(vm, resolved, names[index])) {
				pan.Destinations[index] = SoundOptionFloat(vm, *value);
			}
		}
	}

	struct SoundPlayOptions {
		audio::SoundMixParameters Mix;
		float Priority = 0;
		std::string Group;
		std::string Uniqueness;
	};

	static SoundPlayOptions ReadSoundPlayOptions(SquirrelVm& vm, const SquirrelValue& table, bool loudness_enabled) {
		SoundPlayOptions result;
		result.Mix.VoiceVolume = audio::SoundVolumeToLinear(1.0f, audio::SoundVolumeUnit::Setting, loudness_enabled);
		if (const auto input = FindSoundOption(vm, table, "volume")) {
			const auto value = SoundResolvedValue(vm, *input);
			if (vm.Type(value) == SquirrelValueType::String) {
				const auto setting = audio::SoundVolumeFromLinear(audio::ParseSoundVolume(SoundVoiceCString(value), loudness_enabled), loudness_enabled);
				result.Mix.VoiceVolume = audio::SoundVolumeToLinear(setting, audio::SoundVolumeUnit::Setting, loudness_enabled);
			} else if (vm.Type(value) == SquirrelValueType::Integer || vm.Type(value) == SquirrelValueType::Float) {
				result.Mix.VoiceVolume = audio::SoundVolumeToLinear(SoundOptionFloat(vm, value), audio::SoundVolumeUnit::Setting, loudness_enabled);
			}
		}
		if (const auto value = FindSoundOption(vm, table, "pitch")) {
			result.Mix.VoicePitchOctaves = SoundOptionFloat(vm, *value);
		}
		if (const auto value = FindSoundOption(vm, table, "priority")) {
			result.Priority = SoundOptionFloat(vm, *value);
		}
		if (const auto value = FindSoundOption(vm, table, "uniq")) {
			result.Uniqueness = SoundVoiceCString(SoundResolvedValue(vm, *value));
		}
		if (const auto value = FindSoundOption(vm, table, "group")) {
			result.Group = SoundVoiceCString(SoundResolvedValue(vm, *value));
		}
		if (const auto value = FindSoundOption(vm, table, "pan")) {
			ReadSoundPan(vm, *value, result.Mix.Pan);
		}
		if (const auto value = FindSoundOption(vm, table, "panSrcCh")) {
			static constexpr std::array<std::string_view, 8> names{"source_l", "source_r", "source_c", "source_lfe", "source_sl", "source_sr", "source_sbl", "source_sbr"};
			const auto object = SoundResolvedValue(vm, *value);
			for (std::size_t index = 0; index != names.size(); ++index) {
				if (index < 2) {
					ReadSoundPan(vm, vm.ReadSlot(object, SquirrelString(std::string(names[index]))), result.Mix.SourcePan[index]);
				} else if (const auto source = FindSoundOption(vm, object, names[index])) {
					ReadSoundPan(vm, *source, result.Mix.SourcePan[index]);
				}
			}
			result.Mix.UseSourcePan = true;
		}
		if (const auto value = FindSoundOption(vm, table, "hidMask")) {
			const auto resolved = SoundResolvedValue(vm, *value);
			// Other HID routing needs its real output producer; do not silently route it to this pool.
			if (!std::holds_alternative<std::int32_t>(resolved) || std::get<std::int32_t>(resolved) != 1) {
				throw std::runtime_error("Sound.playVoice hidMask requires recovered routing beyond the actual primary output");
			}
		}

		return result;
	}

	static SquirrelNativeResult SoundPlayVoice(SquirrelVm& vm, audio::SoundResourceControl& sound, const std::function<std::uint8_t()>& actual_output_channels, std::optional<std::uint16_t> verified_wma_encode_options, SoundBufferedPlaybackMode mode) {
		const auto count = vm.NativeStackSize();
		std::int32_t cue_index = 2;
		std::int32_t options_index = 0;
		std::int32_t play_count_index = 0;
		const auto type_at = [&](std::int32_t index) {
			return vm.Type(vm.NativeStackValue(index));
		};
		if (count < 2 || count > 5 || type_at(2) != SquirrelValueType::String) {
			throw std::runtime_error("invalid argument");
		}
		if (count >= 3 && type_at(3) == SquirrelValueType::Table) {
			options_index = 3;
			if (count == 4 && type_at(4) == SquirrelValueType::Integer) {
				play_count_index = 4;
			} else if (count != 3) {
				throw std::runtime_error("invalid argument");
			}
		} else if (count >= 3 && type_at(3) == SquirrelValueType::String) {
			cue_index = 3;
			if (count >= 4) {
				if (type_at(4) != SquirrelValueType::Table) {
					throw std::runtime_error("invalid argument");
				}
				options_index = 4;
			}
			if (count == 5) {
				if (type_at(5) != SquirrelValueType::Integer) {
					throw std::runtime_error("invalid argument");
				}
				play_count_index = 5;
			}
		} else if (count != 2) {
			throw std::runtime_error("invalid argument");
		}

		SoundPlayOptions options;
		if (options_index != 0) {
			options = ReadSoundPlayOptions(vm, vm.NativeStackValue(options_index), sound.LoudnessVolumeEnabled());
		} else {
			options.Mix.VoiceVolume = audio::SoundVolumeToLinear(1.0f, audio::SoundVolumeUnit::Setting, sound.LoudnessVolumeEnabled());
		}
		// These names own their bytes across output-reader/native callbacks and resource preparation.
		const auto cue_value = vm.NativeStackValue(cue_index);
		const auto cue = std::string(SoundVoiceCString(cue_value));
		const auto archive_value = cue_index == 3 ? vm.NativeStackValue(2) : SquirrelValue{};
		const auto archive = cue_index == 3 ? std::string(SoundVoiceCString(archive_value)) : std::string{};
		if (!actual_output_channels) {
			throw std::runtime_error("Sound.playVoice requires its actual containing buffered output geometry");
		}
		const auto channels = actual_output_channels();
		audio::BufferedSoundCueRequest request;
		request.ArchiveName = archive;
		request.CueName = cue;
		request.Group = options.Group;
		request.Uniqueness = options.Uniqueness;
		request.Priority = options.Priority;
		request.PlayCount = play_count_index == 0 ? 0 : std::get<std::int32_t>(vm.NativeStackValue(play_count_index));
		request.VerifiedWmaEncodeOptions = verified_wma_encode_options;
		request.IncrementalWma = mode == SoundBufferedPlaybackMode::IncrementalWma;
		options.Mix.MasterVolume = sound.MasterVolume();
		const auto prepared = sound.BufferedCues().Prepare(request, options.Mix, channels);
		vm.PushNativeValue(prepared.Cue ? std::bit_cast<std::int32_t>(prepared.Cue->Value) : std::int32_t{-1});

		return SquirrelNativeResult::StackStatus(1);
	}

	class ScriptSoundBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, audio::SoundResourceControl& sound)
		    : Vm(vm), Sound(sound) {
			if (sound.IsClosed()) {
				throw std::runtime_error("Sound registration requires its actual live resource/cue owner");
			}
		}

		SquirrelVm& Vm;
		audio::SoundResourceControl& Sound;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::function<std::uint8_t()> OutputChannels;
		std::optional<std::uint16_t> VerifiedWmaEncodeOptions;
		SoundBufferedPlaybackMode PlaybackMode = SoundBufferedPlaybackMode::WholeBuffer;

		SquirrelNativeResult Invoke(SquirrelVm& vm, const SoundStaticMethod& method) {
			switch (method.Operation) {
			case SoundStaticOperation::PlayVoice:
				return SoundPlayVoice(vm, Sound, OutputChannels, VerifiedWmaEncodeOptions, PlaybackMode);
			case SoundStaticOperation::SetLoudnessVolumeEnabled:
				Sound.SetLoudnessVolumeEnabled(SoundBooleanArgument(vm));
				return SquirrelNativeResult::NoValues();
			case SoundStaticOperation::GetLoudnessVolumeEnabled:
				return SquirrelNativeResult::ReturnValue(Sound.LoudnessVolumeEnabled());
			case SoundStaticOperation::Load:
				static_cast<void>(Sound.Load(SoundArgumentString(vm)));
				return SquirrelNativeResult::NoValues();
			case SoundStaticOperation::Unload:
				static_cast<void>(Sound.Unload(SoundArgumentString(vm)));
				return SquirrelNativeResult::NoValues();
			case SoundStaticOperation::GetLoading:
				return SquirrelNativeResult::ReturnValue(Sound.GetLoading(SoundArgumentString(vm)));
			case SoundStaticOperation::GetLoaded:
				return SquirrelNativeResult::ReturnValue(Sound.GetLoaded(SoundArgumentString(vm)));
			case SoundStaticOperation::LoadArchive:
				static_cast<void>(Sound.LoadArchive(SoundArgumentString(vm)));
				return SquirrelNativeResult::NoValues();
			case SoundStaticOperation::UnloadArchive:
				static_cast<void>(Sound.UnloadArchive(SoundArgumentString(vm)));
				return SquirrelNativeResult::NoValues();
			case SoundStaticOperation::GetArchiveLoading:
				return SquirrelNativeResult::ReturnValue(Sound.GetArchiveLoading(SoundArgumentString(vm)));
			case SoundStaticOperation::GetArchiveLoaded:
				return SquirrelNativeResult::ReturnValue(Sound.GetArchiveLoaded(SoundArgumentString(vm)));
			case SoundStaticOperation::SetVoiceVolume:
				return SoundVoiceVolume(vm, Sound);
			case SoundStaticOperation::AnimateVoiceVolume:
				return SoundVoiceVolumeAnimation(vm, Sound);
			case SoundStaticOperation::AnimateGroupVolume:
				return SoundGroupVolumeAnimation(vm, Sound);
			case SoundStaticOperation::SetGroupVolume:
				return SoundGroupVolume(vm, Sound);
			case SoundStaticOperation::GetGroupVolume:
				return SquirrelNativeResult::ReturnValue(audio::SoundVolumeFromLinear(Sound.GroupVolume(SoundArgumentString(vm)), Sound.LoudnessVolumeEnabled()));
			case SoundStaticOperation::SetMasterVolume:
				return SoundMasterVolume(vm, Sound);
			case SoundStaticOperation::GetMasterVolume:
				return SquirrelNativeResult::ReturnValue(audio::SoundVolumeFromLinear(Sound.MasterVolume(), Sound.LoudnessVolumeEnabled()));
			case SoundStaticOperation::GetVoicePronounceId:
				return SoundVoicePronounceId(vm, Sound.BufferedCues());
			case SoundStaticOperation::GetVoicePlaying:
				return SoundVoicePlaying(vm, Sound.BufferedCues());
			case SoundStaticOperation::PauseVoice:
				return SoundVoiceControl(vm, Sound.BufferedCues(), audio::BufferedSoundCueControl::Pause);
			case SoundStaticOperation::ResumeVoice:
				return SoundVoiceControl(vm, Sound.BufferedCues(), audio::BufferedSoundCueControl::Resume);
			case SoundStaticOperation::StopVoice:
				return SoundVoiceControl(vm, Sound.BufferedCues(), audio::BufferedSoundCueControl::Stop);
			default:
				throw std::runtime_error(std::string("unrecovered Sound operation: Sound.") + method.Name + "; actual device/clock/focus/parameter prerequisites remain open");
			}
		}
	};

	ScriptSoundBindings::ScriptSoundBindings(SquirrelVm& actual_vm, audio::SoundResourceControl& actual_sound)
	    : m_Implementation(std::make_shared<Implementation>(actual_vm, actual_sound)) {
		auto& state = *m_Implementation;
		state.Class = actual_vm.CreateClass();
		state.ClassRoot = actual_vm.Retain(state.Class);
		actual_vm.SetClassTypeTag(state.Class, &kSoundClassTag);
		actual_vm.NewSlot(state.Class, "constructor", actual_vm.CreateNativeClosure("Sound.constructor", [](SquirrelVm&, std::span<const SquirrelValue>) {
			// Native CopyOnly04b530 returns zero without attaching instance-up data or allocating an MSound owner.
			return SquirrelNativeResult::NoValues();
		}));
		actual_vm.NewSlot(state.Class, "weakref", actual_vm.CreateNativeClosure("Sound.weakref", [](SquirrelVm& vm, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::ReturnValue(vm.CreateWeakReference(vm.NativeStackValue(-1)));
		}));
		actual_vm.NewSlot(actual_vm.RootTable(), "Sound", state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		for (const auto& method : kSoundStaticMethods) {
			const auto callback = [weak, method](SquirrelVm& vm, std::span<const SquirrelValue>) {
				const auto owner = weak.lock();
				if (!owner || &owner->Vm != &vm) {
					throw std::runtime_error("Sound callback requires its actual live VM/resource owner");
				}

				if (method.Captured) {
					RequireSoundMethodCapture(vm, method.Operation);
				}

				return owner->Invoke(vm, method);
			};
			SquirrelValue closure;
			if (method.Captured) {
				const std::array<SquirrelValue, 1> captures{SoundMethodCapture(actual_vm, method.Operation)};
				closure = actual_vm.CreateNativeClosureWithCaptures(std::string("Sound.") + method.Name, callback, captures);
			} else {
				closure = actual_vm.CreateNativeClosure(std::string("Sound.") + method.Name, callback);
			}
			actual_vm.NewSlot(state.Class, method.Name, closure, true);
		}
	}

	ScriptSoundBindings::~ScriptSoundBindings() = default;

	SquirrelValue ScriptSoundBindings::SoundClass() const {
		return m_Implementation->Class;
	}

	void ScriptSoundBindings::ConfigureBufferedPlayback(std::function<std::uint8_t()> actual_output_channels, std::optional<SoundBufferedDecoderProfile> verified_decoder_profile, SoundBufferedPlaybackMode mode) {
		auto& state = *m_Implementation;
		if (!actual_output_channels || state.OutputChannels || state.Sound.IsClosed()) {
			throw std::runtime_error("Sound playback requires one actual live output geometry reader");
		}

		if (verified_decoder_profile && *verified_decoder_profile != SoundBufferedDecoderProfile::VerifiedWmaOptions31) {
			throw std::runtime_error("Sound playback decoder profile is not explicitly verified");
		}

		if (mode != SoundBufferedPlaybackMode::WholeBuffer && mode != SoundBufferedPlaybackMode::IncrementalWma) {
			throw std::runtime_error("Sound playback mode is outside the explicit whole-buffer/incremental domain");
		}
		if (mode == SoundBufferedPlaybackMode::IncrementalWma && verified_decoder_profile != SoundBufferedDecoderProfile::VerifiedWmaOptions31) {
			throw std::runtime_error("incremental Sound playback requires the explicitly verified WMA options31 profile");
		}

		state.OutputChannels = std::move(actual_output_channels);
		state.VerifiedWmaEncodeOptions = verified_decoder_profile ? std::optional<std::uint16_t>{31} : std::nullopt;
		state.PlaybackMode = mode;
	}
} // namespace sgre::script
