#pragma once

#include "sgre/script/object_bindings.hpp"

namespace sgre::script {

	class ScriptEmoteRegistrationFacet {
	public:
		// VM/Object enclose this class service. Only declaration metadata and native weakref are admitted.
		// Construction and instance controls reject the missing actual Emote owner before any native-state allocation.
		ScriptEmoteRegistrationFacet(SquirrelVm& vm, ScriptObjectBindings& objects);
		ScriptEmoteRegistrationFacet(const ScriptEmoteRegistrationFacet&) = delete;
		ScriptEmoteRegistrationFacet& operator=(const ScriptEmoteRegistrationFacet&) = delete;
		[[nodiscard]] SquirrelValue EmoteClass() const;

	private:
		SquirrelValue m_Class;
		SquirrelHostRoot m_ClassRoot;
	};

} // namespace sgre::script
