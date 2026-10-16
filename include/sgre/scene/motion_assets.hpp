#pragma once

#include "sgre/scene/motion_source_registry.hpp"
#include "sgre/scene/texture_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sgre::scene {

	class MotionReceivingDocument;

	struct LoadedMotionAsset {
		MotionProjectIdentity Identity;
		std::shared_ptr<const resource::PsbDocument> Document;
		std::shared_ptr<const std::vector<std::uint8_t>> Backing;
		std::shared_ptr<const resource::MotionResource> Model;
		std::vector<MotionSourceAlias> Aliases;
	};

	class MotionAssetCatalog {
	public:
		// One catalog serves the managers sharing this cache. The cache outlives the catalog and loaded owners.
		explicit MotionAssetCatalog(TextureCache& cache, std::size_t max_documents = 4096);
		~MotionAssetCatalog();
		MotionAssetCatalog(const MotionAssetCatalog&) = delete;
		MotionAssetCatalog& operator=(const MotionAssetCatalog&) = delete;
		MotionAssetCatalog(MotionAssetCatalog&&) = delete;
		MotionAssetCatalog& operator=(MotionAssetCatalog&&) = delete;

		// Preparation parses only when this immutable loaded document has no live prepared owner.
		// Native adapters separately retain the script PSBObject; sharing bytes alone does not root a VM instance.
		[[nodiscard]] std::shared_ptr<const LoadedMotionAsset> Prepare(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing);
		// Aliasing ownership retains actual preload wrappers and metadata after the original pixels retire.
		[[nodiscard]] std::shared_ptr<const LoadedMotionAsset> Prepare(std::shared_ptr<const MotionReceivingDocument> document);
		// Remove expired weak bookkeeping without extending a resource's native lifetime.
		void Prune();
		[[nodiscard]] std::size_t DocumentCount() const;
		[[nodiscard]] bool UsesCache(const TextureCache& cache) const noexcept;
		// Checks the exact still-live product of this catalog; matching model/identity fields alone are insufficient.
		[[nodiscard]] bool ContainsPrepared(const LoadedMotionAsset& asset) const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
