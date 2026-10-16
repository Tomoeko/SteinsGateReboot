#pragma once

#include "sgre/audio/sound_resource_control.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace sgre::script {
	enum class SoundBufferedDecoderProfile : std::uint16_t {
		VerifiedWmaOptions31 = 31
	};

	// Authored host storage/worker policy; the script call shape and recovered submission remain unchanged.
	enum class SoundBufferedPlaybackMode : std::uint8_t {
		WholeBuffer,
		IncrementalWma
	};

	class ScriptSoundBindings {
	public:
		// VM and actual resource/cue owner enclose these callbacks. Sound() never constructs another host owner.
		// Native registry metadata is complete; only recovered resource and actual prepared-cue controls/queries are callable in this profile.
		ScriptSoundBindings(SquirrelVm& actual_vm, audio::SoundResourceControl& actual_sound);
		~ScriptSoundBindings();
		ScriptSoundBindings(const ScriptSoundBindings&) = delete;
		ScriptSoundBindings& operator=(const ScriptSoundBindings&) = delete;
		ScriptSoundBindings(ScriptSoundBindings&&) = delete;
		ScriptSoundBindings& operator=(ScriptSoundBindings&&) = delete;
		[[nodiscard]] SquirrelValue SoundClass() const;
		// The containing output owner encloses this reader and supplies its live channel geometry.
		// No hardware, clock, focus or native-service readiness is inferred from this connection.
		// An explicit measured content profile; an absent WMA extension never selects it.
		// IncrementalWma additionally requires the verified31 profile and a genuine typed output/source consumer.
		void ConfigureBufferedPlayback(std::function<std::uint8_t()> actual_output_channels, std::optional<SoundBufferedDecoderProfile> verified_decoder_profile = std::nullopt, SoundBufferedPlaybackMode mode = SoundBufferedPlaybackMode::WholeBuffer);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
