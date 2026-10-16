#include "sgre/text/vector_glyph_cache.hpp"

#include <cmath>
#include <map>
#include <stdexcept>

namespace sgre::text {

	using VectorGlyphEntries = std::map<std::uint32_t, VectorGlyphAtlasRegion>;
	using VectorGlyphEdgeBuckets = std::map<std::int32_t, VectorGlyphEntries>;

	static constexpr std::size_t kMaximumCacheCapacity = 1048576;

	static bool IsDrawable(const VectorGlyphAtlasRegion& region) {
		return region.WidthPixels > 0 && region.HeightPixels > 0;
	}

	static TextCursor DrawPosition(std::uint32_t size_word, TextCursor origin, TextCursor glyph_position) {
		TextCursor position;
		position.X = origin.X + glyph_position.X;
		const auto baseline = static_cast<float>(size_word) + origin.Y;
		position.Y = baseline + glyph_position.Y;

		return position;
	}

	static void ValidatePosition(TextCursor position) {
		if (!std::isfinite(position.X) || !std::isfinite(position.Y)) {
			throw std::runtime_error("vector glyph draw position is not finite");
		}
	}

	class VectorGlyphCache::Implementation {
	public:
		explicit Implementation(VectorGlyphCacheLimits limits) : Limits(limits) {
			if (limits.MaxSizeBuckets == 0 || limits.MaxSizeBuckets > kMaximumCacheCapacity || limits.MaxEdgeBuckets == 0 || limits.MaxEdgeBuckets > kMaximumCacheCapacity || limits.MaxGlyphEntries == 0 || limits.MaxGlyphEntries > kMaximumCacheCapacity || limits.MaxGlyphsPerRun == 0 || limits.MaxGlyphsPerRun > kMaximumCacheCapacity) {
				throw std::runtime_error("vector glyph cache requires bounded positive capacities");
			}
		}

		VectorGlyphCacheLimits Limits;
		std::map<std::uint32_t, VectorGlyphEdgeBuckets> Sizes;
		std::size_t EdgeCount = 0;
		std::size_t GlyphCount = 0;
		bool Mutating = false;

		class Mutation {
		public:
			explicit Mutation(Implementation& owner) : Owner(owner) {
				if (owner.Mutating) {
					throw std::runtime_error("vector glyph cache cannot mutate during glyph preparation");
				}

				Owner.Mutating = true;
			}

			~Mutation() {
				Owner.Mutating = false;
			}

		private:
			Implementation& Owner;
		};

		VectorGlyphEntries& Bucket(std::uint32_t size_word, std::int32_t edge_width) {
			auto size = Sizes.find(size_word);
			if (size == Sizes.end()) {
				if (Sizes.size() == Limits.MaxSizeBuckets) {
					throw std::runtime_error("vector glyph cache size-bucket capacity is exhausted");
				}

				size = Sizes.try_emplace(size_word).first;
			}

			auto edge = size->second.find(edge_width);
			if (edge == size->second.end()) {
				if (EdgeCount == Limits.MaxEdgeBuckets) {
					throw std::runtime_error("vector glyph cache edge-bucket capacity is exhausted");
				}

				edge = size->second.try_emplace(edge_width).first;
				++EdgeCount;
			}

			return edge->second;
		}

		std::optional<VectorGlyphAtlasRegion> Find(VectorGlyphCacheKey key) const {
			const auto size = Sizes.find(key.SizeWord);
			if (size == Sizes.end()) {
				return {};
			}

			const auto edge = size->second.find(key.EdgeWidth);
			if (edge == size->second.end()) {
				return {};
			}

			const auto glyph = edge->second.find(key.GlyphIndex);
			if (glyph == edge->second.end()) {
				return {};
			}

			return glyph->second;
		}

		void ValidateRun(std::span<const TextRunGlyph> run) const {
			if (run.size() > Limits.MaxGlyphsPerRun) {
				throw std::runtime_error("vector glyph cache run exceeds its bounded glyph capacity");
			}
		}
	};

	VectorGlyphCache::VectorGlyphCache(VectorGlyphCacheLimits limits) : m_Implementation(std::make_unique<Implementation>(limits)) {
	}

	VectorGlyphCache::~VectorGlyphCache() = default;

	void VectorGlyphCache::PrepareRun(std::uint32_t size_word, std::int32_t edge_width, std::span<const TextRunGlyph> run, const GlyphPrepare& prepare) {
		auto& state = *m_Implementation;
		state.ValidateRun(run);
		const Implementation::Mutation mutation(state);
		if (run.empty()) {
			return;
		}

		auto& entries = state.Bucket(size_word, edge_width);
		for (const auto& glyph : run) {
			if (entries.contains(glyph.GlyphIndex)) {
				continue;
			}
			if (state.GlyphCount == state.Limits.MaxGlyphEntries) {
				throw std::runtime_error("vector glyph cache entry capacity is exhausted");
			}
			if (!prepare) {
				throw std::runtime_error("vector glyph cache miss requires actual glyph preparation");
			}

			VectorGlyphCacheKey key;
			key.SizeWord = size_word;
			key.EdgeWidth = edge_width;
			key.GlyphIndex = glyph.GlyphIndex;
			const auto region = prepare(key);
			entries.emplace(glyph.GlyphIndex, region);
			++state.GlyphCount;
		}
	}

	std::size_t VectorGlyphCache::PrepareDrawRun(std::uint32_t size_word, std::int32_t edge_width, TextCursor origin, std::uint32_t color_rgba, std::span<const TextRunGlyph> run, std::span<CachedVectorGlyphDraw> output) {
		auto& state = *m_Implementation;
		state.ValidateRun(run);
		if (output.size() < run.size()) {
			throw std::runtime_error("vector glyph draw output capacity must cover its run");
		}

		ValidatePosition(origin);
		for (const auto& glyph : run) {
			ValidatePosition(glyph.Position);
			ValidatePosition(DrawPosition(size_word, origin, glyph.Position));
		}

		const Implementation::Mutation mutation(state);
		const auto& entries = state.Bucket(size_word, edge_width);
		std::size_t count = 0;
		for (const auto& glyph : run) {
			const auto found = entries.find(glyph.GlyphIndex);
			if (found == entries.end() || !IsDrawable(found->second)) {
				continue;
			}

			auto& draw = output[count++];
			draw.Position = DrawPosition(size_word, origin, glyph.Position);
			draw.ColorRgba = color_rgba;
			draw.Region = found->second;
		}

		return count;
	}

	std::optional<VectorGlyphAtlasRegion> VectorGlyphCache::Find(VectorGlyphCacheKey key) const {
		return m_Implementation->Find(key);
	}

	VectorGlyphCacheCounts VectorGlyphCache::Counts() const {
		VectorGlyphCacheCounts counts;
		counts.SizeBuckets = m_Implementation->Sizes.size();
		counts.EdgeBuckets = m_Implementation->EdgeCount;
		counts.GlyphEntries = m_Implementation->GlyphCount;

		return counts;
	}

	void VectorGlyphCache::Clear() {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		state.Sizes.clear();
		state.EdgeCount = 0;
		state.GlyphCount = 0;
	}

} // namespace sgre::text
