#pragma once

#include "sgre/scene/motion_random.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::script {

	class ScriptRandomBindings {
	public:
		// VM/Object services and the actual contemporary seed buffer enclose the binding and explicit VM Close.
		ScriptRandomBindings(SquirrelVm& vm, ScriptObjectBindings& objects, std::span<const std::uint32_t, scene::kMotionSharedSeedWords> actual_seed, std::size_t max_instances = 65536);
		~ScriptRandomBindings();
		ScriptRandomBindings(const ScriptRandomBindings&) = delete;
		ScriptRandomBindings& operator=(const ScriptRandomBindings&) = delete;
		ScriptRandomBindings(ScriptRandomBindings&&) = delete;
		ScriptRandomBindings& operator=(ScriptRandomBindings&&) = delete;

		[[nodiscard]] SquirrelValue RandomClass() const;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
