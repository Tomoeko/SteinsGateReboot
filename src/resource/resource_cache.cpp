#include "sgre/resource/resource_cache.hpp"

#include "sgre/io/pending_raw_reads.hpp"
#include "sgre/scene/bitmap_font_receiving_document.hpp"
#include "sgre/scene/image_receiving_document.hpp"
#include "sgre/scene/motion_receiving_document.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::resource {
	static constexpr std::size_t kMaxResourceLoadingTargets = 65536;

	struct ResourceCacheEntry {
		std::string Name;
		CachedResourceInfo Info;
		std::vector<std::uint8_t> Bytes;
		std::unique_ptr<io::RawFileReadDestination> RawRead;
		std::unique_ptr<io::ResourceFileReadDestination> ResourceRead;
		std::unique_ptr<io::PublishedPsbFile> Psb;
		std::shared_ptr<const scene::BitmapFontReceivingDocument> Font;
		std::shared_ptr<const scene::MotionReceivingDocument> Motion;
		std::shared_ptr<const scene::ImageReceivingDocument> Image;
		std::optional<io::ResourceReadFormat> Format;
	};

	struct ResourceCacheSlot {
		std::uint64_t Generation = 1;
		std::optional<ResourceCacheEntry> Entry;
	};

	class ResourceCache::Implementation {
	public:
		Implementation(std::uint32_t initial_limit, ResourceCacheLimits limits)
		    : Limits(limits), CacheLimit(initial_limit) {
			if (limits.MaxEntries == 0 || limits.MaxEntries > 65536 || limits.MaxNameBytes == 0 || limits.MaxNameBytes > 65536 || limits.MaxEntryBytes == 0 || limits.MaxEntryBytes > limits.MaxOwnedBytes || limits.MaxOwnedBytes > std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("resource cache requires bounded positive allocation limits");
			}

			Slots.reserve(limits.MaxEntries);
			CacheOrder.reserve(limits.MaxEntries);
		}

		std::thread::id OwnerThread = std::this_thread::get_id();
		std::atomic<bool> HasRawReadDestinations = false;
		std::atomic<io::PendingRawReadRequests*> ReadQueue = nullptr;
		ResourceCacheLimits Limits;
		std::uint32_t CacheLimit = 0;
		std::size_t OwnedBytes = 0;
		std::size_t ChargedBytes = 0;
		std::vector<ResourceCacheSlot> Slots;
		std::map<std::string, std::uint32_t, std::less<>> Names;
		std::vector<std::uint32_t> CacheOrder;

		CachedResourceHandle Handle(std::uint32_t index) const {
			CachedResourceHandle handle;
			handle.Index = index;
			handle.Generation = Slots[index].Generation;
			handle.Owner = this;

			return handle;
		}

		bool Contains(CachedResourceHandle handle) const {
			return handle.Owner == this && handle.Index < Slots.size() && handle.Generation == Slots[handle.Index].Generation && Slots[handle.Index].Entry.has_value();
		}

		void ValidateHandle(CachedResourceHandle handle) const {
			if (!Contains(handle)) {
				throw std::runtime_error("resource cache handle is stale or belongs to another owner");
			}
		}

		ResourceCacheEntry& Entry(CachedResourceHandle handle) {
			ValidateHandle(handle);

			return *Slots[handle.Index].Entry;
		}

		const ResourceCacheEntry& Entry(CachedResourceHandle handle) const {
			ValidateHandle(handle);

			return *Slots[handle.Index].Entry;
		}

		CachedResourceHandle Request(std::string_view name) {
			if (name.empty() || name.size() > Limits.MaxNameBytes || name.find('\0') != std::string_view::npos) {
				throw std::runtime_error("resource cache name is empty, contains NUL, or exceeds its limit");
			}

			const auto found = Names.find(name);
			if (found != Names.end()) {
				return Handle(found->second);
			}

			const auto free = std::find_if(Slots.begin(), Slots.end(), [](const auto& slot) {
				return !slot.Entry;
			});
			const auto index = static_cast<std::size_t>(free - Slots.begin());
			if (index == Slots.size()) {
				if (index == Limits.MaxEntries) {
					throw std::runtime_error("resource cache entry count exceeds its allocation limit");
				}

				Slots.emplace_back();
			}

			ResourceCacheEntry entry;
			entry.Name = std::string(name);
			Names.emplace(entry.Name, static_cast<std::uint32_t>(index));
			Slots[index].Entry = std::move(entry);

			return Handle(static_cast<std::uint32_t>(index));
		}

		void RequireReadOwnerThread() const {
			if ((HasRawReadDestinations.load(std::memory_order_acquire) || ReadQueue.load(std::memory_order_acquire)) && std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("raw read publication requires its actual receiving manager thread");
			}
		}

		void ValidateComplete(const ResourceCacheEntry& entry, std::size_t byte_count, std::uint32_t charged_bytes) const {
			if (entry.Info.Residency != ResourceResidency::Loading || entry.Info.Completion != ResourceCompletion::Pending) {
				throw std::runtime_error("resource cache completion requires a pending load");
			}
			if (byte_count > Limits.MaxEntryBytes || byte_count > Limits.MaxOwnedBytes - OwnedBytes || charged_bytes > Limits.MaxOwnedBytes - ChargedBytes) {
				throw std::runtime_error("resource cache completion exceeds its allocation or accounting limit");
			}
		}

		void CommitBytes(ResourceCacheEntry& entry, std::vector<std::uint8_t> bytes, std::uint32_t charged_bytes) {
			entry.Bytes = std::move(bytes);
			entry.Info.Completion = ResourceCompletion::Succeeded;
			entry.Info.ChargedBytes = charged_bytes;
			OwnedBytes += entry.Bytes.size();
			ChargedBytes += charged_bytes;
		}

		static const std::vector<std::uint8_t>& Storage(const ResourceCacheEntry& entry) {
			if (entry.Image) {
				return entry.Image->MetadataStorage();
			}

			if (entry.Motion) {
				return entry.Motion->MetadataStorage();
			}

			if (entry.Font) {
				return entry.Font->MetadataStorage();
			}

			return entry.Psb ? entry.Psb->BackingStorage() : entry.Bytes;
		}

		void ObserveResourceRead(ResourceCacheEntry& entry) {
			if (!entry.ResourceRead || entry.Info.Completion != ResourceCompletion::Pending) {
				return;
			}

			const auto completion = entry.ResourceRead->Completion();
			if (completion == io::FileReadCompletion::Pending) {
				return;
			}
			if (completion == io::FileReadCompletion::Succeeded) {
				const auto byte_count = entry.ResourceRead->PreparedByteCount();
				const auto charge = entry.ResourceRead->PreparedCharge();
				ValidateComplete(entry, byte_count, charge);
				auto result = entry.ResourceRead->TakePrepared();
				if (result.Image) {
					entry.Image = std::move(result.Image);
					entry.Info.Completion = ResourceCompletion::Succeeded;
					entry.Info.ChargedBytes = charge;
					OwnedBytes += byte_count;
					ChargedBytes += charge;
				} else if (result.Motion) {
					entry.Motion = std::move(result.Motion);
					entry.Info.Completion = ResourceCompletion::Succeeded;
					entry.Info.ChargedBytes = charge;
					OwnedBytes += byte_count;
					ChargedBytes += charge;
				} else if (result.Font) {
					entry.Font = std::move(result.Font);
					entry.Info.Completion = ResourceCompletion::Succeeded;
					entry.Info.ChargedBytes = charge;
					OwnedBytes += byte_count;
					ChargedBytes += charge;
				} else if (result.Psb) {
					entry.Psb = std::move(result.Psb);
					entry.Info.Completion = ResourceCompletion::Succeeded;
					entry.Info.ChargedBytes = charge;
					OwnedBytes += byte_count;
					ChargedBytes += charge;
				} else {
					CommitBytes(entry, std::move(result.Bytes), charge);
				}
			} else {
				auto result = entry.ResourceRead->TakePrepared();
				entry.Info.Error = std::move(result.Error);
				entry.Info.UnsupportedProduction = result.UnsupportedProduction;
				entry.Info.Completion = ResourceCompletion::Failed;
			}
		}

		void ObserveRawRead(ResourceCacheEntry& entry) {
			if (!entry.RawRead || entry.Info.Completion != ResourceCompletion::Pending) {
				return;
			}

			const auto completion = entry.RawRead->Completion();
			if (completion == io::FileReadCompletion::Pending) {
				return;
			}
			if (completion == io::FileReadCompletion::Succeeded) {
				const auto byte_count = entry.RawRead->PreparedByteCount();
				ValidateComplete(entry, byte_count, static_cast<std::uint32_t>(byte_count));
				auto result = entry.RawRead->TakePrepared();
				CommitBytes(entry, std::move(result.Bytes), static_cast<std::uint32_t>(byte_count));
			} else {
				auto result = entry.RawRead->TakePrepared();
				entry.Info.Error = std::move(result.Error);
				entry.Info.Completion = ResourceCompletion::Failed;
			}
		}

		void RemoveCachedIndex(std::uint32_t index) {
			const auto found = std::find(CacheOrder.begin(), CacheOrder.end(), index);
			if (found == CacheOrder.end()) {
				throw std::runtime_error("resource cache order is missing a retained entry");
			}

			CacheOrder.erase(found);
		}

		void Retain(std::uint32_t index) {
			auto& entry = *Slots[index].Entry;
			if (entry.Info.Completion != ResourceCompletion::Succeeded) {
				Erase(index);

				return;
			}

			// Native 248f80 inserts before the first entry in the same discardable group.
			const auto found = std::find_if(CacheOrder.begin(), CacheOrder.end(), [&](auto cached) {
				return Slots[cached].Entry->Info.Discardable == entry.Info.Discardable;
			});
			entry.Info.Residency = ResourceResidency::Cached;
			CacheOrder.insert(found, index);
		}

		void Erase(std::uint32_t index) {
			auto& slot = Slots[index];
			if (slot.Generation == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("resource cache generation identity exhausted");
			}

			OwnedBytes -= Storage(*slot.Entry).size();
			ChargedBytes -= slot.Entry->Info.ChargedBytes;
			Names.erase(slot.Entry->Name);
			slot.Entry.reset();
			++slot.Generation;
		}

		void Release(CachedResourceHandle resource) {
			RequireReadOwnerThread();
			auto& entry = Entry(resource);
			if (entry.Info.References == 0) {
				throw std::runtime_error("resource cache release has no acquired reference");
			}

			--entry.Info.References;
			if (entry.Info.References == 0 && entry.Info.Residency == ResourceResidency::Active) {
				Retain(resource.Index);
				Trim();
			} else if (entry.Info.References == 0 && entry.Info.Residency == ResourceResidency::Loading) {
				entry.Info.Discardable = true;
				if (auto* queue = ReadQueue.load(std::memory_order_acquire)) {
					queue->ResortPending();
				}
			}
		}

		class BytePin {
		public:
			BytePin(std::shared_ptr<Implementation> owner, CachedResourceHandle handle)
			    : m_Owner(std::move(owner)), m_Handle(handle) {
				auto& entry = m_Owner->Entry(handle);
				if (entry.Info.References == std::numeric_limits<std::uint32_t>::max()) {
					throw std::runtime_error("resource cache reference count exhausted");
				}

				++entry.Info.References;
			}

			~BytePin() noexcept {
				try {
					m_Owner->Release(m_Handle);
				} catch (...) {
					// An active pin cannot become stale or lose its own reference through cache eviction.
					std::terminate();
				}
			}

			BytePin(const BytePin&) = delete;
			BytePin& operator=(const BytePin&) = delete;

		private:
			std::shared_ptr<Implementation> m_Owner;
			CachedResourceHandle m_Handle;
		};

		void Trim() {
			std::size_t active = 0;
			std::size_t cached = 0;
			for (const auto& slot : Slots) {
				if (slot.Entry && slot.Entry->Info.Residency == ResourceResidency::Active) {
					active += slot.Entry->Info.ChargedBytes;
				} else if (slot.Entry && slot.Entry->Info.Residency == ResourceResidency::Cached) {
					cached += slot.Entry->Info.ChargedBytes;
				}
			}

			// Native 248b60 never evicts active owners and skips trimming when total cached charge is zero.
			while (cached != 0 && cached + active > CacheLimit) {
				const auto index = CacheOrder.back();
				cached -= Slots[index].Entry->Info.ChargedBytes;
				CacheOrder.pop_back();
				Erase(index);
			}
		}
	};

	ResourceCache::ResourceCache(std::uint32_t initial_limit_bytes, ResourceCacheLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(initial_limit_bytes, limits)) {
	}

	ResourceCache::~ResourceCache() {
		if (!CanRetireReadDestinations()) {
			std::terminate();
		}
	}

	CachedResourceHandle ResourceCache::Acquire(std::string_view name) {
		auto& state = *m_Implementation;
		state.RequireReadOwnerThread();
		const auto handle = state.Request(name);
		auto& entry = state.Entry(handle);
		if (entry.Info.References == std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("resource cache reference count exhausted");
		}

		const auto old_references = entry.Info.References;
		++entry.Info.References;
		if (entry.Info.Residency == ResourceResidency::Loading && old_references == 0) {
			if (auto* queue = state.ReadQueue.load(std::memory_order_acquire)) {
				queue->ResortPending();
			}
		}
		if (entry.Info.Residency == ResourceResidency::Cached) {
			state.RemoveCachedIndex(handle.Index);
			entry.Info.Residency = ResourceResidency::Active;
			state.Trim();
		}

		return handle;
	}

	CachedResourceHandle ResourceCache::Prefetch(std::string_view name) {
		auto& state = *m_Implementation;
		state.RequireReadOwnerThread();
		const auto handle = state.Request(name);
		auto& entry = state.Entry(handle);
		if (entry.Info.Residency == ResourceResidency::Cached) {
			entry.Info.Discardable = false;
			state.RemoveCachedIndex(handle.Index);
			state.CacheOrder.insert(state.CacheOrder.begin(), handle.Index);
		}

		return handle;
	}

	void ResourceCache::Release(CachedResourceHandle resource) {
		m_Implementation->RequireReadOwnerThread();
		m_Implementation->Release(resource);
	}

	void ResourceCache::Complete(CachedResourceHandle resource, std::vector<std::uint8_t> bytes, std::uint32_t charged_bytes) {
		auto& state = *m_Implementation;
		auto& entry = state.Entry(resource);
		if (entry.RawRead || entry.ResourceRead) {
			throw std::runtime_error("resource completion belongs to its actual raw read destination");
		}

		state.ValidateComplete(entry, bytes.size(), charged_bytes);
		state.CommitBytes(entry, std::move(bytes), charged_bytes);
	}

	void ResourceCache::Fail(CachedResourceHandle resource, std::string error) {
		auto& entry = m_Implementation->Entry(resource);
		if (entry.RawRead || entry.ResourceRead || entry.Info.Residency != ResourceResidency::Loading || entry.Info.Completion != ResourceCompletion::Pending || error.empty() || error.size() > 4096) {
			throw std::runtime_error("resource cache failure requires a pending load and bounded diagnostic");
		}

		entry.Info.Error = std::move(error);
		entry.Info.Completion = ResourceCompletion::Failed;
	}

	io::RawFileReadDestination& ResourceCache::CreateRawReadDestination(CachedResourceHandle resource) {
		auto& state = *m_Implementation;
		if (std::this_thread::get_id() != state.OwnerThread) {
			throw std::runtime_error("raw read destination creation requires its actual manager thread");
		}

		auto& entry = state.Entry(resource);
		if (entry.Info.Residency != ResourceResidency::Loading || entry.Info.Completion != ResourceCompletion::Pending || entry.RawRead || entry.ResourceRead) {
			throw std::runtime_error("raw read destination requires its actual unassigned Loading entry");
		}

		entry.RawRead = std::make_unique<io::RawFileReadDestination>();
		entry.Format = io::ResourceReadFormat{};
		state.HasRawReadDestinations.store(true, std::memory_order_release);

		return *entry.RawRead;
	}

	io::ResourceFileReadDestination& ResourceCache::CreateResourceReadDestination(CachedResourceHandle resource, io::ResourceReadFormat format) {
		auto& state = *m_Implementation;
		if (std::this_thread::get_id() != state.OwnerThread) {
			throw std::runtime_error("typed resource receiver creation requires its actual manager thread");
		}

		auto& entry = state.Entry(resource);
		if (entry.Info.Residency != ResourceResidency::Loading || entry.Info.Completion != ResourceCompletion::Pending || entry.RawRead || entry.ResourceRead || entry.Format) {
			throw std::runtime_error("typed receiver requires its actual unassigned Loading entry");
		}

		entry.ResourceRead = std::make_unique<io::ResourceFileReadDestination>(format);
		entry.Format = format;
		state.HasRawReadDestinations.store(true, std::memory_order_release);

		return *entry.ResourceRead;
	}

	std::optional<io::ResourceReadFormat> ResourceCache::ReadFormat(CachedResourceHandle resource) const {
		m_Implementation->RequireReadOwnerThread();

		return m_Implementation->Entry(resource).Format;
	}

	CachedResourceReceivingObservation ResourceCache::ObserveReceiving(CachedResourceHandle resource) const {
		const auto& state = *m_Implementation;
		if (std::this_thread::get_id() != state.OwnerThread) {
			throw std::runtime_error("resource receiving observation requires its actual manager owner thread");
		}
		const auto& entry = state.Entry(resource);
		CachedResourceReceivingObservation observation;
		observation.Residency = entry.Info.Residency;
		observation.Completion = entry.Info.Completion;
		observation.References = entry.Info.References;
		observation.ChargedBytes = entry.Info.ChargedBytes;
		observation.Format = entry.Format;
		observation.HasFontDocument = static_cast<bool>(entry.Font);
		if (entry.ResourceRead) {
			observation.Receiving = entry.ResourceRead->ObserveReceiving();
		}

		return observation;
	}

	void ResourceCache::DiscardUnstartedRead(CachedResourceHandle resource) {
		auto& state = *m_Implementation;
		state.RequireReadOwnerThread();
		const auto& entry = state.Entry(resource);
		if (entry.Info.Residency != ResourceResidency::Loading || entry.Info.Completion != ResourceCompletion::Pending || entry.Info.References != 1 || (entry.RawRead && (!entry.RawRead->CanRetire() || entry.RawRead->Completion() != io::FileReadCompletion::Pending)) || (entry.ResourceRead && (!entry.ResourceRead->CanRetire() || entry.ResourceRead->Completion() != io::FileReadCompletion::Pending))) {
			throw std::runtime_error("unstarted read rollback cannot remove an actual published/coalesced receiving owner");
		}

		state.Erase(resource.Index);
	}

	void ResourceCache::AttachRawReadQueue(io::PendingRawReadRequests& queue) {
		auto& state = *m_Implementation;
		if (std::this_thread::get_id() != state.OwnerThread) {
			throw std::runtime_error("actual raw reader attachment requires its receiving manager thread");
		}

		if (!queue.UsesCache(*this) || !state.Names.empty()) {
			throw std::runtime_error("actual persistent raw reader attachment requires its initial receiving cache");
		}

		io::PendingRawReadRequests* expected = nullptr;
		if (!state.ReadQueue.compare_exchange_strong(expected, &queue, std::memory_order_release, std::memory_order_relaxed)) {
			throw std::runtime_error("resource manager already has its actual raw reader owner");
		}
	}

	void ResourceCache::DetachRawReadQueue(io::PendingRawReadRequests& queue) {
		auto& state = *m_Implementation;
		if (std::this_thread::get_id() != state.OwnerThread) {
			throw std::runtime_error("actual raw reader detachment requires its receiving manager thread");
		}

		if (!queue.UsesCache(*this)) {
			throw std::runtime_error("raw reader detachment belongs to a different receiving cache");
		}

		auto* expected = &queue;
		if (!state.ReadQueue.compare_exchange_strong(expected, nullptr, std::memory_order_release, std::memory_order_relaxed)) {
			throw std::runtime_error("raw reader detachment does not match the actual attached owner");
		}
	}

	bool ResourceCache::CanRetireReadDestinations() const noexcept {
		const auto& state = *m_Implementation;
		if (state.ReadQueue.load(std::memory_order_acquire)) {
			return false;
		}
		if (!state.HasRawReadDestinations.load(std::memory_order_acquire)) {
			return true;
		}
		if (std::this_thread::get_id() != state.OwnerThread) {
			return false;
		}

		for (const auto& slot : state.Slots) {
			if (slot.Entry && ((slot.Entry->RawRead && !slot.Entry->RawRead->CanRetire()) || (slot.Entry->ResourceRead && !slot.Entry->ResourceRead->CanRetire()))) {
				return false;
			}
		}

		return true;
	}

	void ResourceCache::RequireCanRetireReadDestinations() const {
		if (!CanRetireReadDestinations()) {
			throw std::runtime_error("resource manager still owns actual queued or selected read destinations");
		}
	}

	void ResourceCache::Advance() {
		auto& state = *m_Implementation;
		state.RequireReadOwnerThread();
		for (auto found = state.Names.begin(); found != state.Names.end();) {
			const auto index = found->second;
			++found;
			auto& entry = *state.Slots[index].Entry;
			state.ObserveRawRead(entry);
			state.ObserveResourceRead(entry);
			if (entry.Info.Residency != ResourceResidency::Loading || entry.Info.Completion == ResourceCompletion::Pending) {
				continue;
			}

			if (entry.Info.References != 0) {
				entry.Info.Residency = ResourceResidency::Active;
			} else {
				state.Retain(index);
			}
		}

		state.Trim();
	}

	bool ResourceCache::ObserveLoadingTargets(std::span<const CachedResourceHandle> targets) {
		auto& state = *m_Implementation;
		state.RequireReadOwnerThread();
		if (targets.size() > kMaxResourceLoadingTargets) {
			throw std::runtime_error("resource loading target observation exceeds its work limit");
		}

		// Native target names must resolve to acquired Loading/Active entries; reject its unsafe missing-map domain.
		for (const auto handle : targets) {
			const auto& entry = state.Entry(handle);
			if (entry.Info.References == 0 || entry.Info.Residency == ResourceResidency::Cached) {
				throw std::runtime_error("resource loading target observation requires actual acquired loading or active owners");
			}
		}

		for (const auto handle : targets) {
			auto& entry = state.Entry(handle);
			if (entry.Info.Residency == ResourceResidency::Active) {
				continue;
			}
			state.ObserveRawRead(entry);
			state.ObserveResourceRead(entry);
			if (entry.Info.Completion == ResourceCompletion::Pending) {
				return true;
			}

			entry.Info.Residency = ResourceResidency::Active;
			state.Trim();
		}

		return false;
	}

	void ResourceCache::ClearCache() {
		auto& state = *m_Implementation;
		state.RequireReadOwnerThread();
		for (std::size_t index = 0; index < state.Slots.size(); ++index) {
			auto& slot = state.Slots[index];
			if (slot.Entry && slot.Entry->Info.Residency == ResourceResidency::Loading && slot.Entry->Info.References == 0) {
				auto* queue = state.ReadQueue.load(std::memory_order_acquire);
				if (!queue || !queue->CancelPendingResource(state.Handle(static_cast<std::uint32_t>(index)))) {
					slot.Entry->Info.Discardable = true;
				}
			}
		}

		for (const auto index : state.CacheOrder) {
			state.Erase(index);
		}

		state.CacheOrder.clear();
	}

	void ResourceCache::SetLimit(std::uint32_t bytes) {
		// Native 052ce0 only updates policy; it does not synchronously release cached resources.
		m_Implementation->CacheLimit = bytes;
	}

	std::uint32_t ResourceCache::Limit() const {
		return m_Implementation->CacheLimit;
	}

	ResourceCacheState ResourceCache::State() const {
		ResourceCacheState result;
		result.CacheLimitSize = Limit();
		for (const auto& slot : m_Implementation->Slots) {
			if (!slot.Entry) {
				continue;
			}

			const auto& entry = *slot.Entry;
			switch (entry.Info.Residency) {
			case ResourceResidency::Loading:
				++result.LoadingEntryNum;
				break;
			case ResourceResidency::Active:
				++result.ActiveEntryNum;
				result.TotalActiveSize += entry.Info.ChargedBytes;
				break;
			case ResourceResidency::Cached:
				++result.CacheEntryNum;
				result.TotalCacheSize += entry.Info.ChargedBytes;
				break;
			}
		}

		return result;
	}

	CachedResourceInfo ResourceCache::Info(CachedResourceHandle resource) const {
		return m_Implementation->Entry(resource).Info;
	}

	bool ResourceCache::Contains(std::string_view name) const {
		return m_Implementation->Names.contains(name);
	}

	std::optional<CachedResourceHandle> ResourceCache::FindNamedResource(std::string_view name) const {
		const auto& state = *m_Implementation;
		state.RequireReadOwnerThread();
		const auto found = state.Names.find(name);
		if (found == state.Names.end()) {
			return std::nullopt;
		}

		return state.Handle(found->second);
	}

	bool ResourceCache::Contains(CachedResourceHandle resource) const {
		return m_Implementation->Contains(resource);
	}

	std::string_view ResourceCache::Name(CachedResourceHandle resource) const {
		return m_Implementation->Entry(resource).Name;
	}

	std::span<const std::uint8_t> ResourceCache::Bytes(CachedResourceHandle resource) const {
		const auto& entry = m_Implementation->Entry(resource);
		if (entry.Info.Residency != ResourceResidency::Active || entry.Info.References == 0 || entry.Info.Completion != ResourceCompletion::Succeeded) {
			throw std::runtime_error("resource cache bytes require a successful acquired active resource: " + entry.Name);
		}

		return Implementation::Storage(entry);
	}
	std::shared_ptr<const std::vector<std::uint8_t>> ResourceCache::PinBytes(CachedResourceHandle resource) {
		if (m_Implementation->Entry(resource).Font || m_Implementation->Entry(resource).Motion || m_Implementation->Entry(resource).Image) {
			throw std::runtime_error("purged texture document requires its typed resource pin");
		}

		static_cast<void>(Bytes(resource));
		const auto* bytes = &ByteStorage(resource);
		auto pin = std::make_shared<Implementation::BytePin>(m_Implementation, resource);

		return std::shared_ptr<const std::vector<std::uint8_t>>(std::move(pin), bytes);
	}
	std::shared_ptr<const std::vector<std::uint8_t>> ResourceCache::TryPinActiveBinary(std::string_view name) {
		auto& state = *m_Implementation;
		state.RequireReadOwnerThread();
		if (name.empty() || name.size() > state.Limits.MaxNameBytes || name.find('\0') != std::string_view::npos) {
			throw std::runtime_error("script buffer lookup requires its exact bounded native name");
		}

		const auto found = state.Names.find(name);
		if (found == state.Names.end()) {
			return {};
		}

		auto& entry = *state.Slots[found->second].Entry;
		if (entry.Info.Residency != ResourceResidency::Active) {
			return {};
		}
		if (!entry.Format || entry.Format->Kind != io::ResourceFileKind::Binary || entry.Psb || entry.Font || entry.Motion || entry.Image || entry.Info.Completion != ResourceCompletion::Succeeded || entry.Bytes.empty() || entry.Bytes.size() >= std::numeric_limits<std::uint32_t>::max() || entry.Info.References == 0 || entry.Info.References >= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
			throw std::runtime_error("active script buffer hit is outside the proved nonempty raw binary/pointer-pin domain");
		}

		const auto* bytes = &entry.Bytes;
		auto pin = std::make_shared<Implementation::BytePin>(m_Implementation, state.Handle(found->second));

		return std::shared_ptr<const std::vector<std::uint8_t>>(std::move(pin), bytes);
	}

	const std::vector<std::uint8_t>& ResourceCache::ByteStorage(CachedResourceHandle resource) const {
		static_cast<void>(Bytes(resource));

		return Implementation::Storage(m_Implementation->Entry(resource));
	}

	const io::PublishedPsbFile& ResourceCache::PsbFile(CachedResourceHandle resource) const {
		static_cast<void>(Bytes(resource));
		const auto& entry = m_Implementation->Entry(resource);
		if (!entry.Psb) {
			throw std::runtime_error("resource does not own an actual completed PSB object");
		}

		return *entry.Psb;
	}

	std::shared_ptr<const io::PublishedPsbFile> ResourceCache::PinPsbFile(CachedResourceHandle resource) {
		const auto* file = &PsbFile(resource);
		auto pin = std::make_shared<Implementation::BytePin>(m_Implementation, resource);

		return std::shared_ptr<const io::PublishedPsbFile>(std::move(pin), file);
	}

	bool ResourceCache::HasImageFile(CachedResourceHandle resource) const {
		static_cast<void>(Bytes(resource));
		return static_cast<bool>(m_Implementation->Entry(resource).Image);
	}

	const scene::ImageReceivingDocument& ResourceCache::ImageFile(CachedResourceHandle resource) const {
		static_cast<void>(Bytes(resource));
		const auto& entry = m_Implementation->Entry(resource);
		if (!entry.Image) {
			throw std::runtime_error("resource does not own a completed Image receiving document");
		}
		return *entry.Image;
	}

	std::shared_ptr<const scene::ImageReceivingDocument> ResourceCache::PinImageFile(CachedResourceHandle resource) {
		const auto* file = &ImageFile(resource);
		auto pin = std::make_shared<Implementation::BytePin>(m_Implementation, resource);
		return std::shared_ptr<const scene::ImageReceivingDocument>(std::move(pin), file);
	}

	bool ResourceCache::HasMotionFile(CachedResourceHandle resource) const {
		static_cast<void>(Bytes(resource));
		return static_cast<bool>(m_Implementation->Entry(resource).Motion);
	}

	const scene::MotionReceivingDocument& ResourceCache::MotionFile(CachedResourceHandle resource) const {
		static_cast<void>(Bytes(resource));
		const auto& entry = m_Implementation->Entry(resource);
		if (!entry.Motion) {
			throw std::runtime_error("resource does not own a completed Motion receiving document");
		}
		return *entry.Motion;
	}

	std::shared_ptr<const scene::MotionReceivingDocument> ResourceCache::PinMotionFile(CachedResourceHandle resource) {
		const auto* file = &MotionFile(resource);
		auto pin = std::make_shared<Implementation::BytePin>(m_Implementation, resource);
		return std::shared_ptr<const scene::MotionReceivingDocument>(std::move(pin), file);
	}

	bool ResourceCache::HasBitmapFontFile(CachedResourceHandle resource) const {
		static_cast<void>(Bytes(resource));

		return static_cast<bool>(m_Implementation->Entry(resource).Font);
	}

	const scene::BitmapFontReceivingDocument& ResourceCache::BitmapFontFile(CachedResourceHandle resource) const {
		static_cast<void>(Bytes(resource));
		const auto& entry = m_Implementation->Entry(resource);
		if (!entry.Font) {
			throw std::runtime_error("resource does not own a completed bitmap font receiving document");
		}

		return *entry.Font;
	}

	std::shared_ptr<const scene::BitmapFontReceivingDocument> ResourceCache::PinBitmapFontFile(CachedResourceHandle resource) {
		const auto* file = &BitmapFontFile(resource);
		auto pin = std::make_shared<Implementation::BytePin>(m_Implementation, resource);

		return std::shared_ptr<const scene::BitmapFontReceivingDocument>(std::move(pin), file);
	}

} // namespace sgre::resource
