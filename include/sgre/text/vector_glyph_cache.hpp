#pragma once

#include "sgre/text/glyph_run.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace sgre::text {

	struct VectorGlyphCacheKey {
		std::uint32_t SizeWord = 0;
		std::int32_t EdgeWidth = 0;
		std::uint32_t GlyphIndex = 0;
	};

	// Values borrow the actual text owner's atlas; cache entries do not own textures or allocations.
	struct VectorGlyphAtlasRegion {
		std::int32_t AtlasLeftPixels = 0;
		std::int32_t AtlasTopPixels = 0;
		std::int32_t LeftPixel = 0;
		std::int32_t TopTranslationPixel = 0;
		std::int32_t WidthPixels = 0;
		std::int32_t HeightPixels = 0;
	};

	struct VectorGlyphCacheCounts {
		std::size_t SizeBuckets = 0;
		std::size_t EdgeBuckets = 0;
		std::size_t GlyphEntries = 0;
	};

	struct VectorGlyphCacheLimits {
		std::size_t MaxSizeBuckets = 4096;
		std::size_t MaxEdgeBuckets = 65536;
		std::size_t MaxGlyphEntries = 65536;
		std::size_t MaxGlyphsPerRun = 65536;
	};

	struct CachedVectorGlyphDraw {
		TextCursor Position;
		std::uint32_t ColorRgba = 0;
		VectorGlyphAtlasRegion Region;
	};

	class VectorGlyphCache {
	public:
		// The callback prepares an actual glyph into the caller's atlas on a cache miss only.
		// A returned zero/empty region is still cached and will not be retried.
		using GlyphPrepare = std::function<VectorGlyphAtlasRegion(const VectorGlyphCacheKey& key)>;
		explicit VectorGlyphCache(VectorGlyphCacheLimits limits = {});
		~VectorGlyphCache();
		VectorGlyphCache(const VectorGlyphCache&) = delete;
		VectorGlyphCache& operator=(const VectorGlyphCache&) = delete;
		VectorGlyphCache(VectorGlyphCache&&) = delete;
		VectorGlyphCache& operator=(VectorGlyphCache&&) = delete;

		// Run preparation may allocate cache entries. Call before ordinary drawing.
		void PrepareRun(std::uint32_t size_word, std::int32_t edge_width, std::span<const TextRunGlyph> run, const GlyphPrepare& prepare);
		// Prepares caller-owned draw values, preserving native empty-bucket creation and baseline order.
		// A previously prepared bucket performs no allocation. Output capacity must cover the run.
		[[nodiscard]] std::size_t PrepareDrawRun(std::uint32_t size_word, std::int32_t edge_width, TextCursor origin, std::uint32_t color_rgba, std::span<const TextRunGlyph> run, std::span<CachedVectorGlyphDraw> output);
		[[nodiscard]] std::optional<VectorGlyphAtlasRegion> Find(VectorGlyphCacheKey key) const;
		[[nodiscard]] VectorGlyphCacheCounts Counts() const;
		void Clear();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::text
