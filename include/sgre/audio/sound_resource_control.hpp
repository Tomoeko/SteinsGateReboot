#pragma once

#include "sgre/audio/buffered_sound_cues.hpp"
#include "sgre/core/frame_clock.hpp"
#include "sgre/io/mounted_resource_read.hpp"
#include "sgre/scene/parallel_task_set.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace sgre::audio {
	struct SoundResourceControlLimits {
		SoundConfigurationLoadLimits Loads;
		BufferedSoundSourceLimits Sources;
		BufferedSoundCueLimits Cues;
		io::MountedResourceReadLimits Reads;
	};

	// Actual resource/cue control domain of Sound. This owner creates no substitute native audio service.
	// Cache/mounts/processor enclose it; configured seed/spec/wrapped policy are supplied from the actual host.
	class SoundResourceControl {
	public:
		SoundResourceControl(resource::ResourceCache& actual_cache, resource::ArchiveMounts& actual_mounts, io::FileReadProcessor& actual_processor, std::string actual_spec, std::string actual_seed, bool actual_wrapped_psb_policy, SoundResourceControlLimits limits = {});
		// Actual empty task/cue/configuration owners exist immediately; early PostLoop/auxiliary phases never resolve cache.
		// First real configuration load resolves one stable whole receiving bundle before mapping/request mutation.
		// Provider cannot mutate this owner and real services enclose all pending requests through final retirement.
		SoundResourceControl(SoundReadServicesProvider actual_services, std::string actual_spec, std::string actual_seed, bool actual_wrapped_psb_policy, SoundResourceControlLimits limits = {});
		~SoundResourceControl() noexcept;
		SoundResourceControl(const SoundResourceControl&) = delete;
		SoundResourceControl& operator=(const SoundResourceControl&) = delete;
		SoundResourceControl(SoundResourceControl&&) = delete;
		SoundResourceControl& operator=(SoundResourceControl&&) = delete;
		[[nodiscard]] bool Load(std::string_view exact_filename);
		[[nodiscard]] bool Unload(std::string_view exact_filename);
		[[nodiscard]] bool GetLoading(std::string_view exact_filename) const;
		[[nodiscard]] bool GetLoaded(std::string_view exact_filename) const;
		[[nodiscard]] bool LoadArchive(std::string_view exact_archive_name);
		[[nodiscard]] bool UnloadArchive(std::string_view exact_archive_name);
		[[nodiscard]] bool GetArchiveLoading(std::string_view exact_archive_name) const;
		[[nodiscard]] bool GetArchiveLoaded(std::string_view exact_archive_name) const;
		[[nodiscard]] SoundConfigurationLoadInfo ConfigurationInfo(std::string_view exact_filename) const;
		[[nodiscard]] SoundArchiveLoadInfo ArchiveInfo(std::string_view exact_archive_name) const;
		// Diagnostic borrows require this actual configuration/archive and cache reference until their last use.
		[[nodiscard]] const io::PublishedPsbFile& ConfigurationFile(std::string_view exact_filename) const;
		[[nodiscard]] const io::PublishedPsbFile& ArchiveFile(std::string_view exact_archive_name) const;
		[[nodiscard]] BufferedSoundCues& BufferedCues();
		// Real output borrows the actual containing decoded pool before any cue is prepared.
		void ConnectOutput(BufferedSoundOutput& actual_output);
		[[nodiscard]] bool PauseAll();
		[[nodiscard]] bool ResumeAll();
		[[nodiscard]] bool GetAllPaused() const;
		// Native option changes future setting conversions; already retained linear gains remain unchanged.
		void SetLoudnessVolumeEnabled(bool enabled);
		[[nodiscard]] bool LoudnessVolumeEnabled() const;
		void SetMasterVolume(float linear_volume);
		[[nodiscard]] float MasterVolume() const;
		void SetGroupVolume(std::string_view exact_group, float linear_volume);
		[[nodiscard]] float GroupVolume(std::string_view exact_group) const;
		// The actual selected Looper clock encloses Sound through retirement; this registration never advances it.
		// The actual selected clock encloses this owner and every extracted Parallel callback lease.
		void ConfigureVolumeAnimations(const core::FrameClock& actual_selected_clock);
		void AnimateVoiceVolume(BufferedSoundCueId cue, float target_linear, std::int32_t duration, std::int32_t curve);
		void AnimateGroupVolume(std::string_view exact_group, float target_linear, std::int32_t duration, std::int32_t curve);
		// ApplicationClockPump's actual borrowed receiver uses these without implying OS audio/device readiness.
		// The caller clears that pump's auxiliary registration before destroying this containing owner.
		void RequireAuxiliaryReceiver() const;
		void InvokeAuxiliaryPause(bool suspended);
		[[nodiscard]] bool AuxiliaryOwnsPause() const;
		[[nodiscard]] scene::ParallelTaskSet& ParallelTasks();
		// Caller drives the actual postloop after prepared reader work: Parallel, Serial, cue retirement.
		// Native clock/focus/device registration and automatic timing are not inferred from this control phase.
		void AdvancePostLoop();
		// Nonblocking containing shutdown. Real postloop/reader work continues until all cue/worker/resource borrowers retire.
		// New load/output requests reject after this call; existing resource delivery is not cancelled or fabricated.
		void RequestClose();
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		// After a live-reader close request and complete borrower retirement, Driver may close that reader first.
		// Its concrete cache/processor objects must still enclose this final Close; no postloop/request is admitted then.
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::audio
