#pragma once

#include "sgre/script/resource_bindings.hpp"
#include "sgre/text/vector_font.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sgre::script {

	struct ScriptVectorFontState {
		std::string ResourceName;
		std::int32_t Mode = 0;
		std::string Language = "en";
		bool ResourceCompleted = false;
		bool ProviderPrepared = false;
	};

	struct ScriptVectorFontLimits {
		std::size_t MaxInstances = 4096;
		std::size_t MaxNameBytes = 65536;
		std::size_t MaxTotalNameBytes = 16 * 1024 * 1024;
	};

	class ScriptVectorFontBindings {
	public:
		// Queries only globally active resources; must not promote pending work or acquire another manager reference.
		// Its returned bytes borrow the original request lease, which is retained until this wrapper retires.
		using ActiveAssetFind = std::function<std::optional<ScriptResourceAsset>(std::string_view resource_name)>;
		ScriptVectorFontBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptResourceBindings::TypedAssetRequest request, ScriptResourceBindings::ErrorSink errors, ScriptVectorFontLimits limits = {}, ActiveAssetFind find_active = {});
		~ScriptVectorFontBindings();
		ScriptVectorFontBindings(const ScriptVectorFontBindings&) = delete;
		ScriptVectorFontBindings& operator=(const ScriptVectorFontBindings&) = delete;
		ScriptVectorFontBindings(ScriptVectorFontBindings&&) = delete;
		ScriptVectorFontBindings& operator=(ScriptVectorFontBindings&&) = delete;

		[[nodiscard]] SquirrelValue VectorFontClass() const;
		[[nodiscard]] ScriptVectorFontState State(const SquirrelValue& instance) const;
		// Lazily prepares CPU metadata after resource completion. Call during preparation, not drawing.
		// TextRender consumers must retain the VectorFont script wrapper while borrowing this provider.
		[[nodiscard]] text::VectorFont& Provider(const SquirrelValue& instance);
		// Native FontInfo construction accepts a missing raw provider without dereferencing it.
		[[nodiscard]] text::VectorFont* TryProvider(const SquirrelValue& instance);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
