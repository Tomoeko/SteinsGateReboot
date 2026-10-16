#include "sgre/scene/motion_assets.hpp"

#include "sgre/scene/motion_receiving_document.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	struct MotionAssetRecord {
		std::uint64_t DocumentIdentity = 0;
		std::weak_ptr<const resource::PsbDocument> Document;
		std::weak_ptr<const LoadedMotionAsset> Prepared;
	};

	class MotionAssetCatalog::Implementation {
	public:
		Implementation(TextureCache& cache, std::size_t max_documents) : Cache(cache), MaxDocuments(max_documents) {
			if (max_documents == 0 || max_documents > 65536) {
				throw std::runtime_error("motion asset catalog requires a bounded positive document limit");
			}

			Records.reserve(max_documents);
		}

		TextureCache& Cache;
		std::size_t MaxDocuments;
		std::vector<MotionAssetRecord> Records;

		void Prune() {
			std::erase_if(Records, [](const auto& record) {
				return record.Document.expired();
			});
		}
	};

	MotionAssetCatalog::MotionAssetCatalog(TextureCache& cache, std::size_t max_documents) : m_Implementation(std::make_unique<Implementation>(cache, max_documents)) {
	}

	MotionAssetCatalog::~MotionAssetCatalog() = default;

	std::shared_ptr<const LoadedMotionAsset> MotionAssetCatalog::Prepare(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing) {
		auto& state = *m_Implementation;
		if (!document) {
			throw std::runtime_error("motion asset preparation requires its loaded PSB document");
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
			throw std::runtime_error("motion asset loaded-document capacity is exhausted");
		}

		if (!backing || document->SerializedSize > backing->size()) {
			throw std::runtime_error("motion asset preparation requires complete retained PSB backing");
		}

		auto prepared = std::make_shared<LoadedMotionAsset>();
		prepared->Model = std::make_shared<const resource::MotionResource>(resource::ReadMotionResource(*document));
		prepared->Aliases = ReadMotionSourceAliases(*document);
		prepared->Identity.RootOffset = document->Header.RootOffset;
		prepared->Document = std::move(document);
		prepared->Backing = std::move(backing);
		if (found != state.Records.end()) {
			prepared->Identity.DocumentInstance = found->DocumentIdentity;
			found->Prepared = prepared;
		} else {
			prepared->Identity.DocumentInstance = state.Cache.AllocateDocumentIdentity();
			MotionAssetRecord record;
			record.DocumentIdentity = prepared->Identity.DocumentInstance;
			record.Document = prepared->Document;
			record.Prepared = prepared;
			state.Records.push_back(std::move(record));
		}

		return prepared;
	}

	std::shared_ptr<const LoadedMotionAsset> MotionAssetCatalog::Prepare(std::shared_ptr<const MotionReceivingDocument> document) {
		auto& state = *m_Implementation;
		if (!document || !document->UsesCache(state.Cache)) {
			throw std::runtime_error("motion receiving adoption requires its loaded document and actual cache scope");
		}
		const auto& asset = document->PreparedAsset();
		state.Prune();
		const auto found = std::find_if(state.Records.begin(), state.Records.end(), [&](const auto& record) {
			return record.Document.lock().get() == asset.Document.get();
		});
		if (found != state.Records.end()) {
			if (found->DocumentIdentity != asset.Identity.DocumentInstance) {
				throw std::runtime_error("motion receiving adoption conflicts with the actual catalog document identity");
			}
			if (const auto prepared = found->Prepared.lock()) {
				if (prepared.get() != &asset) {
					throw std::runtime_error("motion receiving adoption conflicts with an existing prepared owner");
				}
				return prepared;
			}
		} else if (state.Records.size() == state.MaxDocuments) {
			throw std::runtime_error("motion asset loaded-document capacity is exhausted");
		}

		auto prepared = std::shared_ptr<const LoadedMotionAsset>(std::move(document), &asset);
		if (found != state.Records.end()) {
			found->Prepared = prepared;
		} else {
			MotionAssetRecord record;
			record.DocumentIdentity = asset.Identity.DocumentInstance;
			record.Document = asset.Document;
			record.Prepared = prepared;
			state.Records.push_back(std::move(record));
		}

		return prepared;
	}

	bool MotionAssetCatalog::ContainsPrepared(const LoadedMotionAsset& asset) const {
		for (const auto& record : m_Implementation->Records) {
			if (record.DocumentIdentity == asset.Identity.DocumentInstance && record.Document.lock().get() == asset.Document.get() && record.Prepared.lock().get() == &asset) {
				return true;
			}
		}

		return false;
	}

	void MotionAssetCatalog::Prune() {
		m_Implementation->Prune();
	}

	std::size_t MotionAssetCatalog::DocumentCount() const {
		std::size_t count = 0;
		for (const auto& record : m_Implementation->Records) {
			if (!record.Document.expired()) {
				++count;
			}
		}

		return count;
	}

	bool MotionAssetCatalog::UsesCache(const TextureCache& cache) const noexcept {
		return &m_Implementation->Cache == &cache;
	}

} // namespace sgre::scene
