#pragma once

#include "sgre/script/motion_native_owners.hpp"

#include <memory>

namespace sgre::script {
	class ScriptMotionRootCoordinates {
	public:
		// Install before actual Motion instances lock their class. VM/Owners enclose this adapter and every invocation.
		// Closures borrow this component weakly; root state lives only in the genuine registered player task.
		ScriptMotionRootCoordinates(SquirrelVm& vm, const SquirrelValue& actual_motion_class, ScriptMotionNativeOwners& actual_owners);
		~ScriptMotionRootCoordinates();
		ScriptMotionRootCoordinates(const ScriptMotionRootCoordinates&) = delete;
		ScriptMotionRootCoordinates& operator=(const ScriptMotionRootCoordinates&) = delete;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
