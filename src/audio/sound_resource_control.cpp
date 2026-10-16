#include "sgre/audio/sound_resource_control.hpp"

#include "sgre/audio/sound_volume_animations.hpp"

#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::audio {
	class SoundResourceControl::Implementation {
	public:
		Implementation(resource::ResourceCache& cache, resource::ArchiveMounts& mounts, io::FileReadProcessor& processor, std::string spec, std::string seed, bool wrapped, SoundResourceControlLimits limits)
		    : Services(SoundReadServices{cache, mounts, processor}), Seed(std::move(seed)), Limits(limits), Loads(cache, mounts, processor, std::move(spec), limits.Loads), Cues(Loads, limits.Sources, limits.Cues) {
			Initialize(wrapped);
		}

		Implementation(SoundReadServicesProvider provider, std::string spec, std::string seed, bool wrapped, SoundResourceControlLimits limits)
		    : Provider(std::move(provider)), Seed(std::move(seed)), Limits(limits), Loads(BoundServicesProvider(), std::move(spec), limits.Loads), Cues(Loads, limits.Sources, limits.Cues) {
			if (!Provider) {
				throw std::runtime_error("deferred Sound control requires its actual receiving-services provider");
			}

			Initialize(wrapped);
		}

		SoundReadServicesProvider BoundServicesProvider() {
			return [this] {
				RequireOwner();
				if (!Services) {
					throw std::runtime_error("Sound loads require their containing control's actual already resolved request bundle");
				}

				return *Services;
			};
		}

		void Initialize(bool wrapped) {
			if (Seed.empty() || Seed.size() > 65536 || Seed.find('\0') != std::string::npos) {
				throw std::runtime_error("Sound control requires the actual bounded resource cipher seed");
			}

			Format.Kind = io::ResourceFileKind::Psb;
			Format.Wrapped = wrapped;
			Parallel.SetRetireWhenEmpty(false);
		}

		std::optional<SoundReadServices> Services;
		SoundReadServicesProvider Provider;
		std::string Seed;
		SoundResourceControlLimits Limits;
		io::ResourceReadFormat Format;
		SoundConfigurationLoads Loads;
		BufferedSoundCues Cues;
		scene::ParallelTaskSet Parallel;
		std::optional<SoundVolumeAnimations> VolumeAnimations;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		bool Calling = false;
		bool ResolvingServices = false;
		bool AuxiliaryPaused = false;
		float MasterVolume = 1.0f;
		bool LoudnessVolumeEnabled = true;
		bool Closing = false;
		bool Closed = false;

		void RequireOwner(bool allow_calling = false) const {
			if (Closed || ResolvingServices || (Calling && !allow_calling) || (Services && Services->Processor.IsClosed()) || std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("Sound resource control requires its actual live serialized receiving owner");
			}
		}

		void RequireRetirementOwner() const {
			if (Closed || ResolvingServices || Calling || std::this_thread::get_id() != OwnerThread || (Services && Services->Processor.IsClosed() && !Closing)) {
				throw std::runtime_error("Sound final retirement requires its serialized owner and an existing live-receiving close request");
			}
		}

		void RequireAcceptingRequests() const {
			RequireOwner();
			if (Closing) {
				throw std::runtime_error("Sound resource request requires an owner without a pending close request");
			}
		}

		const SoundReadServices& ActualServices() {
			RequireOwner();
			if (Services) {
				return *Services;
			}

			ResolvingServices = true;
			try {
				const auto actual = Provider();
				if (actual.Processor.IsClosed() || !actual.Processor.UsesReceivingOwners(actual.Cache, actual.Mounts)) {
					throw std::runtime_error("deferred Sound control requires one actual live cache/mount/processor bundle");
				}

				Services.emplace(actual);
			} catch (...) {
				ResolvingServices = false;
				throw;
			}
			ResolvingServices = false;

			return *Services;
		}

		io::PreparedResourceFileRead PrepareRead(std::string_view name) {
			const auto& actual = ActualServices();
			io::PreparedResourceFileRead source;
			source.Format = Format;
			if (!actual.Cache.FindNamedResource(name)) {
				const auto filename = Format.Wrapped ? std::string(name) + ".m" : std::string(name);
				const auto occurrence = filename.find(".m");
				const auto native_sort_key = filename.size() >= 2 && occurrence == filename.size() - 2 ? std::string_view(filename).substr(0, filename.size() - 2) : std::string_view(filename);
				if (native_sort_key != name) {
					throw std::runtime_error("Sound resource identity exceeds the recovered native sorter-key domain");
				}

				source = io::PrepareQueuedMountedResourceRead(actual.Mounts, filename, Format, Seed, Limits.Reads);
			}

			return source;
		}
	};

	SoundResourceControl::SoundResourceControl(resource::ResourceCache& actual_cache, resource::ArchiveMounts& actual_mounts, io::FileReadProcessor& actual_processor, std::string actual_spec, std::string actual_seed, bool actual_wrapped_psb_policy, SoundResourceControlLimits limits)
	    : m_Implementation(std::make_unique<Implementation>(actual_cache, actual_mounts, actual_processor, std::move(actual_spec), std::move(actual_seed), actual_wrapped_psb_policy, limits)) {
	}

	SoundResourceControl::SoundResourceControl(SoundReadServicesProvider actual_services, std::string actual_spec, std::string actual_seed, bool actual_wrapped_psb_policy, SoundResourceControlLimits limits)
	    : m_Implementation(std::make_unique<Implementation>(std::move(actual_services), std::move(actual_spec), std::move(actual_seed), actual_wrapped_psb_policy, limits)) {
	}

	SoundResourceControl::~SoundResourceControl() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	bool SoundResourceControl::Load(std::string_view exact_filename) {
		auto& owner = *m_Implementation;
		owner.RequireAcceptingRequests();
		if (owner.Loads.GetLoaded(exact_filename) || owner.Loads.GetLoading(exact_filename)) {
			return false;
		}

		return owner.Loads.LoadPrepared(std::string(exact_filename), owner.PrepareRead(exact_filename));
	}

	bool SoundResourceControl::Unload(std::string_view exact_filename) {
		m_Implementation->RequireOwner();

		return m_Implementation->Cues.UnloadConfiguration(exact_filename);
	}

	bool SoundResourceControl::GetLoading(std::string_view exact_filename) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Loads.GetLoading(exact_filename);
	}

	bool SoundResourceControl::GetLoaded(std::string_view exact_filename) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Loads.GetLoaded(exact_filename);
	}

	bool SoundResourceControl::LoadArchive(std::string_view exact_archive_name) {
		auto& owner = *m_Implementation;
		owner.RequireAcceptingRequests();
		if (owner.Loads.GetArchiveLoaded(exact_archive_name) || owner.Loads.GetArchiveLoading(exact_archive_name)) {
			return false;
		}

		// Native381080 requires a completed configuration declaration before requesting the concrete archive file.
		const auto filename = owner.Loads.PrepareArchiveFilename(exact_archive_name);
		if (!filename) {
			return false;
		}

		return owner.Loads.LoadArchivePrepared(std::string(exact_archive_name), owner.PrepareRead(*filename));
	}

	bool SoundResourceControl::UnloadArchive(std::string_view exact_archive_name) {
		m_Implementation->RequireOwner();

		return m_Implementation->Cues.UnloadArchive(exact_archive_name);
	}

	bool SoundResourceControl::GetArchiveLoading(std::string_view exact_archive_name) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Loads.GetArchiveLoading(exact_archive_name);
	}

	bool SoundResourceControl::GetArchiveLoaded(std::string_view exact_archive_name) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Loads.GetArchiveLoaded(exact_archive_name);
	}

	SoundConfigurationLoadInfo SoundResourceControl::ConfigurationInfo(std::string_view exact_filename) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Loads.Info(exact_filename);
	}

	SoundArchiveLoadInfo SoundResourceControl::ArchiveInfo(std::string_view exact_archive_name) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Loads.ArchiveInfo(exact_archive_name);
	}

	const io::PublishedPsbFile& SoundResourceControl::ConfigurationFile(std::string_view exact_filename) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Loads.File(exact_filename);
	}

	const io::PublishedPsbFile& SoundResourceControl::ArchiveFile(std::string_view exact_archive_name) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Loads.ArchiveFile(exact_archive_name);
	}

	BufferedSoundCues& SoundResourceControl::BufferedCues() {
		m_Implementation->RequireOwner();

		return m_Implementation->Cues;
	}

	void SoundResourceControl::ConnectOutput(BufferedSoundOutput& actual_output) {
		m_Implementation->RequireAcceptingRequests();
		m_Implementation->Cues.ConnectOutput(actual_output);
	}

	bool SoundResourceControl::PauseAll() {
		m_Implementation->RequireOwner();

		return m_Implementation->Cues.PauseAll();
	}

	bool SoundResourceControl::ResumeAll() {
		m_Implementation->RequireOwner();

		return m_Implementation->Cues.ResumeAll();
	}

	bool SoundResourceControl::GetAllPaused() const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Cues.GetAllPaused();
	}

	void SoundResourceControl::SetLoudnessVolumeEnabled(bool enabled) {
		m_Implementation->RequireOwner();
		m_Implementation->LoudnessVolumeEnabled = enabled;
	}

	bool SoundResourceControl::LoudnessVolumeEnabled() const {
		m_Implementation->RequireOwner();

		return m_Implementation->LoudnessVolumeEnabled;
	}

	void SoundResourceControl::SetMasterVolume(float linear_volume) {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		if (owner.MasterVolume == linear_volume) {
			return;
		}

		// Native383100 updates every active cue before the immediate source operation-set commit.
		owner.MasterVolume = linear_volume;
		owner.Cues.SetMasterVolume(linear_volume);
	}

	float SoundResourceControl::MasterVolume() const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->MasterVolume;
	}

	void SoundResourceControl::SetGroupVolume(std::string_view exact_group, float linear_volume) {
		m_Implementation->RequireOwner();
		m_Implementation->Cues.SetGroupVolume(exact_group, linear_volume);
	}

	float SoundResourceControl::GroupVolume(std::string_view exact_group) const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->Cues.GroupVolume(exact_group);
	}

	void SoundResourceControl::ConfigureVolumeAnimations(const core::FrameClock& actual_selected_clock) {
		auto& owner = *m_Implementation;
		owner.RequireAcceptingRequests();
		if (owner.VolumeAnimations) {
			throw std::runtime_error("Sound volume animations require one actual selected clock registration");
		}
		owner.VolumeAnimations.emplace(owner.Cues, actual_selected_clock, owner.Parallel);
	}

	void SoundResourceControl::AnimateVoiceVolume(BufferedSoundCueId cue, float target_linear, std::int32_t duration, std::int32_t curve) {
		auto& owner = *m_Implementation;
		owner.RequireAcceptingRequests();
		if (!owner.VolumeAnimations) {
			throw std::runtime_error("Sound volume animation requires its actual selected Looper clock");
		}
		owner.VolumeAnimations->AppendVoice(cue, target_linear, duration, curve);
	}

	void SoundResourceControl::AnimateGroupVolume(std::string_view exact_group, float target_linear, std::int32_t duration, std::int32_t curve) {
		auto& owner = *m_Implementation;
		owner.RequireAcceptingRequests();
		if (!owner.VolumeAnimations) {
			throw std::runtime_error("Sound volume animation requires its actual selected Looper clock");
		}
		owner.VolumeAnimations->AppendGroup(exact_group, target_linear, duration, curve);
	}

	void SoundResourceControl::RequireAuxiliaryReceiver() const {
		m_Implementation->RequireOwner();
		static_cast<void>(m_Implementation->Cues.GetAllPaused());
	}

	void SoundResourceControl::InvokeAuxiliaryPause(bool suspended) {
		auto& owner = *m_Implementation;
		RequireAuxiliaryReceiver();
		if (suspended) {
			if (!owner.AuxiliaryPaused && !owner.Cues.GetAllPaused()) {
				static_cast<void>(owner.Cues.PauseAll());
				owner.AuxiliaryPaused = true;
			}
		} else if (owner.AuxiliaryPaused) {
			static_cast<void>(owner.Cues.ResumeAll());
			owner.AuxiliaryPaused = false;
		}
	}

	bool SoundResourceControl::AuxiliaryOwnsPause() const {
		m_Implementation->RequireOwner(true);

		return m_Implementation->AuxiliaryPaused;
	}

	scene::ParallelTaskSet& SoundResourceControl::ParallelTasks() {
		m_Implementation->RequireOwner();

		return m_Implementation->Parallel;
	}

	void SoundResourceControl::AdvancePostLoop() {
		auto& owner = *m_Implementation;
		owner.RequireOwner();
		owner.Calling = true;
		try {
			if (owner.Parallel.Status() == scene::TaskStatus::Active) {
				owner.Parallel.Update();
			}
			owner.Loads.AdvanceSerialTasks();
			owner.Cues.CommitAndRetire();
			owner.Loads.RetireReadyResources();
		} catch (...) {
			owner.Calling = false;
			throw;
		}
		owner.Calling = false;
	}

	void SoundResourceControl::RequestClose() {
		auto& owner = *m_Implementation;
		if (owner.Closed) {
			return;
		}
		owner.RequireOwner();
		owner.Parallel.RequireIdleTraversal();
		owner.Closing = true;
		if (owner.VolumeAnimations) {
			owner.VolumeAnimations->CancelAll();
		}
		// No serial/resource completion is synthesized. The real postloop continues to own its original task order.
		owner.Cues.RequestClose();
	}

	bool SoundResourceControl::CanClose() const noexcept {
		const auto& owner = *m_Implementation;
		if (std::this_thread::get_id() != owner.OwnerThread || owner.Calling || owner.ResolvingServices) {
			return false;
		}
		if (owner.Closed) {
			return true;
		}
		// Driver may retire its reader before final Sound/cache destruction. No pending Sound callback may cross that boundary.
		if ((owner.Services && owner.Services->Processor.IsClosed() && (!owner.Closing || owner.Parallel.Count() != 0)) || !owner.Parallel.CanClear() || owner.Loads.SerialLoads().Count() != 0 || !owner.Loads.SerialLoads().CanClear()) {
			return false;
		}
		try {
			return (!owner.VolumeAnimations || owner.VolumeAnimations->CanClose()) && owner.Cues.CanClose() && owner.Loads.CanClose();
		} catch (...) {
			// This noexcept observation never grants shutdown if an actual contained owner rejects its lifetime/phase.
			return false;
		}
	}

	bool SoundResourceControl::IsClosed() const noexcept {
		return m_Implementation->Closed;
	}

	void SoundResourceControl::Close() {
		auto& owner = *m_Implementation;
		if (owner.Closed) {
			return;
		}
		owner.RequireRetirementOwner();
		// Preserve the existing pending-task rejection before mutating any cue when Close has not been requested.
		if (!owner.Parallel.CanClear() || owner.Loads.SerialLoads().Count() != 0 || !owner.Loads.SerialLoads().CanClear()) {
			throw std::runtime_error("Sound control close requires every actual pending serial/parallel borrower to finish");
		}
		if (!owner.Closing) {
			RequestClose();
		}
		if (!CanClose()) {
			throw std::runtime_error("Sound control close requires actual cue/callback retirement, decoder acknowledgement and unborrowed resources");
		}
		owner.Calling = true;
		try {
			owner.Cues.Close();
			owner.Parallel.Clear();
			owner.Loads.Close();
		} catch (...) {
			owner.Calling = false;
			throw;
		}
		owner.Calling = false;
		owner.Closed = true;
	}
} // namespace sgre::audio
