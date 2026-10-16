#pragma once

#include "sgre/script/object_bindings.hpp"
#include "sgre/script/script_scheduler.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace sgre::script {

	struct ScriptThreadFileServices {
		resource::ResourceCache& Cache;
		scene::CommonProcessTasks& Driver;
		core::BackgroundCallManager& Background;
		resource::ArchiveMounts& Mounts;
		std::string Seed;
	};

	class ScriptThreadBindings {
	public:
		using ScriptRequest = std::function<ScriptScheduler::LoadPoll(std::string_view)>;
		using FileServicesProvider = std::function<ScriptThreadFileServices()>;
		using InstanceInitializer = std::function<void(const SquirrelValue&, const SquirrelValue&)>;

		// Native runtime integration uses the singular Object bridge and requires VM/scheduler/Object owners to outlive it.
		ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptRequest request, ScriptObjectBindings& objects);
		// Concrete compiled-file requests own real filename/buffer fields and attach fresh readers to Driver Parallel.
		// Referenced services outlive bindings, scheduler and attached readers; pending retirement requires a reader fence.
		ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptThreadFileServices file_services, ScriptObjectBindings& objects);
		// Native registration precedes deferred Driver setup. Resolve the actual whole bundle only for a string request.
		// Provider cannot mutate VM/binding/service ownership or destroy its captured owner during resolution.
		// It throws until the real ResourceManager exists and after its retirement.
		// Provider, bundle owners and this adapter enclose every actual pending reader and scheduler file owner.
		ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, FileServicesProvider actual_file_services, ScriptObjectBindings& objects);
		// Generic standalone fixtures have no Object-derived native cleanup bridge.
		ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptRequest request, const SquirrelValue& object_class = {}, InstanceInitializer initialize = {});
		~ScriptThreadBindings();
		ScriptThreadBindings(const ScriptThreadBindings&) = delete;
		ScriptThreadBindings& operator=(const ScriptThreadBindings&) = delete;
		ScriptThreadBindings(ScriptThreadBindings&&) = delete;
		ScriptThreadBindings& operator=(ScriptThreadBindings&&) = delete;

		[[nodiscard]] SquirrelValue Spawn(const SquirrelValue& target, std::span<const SquirrelValue> parameters = {}, const SquirrelValue& environment = {});
		[[nodiscard]] ScriptThreadHandle ThreadHandle(const SquirrelValue& instance) const;
		[[nodiscard]] SquirrelValue ThreadClass() const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptRequest request, ScriptObjectBindings* objects, const SquirrelValue& object_class, InstanceInitializer initialize, const ScriptThreadFileServices* file_services = nullptr, FileServicesProvider actual_file_services = {});
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
