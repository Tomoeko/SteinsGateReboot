#include "sgre/script/text_font_owner.hpp"

#include <array>
#include <bit>
#include <exception>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::script {

	static constexpr std::size_t kMaximumFaceNameBytes = 65536;
	static constexpr std::size_t kMaximumDiagnosticBytes = 4096;

	struct RetainedTextFontFamily {
		RetainedTextFontFamily(text::TextFontFamily family, std::size_t maximum)
		    : Selection(family, maximum) {
		}

		text::TextFontSelectionCache Selection;
		std::vector<std::unique_ptr<ScriptTextFontInfo>> Fonts;
	};

	class ScriptTextFontOwner::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptVectorFontBindings& vectors, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, const SquirrelHostRoot& cached_self, std::size_t maximum)
		    : Vm(vm), Objects(objects), Psb(psb), Vectors(vectors), Fonts(fonts), Textures(textures), CachedSelf(cached_self), Maximum(maximum), Normal(text::TextFontFamily::Normal, maximum), Ruby(text::TextFontFamily::Ruby, maximum) {
			if (vm.Type(cached_self.Value()) != SquirrelValueType::WeakReference || !fonts.UsesCache(textures)) {
				throw std::runtime_error("text font owner requires its actual cached weak self and one font texture catalog");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptPsbBindings& Psb;
		ScriptVectorFontBindings& Vectors;
		scene::BitmapFontAssetCatalog& Fonts;
		scene::TextureCache& Textures;
		const SquirrelHostRoot& CachedSelf;
		std::size_t Maximum = 0;
		RetainedTextFontFamily Normal;
		RetainedTextFontFamily Ruby;
		bool Closed = false;
		bool Mutating = false;

		RetainedTextFontFamily& Family(text::TextFontFamily family) {
			if (family == text::TextFontFamily::Normal) {
				return Normal;
			}
			if (family == text::TextFontFamily::Ruby) {
				return Ruby;
			}

			throw std::runtime_error("text font owner received an unknown family");
		}

		void RequireOpen() const {
			if (Closed || Vm.IsClosed()) {
				throw std::runtime_error("text font owner or its borrowed VM is retired");
			}
		}

		class Mutation {
		public:
			explicit Mutation(Implementation& owner)
			    : Owner(owner) {
				owner.RequireOpen();
				if (owner.Mutating) {
					throw std::runtime_error("text font owner cannot mutate during its real findFont callback");
				}

				Owner.Mutating = true;
			}
			~Mutation() {
				Owner.Mutating = false;
			}

		private:
			Implementation& Owner;
		};

		TextFontLookupResult FindSource(text::TextFontSelectionKey key, std::string_view name, SquirrelValue& source) {
			TextFontLookupResult result;
			std::optional<SquirrelRunResult> returned;
			try {
				const auto receiver = Vm.ResolveWeakReference(CachedSelf.Value());
				if (Vm.Type(receiver) == SquirrelValueType::Null) {
					result.CallbackFailed = true;
					result.Diagnostic = "actual text wrapper cached weak self has expired";

					return result;
				}

				const auto context = Vm.RootContext();
				auto callable = Vm.ReadSlotInContext(context, receiver, std::string("findFont"));
				if (Vm.Type(callable) == SquirrelValueType::WeakReference) {
					callable = Vm.ResolveWeakReference(callable);
				}
				if (Vm.Type(callable) != SquirrelValueType::Closure && Vm.Type(callable) != SquirrelValueType::NativeClosure) {
					result.CallbackFailed = true;
					result.Diagnostic = "actual findFont member is not a callable closure";

					return result;
				}

				const std::array<SquirrelValue, 4> arguments{receiver, key.RequestedSize, std::string(name), std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(key.Style))};
				result.CallbackInvoked = true;
				returned = Vm.InvokeInContext(context, callable, arguments);
			} catch (const std::runtime_error& error) {
				result.CallbackFailed = true;
				result.Diagnostic = std::string_view(error.what()).substr(0, kMaximumDiagnosticBytes);

				return result;
			}

			if (returned->State != SquirrelRunState::Completed) {
				throw std::runtime_error("text findFont callback suspension is not recovered");
			}

			source = returned->Value;

			return result;
		}

		static void ClearFamily(RetainedTextFontFamily& family, std::exception_ptr& failure) {
			for (auto& font : family.Fonts) {
				try {
					font->Close();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}

			family.Fonts.clear();
			family.Selection.Clear();
		}

		void ClearFamilies() {
			std::exception_ptr failure;
			ClearFamily(Normal, failure);
			ClearFamily(Ruby, failure);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ScriptTextFontOwner::ScriptTextFontOwner(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptVectorFontBindings& vectors, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, const SquirrelHostRoot& cached_self, std::size_t max_fonts)
	    : m_Implementation(std::make_unique<Implementation>(vm, objects, psb, vectors, fonts, textures, cached_self, max_fonts)) {
	}

	ScriptTextFontOwner::~ScriptTextFontOwner() {
		try {
			Close();
		} catch (...) {
		}
	}

	TextFontLookupResult ScriptTextFontOwner::Select(text::TextFontFamily family, text::TextFontSelectionKey key, std::string_view face_name, bool filtering) {
		auto& owner = *m_Implementation;
		const Implementation::Mutation mutation(owner);
		auto& state = owner.Family(family);
		if (const auto found = state.Selection.Find(key)) {
			auto& font = *state.Fonts.at(*found);
			font.SetSize(key.RequestedSize);
			font.SetFiltering(filtering);
			TextFontLookupResult result;
			result.Font = &font;

			return result;
		}
		if (state.Fonts.size() >= owner.Maximum || face_name.size() > kMaximumFaceNameBytes) {
			throw std::runtime_error("text font lookup exceeds its bounded retained entries or face name");
		}

		if (family == text::TextFontFamily::Ruby) {
			key.Style = text::TextFontStyle::Regular;
		}

		SquirrelValue source;
		auto result = owner.FindSource(key, face_name.substr(0, face_name.find('\0')), source);
		owner.RequireOpen();
		if (result.CallbackFailed) {
			state.Selection.ResolveFailed();

			return result;
		}

		auto font = std::make_unique<ScriptTextFontInfo>(owner.Vm, owner.Objects, owner.Psb, owner.Vectors, owner.Fonts, owner.Textures, source, key);
		font->SetFiltering(filtering);
		font->SetSize(key.RequestedSize);
		text::TextFontSelectionEntry entry;
		entry.Key = key;
		state.Fonts.push_back(std::move(font));
		try {
			static_cast<void>(state.Selection.Append(entry));
		} catch (...) {
			state.Fonts.pop_back();

			throw;
		}

		result.Font = state.Fonts.back().get();

		return result;
	}

	std::size_t ScriptTextFontOwner::Count(text::TextFontFamily family) const {
		m_Implementation->RequireOpen();

		return m_Implementation->Family(family).Fonts.size();
	}

	std::optional<std::size_t> ScriptTextFontOwner::Current(text::TextFontFamily family) const {
		m_Implementation->RequireOpen();

		return m_Implementation->Family(family).Selection.Current();
	}

	resource::GlyphSize ScriptTextFontOwner::MeasureCurrentNormalCodeUnit(std::uint16_t code_unit) const {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto selected = owner.Normal.Selection.Current();
		if (!selected) {
			return {};
		}

		return owner.Normal.Fonts.at(*selected)->MeasureCodeUnit(code_unit);
	}

	text::OwnedTextGlyphRun ScriptTextFontOwner::PrepareCurrentNormalCodeUnit(std::uint16_t code_unit) const {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto selected = owner.Normal.Selection.Current();
		if (!selected) {
			return {};
		}
		return owner.Normal.Fonts.at(*selected)->PrepareCodeUnitRun(code_unit);
	}

	resource::GlyphSize ScriptTextFontOwner::MeasureCurrentRubyCodeUnit(std::uint16_t code_unit) const {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto selected = owner.Ruby.Selection.Current();
		if (!selected) {
			return {};
		}

		return owner.Ruby.Fonts.at(*selected)->MeasureCodeUnit(code_unit);
	}

	text::OwnedTextGlyphRun ScriptTextFontOwner::PrepareCurrentRubyCodeUnit(std::uint16_t code_unit) const {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto selected = owner.Ruby.Selection.Current();
		if (!selected) {
			return {};
		}
		return owner.Ruby.Fonts.at(*selected)->PrepareCodeUnitRun(code_unit);
	}

	text::OwnedTextGlyphRun ScriptTextFontOwner::PrepareCurrentRubyRun(std::u16string_view text) const {
		auto& owner = *m_Implementation;
		owner.RequireOpen();
		const auto selected = owner.Ruby.Selection.Current();
		if (!selected) {
			return {};
		}
		return owner.Ruby.Fonts.at(*selected)->PrepareRun(text);
	}

	void ScriptTextFontOwner::Clear() {
		auto& state = *m_Implementation;
		const Implementation::Mutation mutation(state);
		state.ClearFamilies();
	}

	void ScriptTextFontOwner::Close() {
		auto& state = *m_Implementation;
		if (state.Closed) {
			return;
		}
		if (state.Mutating) {
			throw std::runtime_error("text font owner cannot retire during its real findFont callback");
		}

		state.Closed = true;
		state.ClearFamilies();
	}

} // namespace sgre::script
