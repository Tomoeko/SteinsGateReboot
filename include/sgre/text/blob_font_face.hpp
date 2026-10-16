#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::resource {
	class SfntFont;
}

namespace sgre::text {

	enum class FontFaceBlobStatus : std::uint8_t {
		Empty,
		Accepted,
		Repaired,
		Rejected
	};

	struct FontFaceBlobStorage;
	class BlobFontLayoutCache;
	class VectorFontShapePlanCache;

	enum class FontFaceLayoutState : std::uint8_t {
		Unprepared,
		Ready,
		Failed
	};

	// Parent-reference flag observation shares the face serialization contract, including after face teardown.
	class FontFaceTableBlob {
	public:
		FontFaceTableBlob() = default;
		[[nodiscard]] std::span<const std::uint8_t> Bytes() const;
		[[nodiscard]] std::uint32_t Length() const;
		[[nodiscard]] bool IsImmutable() const;

	private:
		friend class BlobFontFace;
		FontFaceTableBlob(std::shared_ptr<FontFaceBlobStorage> parent, std::size_t offset, std::uint32_t length, bool parent_reference);
		std::shared_ptr<FontFaceBlobStorage> m_Parent;
		std::size_t m_Offset = 0;
		std::uint32_t m_Length = 0;
		bool m_ParentReference = false;
	};

	// Uses the same immutable raw resource pin as VectorFont; child table blobs retain it through face teardown.
	// Serialize preparation, table queries, shared logical-flag reads and destruction for one face.
	class BlobFontFace {
	public:
		explicit BlobFontFace(std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::uint32_t face_index = 0, bool initially_immutable = false);
		~BlobFontFace();
		BlobFontFace(const BlobFontFace&) = delete;
		BlobFontFace& operator=(const BlobFontFace&) = delete;
		BlobFontFace(BlobFontFace&&) = delete;
		BlobFontFace& operator=(BlobFontFace&&) = delete;
		[[nodiscard]] FontFaceBlobStatus Status() const;
		[[nodiscard]] std::uint32_t FaceIndex() const;
		[[nodiscard]] bool ParentIsImmutable() const;
		[[nodiscard]] std::span<const std::uint8_t> WholeFontBytes() const;
		[[nodiscard]] FontFaceTableBlob ReferenceTable(std::uint32_t tag);
		[[nodiscard]] FontFaceLayoutState LayoutState() const;
		[[nodiscard]] bool PrepareLayout();
		// Returned cache and its admitted table views borrow this face lifetime.
		[[nodiscard]] const BlobFontLayoutCache* LayoutCache() const;
		[[nodiscard]] bool HasGlyphClasses();
		[[nodiscard]] bool HasPositioning();
		// Actual reader must borrow this face resource; cache views borrow the face lifetime.
		[[nodiscard]] VectorFontShapePlanCache& ShapePlans(const resource::SfntFont& actual_font);
		[[nodiscard]] const VectorFontShapePlanCache* ShapePlanCache() const;

	private:
		std::shared_ptr<FontFaceBlobStorage> m_Parent;
		std::uint32_t m_FaceIndex = 0;
		FontFaceBlobStatus m_Status = FontFaceBlobStatus::Empty;
		FontFaceLayoutState m_LayoutState = FontFaceLayoutState::Unprepared;
		std::unique_ptr<BlobFontLayoutCache> m_LayoutCache;
		// Native face teardown releases its plans before layout metadata and parent bytes.
		std::unique_ptr<VectorFontShapePlanCache> m_ShapePlans;
	};

} // namespace sgre::text
