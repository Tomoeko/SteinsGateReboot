#pragma once

#include "sgre/scene/motion_player_task.hpp"
#include "sgre/script/object_bindings.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace sgre::script {
	class ScriptMotionLayerGetters {
	public:
		// VM/Object registration encloses this adapter. Getters keep only actual weak task/generation views.
		using ChildMotionFactory = std::function<SquirrelValue(const SquirrelObjectIdentity&, const scene::MotionPlayerTask&, const scene::MotionChildBorrow&)>;
		ScriptMotionLayerGetters(SquirrelVm& vm, ScriptObjectBindings& objects, ChildMotionFactory child_motion = {});
		~ScriptMotionLayerGetters();
		ScriptMotionLayerGetters(const ScriptMotionLayerGetters&) = delete;
		ScriptMotionLayerGetters& operator=(const ScriptMotionLayerGetters&) = delete;
		[[nodiscard]] SquirrelValue GetterClass() const;
		[[nodiscard]] SquirrelValue ShapeClass() const;
		// Resolves the real serialized LayerIndexMap across the current selected player catalogue; missing names return null.
		[[nodiscard]] SquirrelValue GetLayerGetter(const scene::MotionPlayerTask& task, std::string_view name, std::optional<SquirrelObjectIdentity> actual_layer = {});
		// Wraps only an existing kind3 child; the owning Layer and selected task keep their original lifetime roles.
		[[nodiscard]] SquirrelValue GetLayerMotion(const scene::MotionPlayerTask& task, std::string_view name, const SquirrelObjectIdentity& actual_layer);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
