#include "sgre/scene/bitmap_font_receiving_document.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaxDecodedFontBytes = 160 * 1024 * 1024;
	static constexpr std::size_t kMaxFontMetadataBytes = 64 * 1024 * 1024;

	class BitmapFontReceivingDocument::Implementation {
	public:
		Implementation(TextureCache& cache, std::vector<std::uint8_t> decoded_psb) {
			if (decoded_psb.size() > kMaxDecodedFontBytes) {
				throw std::runtime_error("font receiving document exceeds its decoded byte limit");
			}

			const auto header = resource::ReadPsbHeader(decoded_psb);
			if (header.DataOffset > decoded_psb.size() || header.DataOffset > kMaxFontMetadataBytes) {
				throw std::runtime_error("font receiving document has an invalid or excessive metadata extent");
			}

			// Native 4cfb10 copies metadata before 4d6510 creates ordered MTexture wrappers; 4cfba0 then frees pixel backing.
			const auto metadata = std::span<const std::uint8_t>(decoded_psb).first(header.DataOffset);
			Asset.Document = std::make_shared<const resource::PsbDocument>(resource::ReadPsbMetadata(metadata, decoded_psb.size()));
			Asset.Model = std::make_shared<const resource::BitmapFont>(resource::ReadBitmapFont(*Asset.Document));
			Asset.Backing = std::make_shared<const std::vector<std::uint8_t>>(metadata.begin(), metadata.end());
			Asset.CacheOwner = &cache;
			Asset.DocumentIdentity = cache.AllocateDocumentIdentity();

			Sources.reserve(Asset.Model->Sources.size());
			try {
				for (const auto& texture : Asset.Model->Sources) {
					Sources.push_back(std::make_unique<MotionTextureWrapper>(cache, Asset.DocumentIdentity, texture, decoded_psb));
				}
			} catch (...) {
				RetireSources();
				throw;
			}
			// The owning input dies only after every upload completes. Retained wrappers guarantee later metadata-only cache hits.
		}

		~Implementation() {
			RetireSources();
		}

		LoadedBitmapFontAsset Asset;
		std::vector<std::unique_ptr<MotionTextureWrapper>> Sources;

		void RetireSources() noexcept {
			for (auto& source : Sources) {
				try {
					source->Close();
				} catch (...) {
					// Cache release expires the reference before reporting a platform failure; continue ordered retirement.
				}
				source.reset();
			}
			Sources.clear();
		}
	};

	BitmapFontReceivingDocument::BitmapFontReceivingDocument(TextureCache& cache, std::vector<std::uint8_t> decoded_psb) : m_Implementation(std::make_unique<Implementation>(cache, std::move(decoded_psb))) {
	}

	BitmapFontReceivingDocument::~BitmapFontReceivingDocument() = default;

	const resource::PsbDocument& BitmapFontReceivingDocument::Document() const {
		return *m_Implementation->Asset.Document;
	}

	const resource::BitmapFont& BitmapFontReceivingDocument::Font() const {
		return *m_Implementation->Asset.Model;
	}

	std::span<const std::uint8_t> BitmapFontReceivingDocument::MetadataBytes() const {
		return *m_Implementation->Asset.Backing;
	}

	const std::vector<std::uint8_t>& BitmapFontReceivingDocument::MetadataStorage() const {
		return *m_Implementation->Asset.Backing;
	}

	std::size_t BitmapFontReceivingDocument::SourceCount() const {
		return m_Implementation->Sources.size();
	}

	MotionTextureWrapper::Borrowed BitmapFontReceivingDocument::Source(std::size_t index) const {
		if (index >= m_Implementation->Sources.size()) {
			throw std::runtime_error("font receiving source index exceeds its retained wrapper count");
		}

		return m_Implementation->Sources[index]->Borrow();
	}

	bool BitmapFontReceivingDocument::UsesCache(const TextureCache& cache) const noexcept {
		return m_Implementation->Asset.CacheOwner == &cache;
	}

	const LoadedBitmapFontAsset& BitmapFontReceivingDocument::PreparedAsset() const {
		return m_Implementation->Asset;
	}

} // namespace sgre::scene
