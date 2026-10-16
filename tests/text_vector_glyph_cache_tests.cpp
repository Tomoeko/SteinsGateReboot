#include "sgre/text/vector_glyph_cache.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("vector glyph cache fixture expected failure");
}

static sgre::text::VectorGlyphAtlasRegion DrawableRegion(std::int32_t width = 3) {
	sgre::text::VectorGlyphAtlasRegion region;
	region.AtlasLeftPixels = 17;
	region.AtlasTopPixels = 29;
	region.LeftPixel = -2;
	region.TopTranslationPixel = 7;
	region.WidthPixels = width;
	region.HeightPixels = 5;

	return region;
}

static void TestKeysMissRetentionAndEmptyBucketPhases() {
	sgre::text::VectorGlyphCache cache;
	cache.PrepareRun(12, 0, {}, {});
	Require(cache.Counts().SizeBuckets == 0, "native empty glyph preparation must not construct a cache bucket");
	Require(cache.PrepareDrawRun(12, 0, {}, 0, {}, {}) == 0 && cache.Counts().SizeBuckets == 1 && cache.Counts().EdgeBuckets == 1 && cache.Counts().GlyphEntries == 0, "native empty draw lookup must still construct its size/edge bucket");
	std::array<sgre::text::TextRunGlyph, 3> run{};
	run[0].GlyphIndex = 0xffffffff;
	run[1].GlyphIndex = 7;
	run[2].GlyphIndex = 0xffffffff;
	std::size_t calls = 0;
	cache.PrepareRun(0xffffffff, std::numeric_limits<std::int32_t>::min(), run, [&](const auto& key) {
		Require(key.SizeWord == 0xffffffff && key.EdgeWidth == std::numeric_limits<std::int32_t>::min(), "native unsigned size/signed edge key bits differ");
		++calls;

		return key.GlyphIndex == 7 ? DrawableRegion() : sgre::text::VectorGlyphAtlasRegion{};
	});
	cache.PrepareRun(0xffffffff, std::numeric_limits<std::int32_t>::min(), run, {});
	Require(calls == 2 && cache.Counts().GlyphEntries == 2 && cache.Find({0xffffffff, std::numeric_limits<std::int32_t>::min(), 0xffffffff})->WidthPixels == 0, "duplicate or failed/empty glyph results must remain cached without retrying or requiring a callback on hits");
	Require(!cache.Find({0x7fffffff, std::numeric_limits<std::int32_t>::min(), 7}) && !cache.Find({0xffffffff, std::numeric_limits<std::int32_t>::max(), 7}), "separate native size and edge keys were conflated");
	cache.Clear();
	Require(cache.Counts().SizeBuckets == 0 && cache.Counts().EdgeBuckets == 0 && cache.Counts().GlyphEntries == 0, "cache retirement must discard value records and all nested buckets");
}

static void TestDrawableLookupBaselineAndOrder() {
	sgre::text::VectorGlyphCache cache;
	std::array<sgre::text::TextRunGlyph, 5> run{};
	for (std::uint32_t index = 0; index < run.size(); ++index) {
		run[index].GlyphIndex = index;
		run[index].Position.X = 0.5f + static_cast<float>(index);
		run[index].Position.Y = 0.5f + static_cast<float>(index);
	}

	cache.PrepareRun(16777217, 2, std::span(run).first(4), [](const auto& key) {
		if (key.GlyphIndex == 0) {
			return sgre::text::VectorGlyphAtlasRegion{};
		}

		return DrawableRegion(key.GlyphIndex == 2 ? -1 : 3);
	});
	std::array<sgre::text::CachedVectorGlyphDraw, 5> output{};
	sgre::text::TextCursor origin;
	origin.X = 0.25f;
	origin.Y = -16777216.0f;
	const auto count = cache.PrepareDrawRun(16777217, 2, origin, 0x89abcdef, run, output);
	Require(count == 2 && output[0].Position.X == 1.75f && output[0].Position.Y == 1.5f && output[1].Position.X == 3.75f && output[1].Position.Y == 3.5f, "cache draw must skip missing/nonpositive rectangles and preserve unsigned-size float conversion then baseline/glyph addition in run order");
	Require(output[0].ColorRgba == 0x89abcdef && output[0].Region.AtlasLeftPixels == 17 && output[0].Region.LeftPixel == -2 && output[0].Region.TopTranslationPixel == 7 && cache.Counts().GlyphEntries == 4, "draw lookup must forward the stored atlas value/color without altering bearings or inserting missing glyphs");
	Require(cache.PrepareDrawRun(16777217, 0, origin, 0, run, output) == 0 && cache.Counts().EdgeBuckets == 2 && cache.Counts().GlyphEntries == 4, "unprepared edge draw must retain an empty bucket without inventing or rasterizing glyph entries");
}

static void TestBoundsCallbackFailureAndMutationGuard() {
	sgre::text::VectorGlyphCacheLimits invalid;
	invalid.MaxGlyphEntries = 0;
	RequireFailure([&] {
		sgre::text::VectorGlyphCache cache(invalid);
	},
	    "bounded positive");
	sgre::text::VectorGlyphCacheLimits limits;
	limits.MaxSizeBuckets = 2;
	limits.MaxEdgeBuckets = 1;
	limits.MaxGlyphEntries = 1;
	limits.MaxGlyphsPerRun = 2;
	sgre::text::VectorGlyphCache cache(limits);
	std::array<sgre::text::TextRunGlyph, 3> run{};
	run[0].GlyphIndex = 1;
	run[1].GlyphIndex = 2;
	RequireFailure([&] {
		cache.PrepareRun(12, 0, run, {});
	},
	    "run exceeds");
	Require(cache.Counts().SizeBuckets == 0, "run capacity failure mutated cache state");
	RequireFailure([&] {
		cache.PrepareRun(12, 0, std::span(run).first(1), [&](const auto&) {
			cache.Clear();

			return DrawableRegion();
		});
	},
	    "cannot mutate");
	Require(cache.Counts().SizeBuckets == 1 && cache.Counts().EdgeBuckets == 1 && cache.Counts().GlyphEntries == 0, "failed actual preparation must preserve its already constructed bucket without committing an entry");
	std::size_t calls = 0;
	RequireFailure([&] {
		cache.PrepareRun(12, 0, std::span(run).first(2), [&](const auto&) {
			++calls;

			return DrawableRegion();
		});
	},
	    "entry capacity");
	Require(calls == 1 && cache.Counts().GlyphEntries == 1 && cache.Find({12, 0, 1}) && !cache.Find({12, 0, 2}), "bounded preparation must retain completed entries and reject before an excess glyph callback");
	RequireFailure([&] {
		static_cast<void>(cache.PrepareDrawRun(24, 0, {}, 0, {}, {}));
	},
	    "edge-bucket capacity");
	Require(cache.Counts().SizeBuckets == 2 && cache.Counts().EdgeBuckets == 1, "edge capacity failure must retain the size bucket already created by the native order");
	RequireFailure([&] {
		static_cast<void>(cache.PrepareDrawRun(48, 0, {}, 0, {}, {}));
	},
	    "size-bucket capacity");
	std::array<sgre::text::CachedVectorGlyphDraw, 2> output{};
	RequireFailure([&] {
		static_cast<void>(cache.PrepareDrawRun(12, 0, {}, 0, std::span(run).first(2), std::span(output).first(1)));
	},
	    "output capacity");
	sgre::text::TextCursor invalid_origin;
	invalid_origin.X = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		static_cast<void>(cache.PrepareDrawRun(12, 0, invalid_origin, 0, {}, {}));
	},
	    "not finite");
	Require(cache.Counts().GlyphEntries == 1, "draw safety rejection must not discard the prepared cache");
}

int main() {
	try {
		TestKeysMissRetentionAndEmptyBucketPhases();
		TestDrawableLookupBaselineAndOrder();
		TestBoundsCallbackFailureAndMutationGuard();
		std::cout << "Vector glyph cache keys, retained misses, draw lookup phases and bounds passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
