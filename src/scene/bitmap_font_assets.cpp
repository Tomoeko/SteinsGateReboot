#include "sgre/scene/bitmap_font_assets.hpp"

#include "sgre/scene/bitmap_font_receiving_document.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaxBitmapFontDocuments = 65536;

	struct BitmapFontAssetRecord {
		std::uint64_t DocumentIdentity = 0;
		std::weak_ptr<const resource::PsbDocument> Document;
		std::weak_ptr<const LoadedBitmapFontAsset> Prepared;
	};

	class BitmapFontAssetCatalog::Implementation {
	public:
		Implementation(TextureCache& cache, std::size_t max_documents) : Cache(cache), MaxDocuments(max_documents) {
			if (max_documents == 0 || max_documents > kMaxBitmapFontDocuments) {
				throw std::runtime_error("bitmap font catalog requires a bounded positive document capacity");
			}

			Records.reserve(max_documents);
		}

		TextureCache& Cache;
		std::size_t MaxDocuments;
		std::vector<BitmapFontAssetRecord> Records;

		void Prune() {
			std::erase_if(Records, [](const auto& record) {
				return record.Document.expired();
			});
		}
	};

	BitmapFontAssetCatalog::BitmapFontAssetCatalog(TextureCache& cache, std::size_t max_documents) : m_Implementation(std::make_unique<Implementation>(cache, max_documents)) {
	}

	BitmapFontAssetCatalog::~BitmapFontAssetCatalog() = default;

	std::shared_ptr<const LoadedBitmapFontAsset> BitmapFontAssetCatalog::Prepare(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing) {
		auto& state = *m_Implementation;
		if (!document) {
			throw std::runtime_error("bitmap font preparation requires a loaded PSB document");
		}

		state.Prune();
		const auto found = std::find_if(state.Records.begin(), state.Records.end(), [&](const auto& record) {
			return record.Document.lock().get() == document.get();
		});
		if (found != state.Records.end()) {
			if (const auto prepared = found->Prepared.lock()) {
				return prepared;
			}
		} else if (state.Records.size() == state.MaxDocuments) {
			throw std::runtime_error("bitmap font loaded-document capacity is exhausted");
		}

		if (!backing || document->SerializedSize > backing->size()) {
			throw std::runtime_error("bitmap font preparation requires complete retained PSB backing");
		}

		auto prepared = std::make_shared<LoadedBitmapFontAsset>();
		prepared->Model = std::make_shared<const resource::BitmapFont>(resource::ReadBitmapFont(*document));
		prepared->CacheOwner = &state.Cache;
		prepared->Document = std::move(document);
		prepared->Backing = std::move(backing);
		if (found != state.Records.end()) {
			prepared->DocumentIdentity = found->DocumentIdentity;
			found->Prepared = prepared;
		} else {
			prepared->DocumentIdentity = state.Cache.AllocateDocumentIdentity();
			BitmapFontAssetRecord record;
			record.DocumentIdentity = prepared->DocumentIdentity;
			record.Document = prepared->Document;
			record.Prepared = prepared;
			state.Records.push_back(std::move(record));
		}

		return prepared;
	}

	void BitmapFontAssetCatalog::Prune() {
		m_Implementation->Prune();
	}

	std::size_t BitmapFontAssetCatalog::DocumentCount() const {
		return static_cast<std::size_t>(std::count_if(m_Implementation->Records.begin(), m_Implementation->Records.end(), [](const auto& record) {
			return !record.Document.expired();
		}));
	}

	bool BitmapFontAssetCatalog::UsesCache(const TextureCache& cache) const noexcept {
		return &m_Implementation->Cache == &cache;
	}

	class BitmapFontTextureOwner::Implementation {
	public:
		Implementation(TextureCache& cache, std::shared_ptr<const LoadedBitmapFontAsset> asset) : Loaded(std::move(asset)) {
			if (!Loaded || Loaded->CacheOwner != &cache || Loaded->DocumentIdentity == 0 || !Loaded->Model || !Loaded->Document || !Loaded->Backing || Loaded->Document->SerializedSize > Loaded->Backing->size()) {
				throw std::runtime_error("bitmap font provider requires a complete prepared asset in its actual cache scope");
			}

			Sources.reserve(Loaded->Model->Sources.size());
			for (const auto& source : Loaded->Model->Sources) {
				Sources.push_back(std::make_unique<MotionTextureWrapper>(cache, Loaded->DocumentIdentity, source, *Loaded->Backing));
			}
		}

		Implementation(TextureCache& cache, std::shared_ptr<const BitmapFontReceivingDocument> document, const LoadedBitmapFontAsset& asset) : Receiving(std::move(document)), Loaded(Receiving, &asset) {
			if (!Receiving || !Receiving->UsesCache(cache)) {
				throw std::runtime_error("bitmap font provider requires its receiving document in the actual cache scope");
			}

			Sources.reserve(Loaded->Model->Sources.size());
			for (const auto& source : Loaded->Model->Sources) {
				Sources.push_back(std::make_unique<MotionTextureWrapper>(cache, Loaded->DocumentIdentity, source, *Loaded->Backing));
			}
		}

		std::shared_ptr<const BitmapFontReceivingDocument> Receiving;
		std::shared_ptr<const LoadedBitmapFontAsset> Loaded;
		std::vector<std::unique_ptr<MotionTextureWrapper>> Sources;
		bool Mutating = false;
		bool Closed = false;

		class Mutation {
		public:
			explicit Mutation(Implementation& owner) : Owner(owner) {
				if (owner.Mutating || owner.Closed) {
					throw std::runtime_error("bitmap font provider mutation requires its live owner outside cleanup");
				}

				Owner.Mutating = true;
			}

			~Mutation() {
				Owner.Mutating = false;
			}

		private:
			Implementation& Owner;
		};

		const MotionTextureWrapper& Source(std::size_t index) const {
			if (Closed || index >= Sources.size() || !Sources[index]) {
				throw std::runtime_error("bitmap font source is outside its live provider");
			}

			return *Sources[index];
		}

		void Close() {
			if (Closed) {
				return;
			}

			const Mutation mutation(*this);
			std::exception_ptr failure;
			for (auto& source : Sources) {
				try {
					source->Close();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}

				source.reset();
			}

			Sources.clear();
			Loaded.reset();
			Receiving.reset();
			Closed = true;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	BitmapFontTextureOwner::BitmapFontTextureOwner(TextureCache& cache, std::shared_ptr<const LoadedBitmapFontAsset> asset) : m_Implementation(std::make_unique<Implementation>(cache, std::move(asset))) {
	}

	BitmapFontTextureOwner::BitmapFontTextureOwner(TextureCache& cache, std::shared_ptr<const BitmapFontReceivingDocument> document) {
		if (!document) {
			throw std::runtime_error("bitmap font provider requires a loaded receiving document");
		}

		const auto& asset = document->PreparedAsset();
		m_Implementation = std::make_unique<Implementation>(cache, std::move(document), asset);
	}

	BitmapFontTextureOwner::~BitmapFontTextureOwner() {
		try {
			Close();
		} catch (...) {
			// Explicit Close reports cleanup failures; every acquired source still retires before model/backing disposal.
		}
	}

	const LoadedBitmapFontAsset& BitmapFontTextureOwner::Asset() const {
		if (m_Implementation->Closed) {
			throw std::runtime_error("bitmap font provider has retired its prepared asset");
		}

		return *m_Implementation->Loaded;
	}

	std::size_t BitmapFontTextureOwner::SourceCount() const {
		return m_Implementation->Sources.size();
	}

	MotionTextureWrapper::Borrowed BitmapFontTextureOwner::Source(std::size_t index) const {
		return m_Implementation->Source(index).Borrow();
	}

	std::int32_t BitmapFontTextureOwner::Minification(std::size_t index) const {
		return m_Implementation->Source(index).Minification();
	}

	std::int32_t BitmapFontTextureOwner::Magnification(std::size_t index) const {
		return m_Implementation->Source(index).Magnification();
	}

	void BitmapFontTextureOwner::SetFiltering(std::int32_t minification, std::int32_t magnification) {
		const Implementation::Mutation mutation(*m_Implementation);
		for (auto& source : m_Implementation->Sources) {
			source->SetFiltering(minification, magnification);
		}
	}

	void BitmapFontTextureOwner::Close() {
		m_Implementation->Close();
	}

} // namespace sgre::scene
