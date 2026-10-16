#pragma once

#include "sgre/io/resource_file_read.hpp"
#include "sgre/resource/archive_mounts.hpp"

#include <cstddef>
#include <string_view>

namespace sgre::io {
	struct MountedResourceReadLimits {
		std::size_t MaxNameBytes = 4096;
		std::size_t MaxPackedBytes = 160 * 1024 * 1024;
		std::size_t MaxDecodedBytes = 160 * 1024 * 1024;
		std::size_t MaxMetadataBytes = 64 * 1024 * 1024;
		std::size_t ShellKeyPeriod = 131;
	};

	// Native4d7ee0 admits logical identities;4d43d0/4d70f0 resolve their real mapping after selection.
	// The actual mount owner and its bound processor enclose the queued request until the existing worker fence.
	[[nodiscard]] PreparedResourceFileRead PrepareQueuedMountedResourceRead(const resource::ArchiveMounts& actual_mounts, std::string_view actual_read_filename, ResourceReadFormat format, std::string_view actual_seed, MountedResourceReadLimits limits = {});
	[[nodiscard]] PreparedRawFileRead ResolveMountedResourceReadRegion(const resource::ArchiveMounts& actual_mounts, std::string_view actual_read_filename, std::size_t max_packed_bytes);

	// Actual admitted mount/body mapping only; native loose-path fallback remains unrecovered.
	[[nodiscard]] PreparedResourceFileRead PrepareMountedResourceRead(const resource::ArchiveMounts& actual_mounts, std::string_view actual_read_filename, ResourceReadFormat format, std::string_view actual_seed, MountedResourceReadLimits limits = {});
} // namespace sgre::io
