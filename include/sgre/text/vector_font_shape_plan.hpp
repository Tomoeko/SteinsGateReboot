#pragma once

#include "sgre/resource/opentype_layout.hpp"
#include "sgre/text/shaping_lookup_plan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::text {

	class BlobFontFace;

	// Logical fields of the native 32-byte key; never serialize the C++ object representation.
	struct VectorFontShapePlanProperties {
		std::uint32_t DirectionWord = 0;
		std::uint32_t ScriptTag = 0;
		// Supplied canonical token identity, not a hash of a language string or a fabricated default.
		std::uint64_t LanguageIdentity = 0;
		std::array<std::uint64_t, 2> UserIdentities{};
		bool operator==(const VectorFontShapePlanProperties&) const = default;
	};

	struct VectorFontShapePlanFeature {
		std::uint32_t Tag = 0;
		std::uint32_t Value = 0;
		std::uint32_t StartCluster = 0;
		std::uint32_t EndCluster = 0;
		bool operator==(const VectorFontShapePlanFeature&) const = default;
	};

	struct VectorFontShapePlanTables {
		std::uint32_t DefinitionBytes = 0;
		std::uint32_t EffectiveDefinitionBytes = 0;
		std::uint32_t SubstitutionBytes = 0;
		std::uint32_t PositioningBytes = 0;
		std::uint32_t SubstitutionLookupCount = 0;
		std::uint32_t PositioningLookupCount = 0;
		bool DefinitionSuppressed = false;
	};

	// An owned metadata snapshot remains a value after face teardown; it cannot execute a lookup.
	struct VectorFontShapePlanSnapshot {
		VectorFontShapePlanProperties Properties;
		std::vector<VectorFontShapePlanFeature> Features;
		std::vector<std::int32_t> Coordinates;
		VectorFontShapePlanTables Tables;
		VectorFontLookupPlan LookupPlan;
		bool Cached = false;
	};

	struct VectorFontShapePlanData;

	// Explicit diagnostic values; copies contain keys and counts, never plan/face/font ownership.
	struct VectorFontShapePlanCachedMetadataObservation {
		VectorFontShapePlanProperties Properties;
		std::vector<VectorFontShapePlanFeature> Features;
		std::vector<std::int32_t> Coordinates;
		std::size_t MetadataStrongReferences = 0;
	};

	// Keeps only the existing metadata control block weakly, including after face/data retirement.
	// It observes C++ strong ownership; it cannot access metadata, resources or an expired face.
	class VectorFontShapePlanMetadataOwnership {
	public:
		VectorFontShapePlanMetadataOwnership() = default;
		[[nodiscard]] std::size_t StrongReferenceCount() const noexcept;
		[[nodiscard]] bool HasExpired() const noexcept;

	private:
		friend class VectorFontShapePlan;
		explicit VectorFontShapePlanMetadataOwnership(const std::shared_ptr<const VectorFontShapePlanData>& data);
		std::weak_ptr<const VectorFontShapePlanData> m_Data;
	};

	// Native call and face-cache references share plan metadata, without retaining the face or font.
	// Serialize all access with the borrowed face's preparation and destruction.
	class VectorFontShapePlan {
	public:
		VectorFontShapePlan() = default;
		[[nodiscard]] VectorFontShapePlanSnapshot Snapshot() const;
		[[nodiscard]] bool SharesMetadataWith(const VectorFontShapePlan& other) const;
		void RequireFace(const BlobFontFace& actual_face) const;
		// Serialized non-frame diagnostics: no caller-handle copy or extra strong owner.
		[[nodiscard]] std::size_t MetadataStrongReferenceCount() const;
		[[nodiscard]] VectorFontShapePlanMetadataOwnership ObserveMetadataOwnership() const;

	private:
		friend class VectorFontShapePlanCache;
		explicit VectorFontShapePlan(std::shared_ptr<const VectorFontShapePlanData> data);
		void RequireLiveFace() const;
		std::shared_ptr<const VectorFontShapePlanData> m_Data;
	};

	// Same-face metadata and plan cache only. GSUB/GPOS execution and positions remain unsupported.
	class VectorFontShapePlanCache {
	public:
		class ConstructionKey {
		private:
			friend class BlobFontFace;
			ConstructionKey() = default;
		};
		VectorFontShapePlanCache(ConstructionKey, BlobFontFace& actual_face, std::uint16_t glyph_count);
		~VectorFontShapePlanCache();
		VectorFontShapePlanCache(const VectorFontShapePlanCache&) = delete;
		VectorFontShapePlanCache& operator=(const VectorFontShapePlanCache&) = delete;
		VectorFontShapePlanCache(VectorFontShapePlanCache&&) = delete;
		VectorFontShapePlanCache& operator=(VectorFontShapePlanCache&&) = delete;
		// Exact input words are copied. Ranges and every nonempty coordinate vector bypass caching.
		[[nodiscard]] VectorFontShapePlan Acquire(const VectorFontConfiguration& configuration, const VectorFontShapePlanProperties& properties, std::span<const VectorFontShapePlanFeature> features, std::span<const std::int32_t> coordinates);
		[[nodiscard]] std::size_t CachedPlanCount() const;
		// Allocating value observation of the real cache iteration order, with no strong metadata pins.
		[[nodiscard]] std::vector<VectorFontShapePlanCachedMetadataObservation> ObserveCachedMetadata() const;

	private:
		static resource::OpenTypeLayoutFont ReadAdmittedLayout(const BlobFontFace& actual_face, std::uint16_t glyph_count, resource::OpenTypeLayoutKind kind);
		struct Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::text
