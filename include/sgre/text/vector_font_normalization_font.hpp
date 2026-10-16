#pragma once

#include "sgre/resource/sfnt.hpp"
#include "sgre/text/blob_font_face.hpp"
#include "sgre/text/neutral_glyph_preparation.hpp"
#include "sgre/text/variation_glyph.hpp"
#include "sgre/text/vector_font_shape_plan.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace sgre::text {
	class VectorFont;
	class VectorFontMetricServices;

	// Engine-default Unicode glyph services; the genuine reader outlives this borrowed userdata binding.
	// VectorFont owns this binding after its reader, so callbacks and the face/cache retire before that reader.
	class VectorFontNormalizationFont {
	public:
		VectorFontNormalizationFont(std::shared_ptr<const std::vector<std::uint8_t>> bytes, const resource::SfntFont& font);
		VectorFontNormalizationFont(const VectorFontNormalizationFont&) = delete;
		VectorFontNormalizationFont& operator=(const VectorFontNormalizationFont&) = delete;
		VectorFontNormalizationFont(VectorFontNormalizationFont&&) = delete;
		VectorFontNormalizationFont& operator=(VectorFontNormalizationFont&&) = delete;
		[[nodiscard]] const resource::SfntFont& Font() const;
		// Borrows the same actual VectorFont metric owner after explicit applied-domain admission.
		[[nodiscard]] const VectorFontMetricServices& MetricServices() const;
		// Face/cache access and shared descriptor lifetimes retain the caller serialization contract.
		[[nodiscard]] BlobFontFace& Face();
		// Metadata only; all native property/feature/coordinate words are explicit inputs.
		[[nodiscard]] VectorFontShapePlan PrepareShapePlan(const VectorFontConfiguration& configuration, const VectorFontShapePlanProperties& properties, std::span<const VectorFontShapePlanFeature> features, std::span<const std::int32_t> coordinates);
		[[nodiscard]] std::optional<std::uint32_t> NominalGlyph(std::uint32_t codepoint) const;
		[[nodiscard]] std::optional<std::uint32_t> VariationGlyph(std::uint32_t codepoint, std::uint32_t selector) const;
		// Requires the same owned buffer and its preceding Unicode/cluster/direction phases.
		// Successful cache admission supplies the actual neutral fallback and following GDEF entry.
		[[nodiscard]] NormalizedGlyphContent PrepareNeutralGlyphs(UnicodeNormalizationBuffer& buffer, const VectorFontFeatureMap& features, NeutralGlyphPreparationContext context, std::span<const VectorFontFeatureRequest> requests = {});

	private:
		friend class VectorFont;
		void BindMetricServices(const VectorFontMetricServices& actual_services);
		const resource::SfntFont& m_Font;
		BlobFontFace m_Face;
		VectorFontVariationMap m_Variations;
		const VectorFontMetricServices* m_MetricServices = nullptr;
	};

} // namespace sgre::text
