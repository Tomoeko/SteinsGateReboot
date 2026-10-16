#include "sgre/script/text_font_info.hpp"

#include "sgre/text/cff_glyph_stroke_path.hpp"
#include "sgre/text/recursive_decomposition.hpp"

#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sgre::script {

	class ScriptTextFontInfo::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptVectorFontBindings& vector_fonts, scene::BitmapFontAssetCatalog& bitmap_fonts, scene::TextureCache& textures, const SquirrelValue& source, text::TextFontSelectionKey key)
		    : Vm(vm), Objects(objects), SourceRoot(vm.Retain(source)), Key(key) {
			if (!bitmap_fonts.UsesCache(textures)) {
				throw std::runtime_error("FontInfo requires one bitmap font catalog and texture cache");
			}
			const auto native_source = Vm.Type(source) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(source) : source;
			if (!Objects.HasInstance(native_source)) {
				return;
			}

			NativeSource = Vm.BorrowObjectIdentity(native_source);
			if (Vm.IsInstanceOf(native_source, psb.ObjectClass())) {
				const auto asset = psb.DocumentAsset(native_source);
				if (asset.Font) {
					Bitmap = std::make_unique<scene::BitmapFontTextureOwner>(textures, asset.Font);
				} else {
					Bitmap = std::make_unique<scene::BitmapFontTextureOwner>(textures, bitmap_fonts.Prepare(asset.Document, asset.Backing));
				}
				Kind = TextFontProviderKind::Bitmap;
			} else if (Vm.IsInstanceOf(native_source, vector_fonts.VectorFontClass())) {
				Vector = vector_fonts.TryProvider(native_source);
				if (Vector) {
					Kind = TextFontProviderKind::Vector;
					VectorCache = std::make_unique<text::VectorGlyphCache>();
				}
			}
		}

		~Implementation() {
			try {
				Close();
			} catch (...) {
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		SquirrelHostRoot SourceRoot;
		std::optional<SquirrelObjectIdentity> NativeSource;
		text::TextFontSelectionKey Key;
		TextFontProviderKind Kind = TextFontProviderKind::Unavailable;
		std::unique_ptr<scene::BitmapFontTextureOwner> Bitmap;
		text::VectorFont* Vector = nullptr;
		std::unique_ptr<text::VectorGlyphCache> VectorCache;
		text::TextGlyphAtlasOwner* Atlas = nullptr;
		bool Mutating = false;
		resource::GlyphSize BitmapScale{1, 1};
		std::uint32_t VectorSize = 0xffffffff;
		bool Closed = false;

		void RequireOpen() const {
			if (Closed || Vm.IsClosed()) {
				throw std::runtime_error("FontInfo owner is retired or its borrowed VM is closed");
			}
			if (Kind != TextFontProviderKind::Unavailable) {
				const auto source = Vm.ResolveObjectIdentity(*NativeSource);
				if (!source || !Objects.HasInstance(*source)) {
					throw std::runtime_error("FontInfo supplied native source was explicitly retired during its provider borrow");
				}
			}
		}

		class Mutation {
		public:
			explicit Mutation(Implementation& owner)
			    : Owner(owner) {
				owner.RequireOpen();
				owner.RequireIdle();
				Owner.Mutating = true;
			}

			~Mutation() {
				Owner.Mutating = false;
			}

		private:
			Implementation& Owner;
		};

		void RequireIdle() const {
			if (Mutating) {
				throw std::runtime_error("FontInfo cannot mutate or retire during glyph preparation/drawing");
			}
		}

		void RequireVectorCache() const {
			if (!Vector || !VectorCache) {
				throw std::runtime_error("FontInfo vector glyph run requires its actual private vector interface");
			}
			if (Atlas && Atlas->State().Closed) {
				throw std::runtime_error("FontInfo cached regions borrow a retired text atlas context");
			}
		}

		void Close() {
			if (Closed) {
				return;
			}

			RequireIdle();
			Closed = true;
			std::exception_ptr failure;
			try {
				if (Bitmap) {
					Bitmap->Close();
				}
			} catch (...) {
				failure = std::current_exception();
			}

			// Native FontInfo retires its private interface before releasing the actual supplied source root.
			Bitmap.reset();
			VectorCache.reset();
			Atlas = nullptr;
			Vector = nullptr;
			Kind = TextFontProviderKind::Unavailable;
			NativeSource.reset();
			SourceRoot = {};
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ScriptTextFontInfo::ScriptTextFontInfo(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptVectorFontBindings& vector_fonts, scene::BitmapFontAssetCatalog& bitmap_fonts, scene::TextureCache& textures, const SquirrelValue& source, text::TextFontSelectionKey key)
	    : m_Implementation(std::make_unique<Implementation>(vm, objects, psb, vector_fonts, bitmap_fonts, textures, source, key)) {
	}

	ScriptTextFontInfo::~ScriptTextFontInfo() = default;

	TextFontProviderKind ScriptTextFontInfo::Kind() const {
		m_Implementation->RequireOpen();

		return m_Implementation->Kind;
	}

	const text::TextFontSelectionKey& ScriptTextFontInfo::Key() const {
		return m_Implementation->Key;
	}

	SquirrelValue ScriptTextFontInfo::Source() const {
		m_Implementation->RequireOpen();

		return m_Implementation->SourceRoot.Value();
	}

	void ScriptTextFontInfo::SetSize(float requested_size) {
		m_Implementation->RequireOpen();
		m_Implementation->RequireIdle();
		if (m_Implementation->Bitmap) {
			const auto scale = requested_size / m_Implementation->Bitmap->Asset().Model->MaxHeight;
			m_Implementation->BitmapScale = {scale, scale};
		} else if (m_Implementation->Vector) {
			m_Implementation->VectorSize = text::VectorFontSizeWord(requested_size);
		}
	}

	void ScriptTextFontInfo::SetFiltering(bool enabled) {
		m_Implementation->RequireOpen();
		m_Implementation->RequireIdle();
		if (m_Implementation->Bitmap) {
			const auto filter = enabled ? 1 : 0;
			m_Implementation->Bitmap->SetFiltering(filter, filter);
		}
	}

	resource::GlyphSize ScriptTextFontInfo::BitmapScale() const {
		m_Implementation->RequireOpen();
		if (!m_Implementation->Bitmap) {
			throw std::runtime_error("FontInfo bitmap scale requires its actual bitmap provider");
		}

		return m_Implementation->BitmapScale;
	}

	resource::GlyphSize ScriptTextFontInfo::MeasureBitmapCodeUnit(std::uint16_t code_unit) const {
		m_Implementation->RequireOpen();
		if (!m_Implementation->Bitmap) {
			throw std::runtime_error("FontInfo bitmap measurement requires its actual bitmap provider");
		}

		const auto& font = *m_Implementation->Bitmap->Asset().Model;

		return resource::MeasureBitmapGlyph(font.FindGlyphCodeUnit(code_unit), m_Implementation->BitmapScale.Width, m_Implementation->BitmapScale.Height);
	}

	resource::GlyphSize ScriptTextFontInfo::MeasureCodeUnit(std::uint16_t code_unit) {
		m_Implementation->RequireOpen();
		m_Implementation->RequireIdle();
		if (m_Implementation->Kind == TextFontProviderKind::Unavailable) {
			return {};
		}
		if (m_Implementation->Kind == TextFontProviderKind::Vector) {
			m_Implementation->Vector->SetSize(static_cast<float>(m_Implementation->VectorSize));

			return m_Implementation->Vector->MeasureCodeUnit(code_unit);
		}

		return MeasureBitmapCodeUnit(code_unit);
	}

	text::OwnedTextGlyphRun ScriptTextFontInfo::PrepareCodeUnitRun(std::uint16_t code_unit) {
		auto& owner = *m_Implementation;
		const Implementation::Mutation mutation(owner);
		text::OwnedTextGlyphRun result;
		if (owner.Kind == TextFontProviderKind::Unavailable) {
			return result;
		}
		if (owner.Bitmap) {
			// Native036890 stores the UTF-16 unit, not a cmap glyph ID, in a bitmap run.
			const auto size = resource::MeasureBitmapGlyph(owner.Bitmap->Asset().Model->FindGlyphCodeUnit(code_unit), owner.BitmapScale.Width, owner.BitmapScale.Height);
			result.Glyphs.push_back({code_unit, {}});
			result.Advance = {size.Width, size.Height};
			return result;
		}
		owner.Vector->SetSize(static_cast<float>(owner.VectorSize));
		const auto size = owner.Vector->MeasureCodeUnit(code_unit);
		auto& buffer = owner.Vector->InputBuffer();
		const auto input = buffer.InputGlyphs();
		if (input.size() != 1) {
			throw std::runtime_error("FontInfo code-unit run requires its actual single-base shaping output");
		}
		const auto position = buffer.PositionRow(0);
		text::ShapedTextGlyph shaped;
		shaped.GlyphIndex = input.front().GlyphIndex;
		shaped.XAdvance26Dot6 = position.XAdvance26Dot6;
		shaped.YAdvance26Dot6 = position.YAdvance26Dot6;
		shaped.XOffset26Dot6 = position.XOffset26Dot6;
		shaped.YOffset26Dot6 = position.YOffset26Dot6;
		result.Glyphs.resize(1);
		result.Advance = text::PrepareTextGlyphRun(std::span<const text::ShapedTextGlyph>(&shaped, 1), result.Glyphs);
		if (result.Advance.X != size.Width || result.Advance.Y != size.Height) {
			throw std::runtime_error("FontInfo actual shaping measurement and owned run advance differ");
		}
		return result;
	}

	text::OwnedTextGlyphRun ScriptTextFontInfo::PrepareRun(std::u16string_view text) {
		auto& owner = *m_Implementation;
		const Implementation::Mutation mutation(owner);
		if (owner.Kind == TextFontProviderKind::Unavailable) {
			return {};
		}
		if (!owner.Vector) {
			throw std::runtime_error("FontInfo whole-string bitmap shaping requires its actual run producer");
		}
		owner.Vector->SetSize(static_cast<float>(owner.VectorSize));

		return owner.Vector->PrepareRun(text);
	}

	const scene::BitmapFontTextureOwner* ScriptTextFontInfo::BitmapProvider() const {
		m_Implementation->RequireOpen();

		return m_Implementation->Bitmap.get();
	}

	std::uint32_t ScriptTextFontInfo::VectorSizeWord() const {
		m_Implementation->RequireOpen();
		if (!m_Implementation->Vector) {
			throw std::runtime_error("FontInfo vector size requires its actual vector provider");
		}

		return m_Implementation->VectorSize;
	}

	text::CffPreparedGlyph ScriptTextFontInfo::PrepareVectorGlyph(std::uint32_t glyph_index) {
		m_Implementation->RequireOpen();
		m_Implementation->RequireIdle();
		if (!m_Implementation->Vector) {
			throw std::runtime_error("FontInfo vector glyph preparation requires its actual vector provider");
		}

		// Native measurement/draw converts the interface word to unsigned float before updating its shared core.
		m_Implementation->Vector->SetSize(static_cast<float>(m_Implementation->VectorSize));

		return m_Implementation->Vector->PrepareGlyph(glyph_index);
	}

	void ScriptTextFontInfo::PrepareVectorRun(text::TextGlyphAtlasOwner& atlas, std::span<const text::TextRunGlyph> run, std::int32_t edge_width) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		state.RequireVectorCache();
		if (edge_width < 0) {
			throw std::runtime_error("FontInfo negative-edge vector glyph pixels remain unrecovered");
		}
		if ((state.Atlas && state.Atlas != &atlas) || atlas.State().Closed) {
			throw std::runtime_error("FontInfo vector cache requires its one live text atlas context");
		}

		state.Atlas = &atlas;
		state.VectorCache->PrepareRun(state.VectorSize, edge_width, run, [&](const text::VectorGlyphCacheKey& key) {
			state.Vector->SetSize(static_cast<float>(key.SizeWord));
			const auto prepared = state.Vector->PrepareGlyph(key.GlyphIndex);
			std::optional<text::CffAtlasGlyph> glyph;
			if (key.EdgeWidth == 0) {
				glyph = atlas.PrepareDefaultCffGlyph(prepared);
			} else if (const auto input = text::CffGlyphStrokeInput::Prepare(prepared, key.EdgeWidth)) {
				auto contours = text::PrepareCffGlyphStroke(input->Commands(), input->WidthPixels());
				const auto raster = text::PreparedCffGlyphRaster::PrepareStroke(*input, std::move(contours));
				glyph = atlas.PrepareCffRaster(raster);
			}

			state.RequireOpen();
			text::VectorGlyphAtlasRegion region;
			if (glyph) {
				region.AtlasLeftPixels = static_cast<std::int32_t>(glyph->Allocation.LeftPixels);
				region.AtlasTopPixels = static_cast<std::int32_t>(glyph->Allocation.TopPixels);
				region.LeftPixel = glyph->LeftPixel;
				region.TopTranslationPixel = glyph->TopTranslationPixel;
				region.WidthPixels = static_cast<std::int32_t>(glyph->Allocation.WidthPixels);
				region.HeightPixels = static_cast<std::int32_t>(glyph->Allocation.HeightPixels);
			}

			return region;
		});
	}

	std::size_t ScriptTextFontInfo::PrepareVectorDrawRun(text::TextCursor origin, std::uint32_t color_rgba, std::span<const text::TextRunGlyph> run, std::span<text::CachedVectorGlyphDraw> output, std::int32_t edge_width) {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		state.RequireVectorCache();
		if (!state.Atlas) {
			throw std::runtime_error("FontInfo vector drawing requires its actual prepared text atlas context");
		}

		return state.VectorCache->PrepareDrawRun(state.VectorSize, edge_width, origin, color_rgba, run, output);
	}

	text::VectorGlyphCacheCounts ScriptTextFontInfo::VectorCacheCounts() const {
		m_Implementation->RequireOpen();
		m_Implementation->RequireVectorCache();

		return m_Implementation->VectorCache->Counts();
	}

	void ScriptTextFontInfo::Close() {
		m_Implementation->Close();
	}

} // namespace sgre::script
