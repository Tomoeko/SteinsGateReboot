#include "sgre/text/vector_font_shape_plan.hpp"

#include "sgre/resource/sfnt.hpp"
#include "sgre/text/blob_font_face.hpp"
#include "sgre/text/vector_font.hpp"
#include "vector_font_fixture.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("shape-plan diagnostic expected rejection containing " + std::string(expected));
}

struct Fixture {
	std::shared_ptr<const std::vector<std::uint8_t>> Bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	std::unique_ptr<sgre::text::BlobFontFace> Face = std::make_unique<sgre::text::BlobFontFace>(Bytes);
	sgre::resource::SfntFont Reader{Face->WholeFontBytes()};
	sgre::text::VectorFontConfiguration Configuration;
	sgre::text::VectorFontShapePlanProperties Properties;

	Fixture() {
		Configuration.HasLanguage = false;
		Configuration.Language.clear();
		Properties.DirectionWord = 4;
		Properties.ScriptTag = 0x4c61746e;
		Require(Face->PrepareLayout(), "actual full-SFNT face layout must prepare");
	}

	sgre::text::VectorFontShapePlanCache& Cache() {
		return Face->ShapePlans(Reader);
	}
};

static void TestActualCacheOrderAndNoObservationRetain() {
	Fixture fixture;
	auto& cache = fixture.Cache();
	std::array<sgre::text::VectorFontShapePlan, 4> calls;
	calls[0] = cache.Acquire(fixture.Configuration, fixture.Properties, {}, {});
	calls[1] = cache.Acquire(fixture.Configuration, fixture.Properties, {}, {});
	auto second_configuration = fixture.Configuration;
	second_configuration.Direction = sgre::text::VectorFontDirection::RightToLeft;
	auto second_properties = fixture.Properties;
	second_properties.DirectionWord = 5;
	calls[2] = cache.Acquire(second_configuration, second_properties, {}, {});
	calls[3] = cache.Acquire(fixture.Configuration, fixture.Properties, {}, {});
	Require(calls[0].SharesMetadataWith(calls[1]) && calls[0].SharesMetadataWith(calls[3]) && !calls[0].SharesMetadataWith(calls[2]), "actual cache returns A,A,B,A metadata identities");
	Require(calls[0].MetadataStrongReferenceCount() == 4 && calls[2].MetadataStrongReferenceCount() == 2, "actual caller plus cache strong counts are observed without a self copy");

	const auto values = cache.ObserveCachedMetadata();
	const auto a_probe = calls[0].ObserveMetadataOwnership();
	const auto b_probe = calls[2].ObserveMetadataOwnership();
	Require(values.size() == 2 && values[0].Properties == second_properties && values[1].Properties == fixture.Properties, "real cache iteration order keeps newest B before A");
	Require(values[0].MetadataStrongReferences == 2 && values[1].MetadataStrongReferences == 4 && a_probe.StrongReferenceCount() == 4 && b_probe.StrongReferenceCount() == 2, "diagnostic value and weak probes add no strong plan references");
	calls[0] = {};
	calls[1] = {};
	calls[2] = {};
	calls[3] = {};
	Require(a_probe.StrongReferenceCount() == 1 && b_probe.StrongReferenceCount() == 1, "only actual cache strong ownership survives caller release");
	fixture.Face.reset();
	Require(a_probe.HasExpired() && b_probe.HasExpired() && a_probe.StrongReferenceCount() == 0 && b_probe.StrongReferenceCount() == 0, "weak diagnostics do not keep cache metadata alive through face teardown");
	Require(values[0].Properties == second_properties && values[1].MetadataStrongReferences == 4, "copied observations remain independent past face teardown");
}

static void TestUncachedCallerSurvivesFaceWithoutMetadataAccess() {
	Fixture fixture;
	auto& cache = fixture.Cache();
	const std::array<std::int32_t, 1> coordinate{0};
	auto call = cache.Acquire(fixture.Configuration, fixture.Properties, {}, coordinate);
	const auto ownership = call.ObserveMetadataOwnership();
	Require(cache.ObserveCachedMetadata().empty() && call.MetadataStrongReferenceCount() == 1 && !call.Snapshot().Cached, "nonempty zero coordinate keeps an actual caller-only plan outside cache");
	fixture.Face.reset();
	Require(!ownership.HasExpired() && ownership.StrongReferenceCount() == 1, "only caller metadata survives actual face teardown");
	RequireFailure([&] {
		static_cast<void>(call.Snapshot());
	},
	    "borrowed face has expired");
	RequireFailure([&] {
		static_cast<void>(call.MetadataStrongReferenceCount());
	},
	    "borrowed face has expired");
	RequireFailure([&] {
		static_cast<void>(call.ObserveMetadataOwnership());
	},
	    "borrowed face has expired");
	call = {};
	Require(ownership.HasExpired() && ownership.StrongReferenceCount() == 0, "weak control block remains observable without retaining the released metadata");
}

static void TestEmptyAndExplicitCallerCopyCounts() {
	const sgre::text::VectorFontShapePlan empty;
	const sgre::text::VectorFontShapePlanMetadataOwnership empty_probe;
	Require(empty_probe.HasExpired() && empty_probe.StrongReferenceCount() == 0, "default weak observation has no owner");
	RequireFailure([&] {
		static_cast<void>(empty.MetadataStrongReferenceCount());
	},
	    "no admitted metadata");
	Fixture fixture;
	auto first = fixture.Cache().Acquire(fixture.Configuration, fixture.Properties, {}, {});
	auto second = first;
	const auto observation = first.ObserveMetadataOwnership();
	Require(first.MetadataStrongReferenceCount() == 3 && second.MetadataStrongReferenceCount() == 3 && observation.StrongReferenceCount() == 3, "a deliberate caller copy contributes its one actual C++ strong reference");
	second = {};
	Require(first.MetadataStrongReferenceCount() == 2, "removing that caller copy restores caller-plus-cache ownership");
}

int main() {
	try {
		TestActualCacheOrderAndNoObservationRetain();
		TestUncachedCallerSurvivesFaceWithoutMetadataAccess();
		TestEmptyAndExplicitCallerCopyCounts();
		std::cout << "Shape-plan owner diagnostic fixture groups PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
