#include "sgre/io/mounted_resource_read.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace sgre::io {
	static void ValidateMountedReadInputs(std::string_view name, const MountedResourceReadLimits& limits) {
		if (limits.MaxNameBytes == 0 || limits.MaxNameBytes > 65536 || limits.MaxPackedBytes == 0 || limits.MaxPackedBytes > 160 * 1024 * 1024 || limits.MaxDecodedBytes == 0 || limits.MaxDecodedBytes > 160 * 1024 * 1024 || limits.MaxMetadataBytes == 0 || limits.MaxMetadataBytes > 64 * 1024 * 1024 || limits.MaxMetadataBytes > limits.MaxDecodedBytes || limits.ShellKeyPeriod == 0 || limits.ShellKeyPeriod > 65536) {
			throw std::runtime_error("mounted resource read requires checked identity/decoder bounds");
		}
		if (name.empty() || name.size() > limits.MaxNameBytes + 2 || name.find('\0') != std::string_view::npos) {
			throw std::runtime_error("mounted request identity exceeds its checked original-name domain");
		}
	}

	PreparedResourceFileRead PrepareQueuedMountedResourceRead(const resource::ArchiveMounts& actual_mounts, std::string_view actual_read_filename, ResourceReadFormat format, std::string_view actual_seed, MountedResourceReadLimits limits) {
		ValidateMountedReadInputs(actual_read_filename, limits);
		PreparedResourceFileRead source;
		source.MountedSource = &actual_mounts;
		source.Region.MaxBytes = limits.MaxPackedBytes;
		source.Format = format;
		source.OriginalFilename = std::string(actual_read_filename);
		source.Seed = std::string(actual_seed);
		source.ShellKeyPeriod = limits.ShellKeyPeriod;
		source.MaxDecodedBytes = limits.MaxDecodedBytes;
		source.MaxMetadataBytes = limits.MaxMetadataBytes;

		return source;
	}

	PreparedRawFileRead ResolveMountedResourceReadRegion(const resource::ArchiveMounts& actual_mounts, std::string_view actual_read_filename, std::size_t max_packed_bytes) {
		if (max_packed_bytes == 0 || max_packed_bytes > 160 * 1024 * 1024) {
			throw std::runtime_error("mounted resource physical preparation requires its actual bounded allocation");
		}

		const auto resolved = actual_mounts.Resolve(actual_read_filename);
		if (!resolved) {
			throw std::runtime_error("mounted resource was not found; native loose-file/path fallback remains unrecovered: " + std::string(actual_read_filename));
		}
		if (resolved->BodyFilename.empty() || resolved->BodyFilename.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos || resolved->Entry.Size == 0 || resolved->Entry.Size > max_packed_bytes || resolved->Entry.Size >= std::numeric_limits<std::uint32_t>::max() || resolved->Entry.Offset > resolved->BodyBytes || resolved->Entry.Size > resolved->BodyBytes - resolved->Entry.Offset) {
			throw std::runtime_error("mounted request requires its genuine prepared physical body mapping and nonempty bounded entry");
		}

		PreparedRawFileRead region;
		region.Filename = resolved->BodyFilename;
		region.Offset = resolved->Entry.Offset;
		region.Length = static_cast<std::uint32_t>(resolved->Entry.Size);
		region.MaxBytes = max_packed_bytes;

		return region;
	}

	PreparedResourceFileRead PrepareMountedResourceRead(const resource::ArchiveMounts& actual_mounts, std::string_view actual_read_filename, ResourceReadFormat format, std::string_view actual_seed, MountedResourceReadLimits limits) {
		auto source = PrepareQueuedMountedResourceRead(actual_mounts, actual_read_filename, format, actual_seed, limits);
		source.Region = ResolveMountedResourceReadRegion(actual_mounts, actual_read_filename, limits.MaxPackedBytes);
		source.MountedSource = nullptr;

		return source;
	}
} // namespace sgre::io
