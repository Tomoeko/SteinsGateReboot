#pragma once

#include "sgre/io/file_read_processor.hpp"
#include "sgre/resource/sound_definitions.hpp"
#include "sgre/scene/serial_task_set.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sgre::audio {

	struct SoundReadServices {
		resource::ResourceCache& Cache;
		resource::ArchiveMounts& Mounts;
		io::FileReadProcessor& Processor;
	};

	using SoundReadServicesProvider = std::function<SoundReadServices()>;

	class SoundConfigurationLoads;

	// A real cue borrower retains the exact configuration/archive/cache owners, without another cache reference.
	// The enclosing loads/cache/reader and serialized owner thread outlive this resource borrow.
	class SoundArchiveCueResource {
		class Implementation;

	public:
		explicit SoundArchiveCueResource(std::unique_ptr<Implementation> implementation);
		~SoundArchiveCueResource();
		SoundArchiveCueResource(const SoundArchiveCueResource&) = delete;
		SoundArchiveCueResource& operator=(const SoundArchiveCueResource&) = delete;
		SoundArchiveCueResource(SoundArchiveCueResource&&) = delete;
		SoundArchiveCueResource& operator=(SoundArchiveCueResource&&) = delete;
		[[nodiscard]] const io::PublishedPsbFile& File() const;
		[[nodiscard]] const resource::SoundArchiveVoiceDefinition& Definition() const;
		[[nodiscard]] std::string_view ConfigurationFilename() const;
		[[nodiscard]] std::string_view ArchiveName() const;
		[[nodiscard]] std::string_view CueName() const;
		[[nodiscard]] bool IsUnloading() const;

	private:
		friend class SoundConfigurationLoads;
		std::unique_ptr<Implementation> m_Implementation;
	};

	enum class SoundConfigurationDiagnostic {
		Pending,
		Compatible,
		IgnoredIdentity,
		WrongSpec,
		OldVersion,
		NewVersion,
		InvalidMetadata,
		FailedDelivery,
		CancelledDelivery
	};

	// The native completion callback diagnoses metadata after setting its separate completion latch.
	[[nodiscard]] SoundConfigurationDiagnostic InspectSoundConfigurationDocument(const resource::PsbDocument& document, std::string_view actual_spec);

	struct SoundConfigurationLoadLimits {
		std::size_t MaxConfigurations = 1024;
		std::size_t MaxNameBytes = 4096;
		std::size_t MaxArchives = 4096;
		resource::SoundDefinitionLimits Definitions;
	};

	struct SoundConfigurationLoadInfo {
		std::string DirectoryPrefix;
		std::string Error;
		SoundConfigurationDiagnostic Diagnostic = SoundConfigurationDiagnostic::Pending;
		bool CompletionLatched = false;
	};

	enum class SoundArchiveLoadDiagnostic {
		Pending,
		DefinitionsReady,
		FailedDelivery,
		CancelledDelivery,
		InvalidDefinitions
	};

	enum class SoundCacheLoadCompletion : std::uint32_t {
		Pending = 0,
		Delivered = 1,
		Cancelled = 3
	};

	struct SoundArchiveLoadInfo {
		std::string ConfigurationFilename;
		std::string Filename;
		std::string Error;
		SoundConfigurationDiagnostic IdentityDiagnostic = SoundConfigurationDiagnostic::Pending;
		SoundArchiveLoadDiagnostic Diagnostic = SoundArchiveLoadDiagnostic::Pending;
		SoundCacheLoadCompletion CacheCompletion = SoundCacheLoadCompletion::Pending;
		bool CompletionLatched = false;
	};

	// Resource control lane of MSound, including archive definitions. No cue/source/audio/clock/focus producer.
	// The actual cache, mounts and processor enclose this owner. This is not the script-visible Sound service.
	class SoundConfigurationLoads {
	public:
		SoundConfigurationLoads(resource::ResourceCache& actual_cache, resource::ArchiveMounts& actual_mounts, io::FileReadProcessor& actual_processor, std::string actual_spec, SoundConfigurationLoadLimits limits = {});
		// Own real empty configuration/Serial groups before application cache setup. Resolve only an actual new load.
		// Provider cannot reenter/mutate this owner. Its one exact receiving bundle outlives all resource/cue borrowers.
		SoundConfigurationLoads(SoundReadServicesProvider actual_services, std::string actual_spec, SoundConfigurationLoadLimits limits = {});
		~SoundConfigurationLoads() noexcept;
		SoundConfigurationLoads(const SoundConfigurationLoads&) = delete;
		SoundConfigurationLoads& operator=(const SoundConfigurationLoads&) = delete;
		SoundConfigurationLoads(SoundConfigurationLoads&&) = delete;
		SoundConfigurationLoads& operator=(SoundConfigurationLoads&&) = delete;
		// Connected-cache route only. A real upstream locator supplies the prepared physical resource mapping.
		// An exact live filename match suppresses a second configuration and reference.
		[[nodiscard]] bool LoadPrepared(std::string filename, io::PreparedResourceFileRead source);
		// Native Sound.unload removes lookup but does not cancel this configuration's pending load.
		[[nodiscard]] bool Unload(std::string_view filename);
		// Native cache-task cancellation releases its one manager reference; it does not cancel selected I/O.
		// This task-control operation is not a new script-visible Sound method.
		[[nodiscard]] bool CancelPendingCacheLoad(std::string_view filename);
		[[nodiscard]] bool GetLoaded(std::string_view filename) const;
		[[nodiscard]] bool GetLoading(std::string_view filename) const;
		[[nodiscard]] SoundConfigurationLoadInfo Info(std::string_view filename) const;
		// Borrow requires this live configuration and its acquired cache reference until the last use.
		[[nodiscard]] const io::PublishedPsbFile& File(std::string_view filename) const;
		// Search actual loaded configuration declarations in filename order; retain the first matching owner.
		[[nodiscard]] bool LoadArchivePrepared(std::string archive_name, io::PreparedResourceFileRead source);
		// Observe the selected actual declaration's derived filename before upstream mounted-read preparation.
		[[nodiscard]] std::optional<std::string> PrepareArchiveFilename(std::string_view archive_name) const;
		// Pending archive unload cancels its cache task, unlike unloading the containing configuration's own load.
		[[nodiscard]] bool UnloadArchive(std::string_view archive_name);
		[[nodiscard]] bool GetArchiveLoaded(std::string_view archive_name) const;
		[[nodiscard]] bool GetArchiveLoading(std::string_view archive_name) const;
		[[nodiscard]] SoundArchiveLoadInfo ArchiveInfo(std::string_view archive_name) const;
		// Both getters borrow the actual archive owner. Their last use must precede archive/configuration unload.
		[[nodiscard]] const io::PublishedPsbFile& ArchiveFile(std::string_view archive_name) const;
		[[nodiscard]] const resource::SoundArchiveDefinitions& ArchiveDefinitions(std::string_view archive_name) const;
		// Native390a20/3947d0 search filename/archive order, optionally all archives, for an actual completed cue definition.
		// Holding this borrow prevents resource destruction after lookup removal until every actual cue/source use retires.
		[[nodiscard]] std::unique_ptr<SoundArchiveCueResource> BorrowCueResource(std::string_view archive_name, std::string_view cue_name);
		[[nodiscard]] bool SelectsCueArchive(const SoundArchiveCueResource& actual_borrow) const;
		// The actual enclosing MSound must update Parallel before calling this serial/retirement phase.
		void AdvanceSerialLoads();
		// Split phases let the genuine containing Sound owner retire cues before polling configuration/archive retirement.
		void AdvanceSerialTasks();
		void RetireReadyResources();
		[[nodiscard]] const scene::SerialTaskSet& SerialLoads() const noexcept;
		[[nodiscard]] std::size_t LiveCount() const noexcept;
		[[nodiscard]] std::size_t RetiringCount() const noexcept;
		[[nodiscard]] std::size_t ArchiveOwnerCount() const noexcept;
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		friend class SoundArchiveCueResource;
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::audio
