#include "sgre/text/glyph_atlas_owner.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace sgre::text {

	static std::span<std::uint8_t> MappedBgraBytes(const scene::MutableTextureMapping& mapping) {
		// The texture mapping boundary exposes bytes; rasterization accesses their unsigned-char representation.
		static_assert(std::is_same_v<std::uint8_t, unsigned char>);

		return {reinterpret_cast<std::uint8_t*>(mapping.Bytes.data()), mapping.Bytes.size()};
	}

	class TextGlyphAtlasOwner::Implementation {
	public:
		explicit Implementation(scene::MutableTextureBackend& backend)
		    : Backend(backend) {
		}

		scene::MutableTextureBackend& Backend;
		GlyphAtlasPacking Packing;
		std::unique_ptr<scene::MutableTextureOwner> Texture;
		bool Filtering = false;
		bool Closed = false;
		bool Mutating = false;

		class Mutation {
		public:
			explicit Mutation(Implementation& owner)
			    : Owner(owner) {
				if (owner.Closed || owner.Mutating) {
					throw std::runtime_error("text glyph atlas requires its live context outside another mutation");
				}

				Owner.Mutating = true;
			}

			~Mutation() {
				Owner.Mutating = false;
			}

		private:
			Implementation& Owner;
		};

		class Mapping {
		public:
			explicit Mapping(scene::MutableTextureOwner& texture)
			    : Texture(texture), Bytes(texture.Map()) {
			}

			~Mapping() noexcept {
				// This context exclusively owns CpuShadow storage; its Unmap never invokes the platform backend.
				Texture.Unmap();
			}

			scene::MutableTextureOwner& Texture;
			scene::MutableTextureMapping Bytes;
		};

		void EnsureTexture() {
			if (Texture) {
				return;
			}

			scene::MutableTextureSpec spec;
			spec.Storage = scene::MutableTextureStorage::CpuShadow;
			spec.Description.OriginalFormat = 87;
			spec.Description.Width = Packing.WidthPixels;
			spec.Description.Height = Packing.HeightPixels;
			auto texture = std::make_unique<scene::MutableTextureOwner>(Backend, spec);
			texture->SetFiltering(0, 0);
			const auto filter = Filtering ? 1u : 0u;
			texture->SetFiltering(filter, filter);
			Texture = std::move(texture);
		}

		std::optional<CffAtlasGlyph> PrepareRaster(const PreparedCffGlyphRaster& raster) {
			raster.RequireValid();
			EnsureTexture();
			if (!PrepareGlyphAtlasRow(Packing, raster.Clip().WidthPixels, raster.Clip().HeightPixels)) {
				return {};
			}

			const Mapping mapping(*Texture);
			if (!mapping.Bytes.Succeeded) {
				return {};
			}

			GlyphAtlasCpuSurface surface;
			surface.Pixels = MappedBgraBytes(mapping.Bytes);
			surface.RowPitchBytes = Texture->RowPitch();

			return RasterizePreparedCffGlyphInAtlas(raster, Packing, surface);
		}
	};

	TextGlyphAtlasOwner::TextGlyphAtlasOwner(scene::MutableTextureBackend& backend)
	    : m_Implementation(std::make_unique<Implementation>(backend)) {
	}

	TextGlyphAtlasOwner::~TextGlyphAtlasOwner() = default;

	TextGlyphAtlasState TextGlyphAtlasOwner::State() const {
		const auto& state = *m_Implementation;
		TextGlyphAtlasState result;
		result.Packing = state.Packing;
		if (state.Texture) {
			result.Texture = state.Texture->State();
		}

		result.Filtering = state.Filtering;
		result.Closed = state.Closed;

		return result;
	}

	void TextGlyphAtlasOwner::SetFiltering(bool enabled) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		state.Filtering = enabled;
		if (state.Texture) {
			const auto filter = enabled ? 1u : 0u;
			state.Texture->SetFiltering(filter, filter);
		}
	}

	std::optional<CffAtlasGlyph> TextGlyphAtlasOwner::PrepareDefaultCffGlyph(const CffPreparedGlyph& glyph) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		const auto raster = PreparedCffGlyphRaster::Prepare(glyph);
		if (!raster) {
			return {};
		}

		return state.PrepareRaster(*raster);
	}

	std::optional<CffAtlasGlyph> TextGlyphAtlasOwner::PrepareCffRaster(const PreparedCffGlyphRaster& raster) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);

		return state.PrepareRaster(raster);
	}

	bool TextGlyphAtlasOwner::Bind(std::uint32_t slot, scene::MutableTextureStage stage) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		if (!state.Texture) {
			return false;
		}

		state.Texture->Bind(slot, stage);

		return true;
	}

	const scene::MutableTextureResource* TextGlyphAtlasOwner::Resource() const {
		const auto& state = *m_Implementation;

		return state.Texture ? &state.Texture->Resource() : nullptr;
	}

	scene::MutableTextureOwner& TextGlyphAtlasOwner::Texture() {
		auto& state = *m_Implementation;
		if (state.Closed || state.Mutating || !state.Texture) {
			throw std::runtime_error("text glyph drawing requires its actual live prepared atlas texture");
		}

		return *state.Texture;
	}

	void TextGlyphAtlasOwner::Close() {
		auto& state = *m_Implementation;
		if (state.Closed) {
			return;
		}

		const Implementation::Mutation mutation(state);
		state.Closed = true;
		state.Texture.reset();
	}

} // namespace sgre::text
