#pragma once

#include "sgre/scene/motion_player_task.hpp"
#include "sgre/script/object_bindings.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace sgre::script {
	class ScriptMotionLayerSetters {
	public:
		// VM/Object and actual containing Layer runtimes outlive these weak selected-layer views.
		ScriptMotionLayerSetters(SquirrelVm& vm, ScriptObjectBindings& objects);
		~ScriptMotionLayerSetters();
		ScriptMotionLayerSetters(const ScriptMotionLayerSetters&) = delete;
		ScriptMotionLayerSetters& operator=(const ScriptMotionLayerSetters&) = delete;
		[[nodiscard]] SquirrelValue SetterClass() const;
		[[nodiscard]] SquirrelValue GetLayerSetter(const scene::MotionPlayerTask& task, std::string_view name, std::optional<scene::MotionChildBorrow> actual_child = {});

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
