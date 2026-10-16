#pragma once

#include "sgre/script/motion_native_owners.hpp"

#include <memory>

namespace sgre::script {
	class ScriptMotionVariables {
	public:
		// Install on the actual Motion class before its first instance. VM/Owners enclose every invocation.
		// Closures borrow this adapter weakly; values and parameter registrations belong to the real task.
		ScriptMotionVariables(SquirrelVm& vm, const SquirrelValue& actual_motion_class, ScriptMotionNativeOwners& actual_owners);
		~ScriptMotionVariables();
		ScriptMotionVariables(const ScriptMotionVariables&) = delete;
		ScriptMotionVariables& operator=(const ScriptMotionVariables&) = delete;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
