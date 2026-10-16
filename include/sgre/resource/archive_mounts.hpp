#pragma once

#include "sgre/io/wrapped_psb_destination.hpp"
#include "sgre/resource/archive_index.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::io {
	class FileReadProcessor;
}

namespace sgre::resource {
	struct ArchiveMountLimits {
		std::size_t MaxPreparedArchives = 64;
		std::size_t MaxMounts = 128;
		std::size_t MaxPendingMounts = 128;
		std::size_t MaxNameBytes = 4096;
		std::size_t MaxTotalEntries = 262144;
		std::size_t MaxRetainedPsbBytes = 64 * 1024 * 1024;
	};

	struct ArchiveReadMountInput {
		std::string Prefix;
		std::string IndexIdentity;
		std::string BodyIdentity;
		// Body extent was prepared from the actual file outside ordinary frames; index input is a real region.
		std::uint64_t BodyBytes = 0;
		io::PreparedWrappedPsbRead IndexRead;
		// Explicit prepared physical mapping; no loose-file/root-directory inference belongs to mounted lookup.
		std::filesystem::path BodyFilename;
	};

	struct ResolvedArchiveEntry {
		std::string IndexIdentity;
		std::string BodyIdentity;
		std::string ResourceFilename;
		ArchiveEntry Entry;
		std::filesystem::path BodyFilename;
		std::uint64_t BodyBytes = 0;
	};

	[[nodiscard]] std::string NormalizeResourcePath(std::string_view path);

	class ArchiveMounts {
	public:
		explicit ArchiveMounts(ArchiveMountLimits limits = {});
		~ArchiveMounts();
		ArchiveMounts(const ArchiveMounts&) = delete;
		ArchiveMounts& operator=(const ArchiveMounts&) = delete;
		ArchiveMounts(ArchiveMounts&&) = delete;
		ArchiveMounts& operator=(ArchiveMounts&&) = delete;

		// Host workers prepare validated indices before queueing mounts; no filesystem work occurs here.
		void Prepare(std::string index_identity, std::string body_identity, ArchiveIndex index);
		void QueueMount(std::string prefix, std::string_view index_identity, std::string_view body_identity, bool alternate_read_mode = false);
		void Advance();
		// Genuine receiving owner: native global pending records remain distinct from a claimed in-flight batch.
		void QueueReadMount(ArchiveReadMountInput input);
		[[nodiscard]] std::size_t QueuedReadMountCount() const;
		[[nodiscard]] bool HasInFlightReadMount() const;
		[[nodiscard]] std::size_t RetainedPsbObjectCount() const;
		// Borrows this actual receiving owner; neither unmount nor temporary reader retirement releases it.
		[[nodiscard]] const io::PublishedPsbFile& RetainedPsbObject(std::size_t index) const;
		void Unmount(std::string_view prefix);
		[[nodiscard]] std::optional<ResolvedArchiveEntry> Resolve(std::string_view resource_filename) const;
		[[nodiscard]] std::size_t PendingMountCount() const;
		[[nodiscard]] std::size_t MountCount() const;

	private:
		friend class sgre::io::FileReadProcessor;
		struct ReadMountRow {
			ArchiveReadMountInput Input;
			io::WrappedPsbReadDestination Destination;
			std::optional<ArchiveIndex> PreparedIndex;
			std::size_t ProducedBytes = 0;
			std::map<std::string, std::size_t, std::less<>> Lookup;
		};
		struct ReadBatch {
			std::vector<std::unique_ptr<ReadMountRow>> Rows;
			bool Published = false;
			std::size_t AvailablePsbBytes = 0;
			std::size_t AvailableEntries = 0;
			std::size_t MaxNameBytes = 0;
		};
		void BindReadProcessor();
		void UnbindReadProcessor();
		[[nodiscard]] std::unique_ptr<ReadBatch> ClaimReadBatch();
		void PublishReadBatch(ReadBatch& batch);
		void RestoreUnstartedReadBatch(ReadBatch& batch);
		// Authored safe teardown retains every produced object even when its mount was not published.
		void RetireReadBatch(ReadBatch& batch);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::resource
