#include "sgre/audio/sound_configuration_loads.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::audio {

	static constexpr std::size_t kMaxSoundConfigurations = 32768;

	static void RequireSoundSpec(std::string_view actual_spec) {
		if (actual_spec.empty() || actual_spec.size() > 4096 || actual_spec.find('\0') != std::string_view::npos) {
			throw std::runtime_error("sound configuration requires the actual bounded startup spec");
		}
	}

	static std::optional<float> SoundConfigurationVersion(const resource::PsbValue& value) {
		if (const auto* number = std::get_if<double>(&value.Data)) {
			if (!std::isfinite(*number) || std::abs(*number) > std::numeric_limits<float>::max()) {
				return std::nullopt;
			}

			return static_cast<float>(*number);
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return static_cast<float>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1.0f : 0.0f;
		}

		return std::nullopt;
	}

	static SoundConfigurationDiagnostic InspectSoundDocumentIdentity(const resource::PsbDocument& document, std::string_view actual_spec, std::string_view expected_id) {
		RequireSoundSpec(actual_spec);
		const auto* id_value = document.Root.Find("id");
		if (!id_value) {
			return SoundConfigurationDiagnostic::IgnoredIdentity;
		}

		const auto* id = std::get_if<std::string>(&id_value->Data);
		if (!id || id->find('\0') != std::string::npos) {
			return SoundConfigurationDiagnostic::InvalidMetadata;
		}
		if (*id != expected_id) {
			return SoundConfigurationDiagnostic::IgnoredIdentity;
		}

		const auto* spec_value = document.Root.Find("spec");
		const auto* spec = spec_value ? std::get_if<std::string>(&spec_value->Data) : nullptr;
		if (!spec || spec->find('\0') != std::string::npos) {
			return SoundConfigurationDiagnostic::InvalidMetadata;
		}
		if (*spec != actual_spec) {
			return SoundConfigurationDiagnostic::WrongSpec;
		}

		const auto* version_value = document.Root.Find("version");
		const auto version = version_value ? SoundConfigurationVersion(*version_value) : std::nullopt;
		if (!version) {
			return SoundConfigurationDiagnostic::InvalidMetadata;
		}
		if (*version < 1.02f) {
			return SoundConfigurationDiagnostic::OldVersion;
		}
		if (*version > 1.02f) {
			return SoundConfigurationDiagnostic::NewVersion;
		}

		return SoundConfigurationDiagnostic::Compatible;
	}

	SoundConfigurationDiagnostic InspectSoundConfigurationDocument(const resource::PsbDocument& document, std::string_view actual_spec) {
		return InspectSoundDocumentIdentity(document, actual_spec, "sound");
	}

	class SoundConfigurationLoads::Implementation {
	public:
		Implementation(resource::ResourceCache& cache, resource::ArchiveMounts& mounts, io::FileReadProcessor& processor, std::string spec, SoundConfigurationLoadLimits limits) : Services(SoundReadServices{cache, mounts, processor}), Spec(std::move(spec)), Limits(limits), Serial(CheckedTaskCapacity(limits)) {
			RequireServices(*Services);
			Initialize();
		}

		Implementation(SoundReadServicesProvider provider, std::string spec, SoundConfigurationLoadLimits limits) : Provider(std::move(provider)), Spec(std::move(spec)), Limits(limits), Serial(CheckedTaskCapacity(limits)) {
			if (!Provider) {
				throw std::runtime_error("deferred sound configuration owner requires its actual receiving-services provider");
			}

			Initialize();
		}

		void Initialize() {
			RequireSoundSpec(Spec);
			Retiring.reserve(Limits.MaxConfigurations);
			Serial.SetRetireWhenEmpty(false);
		}

		static void RequireServices(const SoundReadServices& actual) {
			if (actual.Processor.IsClosed() || !actual.Processor.UsesReceivingOwners(actual.Cache, actual.Mounts)) {
				throw std::runtime_error("sound configuration loads require their actual live cache/mount/reader owners");
			}
		}

		class Archive {
		public:
			Archive(resource::ResourceCache& cache, std::size_t& archive_owners, std::string configuration_filename, std::string filename) : Cache(cache), ArchiveOwners(archive_owners) {
				Information.ConfigurationFilename = std::move(configuration_filename);
				Information.Filename = std::move(filename);
				++ArchiveOwners;
			}

			~Archive() noexcept {
				if (LoadBorrow || CompletionBorrow) {
					std::terminate();
				}

				Definitions.clear();
				try {
					if (Reference) {
						Cache.Release(*Reference);
					}
				} catch (...) {
					std::terminate();
				}
				--ArchiveOwners;
			}

			resource::ResourceCache& Cache;
			std::size_t& ArchiveOwners;
			std::optional<resource::CachedResourceHandle> Reference;
			const io::PublishedPsbFile* Delivered = nullptr;
			SoundArchiveLoadInfo Information;
			resource::SoundArchiveDefinitions Definitions;
			scene::SerialTaskHandle LoadTask;
			bool LoadBorrow = false;
			bool CompletionBorrow = false;
			bool ReadFinished = false;
			bool Unloading = false;

			bool CanRetire() const noexcept {
				// Actual cue resource borrows are checked by their owning shared reference before retirement.
				return Information.CompletionLatched && !LoadBorrow && !CompletionBorrow;
			}
		};

		class Configuration {
		public:
			Configuration(resource::ResourceCache& cache, std::string filename) : Cache(cache), Filename(std::move(filename)) {
				const auto separator = Filename.find_last_of("/\\");
				if (separator != std::string::npos) {
					Information.DirectoryPrefix = Filename.substr(0, separator);
				}
			}

			~Configuration() noexcept {
				if (LoadBorrow || CompletionBorrow || !Archives.empty() || !RetiringArchives.empty()) {
					std::terminate();
				}

				try {
					if (Reference) {
						Cache.Release(*Reference);
					}
				} catch (...) {
					std::terminate();
				}
			}

			resource::ResourceCache& Cache;
			std::string Filename;
			std::optional<resource::CachedResourceHandle> Reference;
			const io::PublishedPsbFile* Delivered = nullptr;
			SoundConfigurationLoadInfo Information;
			std::map<std::string, std::shared_ptr<Archive>, std::less<>> Archives;
			std::vector<std::shared_ptr<Archive>> RetiringArchives;
			scene::SerialTaskHandle LoadTask;
			bool LoadBorrow = false;
			bool CompletionBorrow = false;
			bool ReadFinished = false;
			bool DeliveryRejected = false;
			bool Unloading = false;

			bool CanRetire() const noexcept {
				return Unloading && Archives.empty() && RetiringArchives.empty() && !LoadBorrow && !CompletionBorrow && (Information.CompletionLatched || DeliveryRejected);
			}
		};

		std::optional<SoundReadServices> Services;
		SoundReadServicesProvider Provider;
		std::string Spec;
		SoundConfigurationLoadLimits Limits;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::size_t ArchiveOwners = 0;
		std::map<std::string, std::shared_ptr<Configuration>, std::less<>> Live;
		std::vector<std::shared_ptr<Configuration>> Retiring;
		scene::SerialTaskSet Serial;
		bool Calling = false;
		bool ResolvingServices = false;
		bool Closed = false;

		static std::size_t CheckedTaskCapacity(const SoundConfigurationLoadLimits& limits) {
			if (limits.MaxConfigurations == 0 || limits.MaxConfigurations >= kMaxSoundConfigurations || limits.MaxArchives == 0 || limits.MaxArchives > kMaxSoundConfigurations - limits.MaxConfigurations || limits.MaxNameBytes < 5 || limits.MaxNameBytes > 65536) {
				throw std::runtime_error("sound configuration loads require bounded owner/task/name capacities");
			}
			if (limits.Definitions.MaxVoices == 0 || limits.Definitions.MaxVoices > 65536 || limits.Definitions.MaxChannelsPerVoice == 0 || limits.Definitions.MaxChannelsPerVoice > 64 || limits.Definitions.MaxNameBytes == 0 || limits.Definitions.MaxNameBytes > 65536 || limits.Definitions.MaxTotalChannels == 0 || limits.Definitions.MaxTotalChannels > 65536 || limits.Definitions.MaxOwnedFieldBytes == 0 || limits.Definitions.MaxOwnedFieldBytes > 64 * 1024 * 1024) {
				throw std::runtime_error("sound archive owner requires bounded definition work and storage");
			}

			return (limits.MaxConfigurations + limits.MaxArchives) * 2;
		}

		void RequireOpen() const {
			if (Closed || Calling || ResolvingServices || std::this_thread::get_id() != OwnerThread || (Services && Services->Processor.IsClosed())) {
				throw std::runtime_error("sound configuration operation requires its actual live nonreentrant receiving-thread owner");
			}
		}

		const SoundReadServices& ActualServices() {
			RequireOpen();
			if (Services) {
				return *Services;
			}

			ResolvingServices = true;
			try {
				const auto actual = Provider();
				RequireServices(actual);
				Services.emplace(actual);
			} catch (...) {
				ResolvingServices = false;
				throw;
			}
			ResolvingServices = false;

			return *Services;
		}

		void RequireFilename(std::string_view filename) const {
			if (filename.empty() || filename.size() > Limits.MaxNameBytes || filename.find('\0') != std::string_view::npos) {
				throw std::runtime_error("sound configuration filename exceeds its exact bounded identity domain");
			}
		}

		const Configuration& Find(std::string_view filename) const {
			const auto found = Live.find(filename);
			if (found == Live.end()) {
				throw std::runtime_error("sound configuration has no live lookup: " + std::string(filename));
			}

			return *found->second;
		}

		static void Poll(Configuration& configuration, scene::SerialTask& task) {
			const std::array targets{*configuration.Reference};
			if (configuration.Cache.ObserveLoadingTargets(targets)) {
				return;
			}

			const auto info = configuration.Cache.Info(*configuration.Reference);
			configuration.ReadFinished = true;
			if (info.Completion != resource::ResourceCompletion::Succeeded || info.UnsupportedProduction) {
				configuration.Information.Error = info.Error;
				configuration.Information.Diagnostic = SoundConfigurationDiagnostic::FailedDelivery;
				configuration.DeliveryRejected = true;
			} else {
				configuration.Delivered = &configuration.Cache.PsbFile(*configuration.Reference);
			}

			task.SetStatus(scene::TaskStatus::Retire);
		}

		void Complete(Configuration& configuration, scene::SerialTask& task) {
			if (!configuration.ReadFinished) {
				throw std::runtime_error("sound completion callback requires its preceding actual load task");
			}

			if (configuration.Delivered) {
				// 0x14039132e sets +58 before inspecting id/spec/version. A diagnostic cannot undo that latch.
				configuration.Information.CompletionLatched = true;
				configuration.Information.Diagnostic = InspectSoundConfigurationDocument(configuration.Delivered->Document(), Spec);
			}

			task.SetStatus(scene::TaskStatus::Retire);
		}

		bool LoadPrepared(std::string filename, io::PreparedResourceFileRead source) {
			RequireOpen();
			RequireFilename(filename);
			if (Live.contains(filename)) {
				return false;
			}
			if (Live.size() + Retiring.size() == Limits.MaxConfigurations || Serial.Count() > (Limits.MaxConfigurations + Limits.MaxArchives) * 2 - 2 || source.Format.Kind != io::ResourceFileKind::Psb) {
				throw std::runtime_error("sound configuration requires available concrete owner/task capacity and PSB delivery");
			}

			const auto& services = ActualServices();
			auto& cache = services.Cache;
			auto& processor = services.Processor;
			const auto existing = cache.FindNamedResource(filename);
			if (existing && (cache.ReadFormat(*existing) != source.Format || cache.Info(*existing).References >= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))) {
				throw std::runtime_error("sound configuration coalescing requires the actual immutable PSB mode and reference domain");
			}
			const auto read_filename = source.Format.Wrapped ? filename + ".m" : filename;
			if (!existing && source.OriginalFilename != read_filename) {
				throw std::runtime_error("sound prepared mapping must retain its exact original read identity");
			}

			const auto inserted = Live.emplace(filename, std::make_shared<Configuration>(cache, filename)).first;
			auto* borrowed = inserted->second.get();
			std::unique_ptr<scene::SerialTask> load;
			std::unique_ptr<scene::SerialTask> completion;
			try {
				scene::SerialTaskCallbacks load_callbacks;
				load_callbacks.Update = [borrowed](scene::SerialTask& task) {
					Poll(*borrowed, task);
				};
				load_callbacks.Release = [borrowed]() {
					borrowed->LoadBorrow = false;
				};
				load = std::make_unique<scene::SerialTask>(std::move(load_callbacks));
				scene::SerialTaskCallbacks completion_callbacks;
				completion_callbacks.Update = [this, borrowed](scene::SerialTask& task) {
					Complete(*borrowed, task);
				};
				completion_callbacks.Release = [borrowed]() {
					borrowed->CompletionBorrow = false;
				};
				completion = std::make_unique<scene::SerialTask>(std::move(completion_callbacks));
				borrowed->Reference = cache.Acquire(filename);
				if (!existing) {
					try {
						static_cast<void>(processor.QueuePreparedResource(*borrowed->Reference, read_filename, std::move(source)));
					} catch (...) {
						cache.DiscardUnstartedRead(*borrowed->Reference);
						borrowed->Reference.reset();
						throw;
					}
				}
			} catch (...) {
				// Destroy the unlinked callback owners before the configuration they capture.
				completion.reset();
				load.reset();
				Live.erase(inserted);
				throw;
			}

			// Both callbacks and map storage are allocated before publication; these checked appends allocate nothing.
			borrowed->LoadTask = Serial.Append(std::move(load));
			borrowed->LoadBorrow = true;
			static_cast<void>(Serial.Append(std::move(completion)));
			borrowed->CompletionBorrow = true;

			return true;
		}

		Configuration* SelectConfiguration(std::string_view archive_name) const {
			for (const auto& [filename, configuration] : Live) {
				if (!configuration->Information.CompletionLatched || configuration->Unloading || !configuration->Delivered) {
					continue;
				}

				if (configuration->Archives.contains(archive_name) || resource::ContainsSoundConfigurationArchive(configuration->Delivered->Document(), archive_name)) {
					return configuration.get();
				}
			}

			return nullptr;
		}

		const Archive& FindArchive(std::string_view archive_name) const {
			const auto* configuration = SelectConfiguration(archive_name);
			if (configuration) {
				const auto found = configuration->Archives.find(archive_name);
				if (found != configuration->Archives.end()) {
					return *found->second;
				}
			}

			throw std::runtime_error("sound archive has no loaded configuration/active owner: " + std::string(archive_name));
		}

		static void PollArchive(Archive& archive, scene::SerialTask& task) {
			const std::array targets{*archive.Reference};
			if (archive.Cache.ObserveLoadingTargets(targets)) {
				return;
			}

			const auto info = archive.Cache.Info(*archive.Reference);
			archive.ReadFinished = true;
			// Native393cb0 reports1 because the receiving-cell address exists, even when no PSB was delivered.
			archive.Information.CacheCompletion = SoundCacheLoadCompletion::Delivered;
			if (info.Completion != resource::ResourceCompletion::Succeeded || info.UnsupportedProduction) {
				archive.Information.Error = info.Error;
				archive.Information.Diagnostic = SoundArchiveLoadDiagnostic::FailedDelivery;
			} else {
				archive.Delivered = &archive.Cache.PsbFile(*archive.Reference);
			}

			task.SetStatus(scene::TaskStatus::Retire);
		}

		void CompleteArchive(Archive& archive, scene::SerialTask& task) {
			if (!archive.ReadFinished) {
				throw std::runtime_error("sound archive completion requires its preceding actual load task");
			}

			// 394c30 sets archive+30 before cancellation/null-delivery and metadata branches.
			archive.Information.CompletionLatched = true;
			if (archive.Information.CacheCompletion != SoundCacheLoadCompletion::Cancelled && archive.Delivered) {
				archive.Information.IdentityDiagnostic = InspectSoundDocumentIdentity(archive.Delivered->Document(), Spec, "sound_archive");
				try {
					archive.Definitions = resource::ReadSoundArchiveDefinitions(archive.Delivered->Document(), Limits.Definitions);
					archive.Information.Diagnostic = SoundArchiveLoadDiagnostic::DefinitionsReady;
				} catch (const std::runtime_error& error) {
					archive.Information.Error = "sound archive " + archive.Information.Filename + ": " + error.what();
					archive.Information.Diagnostic = SoundArchiveLoadDiagnostic::InvalidDefinitions;
				}
			}

			task.SetStatus(scene::TaskStatus::Retire);
		}

		bool LoadArchivePrepared(std::string archive_name, io::PreparedResourceFileRead source) {
			RequireOpen();
			if (archive_name.size() > Limits.MaxNameBytes || archive_name.find('\0') != std::string::npos) {
				throw std::runtime_error("sound archive name exceeds its exact bounded identity");
			}

			auto* configuration = SelectConfiguration(archive_name);
			if (!configuration || configuration->Archives.contains(archive_name)) {
				return false;
			}
			if (ArchiveOwners == Limits.MaxArchives || Serial.Count() > (Limits.MaxConfigurations + Limits.MaxArchives) * 2 - 2 || source.Format.Kind != io::ResourceFileKind::Psb) {
				throw std::runtime_error("sound archive load requires actual owner/task capacity and PSB delivery");
			}

			const auto filename = resource::BuildSoundArchiveFilename(configuration->Information.DirectoryPrefix, archive_name, Limits.MaxNameBytes);
			const auto& services = ActualServices();
			auto& cache = services.Cache;
			auto& processor = services.Processor;
			const auto existing = cache.FindNamedResource(filename);
			if (existing && (cache.ReadFormat(*existing) != source.Format || cache.Info(*existing).References >= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))) {
				throw std::runtime_error("sound archive coalescing requires its actual immutable PSB mode and reference domain");
			}
			const auto read_filename = source.Format.Wrapped ? filename + ".m" : filename;
			if (!existing && source.OriginalFilename != read_filename) {
				throw std::runtime_error("sound archive prepared mapping must retain its derived exact read identity");
			}

			configuration->RetiringArchives.reserve(configuration->Archives.size() + configuration->RetiringArchives.size() + 1);
			const auto inserted = configuration->Archives.emplace(archive_name, std::make_shared<Archive>(cache, ArchiveOwners, configuration->Filename, filename)).first;
			auto* borrowed = inserted->second.get();
			std::unique_ptr<scene::SerialTask> load;
			std::unique_ptr<scene::SerialTask> completion;
			try {
				scene::SerialTaskCallbacks load_callbacks;
				load_callbacks.Update = [borrowed](scene::SerialTask& task) {
					PollArchive(*borrowed, task);
				};
				load_callbacks.Release = [borrowed]() {
					borrowed->LoadBorrow = false;
				};
				load = std::make_unique<scene::SerialTask>(std::move(load_callbacks));
				scene::SerialTaskCallbacks completion_callbacks;
				completion_callbacks.Update = [this, borrowed](scene::SerialTask& task) {
					CompleteArchive(*borrowed, task);
				};
				completion_callbacks.Release = [borrowed]() {
					borrowed->CompletionBorrow = false;
				};
				completion = std::make_unique<scene::SerialTask>(std::move(completion_callbacks));
				borrowed->Reference = cache.Acquire(filename);
				if (!existing) {
					try {
						static_cast<void>(processor.QueuePreparedResource(*borrowed->Reference, read_filename, std::move(source)));
					} catch (...) {
						cache.DiscardUnstartedRead(*borrowed->Reference);
						borrowed->Reference.reset();
						throw;
					}
				}
			} catch (...) {
				completion.reset();
				load.reset();
				configuration->Archives.erase(inserted);
				throw;
			}

			borrowed->LoadTask = Serial.Append(std::move(load));
			borrowed->LoadBorrow = true;
			static_cast<void>(Serial.Append(std::move(completion)));
			borrowed->CompletionBorrow = true;

			return true;
		}

		void BeginArchiveRetirement(Archive& archive) {
			archive.Unloading = true;
			if (!archive.Information.CompletionLatched && !archive.ReadFinished) {
				archive.Cache.Release(*archive.Reference);
				archive.Reference.reset();
				archive.ReadFinished = true;
				archive.Information.CacheCompletion = SoundCacheLoadCompletion::Cancelled;
				archive.Information.Diagnostic = SoundArchiveLoadDiagnostic::CancelledDelivery;
				Serial.SetStatus(archive.LoadTask, scene::TaskStatus::Retire);
			}
		}

		void RetireConfigurationArchives(Configuration& configuration) {
			for (auto& [name, archive] : configuration.Archives) {
				BeginArchiveRetirement(*archive);
				if (!archive->CanRetire() || archive.use_count() != 1) {
					configuration.RetiringArchives.push_back(std::move(archive));
				}
			}
			configuration.Archives.clear();
		}

		static void PollRetiringArchives(Configuration& configuration) {
			std::erase_if(configuration.RetiringArchives, [](const auto& archive) {
				return archive->CanRetire() && archive.use_count() == 1;
			});
		}

		void RetireReady() {
			for (auto& [filename, configuration] : Live) {
				PollRetiringArchives(*configuration);
			}
			for (auto& configuration : Retiring) {
				PollRetiringArchives(*configuration);
			}

			std::erase_if(Retiring, [](const auto& configuration) {
				return configuration->CanRetire();
			});
		}
	};

	class SoundArchiveCueResource::Implementation {
	public:
		Implementation(SoundConfigurationLoads::Implementation& owner, std::shared_ptr<SoundConfigurationLoads::Implementation::Configuration> configuration, std::shared_ptr<SoundConfigurationLoads::Implementation::Archive> archive, std::string archive_name, std::string cue_name, const resource::SoundArchiveVoiceDefinition& definition) : Owner(owner), Configuration(std::move(configuration)), Archive(std::move(archive)), ArchiveName(std::move(archive_name)), CueName(std::move(cue_name)), Definition(definition) {
		}

		SoundConfigurationLoads::Implementation& Owner;
		std::shared_ptr<SoundConfigurationLoads::Implementation::Configuration> Configuration;
		std::shared_ptr<SoundConfigurationLoads::Implementation::Archive> Archive;
		std::string ArchiveName;
		std::string CueName;
		const resource::SoundArchiveVoiceDefinition& Definition;
	};

	SoundArchiveCueResource::SoundArchiveCueResource(std::unique_ptr<Implementation> implementation) : m_Implementation(std::move(implementation)) {
		if (!m_Implementation) {
			throw std::runtime_error("sound cue resource borrow requires actual configuration/archive owners");
		}
	}

	SoundArchiveCueResource::~SoundArchiveCueResource() = default;

	const io::PublishedPsbFile& SoundArchiveCueResource::File() const {
		m_Implementation->Owner.RequireOpen();

		return *m_Implementation->Archive->Delivered;
	}

	const resource::SoundArchiveVoiceDefinition& SoundArchiveCueResource::Definition() const {
		m_Implementation->Owner.RequireOpen();

		return m_Implementation->Definition;
	}

	std::string_view SoundArchiveCueResource::ConfigurationFilename() const {
		m_Implementation->Owner.RequireOpen();

		return m_Implementation->Configuration->Filename;
	}

	std::string_view SoundArchiveCueResource::ArchiveName() const {
		m_Implementation->Owner.RequireOpen();

		return m_Implementation->ArchiveName;
	}

	std::string_view SoundArchiveCueResource::CueName() const {
		m_Implementation->Owner.RequireOpen();

		return m_Implementation->CueName;
	}

	bool SoundArchiveCueResource::IsUnloading() const {
		m_Implementation->Owner.RequireOpen();

		return m_Implementation->Configuration->Unloading || m_Implementation->Archive->Unloading;
	}

	SoundConfigurationLoads::SoundConfigurationLoads(resource::ResourceCache& actual_cache, resource::ArchiveMounts& actual_mounts, io::FileReadProcessor& actual_processor, std::string actual_spec, SoundConfigurationLoadLimits limits) : m_Implementation(std::make_unique<Implementation>(actual_cache, actual_mounts, actual_processor, std::move(actual_spec), limits)) {
	}

	SoundConfigurationLoads::SoundConfigurationLoads(SoundReadServicesProvider actual_services, std::string actual_spec, SoundConfigurationLoadLimits limits) : m_Implementation(std::make_unique<Implementation>(std::move(actual_services), std::move(actual_spec), limits)) {
	}

	SoundConfigurationLoads::~SoundConfigurationLoads() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	bool SoundConfigurationLoads::LoadPrepared(std::string filename, io::PreparedResourceFileRead source) {
		return m_Implementation->LoadPrepared(std::move(filename), std::move(source));
	}

	bool SoundConfigurationLoads::Unload(std::string_view filename) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto found = owner.Live.find(filename);
		if (found == owner.Live.end()) {
			return false;
		}

		found->second->Unloading = true;
		owner.RetireConfigurationArchives(*found->second);
		owner.Retiring.push_back(std::move(found->second));
		owner.Live.erase(found);

		return true;
	}

	bool SoundConfigurationLoads::CancelPendingCacheLoad(std::string_view filename) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto found = owner.Live.find(filename);
		if (found == owner.Live.end() || found->second->ReadFinished) {
			return false;
		}

		auto& configuration = *found->second;
		configuration.Cache.Release(*configuration.Reference);
		configuration.Reference.reset();
		configuration.ReadFinished = true;
		configuration.DeliveryRejected = true;
		configuration.Information.Diagnostic = SoundConfigurationDiagnostic::CancelledDelivery;
		owner.Serial.SetStatus(configuration.LoadTask, scene::TaskStatus::Retire);

		return true;
	}

	bool SoundConfigurationLoads::GetLoaded(std::string_view filename) const {
		const auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto found = owner.Live.find(filename);

		return found != owner.Live.end() && found->second->Information.CompletionLatched;
	}

	bool SoundConfigurationLoads::GetLoading(std::string_view filename) const {
		const auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto found = owner.Live.find(filename);

		return found != owner.Live.end() && !found->second->Information.CompletionLatched;
	}

	SoundConfigurationLoadInfo SoundConfigurationLoads::Info(std::string_view filename) const {
		m_Implementation->RequireOpen();

		return m_Implementation->Find(filename).Information;
	}

	const io::PublishedPsbFile& SoundConfigurationLoads::File(std::string_view filename) const {
		m_Implementation->RequireOpen();
		const auto& configuration = m_Implementation->Find(filename);
		if (!configuration.Delivered) {
			throw std::runtime_error("sound configuration has no actual delivered PSB owner");
		}

		return *configuration.Delivered;
	}

	bool SoundConfigurationLoads::LoadArchivePrepared(std::string archive_name, io::PreparedResourceFileRead source) {
		return m_Implementation->LoadArchivePrepared(std::move(archive_name), std::move(source));
	}

	std::optional<std::string> SoundConfigurationLoads::PrepareArchiveFilename(std::string_view archive_name) const {
		const auto& owner = *m_Implementation;
		owner.RequireOpen();
		if (archive_name.size() > owner.Limits.MaxNameBytes || archive_name.find('\0') != std::string_view::npos) {
			throw std::runtime_error("sound archive name exceeds its exact bounded identity");
		}

		const auto* configuration = owner.SelectConfiguration(archive_name);
		if (!configuration) {
			return std::nullopt;
		}

		return resource::BuildSoundArchiveFilename(configuration->Information.DirectoryPrefix, archive_name, owner.Limits.MaxNameBytes);
	}

	bool SoundConfigurationLoads::UnloadArchive(std::string_view archive_name) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		auto* configuration = owner.SelectConfiguration(archive_name);
		if (!configuration) {
			return false;
		}

		const auto found = configuration->Archives.find(archive_name);
		if (found == configuration->Archives.end()) {
			return false;
		}

		owner.BeginArchiveRetirement(*found->second);
		if (!found->second->CanRetire() || found->second.use_count() != 1) {
			configuration->RetiringArchives.push_back(std::move(found->second));
		}
		configuration->Archives.erase(found);

		return true;
	}

	bool SoundConfigurationLoads::GetArchiveLoaded(std::string_view archive_name) const {
		const auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto* configuration = owner.SelectConfiguration(archive_name);
		if (!configuration) {
			return false;
		}

		const auto found = configuration->Archives.find(archive_name);

		return found != configuration->Archives.end() && found->second->Information.CompletionLatched;
	}

	bool SoundConfigurationLoads::GetArchiveLoading(std::string_view archive_name) const {
		const auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto* configuration = owner.SelectConfiguration(archive_name);
		if (!configuration) {
			return false;
		}

		const auto found = configuration->Archives.find(archive_name);

		return found != configuration->Archives.end() && !found->second->Information.CompletionLatched;
	}

	SoundArchiveLoadInfo SoundConfigurationLoads::ArchiveInfo(std::string_view archive_name) const {
		m_Implementation->RequireOpen();

		return m_Implementation->FindArchive(archive_name).Information;
	}

	const io::PublishedPsbFile& SoundConfigurationLoads::ArchiveFile(std::string_view archive_name) const {
		m_Implementation->RequireOpen();
		const auto& archive = m_Implementation->FindArchive(archive_name);
		if (!archive.Delivered) {
			throw std::runtime_error("sound archive has no actual delivered PSB owner");
		}

		return *archive.Delivered;
	}

	const resource::SoundArchiveDefinitions& SoundConfigurationLoads::ArchiveDefinitions(std::string_view archive_name) const {
		m_Implementation->RequireOpen();
		const auto& archive = m_Implementation->FindArchive(archive_name);
		if (archive.Information.Diagnostic != SoundArchiveLoadDiagnostic::DefinitionsReady) {
			throw std::runtime_error("sound archive definition production failed or has not finished: " + archive.Information.Filename + ": " + archive.Information.Error);
		}

		return archive.Definitions;
	}

	std::unique_ptr<SoundArchiveCueResource> SoundConfigurationLoads::BorrowCueResource(std::string_view archive_name, std::string_view cue_name) {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		if (archive_name.size() > owner.Limits.MaxNameBytes || cue_name.empty() || cue_name.size() > owner.Limits.MaxNameBytes || archive_name.find('\0') != std::string_view::npos || cue_name.find('\0') != std::string_view::npos) {
			throw std::runtime_error("sound cue borrow requires exact bounded archive/cue lookup names");
		}

		for (const auto& [filename, configuration] : owner.Live) {
			if (!configuration->Information.CompletionLatched || configuration->Unloading) {
				continue;
			}

			for (const auto& [name, archive] : configuration->Archives) {
				if ((!archive_name.empty() && archive_name != name) || !archive->Information.CompletionLatched || !archive->Delivered || archive->Unloading) {
					continue;
				}

				const auto found = archive->Definitions.find(cue_name);
				if (found != archive->Definitions.end()) {
					auto implementation = std::make_unique<SoundArchiveCueResource::Implementation>(owner, configuration, archive, name, found->first, found->second);

					return std::make_unique<SoundArchiveCueResource>(std::move(implementation));
				}
			}
		}

		return nullptr;
	}

	bool SoundConfigurationLoads::SelectsCueArchive(const SoundArchiveCueResource& actual_borrow) const {
		const auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto& borrowed = *actual_borrow.m_Implementation;
		if (&borrowed.Owner != &owner) {
			throw std::runtime_error("sound cue selection requires this actual enclosing configuration owner");
		}

		const auto* selected = owner.SelectConfiguration(borrowed.ArchiveName);
		if (selected != borrowed.Configuration.get()) {
			return false;
		}
		const auto found = selected->Archives.find(borrowed.ArchiveName);

		return found != selected->Archives.end() && found->second.get() == borrowed.Archive.get();
	}

	void SoundConfigurationLoads::AdvanceSerialLoads() {
		AdvanceSerialTasks();
		RetireReadyResources();
	}

	void SoundConfigurationLoads::AdvanceSerialTasks() {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		owner.Calling = true;
		try {
			if (owner.Serial.Status() == scene::TaskStatus::Active) {
				owner.Serial.Update();
			}

		} catch (...) {
			owner.Calling = false;
			throw;
		}
		owner.Calling = false;
	}

	void SoundConfigurationLoads::RetireReadyResources() {
		m_Implementation->RequireOpen();
		m_Implementation->RetireReady();
	}

	const scene::SerialTaskSet& SoundConfigurationLoads::SerialLoads() const noexcept {
		return m_Implementation->Serial;
	}

	std::size_t SoundConfigurationLoads::LiveCount() const noexcept {
		return m_Implementation->Live.size();
	}

	std::size_t SoundConfigurationLoads::RetiringCount() const noexcept {
		return m_Implementation->Retiring.size();
	}

	std::size_t SoundConfigurationLoads::ArchiveOwnerCount() const noexcept {
		return m_Implementation->ArchiveOwners;
	}

	bool SoundConfigurationLoads::CanClose() const noexcept {
		const auto& owner = *m_Implementation;
		const auto unborrowed = [](const auto& configuration) {
			return configuration.use_count() == 1 && std::all_of(configuration->Archives.begin(), configuration->Archives.end(), [](const auto& entry) {
				return entry.second.use_count() == 1;
			}) &&
			    std::all_of(configuration->RetiringArchives.begin(), configuration->RetiringArchives.end(), [](const auto& archive) {
				    return archive.use_count() == 1;
			    });
		};

		return std::this_thread::get_id() == owner.OwnerThread && !owner.Calling && !owner.ResolvingServices && owner.Serial.Count() == 0 && owner.Serial.CanClear() && std::all_of(owner.Live.begin(), owner.Live.end(), [&](const auto& entry) {
			return unborrowed(entry.second);
		}) &&
		    std::all_of(owner.Retiring.begin(), owner.Retiring.end(), unborrowed);
	}

	void SoundConfigurationLoads::Close() {
		if (!CanClose()) {
			throw std::runtime_error("sound configuration retirement requires every actual serial/cue resource borrower to finish first");
		}

		auto& owner = *m_Implementation;
		owner.Serial.Clear();
		for (auto& [filename, configuration] : owner.Live) {
			owner.RetireConfigurationArchives(*configuration);
		}
		for (auto& configuration : owner.Retiring) {
			owner.RetireConfigurationArchives(*configuration);
			Implementation::PollRetiringArchives(*configuration);
		}
		owner.Live.clear();
		owner.Retiring.clear();
		owner.Closed = true;
	}

} // namespace sgre::audio
