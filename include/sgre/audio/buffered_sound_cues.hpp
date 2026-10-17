#pragma once

#include "sgre/audio/buffered_sound_sources.hpp"
#include "sgre/audio/sound_configuration_loads.hpp"
#include "sgre/audio/user_audio_settings.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace sgre::audio {

	struct BufferedSoundCueId {
		std::uint32_t Value = 0;
	};

	enum class BufferedSoundCueControl {
		Pause,
		Resume,
		Stop
	};

	enum class BufferedSoundCuePreparationStatus {
		MissingDefinition,
		RejectedUniqueness,
		RejectedSlots,
		Prepared,
		PartiallyPrepared,
		FailedSources,
		PendingSources
	};

	struct BufferedSoundCuePreparation {
		BufferedSoundCuePreparationStatus Status = BufferedSoundCuePreparationStatus::MissingDefinition;
		// Identifies an actual logical cue payload even when its source preparation failed, as native381930 does.
		std::optional<BufferedSoundCueId> Cue;
	};

	struct BufferedSoundCueRequest {
		// Empty archive selects the first matching actual completed archive in configuration/archive name order.
		std::string_view ArchiveName;
		std::string_view CueName;
		// Empty options inherit the resource definition; active cues own their resolved names.
		std::string_view Group;
		std::string_view Uniqueness;
		std::int32_t PlayCount = 1;
		float Priority = 0;
		bool StartPaused = false;
		bool LimitUniquenessToArchive = true;
		std::optional<std::uint16_t> VerifiedWmaEncodeOptions;
		// Explicit host policy: only WMA Standard channels use the real worker/ring; defaults remain complete-buffer.
		bool IncrementalWma = false;
	};

	struct BufferedSoundCueLimits {
		std::size_t MaxConsumeFrames = 2048;
	};

	struct BufferedSoundCueInfo {
		// Diagnostic strings borrow this active cue until Stop/retirement/close.
		std::string_view ConfigurationFilename;
		std::string_view ArchiveName;
		std::string_view CueName;
		std::size_t Channels = 0;
		bool CallbackActive = false;
		bool Paused = false;
		std::uint32_t PauseReasons = 0;
		BufferedSoundCuePreparationStatus Preparation = BufferedSoundCuePreparationStatus::FailedSources;
	};

	struct BufferedSoundCueChannelInfo {
		SoundVoiceAddress Source;
		BufferedSoundSourceInfo State;
		std::optional<SoundMix> Mix;
		// Borrows this active cue until Stop/retirement/close; failures never stand in for decoded samples.
		std::string_view Error;
		bool PendingPreparation = false;
	};

	// Owns actual buffered cue payloads, source slots and resource borrows. No script/native-service readiness.
	// Actual loads/cache/reader enclose this serialized owner. Clock/focus/platform output remain separate.
	class BufferedSoundCues {
	public:
		BufferedSoundCues(SoundConfigurationLoads& actual_loads, BufferedSoundSourceLimits source_limits = {}, BufferedSoundCueLimits cue_limits = {});
		~BufferedSoundCues() noexcept;
		BufferedSoundCues(const BufferedSoundCues&) = delete;
		BufferedSoundCues& operator=(const BufferedSoundCues&) = delete;
		BufferedSoundCues(BufferedSoundCues&&) = delete;
		BufferedSoundCues& operator=(BufferedSoundCues&&) = delete;
		// Connect the real source output before any cue preparation; no native engine/device readiness is inferred.
		void ConnectOutput(BufferedSoundOutput& actual_output);
		// Preparation decodes outside ordinary frames. This owner resolves the actual definition group factor; remaining factors/output geometry are supplied.
		[[nodiscard]] BufferedSoundCuePreparation Prepare(const BufferedSoundCueRequest& request, const SoundMixParameters& actual_mix_parameters, std::uint8_t actual_destination_channels);
		[[nodiscard]] bool Stop(BufferedSoundCueId cue);
		[[nodiscard]] bool Pause(BufferedSoundCueId cue);
		[[nodiscard]] bool Resume(BufferedSoundCueId cue);
		// Actual global reason2 transitions commit decoded source state immediately; other reasons stay active.
		[[nodiscard]] bool PauseAll();
		[[nodiscard]] bool ResumeAll();
		[[nodiscard]] bool GetAllPaused() const;
		// Re-evaluate each live channel from retained factors, including a previous zero master gain.
		void SetMasterVolume(float linear_volume);
		// Independent listening controls; original group/mute/voice getters retain their values.
		void SetUserAudioSettings(const UserAudioSettings& settings);
		// Native3837a0 replaces one active logical cue gain and commits every real channel matrix.
		[[nodiscard]] bool SetVoiceVolume(BufferedSoundCueId cue, float linear_volume);
		// Native380720 copies the actual logical gain; its caller-initialized unity survives an absent ID.
		[[nodiscard]] float VoiceVolume(BufferedSoundCueId cue) const;
		// Native382f10 updates matching active cues and the same group lookup used by later preparation.
		void SetGroupVolume(std::string_view exact_group, float linear_volume);
		[[nodiscard]] float GroupVolume(std::string_view exact_group) const;
		// Native384770/384690 select the first exact active-list name, optionally restricted to its archive.
		[[nodiscard]] std::optional<BufferedSoundCueId> FindNamedCue(std::string_view exact_cue_name, std::optional<std::string_view> exact_archive_name = {}) const;
		// Actual native voice controls commit sources immediately; Stop polls only retired cue borrowers.
		// This CPU owner creates no audio device and does not advance its samples or the containing clock.
		[[nodiscard]] bool ApplyControl(BufferedSoundCueId cue, BufferedSoundCueControl control);
		// Actual decoded callback activity is independent of pause/running/null-context and ID lifetime.
		// This prepared CPU owner grants no native engine/device readiness. An absent active ID returns false.
		[[nodiscard]] bool IsPlaying(BufferedSoundCueId cue) const;
		[[nodiscard]] bool UnloadArchive(std::string_view archive_name);
		[[nodiscard]] bool UnloadConfiguration(std::string_view filename);
		// Commit and retire actual sources/cues only. The enclosing postloop still updates Parallel before Serial loads.
		void CommitAndRetire();
		[[nodiscard]] BufferedSoundCueInfo Inspect(BufferedSoundCueId cue) const;
		[[nodiscard]] BufferedSoundCueChannelInfo InspectChannel(BufferedSoundCueId cue, std::size_t channel) const;
		[[nodiscard]] BufferedSoundSourceInfo InspectSource(SoundVoiceAddress address) const;
		// Consumes source-rate frames without allocation/I/O. Caller chooses clock/timing; no pitch/rate conversion is implied.
		// Typed worker/ring reads retain Starved/Prefilling/terminal outcomes; no source progress is invented.
		[[nodiscard]] WmaStreamReadResult ConsumeStreamingChannelFrames(BufferedSoundCueId cue, std::size_t channel, std::span<float> destination);
		[[nodiscard]] std::size_t ConsumeChannelFrames(BufferedSoundCueId cue, std::size_t channel, std::span<float> destination);
		// Accumulates real consumed frames with the recovered matrix. Non-unit frequency ratios fail pending resampling recovery.
		[[nodiscard]] std::size_t ConsumeMixedChannelFrames(BufferedSoundCueId cue, std::size_t channel, std::span<float> output);
		[[nodiscard]] std::size_t ActiveCount() const;
		[[nodiscard]] std::size_t RetiringCount() const;
		// Nonblocking stop request; continue actual postloop polling until callback flush and worker acknowledgement.
		void RequestClose();
		[[nodiscard]] bool CanClose() const;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::audio
