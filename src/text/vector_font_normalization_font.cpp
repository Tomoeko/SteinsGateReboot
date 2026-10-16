#include "sgre/text/vector_font_normalization_font.hpp"

#include "sgre/text/nominal_glyph.hpp"
#include "sgre/text/vector_font_metric_services.hpp"

#include <stdexcept>
#include <utility>

namespace sgre::text {

	static std::shared_ptr<const std::vector<std::uint8_t>> ValidateNormalizationResource(std::shared_ptr<const std::vector<std::uint8_t>> bytes, const resource::SfntFont& font) {
		if (!bytes || bytes->empty()) {
			throw std::runtime_error("normalization font requires a live immutable resource pin");
		}
		for (const auto& entry : font.Tables()) {
			const auto table = font.FindTable(entry.Tag);
			if (entry.Offset > bytes->size() || entry.Size > bytes->size() - entry.Offset || !table || table->size() != entry.Size || table->data() != bytes->data() + entry.Offset) {
				throw std::runtime_error("normalization font reader does not borrow the supplied resource pin");
			}
		}

		return bytes;
	}

	VectorFontNormalizationFont::VectorFontNormalizationFont(std::shared_ptr<const std::vector<std::uint8_t>> bytes, const resource::SfntFont& font)
	    : m_Font(font), m_Face(ValidateNormalizationResource(std::move(bytes), font)), m_Variations(font) {
	}

	const resource::SfntFont& VectorFontNormalizationFont::Font() const {
		return m_Font;
	}

	void VectorFontNormalizationFont::BindMetricServices(const VectorFontMetricServices& actual_services) {
		if (m_MetricServices || &actual_services.Font() != &m_Font) {
			throw std::runtime_error("normalization metric services require their same actual live reader and one-time binding");
		}

		m_MetricServices = &actual_services;
	}

	const VectorFontMetricServices& VectorFontNormalizationFont::MetricServices() const {
		if (!m_MetricServices) {
			throw std::runtime_error("normalization metric services require actual VectorFont applied-domain admission");
		}

		return *m_MetricServices;
	}

	BlobFontFace& VectorFontNormalizationFont::Face() {
		return m_Face;
	}

	VectorFontShapePlan VectorFontNormalizationFont::PrepareShapePlan(const VectorFontConfiguration& configuration, const VectorFontShapePlanProperties& properties, std::span<const VectorFontShapePlanFeature> features, std::span<const std::int32_t> coordinates) {
		return m_Face.ShapePlans(m_Font).Acquire(configuration, properties, features, coordinates);
	}

	std::optional<std::uint32_t> VectorFontNormalizationFont::NominalGlyph(std::uint32_t codepoint) const {
		return QueryVectorFontNominalGlyph(m_Font, codepoint);
	}

	std::optional<std::uint32_t> VectorFontNormalizationFont::VariationGlyph(std::uint32_t codepoint, std::uint32_t selector) const {
		return m_Variations.Query(codepoint, selector);
	}

} // namespace sgre::text
