#pragma once

#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace sgre::script {
	class ScriptPsbBindings;

	struct ScriptSystemFontState {
		float Scale = 1;
		std::string Name;
	};

	class ScriptFontBindings {
	public:
		// VM Close and instance retirement require these borrowed Object/PSB adapters to remain alive.
		ScriptFontBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, std::size_t max_instances = 65536);
		~ScriptFontBindings();
		ScriptFontBindings(const ScriptFontBindings&) = delete;
		ScriptFontBindings& operator=(const ScriptFontBindings&) = delete;
		ScriptFontBindings(ScriptFontBindings&&) = delete;
		ScriptFontBindings& operator=(ScriptFontBindings&&) = delete;

		[[nodiscard]] SquirrelValue FontClass() const;
		[[nodiscard]] SquirrelValue PsbFontClass() const;
		[[nodiscard]] SquirrelValue SystemFontClass() const;
		[[nodiscard]] ScriptSystemFontState SystemState(const SquirrelValue& instance) const;
		// A provider consumer retains this underlying PSBObject independently, without rooting PSBFont.
		[[nodiscard]] SquirrelValue PsbOwner(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
