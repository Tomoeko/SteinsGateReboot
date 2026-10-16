#include "sgre/scene/image_receiving_document.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sgre::scene {
	static constexpr std::size_t kMaxImageReceivingTextures = 65536;
	static constexpr std::size_t kMaxDecodedImageBytes = 160 * 1024 * 1024;
	static constexpr std::size_t kMaxImageMetadataBytes = 64 * 1024 * 1024;

	static const resource::PsbValue& ImageReceivingField(const resource::PsbValue& value, std::string_view name) {
		const auto* field = value.Find(name);
		if (!field) {
			throw std::runtime_error("image receiving requires PSB field " + std::string(name));
		}

		return *field;
	}

	static const resource::PsbArray& ImageReceivingArray(const resource::PsbValue& value) {
		const auto* array = std::get_if<resource::PsbArray>(&value.Data);
		if (!array || array->size() > kMaxImageReceivingTextures) {
			throw std::runtime_error("image receiving requires a bounded texture array");
		}

		return *array;
	}

	class ImageReceivingDocument::Implementation {
	public:
		Implementation(TextureCache& cache, std::vector<std::uint8_t> decoded_psb)
		    : Cache(cache) {
			if (decoded_psb.size() > kMaxDecodedImageBytes) {
				throw std::runtime_error("image receiving document exceeds its decoded byte limit");
			}
			const auto header = resource::ReadPsbHeader(decoded_psb);
			if (header.DataOffset > decoded_psb.size() || header.DataOffset > kMaxImageMetadataBytes) {
				throw std::runtime_error("image receiving document has an invalid or excessive metadata extent");
			}

			// Native4d6d68 copies/rebases metadata before imageList/texture traversal;4d6e9a purges pixels afterward.
			const auto metadata = std::span<const std::uint8_t>(decoded_psb).first(header.DataOffset);
			Document = resource::ReadPsbMetadata(metadata, decoded_psb.size());
			const auto* id = std::get_if<std::string>(&ImageReceivingField(Document.Root, "id").Data);
			const auto* spec = std::get_if<std::string>(&ImageReceivingField(Document.Root, "spec").Data);
			if (!id || *id != "image" || !spec || *spec != "win") {
				throw std::runtime_error("image receiving requires the recovered image/win target");
			}

			std::vector<resource::ImageTexture> textures;
			for (const auto& image : ImageReceivingArray(ImageReceivingField(Document.Root, "imageList"))) {
				const auto& tiles = ImageReceivingArray(ImageReceivingField(image, "texture"));
				if (tiles.size() > kMaxImageReceivingTextures - textures.size()) {
					throw std::runtime_error("image receiving texture count exceeds its preparation limit");
				}
				for (const auto& tile : tiles) {
					textures.push_back(resource::ReadImageTexture(Document, ImageReceivingField(tile, "image")));
				}
			}

			Metadata.assign(metadata.begin(), metadata.end());
			Identity = Cache.AllocateDocumentIdentity();
			Sources.reserve(textures.size());
			try {
				for (const auto& texture : textures) {
					Sources.push_back(std::make_unique<MotionTextureWrapper>(Cache, Identity, texture, decoded_psb));
				}
			} catch (...) {
				RetireSources();
				throw;
			}
		}

		~Implementation() {
			RetireSources();
		}

		TextureCache& Cache;
		resource::PsbDocument Document;
		std::vector<std::uint8_t> Metadata;
		std::uint64_t Identity = 0;
		std::vector<std::unique_ptr<MotionTextureWrapper>> Sources;

		void RetireSources() noexcept {
			for (auto& source : Sources) {
				try {
					source->Close();
				} catch (...) {
					// Finish native increasing-order retirement after a platform release reports failure.
				}
				source.reset();
			}
			Sources.clear();
		}
	};

	ImageReceivingDocument::ImageReceivingDocument(TextureCache& cache, std::vector<std::uint8_t> decoded_psb)
	    : m_Implementation(std::make_unique<Implementation>(cache, std::move(decoded_psb))) {
	}

	ImageReceivingDocument::~ImageReceivingDocument() = default;

	const resource::PsbDocument& ImageReceivingDocument::Document() const {
		return m_Implementation->Document;
	}

	std::span<const std::uint8_t> ImageReceivingDocument::MetadataBytes() const {
		return m_Implementation->Metadata;
	}

	const std::vector<std::uint8_t>& ImageReceivingDocument::MetadataStorage() const {
		return m_Implementation->Metadata;
	}

	std::uint64_t ImageReceivingDocument::DocumentIdentity() const {
		return m_Implementation->Identity;
	}

	std::size_t ImageReceivingDocument::SourceCount() const {
		return m_Implementation->Sources.size();
	}

	MotionTextureWrapper::Borrowed ImageReceivingDocument::Source(std::size_t index) const {
		if (index >= m_Implementation->Sources.size()) {
			throw std::runtime_error("image receiving source index exceeds its retained wrapper count");
		}

		return m_Implementation->Sources[index]->Borrow();
	}

	bool ImageReceivingDocument::UsesCache(const TextureCache& cache) const noexcept {
		return &m_Implementation->Cache == &cache;
	}

} // namespace sgre::scene
