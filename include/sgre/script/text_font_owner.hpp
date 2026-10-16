#pragma once

#include "sgre/script/text_font_info.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sgre::script {

	struct TextFontLookupResult {
		// Borrows the retained private interface until Clear or owner retirement.
		ScriptTextFontInfo* Font = nullptr;
		bool CallbackInvoked = false;
		bool CallbackFailed = false;
		std::string Diagnostic;
	};

	class ScriptTextFontOwner {
	public:
		// Services and the actual task's cached weak-self root outlive this private normal/ruby owner.
		ScriptTextFontOwner(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptVectorFontBindings& vectors, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, const SquirrelHostRoot& cached_self, std::size_t max_fonts = 65536);
		~ScriptTextFontOwner();
		ScriptTextFontOwner(const ScriptTextFontOwner&) = delete;
		ScriptTextFontOwner& operator=(const ScriptTextFontOwner&) = delete;
		[[nodiscard]] TextFontLookupResult Select(text::TextFontFamily family, text::TextFontSelectionKey key, std::string_view face_name, bool filtering);
		[[nodiscard]] std::size_t Count(text::TextFontFamily family) const;
		[[nodiscard]] std::optional<std::size_t> Current(text::TextFontFamily family) const;
		[[nodiscard]] resource::GlyphSize MeasureCurrentNormalCodeUnit(std::uint16_t code_unit) const;
		[[nodiscard]] text::OwnedTextGlyphRun PrepareCurrentNormalCodeUnit(std::uint16_t code_unit) const;
		[[nodiscard]] resource::GlyphSize MeasureCurrentRubyCodeUnit(std::uint16_t code_unit) const;
		[[nodiscard]] text::OwnedTextGlyphRun PrepareCurrentRubyCodeUnit(std::uint16_t code_unit) const;
		[[nodiscard]] text::OwnedTextGlyphRun PrepareCurrentRubyRun(std::u16string_view text) const;
		void Clear();
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
