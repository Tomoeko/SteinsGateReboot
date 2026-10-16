#pragma once

#include "sgre/core/background_calls.hpp"
#include "sgre/io/pending_raw_reads.hpp"
#include "sgre/resource/archive_mounts.hpp"
#include "sgre/scene/common_process_tasks.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace sgre::io {

	// The default derived postprocessor returns before texture/purge work only in this observed archive-root domain.
	[[nodiscard]] resource::ArchiveIndex PrepareIndexOnlyMount(const PublishedPsbFile& file, std::uint64_t body_bytes);

	class FileReadProcessor {
	public:
		// Actual ResourceManager persistent reader common owner: +108=1, +173=0, ordinary lane2. Cache, archive receiving owner and background manager enclose Close.
		FileReadProcessor(resource::ResourceCache& cache, resource::ArchiveMounts& mounts, core::BackgroundCallManager& background, RawReadQueueLimits limits = {});
		~FileReadProcessor() noexcept;
		FileReadProcessor(const FileReadProcessor&) = delete;
		FileReadProcessor& operator=(const FileReadProcessor&) = delete;
		FileReadProcessor(FileReadProcessor&&) = delete;
		FileReadProcessor& operator=(FileReadProcessor&&) = delete;
		// Prepared raw type0/flag0 only; mounted path production and generic MTex work remain separate domains.
		[[nodiscard]] RawReadRequestHandle QueuePreparedRaw(resource::CachedResourceHandle resource, std::string request_filename, PreparedRawFileRead source);
		[[nodiscard]] RawReadRequestHandle QueuePreparedResource(resource::CachedResourceHandle resource, std::string request_filename, PreparedResourceFileRead source);
		[[nodiscard]] bool UsesReceivingOwners(const resource::ResourceCache& cache, const resource::ArchiveMounts& mounts) const noexcept;
		void QueueReadMount(resource::ArchiveReadMountInput input);
		void Update();
		void Draw(std::uint32_t argument);
		[[nodiscard]] const scene::CommonProcessTasks& Tasks() const noexcept;
		[[nodiscard]] std::size_t PendingRawCount() const;
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::io
