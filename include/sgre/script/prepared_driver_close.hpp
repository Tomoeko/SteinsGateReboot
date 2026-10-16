#pragma once

#include "sgre/resource/resource_manager_process.hpp"
#include "sgre/scene/resource_meter_process.hpp"
#include "sgre/script/driver_logs.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/script_scheduler.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <thread>

namespace sgre::script {

	struct PreparedDriverCloseOwners {
		scene::CommonProcessTasks& Controller;
		scene::LayerTree& Tree;
		resource::ResourceManagerProcess& ResourceManager;
		scene::ResourceMeterProcess& ResourceMeter;
		ScriptScheduler& Scheduler;
		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptDriverLogs& Logs;
	};

	enum class PreparedDriverClosePhase : std::uint8_t {
		Threads,
		Vm,
		Histories,
		Scene,
		Closed
	};

	class PreparedDriverClose {
	public:
		// Concrete supplied owners and their real binding/receiver/backing services enclose this actual scene.
		// Scheduler uses this VM; typed Thread services use this Controller and ResourceManager Cache.
		// Caller supplies the real root Layer/renderer operations. No application/bootstrap/+138 owner is supplied.
		PreparedDriverClose(PreparedDriverCloseOwners owners, scene::LayerManagerFrameOperations actual_root_layers, std::size_t max_tasks_per_group = 64);
		~PreparedDriverClose() noexcept;
		PreparedDriverClose(const PreparedDriverClose&) = delete;
		PreparedDriverClose& operator=(const PreparedDriverClose&) = delete;
		PreparedDriverClose(PreparedDriverClose&&) = delete;
		PreparedDriverClose& operator=(PreparedDriverClose&&) = delete;

		[[nodiscard]] scene::DefaultDriverScene& Scene();
		[[nodiscard]] PreparedDriverClosePhase Phase() const noexcept;
		// All actual owner guards precede notification/group mutation; pending Thread receiver borrows reject.
		void RequireCanClose() const;
		// Thread -> VM -> histories -> actual Parallel scene -> Serial; late callback changes are rechecked.
		// Explicit supported failure completion keeps the first error. Rejected owner retirement remains live.
		void Close();

	private:
		void RequireCanRetireScene() const;
		void RequireCanNotifyThreads() const;
		void RequirePendingOwners() const;
		void RememberFailure(std::exception_ptr failure) noexcept;
		scene::CommonProcessTasks& m_Controller;
		scene::LayerTree& m_Tree;
		resource::ResourceManagerProcess& m_ResourceManager;
		scene::ResourceMeterProcess& m_ResourceMeter;
		ScriptScheduler& m_Scheduler;
		SquirrelVm& m_Vm;
		ScriptObjectBindings& m_Objects;
		ScriptLayerBindings& m_Layers;
		ScriptDriverLogs& m_Logs;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		PreparedDriverClosePhase m_Phase = PreparedDriverClosePhase::Threads;
		std::exception_ptr m_Failure;
		bool m_Calling = false;
		bool m_CloseStarted = false;
		scene::DefaultDriverScene m_Scene;
	};

} // namespace sgre::script
