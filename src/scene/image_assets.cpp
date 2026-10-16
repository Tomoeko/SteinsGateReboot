#include "sgre/scene/image_assets.hpp"

#include "sgre/scene/image_receiving_document.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaxImageRecords = 65536;
	static constexpr std::uint32_t kMaxImageCoordinate = 16384;

	static const resource::PsbValue& RequireImageField(const resource::PsbValue& value, std::string_view key) {
		const auto* field = value.Find(key);
		if (!field) {
			throw std::runtime_error("native Image requires PSB field " + std::string(key));
		}

		return *field;
	}

	static const resource::PsbArray& ImageArray(const resource::PsbValue& value) {
		const auto* array = std::get_if<resource::PsbArray>(&value.Data);
		if (!array || array->size() > kMaxImageRecords) {
			throw std::runtime_error("native Image requires a bounded PSB array");
		}

		return *array;
	}

	static std::string_view ImageText(const resource::PsbValue& value) {
		const auto* text = std::get_if<std::string>(&value.Data);
		if (!text || text->find('\0') != std::string::npos) {
			throw std::runtime_error("native Image label requires measured zero-terminated text");
		}

		return *text;
	}

	static std::uint32_t ImageInteger(const resource::PsbValue& value, std::string_view key) {
		const auto& field = RequireImageField(value, key);
		std::uint32_t number = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&field.Data)) {
			number = static_cast<std::uint32_t>(*integer);
		} else if (const auto* boolean = std::get_if<bool>(&field.Data)) {
			number = *boolean ? 1u : 0u;
		} else if (const auto* real = std::get_if<double>(&field.Data)) {
			// Native4ca8a0 truncates float atoms through CVTTSS2SI and doubles directly through CVTTSD2SI.
			const auto truncated = std::trunc(*real);
			if (!std::isfinite(truncated) || truncated < 0 || truncated > kMaxImageCoordinate) {
				throw std::runtime_error("native Image real coordinate exceeds its bounded preparation domain");
			}
			number = static_cast<std::uint32_t>(truncated);
		} else {
			throw std::runtime_error("native Image coordinate requires a numeric or bool atom");
		}
		if (number > kMaxImageCoordinate) {
			throw std::runtime_error("native Image coordinate exceeds its bounded preparation domain");
		}

		return number;
	}

	static void ReadSelectedImage(LoadedImageAsset& loaded, std::string_view label) {
		const auto& entries = ImageArray(RequireImageField(loaded.Document->Root, "imageList"));
		const resource::PsbValue* selected = nullptr;
		for (const auto& entry : entries) {
			if (label.empty() || ImageText(RequireImageField(entry, "label")) == label) {
				selected = &entry;
				break;
			}
		}
		if (!selected) {
			return;
		}

		loaded.Model.Label = label;
		loaded.Model.Width = ImageInteger(*selected, "width");
		loaded.Model.Height = ImageInteger(*selected, "height");
		const auto& tiles = ImageArray(RequireImageField(*selected, "texture"));
		loaded.Model.Tiles.reserve(tiles.size());
		loaded.TileBounds.reserve(tiles.size());
		for (const auto& value : tiles) {
			resource::ImageTile tile;
			tile.Left = ImageInteger(value, "left");
			tile.Top = ImageInteger(value, "top");
			tile.Width = ImageInteger(value, "width");
			tile.Height = ImageInteger(value, "height");
			tile.Texture = resource::ReadImageTexture(*loaded.Document, RequireImageField(value, "image"));
			PictureTileBounds bounds;
			bounds.Bounds = {static_cast<float>(tile.Left), static_cast<float>(tile.Top), static_cast<float>(tile.Left + tile.Width), static_cast<float>(tile.Top + tile.Height)};
			bounds.Source = static_cast<std::uint32_t>(loaded.Model.Tiles.size());
			loaded.Model.Tiles.push_back(std::move(tile));
			loaded.TileBounds.push_back(bounds);
		}

		loaded.Valid = true;
	}

	struct ImageDocumentRecord {
		std::uint64_t Identity = 0;
		std::weak_ptr<const resource::PsbDocument> Document;
	};

	class ImageAssetCatalog::Implementation {
	public:
		Implementation(TextureCache& cache, std::size_t maximum)
		    : Cache(cache), Maximum(maximum) {
			if (maximum == 0 || maximum > kMaxImageRecords) {
				throw std::runtime_error("Image catalog requires a bounded positive document capacity");
			}

			Records.reserve(maximum);
		}

		TextureCache& Cache;
		std::size_t Maximum;
		std::vector<ImageDocumentRecord> Records;

		void Prune() {
			std::erase_if(Records, [](const auto& record) {
				return record.Document.expired();
			});
		}
	};

	ImageAssetCatalog::ImageAssetCatalog(TextureCache& cache, std::size_t max_documents)
	    : m_Implementation(std::make_unique<Implementation>(cache, max_documents)) {
	}

	ImageAssetCatalog::~ImageAssetCatalog() = default;

	std::shared_ptr<const LoadedImageAsset> ImageAssetCatalog::Prepare(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing, std::string_view label) {
		return PrepareDocument(std::move(document), std::move(backing), label, {});
	}

	std::shared_ptr<const LoadedImageAsset> ImageAssetCatalog::Prepare(std::shared_ptr<const ImageReceivingDocument> receiving, std::string_view label) {
		if (!receiving || !receiving->UsesCache(m_Implementation->Cache)) {
			throw std::runtime_error("image receiving adoption requires its actual cache scope");
		}
		const auto document = std::shared_ptr<const resource::PsbDocument>(receiving, &receiving->Document());
		const auto backing = std::shared_ptr<const std::vector<std::uint8_t>>(receiving, &receiving->MetadataStorage());

		return PrepareDocument(document, backing, label, std::move(receiving));
	}

	std::shared_ptr<const LoadedImageAsset> ImageAssetCatalog::PrepareDocument(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing, std::string_view label, std::shared_ptr<const ImageReceivingDocument> receiving) {
		auto& state = *m_Implementation;
		if (!document || !backing || (!receiving && document->SerializedSize > backing->size()) || label.find('\0') != std::string_view::npos) {
			throw std::runtime_error("native Image preparation requires immutable PSB/backing and measured label bytes");
		}

		state.Prune();
		const auto found = std::find_if(state.Records.begin(), state.Records.end(), [&](const auto& record) {
			return record.Document.lock().get() == document.get();
		});
		if (found == state.Records.end() && state.Records.size() == state.Maximum) {
			throw std::runtime_error("native Image catalog document capacity is exhausted");
		}

		auto loaded = std::make_shared<LoadedImageAsset>();
		loaded->Document = std::move(document);
		loaded->Backing = std::move(backing);
		loaded->CacheOwner = &state.Cache;
		loaded->Receiving = std::move(receiving);
		ReadSelectedImage(*loaded, label);
		if (found != state.Records.end()) {
			loaded->DocumentIdentity = found->Identity;
		} else {
			loaded->DocumentIdentity = loaded->Receiving ? loaded->Receiving->DocumentIdentity() : state.Cache.AllocateDocumentIdentity();
			ImageDocumentRecord record;
			record.Identity = loaded->DocumentIdentity;
			record.Document = loaded->Document;
			state.Records.push_back(std::move(record));
		}

		return loaded;
	}

	void ImageAssetCatalog::Prune() {
		m_Implementation->Prune();
	}

	std::size_t ImageAssetCatalog::DocumentCount() const {
		return static_cast<std::size_t>(std::count_if(m_Implementation->Records.begin(), m_Implementation->Records.end(), [](const auto& record) {
			return !record.Document.expired();
		}));
	}

	bool ImageAssetCatalog::UsesCache(const TextureCache& cache) const noexcept {
		return &m_Implementation->Cache == &cache;
	}

	class ImageTextureOwner::Implementation {
	public:
		Implementation(TextureCache& cache, std::shared_ptr<const LoadedImageAsset> asset)
		    : Loaded(std::move(asset)) {
			if (!Loaded || Loaded->CacheOwner != &cache || Loaded->DocumentIdentity == 0 || !Loaded->Document || !Loaded->Backing || (!Loaded->Receiving && Loaded->Document->SerializedSize > Loaded->Backing->size()) || Loaded->Model.Tiles.size() != Loaded->TileBounds.size()) {
				throw std::runtime_error("native Image source owner requires its complete asset in the actual cache scope");
			}

			Sources.reserve(Loaded->Model.Tiles.size());
			try {
				for (const auto& tile : Loaded->Model.Tiles) {
					auto source = std::make_unique<MotionTextureWrapper>(cache, Loaded->DocumentIdentity, tile.Texture, *Loaded->Backing);
					source->SetAddress(2, 2);
					Sources.push_back(std::move(source));
				}
			} catch (...) {
				const auto original = std::current_exception();
				try {
					Close();
				} catch (...) {
					// Complete source retirement precedes the original preparation failure report.
				}
				std::rethrow_exception(original);
			}
		}

		std::shared_ptr<const LoadedImageAsset> Loaded;
		std::vector<std::unique_ptr<MotionTextureWrapper>> Sources;
		bool Closing = false;
		bool Closed = false;

		void RequireMutation() const {
			if (Closing || Closed) {
				throw std::runtime_error("native Image source mutation requires its live owner outside cleanup");
			}
		}

		const MotionTextureWrapper& Source(std::size_t index) const {
			if (Closed || index >= Sources.size() || !Sources[index]) {
				throw std::runtime_error("native Image source is outside its live owner");
			}

			return *Sources[index];
		}

		void Close() {
			if (Closed) {
				return;
			}

			RequireMutation();
			Closing = true;
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
			Closed = true;
			Closing = false;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ImageTextureOwner::ImageTextureOwner(TextureCache& cache, std::shared_ptr<const LoadedImageAsset> asset)
	    : m_Implementation(std::make_unique<Implementation>(cache, std::move(asset))) {
	}

	ImageTextureOwner::~ImageTextureOwner() {
		try {
			Close();
		} catch (...) {
			// Explicit Close reports failures after every source has retired in native order.
		}
	}

	const LoadedImageAsset& ImageTextureOwner::Asset() const {
		if (m_Implementation->Closed) {
			throw std::runtime_error("native Image source owner has retired its model and backing");
		}

		return *m_Implementation->Loaded;
	}

	std::size_t ImageTextureOwner::SourceCount() const {
		return m_Implementation->Sources.size();
	}

	MotionTextureWrapper::Borrowed ImageTextureOwner::Source(std::size_t index) const {
		return m_Implementation->Source(index).Borrow();
	}

	std::uint32_t ImageTextureOwner::AddressU(std::size_t index) const {
		return m_Implementation->Source(index).AddressU();
	}

	std::uint32_t ImageTextureOwner::AddressV(std::size_t index) const {
		return m_Implementation->Source(index).AddressV();
	}

	void ImageTextureOwner::SetFiltering(std::int32_t minification, std::int32_t magnification) {
		m_Implementation->RequireMutation();
		for (auto& source : m_Implementation->Sources) {
			source->SetFiltering(minification, magnification);
		}
	}

	void ImageTextureOwner::SetAddress(std::uint32_t horizontal, std::uint32_t vertical) {
		m_Implementation->RequireMutation();
		if (horizontal > 2 || vertical > 2) {
			throw std::runtime_error("native Image addressing exceeds clamp/wrap/mirror indices");
		}

		for (auto& source : m_Implementation->Sources) {
			source->SetAddress(horizontal, vertical);
		}
	}

	void ImageTextureOwner::Close() {
		m_Implementation->Close();
	}

} // namespace sgre::scene
