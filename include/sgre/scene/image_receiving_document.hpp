#pragma once

#include "sgre/scene/motion_textures.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::scene {

	class ImageReceivingDocument {
	public:
		// The real cache preparation owner constructs and retires all preloaded tile wrappers.
		ImageReceivingDocument(TextureCache& cache, std::vector<std::uint8_t> decoded_psb);
		~ImageReceivingDocument();
		ImageReceivingDocument(const ImageReceivingDocument&) = delete;
		ImageReceivingDocument& operator=(const ImageReceivingDocument&) = delete;
		ImageReceivingDocument(ImageReceivingDocument&&) = delete;
		ImageReceivingDocument& operator=(ImageReceivingDocument&&) = delete;

		[[nodiscard]] const resource::PsbDocument& Document() const;
		// Logical chunk offsets and supplied length survive; only bytes before DataOffset remain owned here.
		[[nodiscard]] std::span<const std::uint8_t> MetadataBytes() const;
		[[nodiscard]] const std::vector<std::uint8_t>& MetadataStorage() const;
		[[nodiscard]] std::uint64_t DocumentIdentity() const;
		[[nodiscard]] std::size_t SourceCount() const;
		[[nodiscard]] MotionTextureWrapper::Borrowed Source(std::size_t index) const;
		[[nodiscard]] bool UsesCache(const TextureCache& cache) const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
