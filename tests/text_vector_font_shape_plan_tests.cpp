#include "sgre/text/vector_font_shape_plan.hpp"

#include "sgre/resource/sfnt.hpp"
#include "sgre/text/blob_font_face.hpp"
#include "sgre/text/blob_font_layout_cache.hpp"
#include "sgre/text/vector_font.hpp"
#include "sgre/text/vector_font_normalization_font.hpp"
#include "vector_font_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

static_assert(!std::is_default_constructible_v<sgre::resource::OpenTypeLayoutFont::AdmittedTablesKey>);
static_assert(!std::is_default_constructible_v<sgre::text::VectorFontShapePlanCache::ConstructionKey>);
static_assert(!std::is_constructible_v<sgre::text::VectorFontShapePlanCache, sgre::text::BlobFontFace&, std::uint16_t>);

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("VectorFont shape-plan fixture expected failure");
}

static void AppendWord(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
	bytes.push_back(static_cast<std::uint8_t>(value >> 8));
	bytes.push_back(static_cast<std::uint8_t>(value));
}

static std::uint16_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	Require(offset <= bytes.size() && 2 <= bytes.size() - offset, "fixture word exceeds its bytes");

	return std::uint16_t((std::uint16_t(bytes[offset]) << 8) | bytes[offset + 1]);
}

static void WriteTag(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string_view tag) {
	std::copy(tag.begin(), tag.end(), bytes.data() + offset);
}

static std::vector<std::uint8_t> MakeLayout(bool positioning) {
	// One real DFLT/default language, required ccmp feature and glyph-one single lookup.
	std::vector<std::uint8_t> scripts(8);
	vector_font_test::WriteWord(scripts, 0, 1);
	WriteTag(scripts, 2, "DFLT");
	vector_font_test::WriteWord(scripts, 6, 8);
	for (const auto word : std::array<std::uint16_t, 6>{4, 0, 0, 0, 1, 0}) {
		AppendWord(scripts, word);
	}

	std::vector<std::uint8_t> features(8);
	vector_font_test::WriteWord(features, 0, 1);
	WriteTag(features, 2, "ccmp");
	vector_font_test::WriteWord(features, 6, 8);
	for (const auto word : std::array<std::uint16_t, 3>{0, 1, 0}) {
		AppendWord(features, word);
	}

	std::vector<std::uint8_t> lookups;
	for (const auto word : std::array<std::uint16_t, 6>{1, 4, 1, 0, 1, 8}) {
		AppendWord(lookups, word);
	}
	if (positioning) {
		for (const auto word : std::array<std::uint16_t, 7>{1, 8, 1, 127, 1, 1, 1}) {
			AppendWord(lookups, word);
		}
	} else {
		for (const auto word : std::array<std::uint16_t, 6>{1, 6, 1, 1, 1, 1}) {
			AppendWord(lookups, word);
		}
	}

	std::vector<std::uint8_t> result(10);
	vector_font_test::WriteDoubleWord(result, 0, 0x10000);
	vector_font_test::WriteWord(result, 4, static_cast<std::uint16_t>(result.size()));
	vector_font_test::Append(result, scripts);
	vector_font_test::WriteWord(result, 6, static_cast<std::uint16_t>(result.size()));
	vector_font_test::Append(result, features);
	vector_font_test::WriteWord(result, 8, static_cast<std::uint16_t>(result.size()));
	vector_font_test::Append(result, lookups);

	return result;
}

static std::vector<std::uint8_t> MakeFont(std::span<const std::uint8_t> substitution, std::span<const std::uint8_t> positioning, std::span<const std::uint8_t> definition = {}) {
	const auto base = vector_font_test::MakeFont();
	const sgre::resource::SfntFont original(base);
	std::map<std::string, std::vector<std::uint8_t>> tables;
	for (const auto& entry : original.Tables()) {
		const auto bytes = *original.FindTable(entry.Tag);
		tables[entry.Tag] = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
	}
	if (!substitution.empty()) {
		tables["GSUB"] = std::vector<std::uint8_t>(substitution.begin(), substitution.end());
	}
	if (!positioning.empty()) {
		tables["GPOS"] = std::vector<std::uint8_t>(positioning.begin(), positioning.end());
	}
	if (!definition.empty()) {
		tables["GDEF"] = std::vector<std::uint8_t>(definition.begin(), definition.end());
	}

	std::vector<std::uint8_t> result(12 + tables.size() * 16);
	vector_font_test::WriteDoubleWord(result, 0, 0x4f54544f);
	vector_font_test::WriteWord(result, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, bytes] : tables) {
		while (result.size() % 4 != 0) {
			result.push_back(0);
		}
		const auto record = 12 + index++ * 16;
		WriteTag(result, record, tag);
		vector_font_test::WriteDoubleWord(result, record + 8, static_cast<std::uint32_t>(result.size()));
		vector_font_test::WriteDoubleWord(result, record + 12, static_cast<std::uint32_t>(bytes.size()));
		vector_font_test::Append(result, bytes);
	}

	return result;
}

static sgre::text::VectorFontConfiguration NullLanguageConfiguration() {
	return sgre::text::ResolveVectorFontConfiguration(0, "");
}

static sgre::text::VectorFontShapePlanProperties NullLanguageProperties() {
	sgre::text::VectorFontShapePlanProperties properties;
	properties.DirectionWord = 4;
	properties.ScriptTag = 0x4c61746e;

	return properties;
}

static sgre::text::VectorFontShapePlanFeature GlobalFeature(std::uint32_t tag, std::uint32_t value) {
	sgre::text::VectorFontShapePlanFeature feature;
	feature.Tag = tag;
	feature.Value = value;
	feature.StartCluster = 0;
	feature.EndCluster = 0xffffffff;

	return feature;
}

static void RequireSingleSchedules(const sgre::text::VectorFontLookupPlan& plan) {
	Require(plan.Substitutions.StageEnds == std::vector<std::size_t>{0, 1} && plan.Positioning.StageEnds == std::vector<std::size_t>{1}, "required ccmp remains in the original GSUB stage one and GPOS stage zero");
	for (const auto* schedule : {&plan.Substitutions, &plan.Positioning}) {
		Require(schedule->Lookups.size() == 1 && schedule->Lookups[0].Index == 0 && schedule->Lookups[0].Mask == 2 && schedule->Lookups[0].Options == 3, "actual feature and required lookup merge into the independent index/mask/policy vector");
	}
}

static void TestActualFaceAndEffectiveMetadata() {
	const auto substitution = MakeLayout(false);
	const auto positioning = MakeLayout(true);
	sgre::text::VectorFont font(std::make_shared<const std::vector<std::uint8_t>>(MakeFont(substitution, positioning)));
	auto& normalizer = font.NormalizationFont();
	Require(normalizer.Face().LayoutState() == sgre::text::FontFaceLayoutState::Unprepared && normalizer.Face().ShapePlanCache() == nullptr, "real normalization construction does not prepare layout or publish a plan cache");
	const auto plan = normalizer.PrepareShapePlan(NullLanguageConfiguration(), NullLanguageProperties(), {}, {});
	plan.RequireFace(normalizer.Face());
	const auto snapshot = plan.Snapshot();
	Require(normalizer.Face().LayoutState() == sgre::text::FontFaceLayoutState::Ready && normalizer.Face().ShapePlanCache()->CachedPlanCount() == 1, "actual same-face preparation precedes plan cache publication");
	Require(snapshot.Tables.SubstitutionBytes == substitution.size() && snapshot.Tables.PositioningBytes == positioning.size() && snapshot.Tables.SubstitutionLookupCount == 1 && snapshot.Tables.PositioningLookupCount == 1, "metadata matches the admitted actual table lengths and accelerator counts");
	Require(snapshot.Properties == NullLanguageProperties() && snapshot.Cached && snapshot.Features.empty() && snapshot.Coordinates.empty(), "zero feature/coordinate words and null language remain exact");
	RequireSingleSchedules(snapshot.LookupPlan);
}

static void TestCompleteKeysAndInputOwnership() {
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(MakeLayout(false), MakeLayout(true)));
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::BlobFontFace face(pin);
	auto& cache = face.ShapePlans(reader);
	const auto configuration = NullLanguageConfiguration();
	const auto properties = NullLanguageProperties();
	std::array<sgre::text::VectorFontShapePlanFeature, 2> features{GlobalFeature(0x63636d70, 1), GlobalFeature(0x63636d70, 2)};
	const auto first = cache.Acquire(configuration, properties, features, {});
	const auto second = cache.Acquire(configuration, properties, features, {});
	Require(first.SharesMetadataWith(second), "same complete key reuses its actual cached metadata");
	const auto original = features;
	std::reverse(features.begin(), features.end());
	const auto reversed = cache.Acquire(configuration, properties, features, {});
	Require(!first.SharesMetadataWith(reversed) && first.Snapshot().Features == std::vector<sgre::text::VectorFontShapePlanFeature>(original.begin(), original.end()), "original feature order and every word belong to the key and owned metadata");
	features[0].Value = 0;
	Require(!reversed.SharesMetadataWith(cache.Acquire(configuration, properties, features, {})), "the original zero feature value is not erased from its cache key");
	features[0].Tag = 0x6c696761;
	Require(!reversed.SharesMetadataWith(cache.Acquire(configuration, properties, features, {})), "feature tags remain original full-width key words");
}

static void TestLanguageDirectionAndSameFaceIdentity() {
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(MakeLayout(false), MakeLayout(true)));
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::BlobFontFace first_face(pin);
	sgre::text::BlobFontFace second_face(pin);
	auto& cache = first_face.ShapePlans(reader);
	const auto null_plan = cache.Acquire(NullLanguageConfiguration(), NullLanguageProperties(), {}, {});
	auto properties = NullLanguageProperties();
	// Synthetic opaque token supplied explicitly by this fixture; not an engine language default.
	properties.LanguageIdentity = 0x123456789abcdef0;
	const auto english = cache.Acquire(sgre::text::ResolveVectorFontConfiguration(0, "en"), properties, {}, {});
	Require(!english.SharesMetadataWith(null_plan) && english.Snapshot().Properties.LanguageIdentity == properties.LanguageIdentity, "null and explicit canonical en identities remain distinct keys");
	auto configuration = NullLanguageConfiguration();
	configuration.Direction = sgre::text::VectorFontDirection::RightToLeft;
	properties = NullLanguageProperties();
	properties.DirectionWord = 5;
	const auto right = cache.Acquire(configuration, properties, {}, {});
	Require(!right.SharesMetadataWith(null_plan), "direction word participates in full property equality");
	configuration.Script = sgre::text::VectorFontScript::Cyrillic;
	properties.ScriptTag = 0x4379726c;
	Require(!right.SharesMetadataWith(cache.Acquire(configuration, properties, {}, {})), "native script tag word participates in full property equality");
	const auto foreign = second_face.ShapePlans(reader).Acquire(NullLanguageConfiguration(), NullLanguageProperties(), {}, {});
	Require(!foreign.SharesMetadataWith(null_plan), "identical resource bytes and scalar keys do not merge different actual faces");
	RequireFailure([&] {
		null_plan.RequireFace(second_face);
	},
	    "different actual face");
}

static void TestRangesAndNonemptyCoordinatesBypass() {
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(MakeLayout(false), MakeLayout(true)));
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::BlobFontFace face(pin);
	auto& cache = face.ShapePlans(reader);
	std::array<sgre::text::VectorFontShapePlanFeature, 1> features{GlobalFeature(0x63636d70, 1)};
	const auto configuration = NullLanguageConfiguration();
	const auto properties = NullLanguageProperties();
	const auto global = cache.Acquire(configuration, properties, features, {});
	features[0].StartCluster = 3;
	features[0].EndCluster = 7;
	const auto ranged = cache.Acquire(configuration, properties, features, {});
	const auto repeated = cache.Acquire(configuration, properties, features, {});
	Require(!ranged.SharesMetadataWith(repeated) && !ranged.Snapshot().Cached && ranged.Snapshot().Features[0] == features[0], "start/end range words remain exact and repeated ranged requests stay uncached");
	features[0].StartCluster = 0;
	features[0].EndCluster = 0xffffffff;
	const std::array<std::int32_t, 3> coordinates{0, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()};
	const auto coordinated = cache.Acquire(configuration, properties, features, coordinates);
	Require(!coordinated.SharesMetadataWith(cache.Acquire(configuration, properties, features, coordinates)) && !coordinated.Snapshot().Cached && coordinated.Snapshot().Coordinates == std::vector<std::int32_t>(coordinates.begin(), coordinates.end()), "all original coordinate words are retained and a nonempty vector bypasses caching");
	const std::array<std::int32_t, 1> zero_coordinate{0};
	Require(!cache.Acquire(configuration, properties, features, zero_coordinate).Snapshot().Cached && cache.CachedPlanCount() == 1 && global.SharesMetadataWith(cache.Acquire(configuration, properties, features, {})), "nonempty all-zero coordinates also bypass; no bypass request publishes a cache entry");
}

static void TestForeignReaderRejectedBeforePreparation() {
	const auto bytes = MakeFont(MakeLayout(false), MakeLayout(true));
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(bytes);
	const sgre::resource::SfntFont foreign(bytes);
	const sgre::resource::SfntFont actual(*pin);
	sgre::text::BlobFontFace face(pin);
	RequireFailure([&] {
		(void)face.ShapePlans(foreign);
	},
	    "does not borrow its actual face resource");
	Require(face.LayoutState() == sgre::text::FontFaceLayoutState::Unprepared && face.ShapePlanCache() == nullptr, "same bytes in another allocation cannot publish sanitized actual-face metadata");
	const sgre::resource::SfntFont second_actual(*pin);
	Require(&face.ShapePlans(actual) == &face.ShapePlans(second_actual), "a second reader borrowing the exact same immutable resource remains the same face");
	sgre::text::BlobFontFace nonzero_face(pin, 1);
	RequireFailure([&] {
		(void)nonzero_face.ShapePlans(actual);
	},
	    "face-zero reader");
}

static void TestBorrowExpiryWithoutResourcePin() {
	sgre::text::VectorFontShapePlan retained;
	sgre::text::VectorFontShapePlanSnapshot owned;
	RequireFailure([&] {
		(void)retained.Snapshot();
	},
	    "no admitted metadata");
	std::weak_ptr<const std::vector<std::uint8_t>> observer;
	{
		auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(MakeLayout(false), MakeLayout(true)));
		observer = pin;
		sgre::text::VectorFont font(pin);
		pin.reset();
		retained = font.NormalizationFont().PrepareShapePlan(NullLanguageConfiguration(), NullLanguageProperties(), {}, {});
		owned = retained.Snapshot();
		Require(!observer.expired(), "actual font is the live resource owner");
	}
	Require(observer.expired() && owned.Tables.SubstitutionLookupCount == 1 && owned.LookupPlan.Substitutions.Lookups.size() == 1, "owned snapshot remains metadata without a hidden resource, font or face pin");
	RequireFailure([&] {
		(void)retained.Snapshot();
	},
	    "borrowed face has expired");
	RequireFailure([&] {
		(void)retained.SharesMetadataWith(retained);
	},
	    "borrowed face has expired");
}

static void DamageCoverageCount(std::vector<std::uint8_t>& layout) {
	const auto list = ReadWord(layout, 8);
	const auto lookup = std::size_t(list) + ReadWord(layout, list + 2);
	const auto subtable = lookup + ReadWord(layout, lookup + 6);
	const auto coverage = subtable + ReadWord(layout, subtable + 2);
	vector_font_test::WriteWord(layout, coverage + 2, 0xffff);
}

static void TestActualRepairsAndCountedNullLookup() {
	auto substitution = MakeLayout(false);
	auto positioning = MakeLayout(true);
	DamageCoverageCount(substitution);
	DamageCoverageCount(positioning);
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(substitution, positioning));
	const auto original_bytes = *pin;
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::BlobFontFace face(pin);
	const auto repaired = face.ShapePlans(reader).Acquire(NullLanguageConfiguration(), NullLanguageProperties(), {}, {});
	Require(face.LayoutCache()->Substitution().Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && face.LayoutCache()->Positioning().Status() == sgre::text::GlyphPositioningBlobStatus::Repaired, "genuine face cache owns the two repaired admitted blobs");
	Require(*pin == original_bytes && ReadWord(face.LayoutCache()->Substitution().Bytes(), 58) == 0 && ReadWord(face.LayoutCache()->Positioning().Bytes(), 58) == 0, "repair clears actual coverage-offset fields without editing the immutable input");
	RequireSingleSchedules(repaired.Snapshot().LookupPlan);
	RequireFailure([&] {
		const sgre::resource::OpenTypeLayoutFont raw(reader);
		(void)raw;
	},
	    "range exceeds its table");

	substitution = MakeLayout(false);
	// Target is inside the blob but too short for a lookup header, so its counted offset is repaired.
	const auto list = ReadWord(substitution, 8);
	vector_font_test::WriteWord(substitution, std::size_t(list) + 2, static_cast<std::uint16_t>(substitution.size() - list - 2));
	const auto gap_pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(substitution, MakeLayout(true)));
	const sgre::resource::SfntFont gap_reader(*gap_pin);
	sgre::text::BlobFontFace gap_face(gap_pin);
	const auto gap = gap_face.ShapePlans(gap_reader).Acquire(NullLanguageConfiguration(), NullLanguageProperties(), {}, {});
	Require(gap_face.LayoutCache()->Substitution().Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && ReadWord(gap_face.LayoutCache()->Substitution().Bytes(), 46) == 0 && gap.Snapshot().Tables.SubstitutionLookupCount == 1, "repaired null lookup retains its native counted slot and actual cache provenance");
	RequireSingleSchedules(gap.Snapshot().LookupPlan);
}

static void TestRejectedTablesAndEmptyRoots() {
	const std::array<std::uint8_t, 2> invalid_substitution{0, 1};
	std::vector<std::uint8_t> empty_positioning(10);
	vector_font_test::WriteDoubleWord(empty_positioning, 0, 0x10000);
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(invalid_substitution, empty_positioning));
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::BlobFontFace face(pin);
	const auto snapshot = face.ShapePlans(reader).Acquire(NullLanguageConfiguration(), NullLanguageProperties(), {}, {}).Snapshot();
	Require(face.LayoutCache()->Substitution().Status() == sgre::text::GlyphSubstitutionBlobStatus::Rejected && snapshot.Tables.SubstitutionBytes == 0 && snapshot.Tables.PositioningBytes == 10, "rejected actual blob has an effective empty view while valid zero roots remain admitted");
	Require(snapshot.LookupPlan.Substitutions.Lookups.empty() && snapshot.LookupPlan.Positioning.Lookups.empty() && snapshot.LookupPlan.Substitutions.StageEnds == std::vector<std::size_t>{0, 0}, "empty effective tables preserve owned stage pauses without invented lookup payloads");
}

static void TestActualDefinitionSuppression() {
	std::vector<std::uint8_t> definition(0x382);
	vector_font_test::WriteDoubleWord(definition, 0, 0x10000);
	vector_font_test::WriteWord(definition, 4, 12);
	vector_font_test::WriteWord(definition, 12, 1);
	vector_font_test::WriteWord(definition, 14, 1);
	vector_font_test::WriteWord(definition, 16, 1);
	vector_font_test::WriteWord(definition, 18, 3);
	std::vector<std::uint8_t> substitution(0x310a);
	std::vector<std::uint8_t> positioning(0xb586);
	vector_font_test::WriteDoubleWord(substitution, 0, 0x10000);
	vector_font_test::WriteDoubleWord(positioning, 0, 0x10000);
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(substitution, positioning, definition));
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::BlobFontFace face(pin);
	const auto snapshot = face.ShapePlans(reader).Acquire(NullLanguageConfiguration(), NullLanguageProperties(), {}, {}).Snapshot();
	Require(snapshot.Tables.DefinitionSuppressed && snapshot.Tables.DefinitionBytes == 0x382 && snapshot.Tables.EffectiveDefinitionBytes == 0 && !face.HasGlyphClasses(), "plan reader uses the native effective suppressed GDEF view rather than retained raw class bytes");
	Require(face.LayoutCache()->Definition().Bytes()[19] == 3 && snapshot.LookupPlan.Substitutions.Lookups.empty(), "suppression preserves the real owned blob and does not fabricate lookup execution");
}

static void TestUnsupportedRequestsDoNotPublishPlans() {
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(MakeLayout(false), MakeLayout(true)));
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::BlobFontFace face(pin);
	auto& cache = face.ShapePlans(reader);
	auto properties = NullLanguageProperties();
	properties.UserIdentities[1] = 1;
	RequireFailure([&] {
		(void)cache.Acquire(NullLanguageConfiguration(), properties, {}, {});
	},
	    "user identities remain unsupported");
	properties = NullLanguageProperties();
	properties.UserIdentities[0] = 0x100000000;
	RequireFailure([&] {
		(void)cache.Acquire(NullLanguageConfiguration(), properties, {}, {});
	},
	    "user identities remain unsupported");
	properties = NullLanguageProperties();
	properties.DirectionWord = 0;
	RequireFailure([&] {
		(void)cache.Acquire(NullLanguageConfiguration(), properties, {}, {});
	},
	    "properties do not match");
	RequireFailure([&] {
		(void)cache.Acquire(sgre::text::ResolveVectorFontConfiguration(0, "en"), NullLanguageProperties(), {}, {});
	},
	    "actual canonical en language identity");
	const std::array<sgre::text::VectorFontShapePlanFeature, 1> invalid_features{GlobalFeature(0x80202020, 1)};
	RequireFailure([&] {
		(void)cache.Acquire(NullLanguageConfiguration(), NullLanguageProperties(), invalid_features, {});
	},
	    "ASCII profile");
	Require(cache.CachedPlanCount() == 0, "unsupported input cannot publish a plan reference");

	auto variations = MakeLayout(false);
	variations.insert(variations.begin() + 10, 4, 0);
	vector_font_test::WriteDoubleWord(variations, 0, 0x10001);
	for (const std::size_t field : {4U, 6U, 8U}) {
		vector_font_test::WriteWord(variations, field, static_cast<std::uint16_t>(ReadWord(variations, field) + 4));
	}
	const auto variation_offset = variations.size();
	vector_font_test::WriteDoubleWord(variations, 10, static_cast<std::uint32_t>(variation_offset));
	variations.resize(variation_offset + 8);
	vector_font_test::WriteDoubleWord(variations, variation_offset, 0x10000);
	const auto variation_pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(variations, MakeLayout(true)));
	const sgre::resource::SfntFont variation_reader(*variation_pin);
	sgre::text::BlobFontFace variation_face(variation_pin);
	auto& variation_cache = variation_face.ShapePlans(variation_reader);
	RequireFailure([&] {
		(void)variation_cache.Acquire(NullLanguageConfiguration(), NullLanguageProperties(), {}, {});
	},
	    "unsupported layout feature variations");
	Require(variation_cache.CachedPlanCount() == 0, "unimplemented variable feature semantics do not publish a plan");
}

static void TestAuthoredCapacityRetainsExistingPlans() {
	const auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont(MakeLayout(false), MakeLayout(true)));
	const sgre::resource::SfntFont reader(*pin);
	sgre::text::BlobFontFace face(pin);
	auto& cache = face.ShapePlans(reader);
	const auto configuration = NullLanguageConfiguration();
	const auto properties = NullLanguageProperties();
	const std::array<sgre::text::VectorFontShapePlanFeature, 1> first_features{GlobalFeature(0x63636d70, 1)};
	const auto retained = cache.Acquire(configuration, properties, first_features, {});
	for (std::uint32_t value = 2; value <= 64; ++value) {
		const std::array<sgre::text::VectorFontShapePlanFeature, 1> features{GlobalFeature(0x63636d70, value)};
		(void)cache.Acquire(configuration, properties, features, {});
	}
	const std::array<sgre::text::VectorFontShapePlanFeature, 1> overflow{GlobalFeature(0x63636d70, 65)};
	RequireFailure([&] {
		(void)cache.Acquire(configuration, properties, overflow, {});
	},
	    "bounded face cache count");
	Require(cache.CachedPlanCount() == 64 && retained.SharesMetadataWith(cache.Acquire(configuration, properties, first_features, {})), "authored safety bound preserves existing exact-key entries rather than inventing eviction");
}

int main() {
	try {
		TestActualFaceAndEffectiveMetadata();
		TestCompleteKeysAndInputOwnership();
		TestLanguageDirectionAndSameFaceIdentity();
		TestRangesAndNonemptyCoordinatesBypass();
		TestForeignReaderRejectedBeforePreparation();
		TestBorrowExpiryWithoutResourcePin();
		TestActualRepairsAndCountedNullLookup();
		TestRejectedTablesAndEmptyRoots();
		TestActualDefinitionSuppression();
		TestUnsupportedRequestsDoNotPublishPlans();
		TestAuthoredCapacityRetainsExistingPlans();
		std::cout << "VectorFont same-face metadata/cache: eleven groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
