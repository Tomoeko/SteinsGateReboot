#pragma once

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_textures.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::scene {

	class MotionReceivingDocument {
	public:
		// Construct on the actual cache preparation owner. Its services outlive all receiving and catalog pins.
		MotionReceivingDocument(TextureCache& cache, std::vector<std::uint8_t> decoded_psb);
		~MotionReceivingDocument();
		MotionReceivingDocument(const MotionReceivingDocument&) = delete;
		MotionReceivingDocument& operator=(const MotionReceivingDocument&) = delete;
		MotionReceivingDocument(MotionReceivingDocument&&) = delete;
		MotionReceivingDocument& operator=(MotionReceivingDocument&&) = delete;

		[[nodiscard]] const resource::PsbDocument& Document() const;
		[[nodiscard]] const resource::MotionResource& Motion() const;
		// Chunk offsets remain logical original offsets; this retained span ends at DataOffset.
		[[nodiscard]] std::span<const std::uint8_t> MetadataBytes() const;
		[[nodiscard]] const std::vector<std::uint8_t>& MetadataStorage() const;
		[[nodiscard]] std::size_t SourceCount() const;
		[[nodiscard]] MotionTextureWrapper::Borrowed Source(std::size_t index) const;
		[[nodiscard]] bool UsesCache(const TextureCache& cache) const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
		[[nodiscard]] const LoadedMotionAsset& PreparedAsset() const;
		friend class MotionAssetCatalog;
	};

} // namespace sgre::scene
