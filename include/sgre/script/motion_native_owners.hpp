#pragma once

#include "sgre/scene/motion_player_task.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace sgre::script {

	class ScriptMotionNativeOwners {
	public:
		// The supplied actual Motion class derives from Object. Class registration/factory conversions remain separate.
		// VM/Object/Layer, each real runtime, clock/seed and source services enclose these native wrappers and tasks.
		ScriptMotionNativeOwners(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, const SquirrelValue& actual_motion_class, std::size_t maximum_instances = 65536);
		~ScriptMotionNativeOwners() noexcept;
		ScriptMotionNativeOwners(const ScriptMotionNativeOwners&) = delete;
		ScriptMotionNativeOwners& operator=(const ScriptMotionNativeOwners&) = delete;
		ScriptMotionNativeOwners(ScriptMotionNativeOwners&&) = delete;
		ScriptMotionNativeOwners& operator=(ScriptMotionNativeOwners&&) = delete;

		// Genuine273100 new-only route: borrowed Layer object, actual selected primary/secondary runtime, Second task/listener.
		// No clone, spatial provider, asynchronous downgrade or application clock/random default is supplied.
		void ConstructNew(const SquirrelValue& instance, const SquirrelValue& layer, scene::MotionManagerRuntime& actual_runtime, bool actual_asynchronous_mode, scene::MotionTaskInsertion insertion = scene::MotionTaskInsertion::Append);
		// Native273420 copies an actual root player into the supplied destination Layer and installs a fresh listener.
		// Source wrapper/Layer borrows last only through construction. Child-wrapper and alternate-texture domains remain explicit gates.
		void ConstructClone(const SquirrelValue& instance, const SquirrelValue& destination_layer, const SquirrelValue& source_motion, scene::MotionManagerRuntime& actual_destination_runtime, bool actual_asynchronous_mode);
		[[nodiscard]] bool Contains(const SquirrelValue& instance) const;
		[[nodiscard]] bool IsDetached(const SquirrelValue& instance) const;
		[[nodiscard]] bool EventEnabled(const SquirrelValue& instance) const;
		void SetEventEnabled(const SquirrelValue& instance, bool enabled);
		// A copied weak task view never borrows storage inside a registry row. Detached VM properties supply their recovered defaults.
		[[nodiscard]] scene::MotionPlayerTask Task(const SquirrelValue& instance) const;
		[[nodiscard]] SquirrelObjectIdentity LayerIdentity(const SquirrelValue& instance) const;
		// Native273300 wraps an existing kind3 player with release policy false and no new task/listener.
		[[nodiscard]] SquirrelValue CreateBorrowedChild(const SquirrelObjectIdentity& layer, const scene::MotionPlayerTask& containing, const scene::MotionChildBorrow& child);
		[[nodiscard]] const scene::MotionPlayerCommands& Commands(const SquirrelValue& instance) const;
		[[nodiscard]] const scene::MotionRootScene& Root(const SquirrelValue& instance) const;
		void SkipToSync(const SquirrelValue& instance);
		void ReleaseSyncWait(const SquirrelValue& instance);
		void SetCoordinate(const SquirrelValue& instance, resource::MotionPoint coordinate);
		void SetZoom(const SquirrelValue& instance, float horizontal, float vertical);
		void SetOpacity(const SquirrelValue& instance, std::uint32_t opacity);
		void SetAngleDegrees(const SquirrelValue& instance, float degrees);
		void SetAngleRadians(const SquirrelValue& instance, float radians);
		void SetVariable(const SquirrelValue& instance, std::string_view name, float value, std::uint32_t flags);
		void AnimateVariable(const SquirrelValue& instance, std::string_view name, float value, std::int32_t duration, std::int32_t curve);
		void RegisterAlternateTexture(const SquirrelValue& instance, const scene::NativeTextureReference& descriptor, std::string_view uid, const SquirrelValue& texture, const scene::MotionBounds& rectangle, resource::MotionPoint origin);
		void UnregisterAlternateTexture(const SquirrelValue& instance, std::string_view uid);
		void ClearAlternateTextures(const SquirrelValue& instance);
		void RequireCanRelease(const SquirrelValue& instance) const;
		// Idle wrapper retirement requests status2; the actual Second group still owns player/rows until normal retirement.
		void ReleaseInstance(const SquirrelValue& instance);
		[[nodiscard]] std::size_t InstanceCount() const noexcept;
		[[nodiscard]] bool CanClose() const noexcept;
		// VM release hooks or explicit wrapper completion remove every actual wrapper before the adapter closes.
		void Close();

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
