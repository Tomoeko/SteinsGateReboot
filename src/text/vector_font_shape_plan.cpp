#include "sgre/text/vector_font_shape_plan.hpp"

#include "sgre/text/blob_font_face.hpp"
#include "sgre/text/blob_font_layout_cache.hpp"
#include "sgre/text/shaping_features.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::text {

	// Authored safety bounds, not native cache capacity or an inferred eviction policy.
	static constexpr std::size_t kMaximumCachedShapePlans = 64;
	static constexpr std::size_t kMaximumCachedMetadataRecords = 262144;
	static constexpr std::size_t kMaximumShapePlanFeatures = 4096;
	static constexpr std::size_t kMaximumShapePlanCoordinates = 1024;

	struct VectorFontShapePlanFaceLifetime {
		const BlobFontFace* Face = nullptr;
	};

	struct VectorFontShapePlanData {
		std::weak_ptr<VectorFontShapePlanFaceLifetime> Lifetime;
		VectorFontShapePlanSnapshot Metadata;
	};

	static std::uint32_t ShapePlanScriptTag(VectorFontScript script) {
		switch (script) {
		case VectorFontScript::Latin:
			return 0x4c61746e;
		case VectorFontScript::Cyrillic:
			return 0x4379726c;
		case VectorFontScript::Han:
			return 0x48616e69;
		default:
			throw std::runtime_error("VectorFont shape plan requires the recovered neutral script");
		}
	}

	static void ValidateShapePlanProperties(const VectorFontConfiguration& configuration, const VectorFontShapePlanProperties& properties) {
		if (properties.ScriptTag != ShapePlanScriptTag(configuration.Script) || properties.DirectionWord != static_cast<std::uint32_t>(configuration.Direction)) {
			throw std::runtime_error("VectorFont shape plan properties do not match the actual configuration");
		}
		if (configuration.Direction != VectorFontDirection::LeftToRight && configuration.Direction != VectorFontDirection::RightToLeft && configuration.Direction != VectorFontDirection::TopToBottom) {
			throw std::runtime_error("VectorFont shape plan direction is outside the recovered neutral profile");
		}
		if (configuration.HasLanguage) {
			if (configuration.Language != "en" || properties.LanguageIdentity == 0) {
				throw std::runtime_error("VectorFont shape plan requires the supplied actual canonical en language identity");
			}
		} else if (!configuration.Language.empty() || properties.LanguageIdentity != 0) {
			throw std::runtime_error("VectorFont shape plan absent language requires its original null identity");
		}
		if (properties.UserIdentities != std::array<std::uint64_t, 2>{}) {
			throw std::runtime_error("VectorFont shape plan nonzero property user identities remain unsupported");
		}
	}

	static std::vector<VectorFontFeatureRequest> ShapePlanFeatureRequests(std::span<const VectorFontShapePlanFeature> features) {
		if (features.size() > kMaximumShapePlanFeatures) {
			throw std::runtime_error("VectorFont shape plan exceeds its bounded feature count");
		}

		std::vector<VectorFontFeatureRequest> requests;
		requests.reserve(features.size());
		for (const auto& feature : features) {
			VectorFontFeatureRequest request;
			for (std::size_t index = 0; index < 4; ++index) {
				const auto byte = static_cast<std::uint8_t>(feature.Tag >> ((3 - index) * 8));
				if (byte > 0x7f) {
					throw std::runtime_error("VectorFont shape plan feature tag is outside the existing ASCII profile");
				}
				request.Tag.push_back(static_cast<char>(byte));
			}
			request.Value = feature.Value;
			request.StartCluster = feature.StartCluster;
			request.EndCluster = feature.EndCluster;
			requests.push_back(std::move(request));
		}

		return requests;
	}

	static bool ShapePlanCacheEligible(std::span<const VectorFontShapePlanFeature> features, std::span<const std::int32_t> coordinates) {
		if (!coordinates.empty()) {
			return false;
		}

		return std::all_of(features.begin(), features.end(), [](const auto& feature) {
			return feature.StartCluster == 0 && feature.EndCluster == 0xffffffff;
		});
	}

	static std::size_t ShapePlanRecordCount(const VectorFontShapePlanSnapshot& metadata) {
		return metadata.Features.size() + metadata.Coordinates.size() + metadata.LookupPlan.FeatureMap.Features.size() + metadata.LookupPlan.Substitutions.Lookups.size() + metadata.LookupPlan.Substitutions.StageEnds.size() + metadata.LookupPlan.Positioning.Lookups.size() + metadata.LookupPlan.Positioning.StageEnds.size();
	}

	resource::OpenTypeLayoutFont VectorFontShapePlanCache::ReadAdmittedLayout(const BlobFontFace& actual_face, std::uint16_t glyph_count, resource::OpenTypeLayoutKind kind) {
		const auto* cache = actual_face.LayoutCache();
		if (actual_face.LayoutState() != FontFaceLayoutState::Ready || !cache) {
			throw std::runtime_error("VectorFont shape plan requires its actual prepared face layout cache");
		}
		const auto& definition = cache->Definition();
		const auto effective_definition = definition.IsSuppressed() ? std::span<const std::uint8_t>() : definition.Bytes();
		const auto layout = kind == resource::OpenTypeLayoutKind::Substitution ? cache->Substitution().Bytes() : cache->Positioning().Bytes();
		resource::OpenTypeLayoutFont reader(resource::OpenTypeLayoutFont::AdmittedTablesKey{}, glyph_count, kind, layout, effective_definition);
		const auto count = kind == resource::OpenTypeLayoutKind::Substitution ? cache->SubstitutionLookups().size() : cache->PositioningLookups().size();
		if (reader.Lookups().size() != count) {
			throw std::runtime_error("VectorFont shape plan metadata count differs from its actual face accelerators");
		}

		return reader;
	}

	struct VectorFontShapePlanCache::Implementation {
		BlobFontFace& Face;
		std::shared_ptr<VectorFontShapePlanFaceLifetime> Lifetime;
		resource::OpenTypeLayoutFont Substitutions;
		resource::OpenTypeLayoutFont Positioning;
		VectorFontShapePlanTables Tables;
		std::vector<std::shared_ptr<const VectorFontShapePlanData>> Plans;
		std::size_t RetainedRecords = 0;

		Implementation(BlobFontFace& actual_face, std::uint16_t glyph_count)
		    : Face(actual_face), Lifetime(std::make_shared<VectorFontShapePlanFaceLifetime>()), Substitutions(ReadAdmittedLayout(actual_face, glyph_count, resource::OpenTypeLayoutKind::Substitution)), Positioning(ReadAdmittedLayout(actual_face, glyph_count, resource::OpenTypeLayoutKind::Positioning)) {
			Lifetime->Face = &actual_face;
			const auto& cache = *actual_face.LayoutCache();
			Tables.DefinitionBytes = cache.Definition().Length();
			Tables.DefinitionSuppressed = cache.Definition().IsSuppressed();
			Tables.EffectiveDefinitionBytes = Tables.DefinitionSuppressed ? 0 : Tables.DefinitionBytes;
			Tables.SubstitutionBytes = cache.Substitution().Length();
			Tables.PositioningBytes = cache.Positioning().Length();
			Tables.SubstitutionLookupCount = static_cast<std::uint32_t>(cache.SubstitutionLookups().size());
			Tables.PositioningLookupCount = static_cast<std::uint32_t>(cache.PositioningLookups().size());
		}

		~Implementation() {
			Lifetime->Face = nullptr;
		}
	};

	VectorFontShapePlanCache::VectorFontShapePlanCache(ConstructionKey, BlobFontFace& actual_face, std::uint16_t glyph_count)
	    : m_Implementation(std::make_unique<Implementation>(actual_face, glyph_count)) {
	}

	VectorFontShapePlanCache::~VectorFontShapePlanCache() = default;

	VectorFontShapePlan VectorFontShapePlanCache::Acquire(const VectorFontConfiguration& configuration, const VectorFontShapePlanProperties& properties, std::span<const VectorFontShapePlanFeature> features, std::span<const std::int32_t> coordinates) {
		ValidateShapePlanProperties(configuration, properties);
		if (coordinates.size() > kMaximumShapePlanCoordinates) {
			throw std::runtime_error("VectorFont shape plan exceeds its bounded coordinate count");
		}
		const auto requests = ShapePlanFeatureRequests(features);
		// Validate collector admission before any cache hit can obscure an unsupported request.
		static_cast<void>(CollectVectorFontFeatures(configuration, requests));
		auto& owner = *m_Implementation;
		if (owner.Lifetime->Face != &owner.Face) {
			throw std::runtime_error("VectorFont shape plan requires its same live face");
		}

		const bool cache_eligible = ShapePlanCacheEligible(features, coordinates);
		if (cache_eligible) {
			for (const auto& plan : owner.Plans) {
				const auto& metadata = plan->Metadata;
				if (metadata.Properties == properties && std::ranges::equal(metadata.Features, features) && std::ranges::equal(metadata.Coordinates, coordinates)) {
					return VectorFontShapePlan(plan);
				}
			}
			if (owner.Plans.size() >= kMaximumCachedShapePlans) {
				throw std::runtime_error("VectorFont shape plan exceeds its bounded face cache count");
			}
		}

		auto candidate = std::make_shared<VectorFontShapePlanData>();
		candidate->Lifetime = owner.Lifetime;
		auto& metadata = candidate->Metadata;
		metadata.Properties = properties;
		metadata.Features.assign(features.begin(), features.end());
		metadata.Coordinates.assign(coordinates.begin(), coordinates.end());
		metadata.Tables = owner.Tables;
		metadata.LookupPlan = PrepareVectorFontLookupPlan(owner.Substitutions, owner.Positioning, configuration, requests);
		metadata.Cached = cache_eligible;
		if (cache_eligible) {
			const auto records = ShapePlanRecordCount(metadata);
			if (records > kMaximumCachedMetadataRecords - owner.RetainedRecords) {
				throw std::runtime_error("VectorFont shape plan exceeds its bounded retained metadata count");
			}
			// Native publication prepends the new cache node; serialized callers need no retry loop.
			owner.Plans.insert(owner.Plans.begin(), candidate);
			owner.RetainedRecords += records;
		}

		return VectorFontShapePlan(std::move(candidate));
	}

	std::size_t VectorFontShapePlanCache::CachedPlanCount() const {
		return m_Implementation->Plans.size();
	}

	VectorFontShapePlan::VectorFontShapePlan(std::shared_ptr<const VectorFontShapePlanData> data) : m_Data(std::move(data)) {
	}

	std::vector<VectorFontShapePlanCachedMetadataObservation> VectorFontShapePlanCache::ObserveCachedMetadata() const {
		const auto& owner = *m_Implementation;
		if (owner.Lifetime->Face != &owner.Face) {
			throw std::runtime_error("VectorFont shape plan diagnostic requires its same live face cache");
		}

		std::vector<VectorFontShapePlanCachedMetadataObservation> observations;
		observations.reserve(owner.Plans.size());
		for (const auto& plan : owner.Plans) {
			VectorFontShapePlanCachedMetadataObservation observation;
			observation.MetadataStrongReferences = static_cast<std::size_t>(plan.use_count());
			observation.Properties = plan->Metadata.Properties;
			observation.Features = plan->Metadata.Features;
			observation.Coordinates = plan->Metadata.Coordinates;
			observations.push_back(std::move(observation));
		}

		return observations;
	}

	std::size_t VectorFontShapePlan::MetadataStrongReferenceCount() const {
		RequireLiveFace();

		return static_cast<std::size_t>(m_Data.use_count());
	}

	VectorFontShapePlanMetadataOwnership VectorFontShapePlan::ObserveMetadataOwnership() const {
		RequireLiveFace();

		return VectorFontShapePlanMetadataOwnership(m_Data);
	}

	VectorFontShapePlanMetadataOwnership::VectorFontShapePlanMetadataOwnership(const std::shared_ptr<const VectorFontShapePlanData>& data) : m_Data(data) {
	}

	std::size_t VectorFontShapePlanMetadataOwnership::StrongReferenceCount() const noexcept {
		return static_cast<std::size_t>(m_Data.use_count());
	}

	bool VectorFontShapePlanMetadataOwnership::HasExpired() const noexcept {
		return m_Data.expired();
	}

	void VectorFontShapePlan::RequireLiveFace() const {
		if (!m_Data) {
			throw std::runtime_error("VectorFont shape plan has no admitted metadata");
		}
		const auto lifetime = m_Data->Lifetime.lock();
		if (!lifetime || !lifetime->Face) {
			throw std::runtime_error("VectorFont shape plan borrowed face has expired");
		}
	}

	VectorFontShapePlanSnapshot VectorFontShapePlan::Snapshot() const {
		RequireLiveFace();

		return m_Data->Metadata;
	}

	bool VectorFontShapePlan::SharesMetadataWith(const VectorFontShapePlan& other) const {
		RequireLiveFace();
		other.RequireLiveFace();

		return m_Data == other.m_Data;
	}

	void VectorFontShapePlan::RequireFace(const BlobFontFace& actual_face) const {
		RequireLiveFace();
		if (m_Data->Lifetime.lock()->Face != &actual_face) {
			throw std::runtime_error("VectorFont shape plan belongs to a different actual face");
		}
	}

} // namespace sgre::text
