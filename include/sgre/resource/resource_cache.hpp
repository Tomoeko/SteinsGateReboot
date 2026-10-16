#pragma once

#include "sgre/io/raw_read_destination.hpp"
#include "sgre/io/resource_file_read.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::io {
	class PendingRawReadRequests;
}

namespace sgre::resource {
	struct CachedResourceHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const CachedResourceHandle&) const = default;
	};

	enum class ResourceCompletion {
		Pending,
		Succeeded,
		Failed
	};

	enum class ResourceResidency {
		Loading,
		Active,
		Cached
	};

	struct ResourceCacheLimits {
		std::size_t MaxEntries = 65536;
		std::size_t MaxNameBytes = 4096;
		std::size_t MaxEntryBytes = 160 * 1024 * 1024;
		std::size_t MaxOwnedBytes = 1024 * 1024 * 1024;
	};

	struct ResourceCacheState {
		std::uint32_t CacheLimitSize = 0;
		std::uint32_t TotalActiveSize = 0;
		std::uint32_t TotalCacheSize = 0;
		std::uint32_t LoadingEntryNum = 0;
		std::uint32_t ActiveEntryNum = 0;
		std::uint32_t CacheEntryNum = 0;
	};

	struct CachedResourceInfo {
		ResourceResidency Residency = ResourceResidency::Loading;
		ResourceCompletion Completion = ResourceCompletion::Pending;
		std::uint32_t References = 0;
		std::uint32_t ChargedBytes = 0;
		bool Discardable = true;
		std::string Error;
		bool UnsupportedProduction = false;
	};

	struct CachedResourceReceivingObservation {
		ResourceResidency Residency = ResourceResidency::Loading;
		ResourceCompletion Completion = ResourceCompletion::Pending;
		std::uint32_t References = 0;
		std::uint32_t ChargedBytes = 0;
		std::optional<io::ResourceReadFormat> Format;
		std::optional<io::ResourceReceivingObservation> Receiving;
		bool HasFontDocument = false;
	};

	class ResourceCache {
	public:
		// The host supplies an explicit limit; native ResourceManager starts at zero before actual startup settings.
		explicit ResourceCache(std::uint32_t initial_limit_bytes, ResourceCacheLimits limits = {});
		~ResourceCache();
		ResourceCache(const ResourceCache&) = delete;
		ResourceCache& operator=(const ResourceCache&) = delete;
		ResourceCache(ResourceCache&&) = delete;
		ResourceCache& operator=(ResourceCache&&) = delete;

		[[nodiscard]] CachedResourceHandle Acquire(std::string_view name);
		[[nodiscard]] CachedResourceHandle Prefetch(std::string_view name);
		void Release(CachedResourceHandle resource);
		// Complete on the owning thread between frames, after workers finish I/O/decoding.
		// Native charge is explicit: PSB object charge and raw byte length have separate producers.
		void Complete(CachedResourceHandle resource, std::vector<std::uint8_t> bytes, std::uint32_t charged_bytes);
		void Fail(CachedResourceHandle resource, std::string error);
		// Actual Loading entry owns this cell; pending request/worker owners borrow it until completion or cancel3.
		[[nodiscard]] io::RawFileReadDestination& CreateRawReadDestination(CachedResourceHandle resource);
		[[nodiscard]] io::ResourceFileReadDestination& CreateResourceReadDestination(CachedResourceHandle resource, io::ResourceReadFormat format);
		[[nodiscard]] std::optional<io::ResourceReadFormat> ReadFormat(CachedResourceHandle resource) const;
		// Receiving owner only; copies existing scalar state without observing/promoting data or retaining a lease.
		[[nodiscard]] CachedResourceReceivingObservation ObserveReceiving(CachedResourceHandle resource) const;
		// Authored admission rollback: only a fresh acquired Loading slot with no published request borrow.
		void DiscardUnstartedRead(CachedResourceHandle resource);
		[[nodiscard]] bool CanRetireReadDestinations() const noexcept;
		void RequireCanRetireReadDestinations() const;
		void Advance();
		// Actual Resource.getLoading promotes each completed referenced target, trimming after each, until first pending.
		// Observes actual prepared completion only; no I/O, decoding, worker wait or new resource reference.
		[[nodiscard]] bool ObserveLoadingTargets(std::span<const CachedResourceHandle> targets);
		void ClearCache();
		void SetLimit(std::uint32_t bytes);
		[[nodiscard]] std::uint32_t Limit() const;
		[[nodiscard]] ResourceCacheState State() const;
		[[nodiscard]] CachedResourceInfo Info(CachedResourceHandle resource) const;
		[[nodiscard]] bool Contains(std::string_view name) const;
		[[nodiscard]] std::optional<CachedResourceHandle> FindNamedResource(std::string_view name) const;
		[[nodiscard]] bool Contains(CachedResourceHandle resource) const;
		[[nodiscard]] std::string_view Name(CachedResourceHandle resource) const;
		// Bytes borrow this owner and require an acquired active reference until their last use.
		[[nodiscard]] std::span<const std::uint8_t> Bytes(CachedResourceHandle resource) const;
		// A pin owns one active reference and may outlive this facade. Destroy pins on the owning thread.
		[[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>> PinBytes(CachedResourceHandle resource);
		// Native ScriptBuffer hit: exact Active-name lookup and one independent raw-data pin; never revive Cached.
		[[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>> TryPinActiveBinary(std::string_view name);
		// Const storage borrows require this actual acquired active owner until last use.
		[[nodiscard]] const std::vector<std::uint8_t>& ByteStorage(CachedResourceHandle resource) const;
		[[nodiscard]] const io::PublishedPsbFile& PsbFile(CachedResourceHandle resource) const;
		// One pin owns exactly one manager reference shared by both document and backing aliases.
		[[nodiscard]] std::shared_ptr<const io::PublishedPsbFile> PinPsbFile(CachedResourceHandle resource);
		[[nodiscard]] bool HasImageFile(CachedResourceHandle resource) const;
		[[nodiscard]] const scene::ImageReceivingDocument& ImageFile(CachedResourceHandle resource) const;
		[[nodiscard]] std::shared_ptr<const scene::ImageReceivingDocument> PinImageFile(CachedResourceHandle resource);
		[[nodiscard]] bool HasMotionFile(CachedResourceHandle resource) const;
		[[nodiscard]] const scene::MotionReceivingDocument& MotionFile(CachedResourceHandle resource) const;
		[[nodiscard]] std::shared_ptr<const scene::MotionReceivingDocument> PinMotionFile(CachedResourceHandle resource);
		[[nodiscard]] bool HasBitmapFontFile(CachedResourceHandle resource) const;
		[[nodiscard]] const scene::BitmapFontReceivingDocument& BitmapFontFile(CachedResourceHandle resource) const;
		// This typed pin owns one actual manager reference; it preserves preload texture ownership after pixel purge.
		[[nodiscard]] std::shared_ptr<const scene::BitmapFontReceivingDocument> PinBitmapFontFile(CachedResourceHandle resource);

	private:
		friend class sgre::io::PendingRawReadRequests;
		void AttachRawReadQueue(io::PendingRawReadRequests& queue);
		void DetachRawReadQueue(io::PendingRawReadRequests& queue);
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::resource
