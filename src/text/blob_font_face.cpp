#include "sgre/text/blob_font_face.hpp"

#include "sgre/resource/sfnt.hpp"
#include "sgre/text/blob_font_layout_cache.hpp"
#include "sgre/text/vector_font_shape_plan.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sgre::text {

	static constexpr std::size_t kMaximumFaceBlobBytes = 64 * 1024 * 1024;
	static constexpr std::uint32_t kMaximumFaceEditRequests = 32;
	static constexpr std::uint32_t kMaximumFaceValidationWork = 1048576;

	struct FontFaceBlobStorage {
		std::shared_ptr<const std::vector<std::uint8_t>> BytesPin;
		bool Immutable = false;
	};

	struct FaceBlobSanitizer {
		std::span<const std::uint8_t> Bytes;
		std::span<std::uint8_t> WritableBytes;
		std::uint32_t Edits = 0;
		std::uint32_t Work = 0;

		bool Range(std::size_t offset, std::size_t length) {
			if (++Work > kMaximumFaceValidationWork) {
				throw std::runtime_error("font face blob exceeds its supported validation work limit");
			}

			return offset <= Bytes.size() && length <= Bytes.size() - offset;
		}

		bool Array(std::size_t offset, std::uint32_t width, std::uint32_t count) {
			if (width != 0 && count >= std::numeric_limits<std::uint32_t>::max() / width) {
				return false;
			}

			return Range(offset, std::size_t(width) * count);
		}

		std::uint32_t Word(std::size_t field, std::size_t width = 2) const {
			if (field > Bytes.size() || width > Bytes.size() - field) {
				throw std::runtime_error("admitted font face field exceeds blob bounds");
			}
			std::uint32_t value = 0;
			for (std::size_t index = 0; index < width; ++index) {
				value = (value << 8) | Bytes[field + index];
			}

			return value;
		}

		bool EraseFaceOffset(std::size_t field) {
			if (Edits >= kMaximumFaceEditRequests) {
				return false;
			}
			++Edits;
			if (WritableBytes.empty()) {
				return false;
			}
			std::fill_n(WritableBytes.begin() + static_cast<std::ptrdiff_t>(field), 4, std::uint8_t(0));

			return true;
		}
	};

	static bool IsDirectorySignature(std::uint32_t signature) {
		return signature == 0x10000 || signature == 0x4f54544f || signature == 0x74727565 || signature == 0x74797031;
	}

	static bool ValidateFaceDirectory(FaceBlobSanitizer& context, std::size_t directory) {
		return context.Range(directory, 12) && context.Range(directory + 4, 8) && context.Array(directory + 12, 16, context.Word(directory + 4));
	}

	static bool ValidateFaceCollection(FaceBlobSanitizer& context) {
		if (!context.Range(8, 4) || !context.Array(12, 4, context.Word(8, 4))) {
			return false;
		}
		const auto count = context.Word(8, 4);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto field = 12 + std::size_t(index) * 4;
			if (!context.Range(field, 4)) {
				return false;
			}
			const auto relative = context.Word(field, 4);
			if (relative == 0) {
				continue;
			}
			if (!context.Range(0, relative)) {
				return false;
			}
			if (!ValidateFaceDirectory(context, relative) && !context.EraseFaceOffset(field)) {
				return false;
			}
		}

		return true;
	}

	static bool ValidateWholeFaceBlob(FaceBlobSanitizer& context) {
		if (!context.Range(0, 4)) {
			return false;
		}
		const auto signature = context.Word(0, 4);
		if (IsDirectorySignature(signature)) {
			return ValidateFaceDirectory(context, 0);
		}
		if (signature != 0x74746366) {
			return true;
		}
		if (!context.Range(4, 4)) {
			return false;
		}
		const auto version_major = context.Word(4);

		return (version_major != 1 && version_major != 2) || ValidateFaceCollection(context);
	}

	BlobFontFace::BlobFontFace(std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::uint32_t face_index, bool initially_immutable)
	    : m_FaceIndex(face_index) {
		if (!bytes || bytes->empty()) {
			return;
		}
		if (bytes->size() > kMaximumFaceBlobBytes) {
			throw std::runtime_error("font face blob exceeds its supported byte limit");
		}
		FaceBlobSanitizer context;
		context.Bytes = *bytes;
		std::vector<std::uint8_t> repaired;
		bool valid = ValidateWholeFaceBlob(context);
		if (context.Edits != 0 && !initially_immutable) {
			repaired.assign(bytes->begin(), bytes->end());
			context.Bytes = repaired;
			context.WritableBytes = repaired;
			context.Edits = 0;
			valid = ValidateWholeFaceBlob(context);
			if (valid && context.Edits != 0) {
				context.Edits = 0;
				valid = ValidateWholeFaceBlob(context) && context.Edits == 0;
			}
		}
		if (!valid) {
			m_Status = FontFaceBlobStatus::Rejected;

			return;
		}
		m_Parent = std::make_shared<FontFaceBlobStorage>();
		m_Parent->Immutable = initially_immutable;
		if (repaired.empty()) {
			m_Parent->BytesPin = std::move(bytes);
			m_Status = FontFaceBlobStatus::Accepted;
		} else {
			m_Parent->BytesPin = std::make_shared<const std::vector<std::uint8_t>>(std::move(repaired));
			m_Status = FontFaceBlobStatus::Repaired;
		}
	}

	FontFaceBlobStatus BlobFontFace::Status() const {
		return m_Status;
	}

	std::uint32_t BlobFontFace::FaceIndex() const {
		return m_FaceIndex;
	}

	bool BlobFontFace::ParentIsImmutable() const {
		return !m_Parent || m_Parent->Immutable;
	}

	std::span<const std::uint8_t> BlobFontFace::WholeFontBytes() const {
		return m_Parent ? std::span<const std::uint8_t>(*m_Parent->BytesPin) : std::span<const std::uint8_t>();
	}

	static std::optional<std::size_t> SelectFaceDirectory(FaceBlobSanitizer& reader, std::uint32_t face_index) {
		const auto signature = reader.Word(0, 4);
		if (IsDirectorySignature(signature)) {
			return 0;
		}
		if (signature != 0x74746366 || (reader.Word(4) != 1 && reader.Word(4) != 2) || face_index >= reader.Word(8, 4)) {
			return std::nullopt;
		}
		const auto offset = reader.Word(12 + std::size_t(face_index) * 4, 4);

		return offset == 0 ? std::nullopt : std::optional<std::size_t>(offset);
	}

	static std::optional<std::uint32_t> FindFaceTableRecord(const FaceBlobSanitizer& reader, std::size_t directory, std::uint32_t tag) {
		const auto count = reader.Word(directory + 4);
		if (count < 64) {
			for (std::uint32_t index = 0; index < count; ++index) {
				if (reader.Word(directory + 12 + std::size_t(index) * 16, 4) == tag) {
					return index;
				}
			}

			return std::nullopt;
		}
		std::int32_t first = 0;
		auto last = static_cast<std::int32_t>(count) - 1;
		while (first <= last) {
			const auto middle = first + (last - first) / 2;
			const auto current = reader.Word(directory + 12 + std::size_t(middle) * 16, 4);
			if (current == tag) {
				return static_cast<std::uint32_t>(middle);
			}
			// Original >=64-record search takes the descending-order branch directions.
			if (current < tag) {
				last = middle - 1;
			} else {
				first = middle + 1;
			}
		}

		return std::nullopt;
	}

	FontFaceTableBlob BlobFontFace::ReferenceTable(std::uint32_t tag) {
		if (!m_Parent) {
			return {};
		}
		const auto bytes = WholeFontBytes();
		if (tag == 0) {
			return FontFaceTableBlob(m_Parent, 0, static_cast<std::uint32_t>(bytes.size()), true);
		}
		m_Parent->Immutable = true;
		FaceBlobSanitizer reader;
		reader.Bytes = bytes;
		const auto directory = SelectFaceDirectory(reader, m_FaceIndex);
		if (!directory) {
			return {};
		}
		const auto record = FindFaceTableRecord(reader, *directory, tag);
		if (!record) {
			return {};
		}
		const auto entry = *directory + 12 + std::size_t(*record) * 16;
		const auto offset = reader.Word(entry + 8, 4);
		const auto requested_length = reader.Word(entry + 12, 4);
		if (requested_length == 0 || offset >= bytes.size()) {
			return {};
		}
		const auto length = std::min(std::size_t(requested_length), bytes.size() - offset);

		return FontFaceTableBlob(m_Parent, offset, static_cast<std::uint32_t>(length), false);
	}

	BlobFontFace::~BlobFontFace() = default;

	FontFaceLayoutState BlobFontFace::LayoutState() const {
		return m_LayoutState;
	}

	bool BlobFontFace::PrepareLayout() {
		if (m_LayoutState != FontFaceLayoutState::Unprepared) {
			return m_LayoutState == FontFaceLayoutState::Ready;
		}
		std::unique_ptr<BlobFontLayoutCache> candidate;
		try {
			candidate = std::make_unique<BlobFontLayoutCache>(BlobFontLayoutCache::ConstructionKey{}, *this);
		} catch (const std::bad_alloc&) {
			m_LayoutState = FontFaceLayoutState::Failed;

			return false;
		}
		if (!candidate->IsComplete()) {
			m_LayoutState = FontFaceLayoutState::Failed;

			return false;
		}
		m_LayoutCache = std::move(candidate);
		m_LayoutState = FontFaceLayoutState::Ready;

		return true;
	}

	const BlobFontLayoutCache* BlobFontFace::LayoutCache() const {
		return m_LayoutCache.get();
	}

	bool BlobFontFace::HasGlyphClasses() {
		return PrepareLayout() && m_LayoutCache->Definition().HasGlyphClasses();
	}

	bool BlobFontFace::HasPositioning() {
		return PrepareLayout() && !m_LayoutCache->Positioning().Bytes().empty();
	}

	static void ValidateShapePlanReader(const BlobFontFace& face, const resource::SfntFont& font) {
		if (face.FaceIndex() != 0 || face.WholeFontBytes().empty() || font.GlyphCount() == 0) {
			throw std::runtime_error("VectorFont shape plan requires its actual engine face-zero reader");
		}
		const auto bytes = face.WholeFontBytes();
		for (const auto& entry : font.Tables()) {
			const auto table = font.FindTable(entry.Tag);
			if (entry.Offset > bytes.size() || entry.Size > bytes.size() - entry.Offset || !table || table->size() != entry.Size || table->data() != bytes.data() + entry.Offset) {
				throw std::runtime_error("VectorFont shape plan reader does not borrow its actual face resource");
			}
		}
	}

	VectorFontShapePlanCache& BlobFontFace::ShapePlans(const resource::SfntFont& actual_font) {
		ValidateShapePlanReader(*this, actual_font);
		if (!PrepareLayout()) {
			throw std::runtime_error("VectorFont shape plan actual face layout preparation failed");
		}
		if (!m_ShapePlans) {
			auto candidate = std::make_unique<VectorFontShapePlanCache>(VectorFontShapePlanCache::ConstructionKey{}, *this, actual_font.GlyphCount());
			m_ShapePlans = std::move(candidate);
		}

		return *m_ShapePlans;
	}

	const VectorFontShapePlanCache* BlobFontFace::ShapePlanCache() const {
		return m_ShapePlans.get();
	}

	FontFaceTableBlob::FontFaceTableBlob(std::shared_ptr<FontFaceBlobStorage> parent, std::size_t offset, std::uint32_t length, bool parent_reference)
	    : m_Parent(std::move(parent)), m_Offset(offset), m_Length(length), m_ParentReference(parent_reference) {
	}

	std::span<const std::uint8_t> FontFaceTableBlob::Bytes() const {
		return m_Parent ? std::span<const std::uint8_t>(*m_Parent->BytesPin).subspan(m_Offset, m_Length) : std::span<const std::uint8_t>();
	}

	std::uint32_t FontFaceTableBlob::Length() const {
		return m_Length;
	}

	bool FontFaceTableBlob::IsImmutable() const {
		return !m_Parent || (m_ParentReference && m_Parent->Immutable);
	}

} // namespace sgre::text
