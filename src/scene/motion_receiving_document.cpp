#include "sgre/scene/motion_receiving_document.hpp"

#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaxDecodedMotionBytes = 160 * 1024 * 1024;
	static constexpr std::size_t kMaxMotionMetadataBytes = 64 * 1024 * 1024;

	class MotionReceivingDocument::Implementation {
	public:
		Implementation(TextureCache& cache, std::vector<std::uint8_t> decoded_psb) : Cache(cache) {
			if (decoded_psb.size() > kMaxDecodedMotionBytes) {
				throw std::runtime_error("motion receiving document exceeds its decoded byte limit");
			}
			const auto header = resource::ReadPsbHeader(decoded_psb);
			if (header.DataOffset > decoded_psb.size() || header.DataOffset > kMaxMotionMetadataBytes) {
				throw std::runtime_error("motion receiving document has an invalid or excessive metadata extent");
			}

			// Native4d6717 copies/rebases metadata before source-key-ordered MTexture construction at4d67cd.
			const auto metadata = std::span<const std::uint8_t>(decoded_psb).first(header.DataOffset);
			Asset.Document = std::make_shared<const resource::PsbDocument>(resource::ReadPsbMetadata(metadata, decoded_psb.size()));
			Asset.Model = std::make_shared<const resource::MotionResource>(resource::ReadMotionResource(*Asset.Document));
			Asset.Aliases = ReadMotionSourceAliases(*Asset.Document);
			Asset.Backing = std::make_shared<const std::vector<std::uint8_t>>(metadata.begin(), metadata.end());
			Asset.Identity.RootOffset = Asset.Document->Header.RootOffset;
			Asset.Identity.DocumentInstance = cache.AllocateDocumentIdentity();
			Sources.reserve(Asset.Model->Sources.size());
			try {
				for (const auto& source : Asset.Model->Sources) {
					Sources.push_back(std::make_unique<MotionTextureWrapper>(cache, Asset.Identity.DocumentInstance, source.Texture, decoded_psb));
				}
			} catch (...) {
				RetireSources();
				throw;
			}
			// Native4d6829 releases original pixel storage only after every real source upload completes.
		}

		~Implementation() {
			RetireSources();
		}

		TextureCache& Cache;
		LoadedMotionAsset Asset;
		std::vector<std::unique_ptr<MotionTextureWrapper>> Sources;

		void RetireSources() noexcept {
			// Native4d3dd0 retires the vector in increasing order before the underlying PSB owner.
			for (auto& source : Sources) {
				try {
					source->Close();
				} catch (...) {
					// Cache release expires its reference before reporting a platform error; finish all owners.
				}
				source.reset();
			}
			Sources.clear();
		}
	};

	MotionReceivingDocument::MotionReceivingDocument(TextureCache& cache, std::vector<std::uint8_t> decoded_psb) : m_Implementation(std::make_unique<Implementation>(cache, std::move(decoded_psb))) {
	}

	MotionReceivingDocument::~MotionReceivingDocument() = default;

	const resource::PsbDocument& MotionReceivingDocument::Document() const {
		return *m_Implementation->Asset.Document;
	}

	const resource::MotionResource& MotionReceivingDocument::Motion() const {
		return *m_Implementation->Asset.Model;
	}

	std::span<const std::uint8_t> MotionReceivingDocument::MetadataBytes() const {
		return *m_Implementation->Asset.Backing;
	}

	const std::vector<std::uint8_t>& MotionReceivingDocument::MetadataStorage() const {
		return *m_Implementation->Asset.Backing;
	}

	std::size_t MotionReceivingDocument::SourceCount() const {
		return m_Implementation->Sources.size();
	}

	MotionTextureWrapper::Borrowed MotionReceivingDocument::Source(std::size_t index) const {
		if (index >= m_Implementation->Sources.size()) {
			throw std::runtime_error("motion receiving source index exceeds its retained wrapper count");
		}

		return m_Implementation->Sources[index]->Borrow();
	}

	bool MotionReceivingDocument::UsesCache(const TextureCache& cache) const noexcept {
		return &m_Implementation->Cache == &cache;
	}

	const LoadedMotionAsset& MotionReceivingDocument::PreparedAsset() const {
		return m_Implementation->Asset;
	}

} // namespace sgre::scene
