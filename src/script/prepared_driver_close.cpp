#include "sgre/script/prepared_driver_close.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::script {

	class PreparedDriverCloseCall {
	public:
		explicit PreparedDriverCloseCall(bool& calling) : m_Calling(calling) {
			m_Calling = true;
		}
		~PreparedDriverCloseCall() noexcept {
			m_Calling = false;
		}
		PreparedDriverCloseCall(const PreparedDriverCloseCall&) = delete;
		PreparedDriverCloseCall& operator=(const PreparedDriverCloseCall&) = delete;

	private:
		bool& m_Calling;
	};

	static void RequireActualThreadObjectRetirement(const ScriptScheduler& scheduler, const SquirrelVm& vm, const ScriptObjectBindings& objects) {
		const auto list = scheduler.NativeThreadList();
		if (std::holds_alternative<std::monostate>(list)) {
			return;
		}

		const auto count = vm.Length(list);
		for (std::size_t index = 0; index < count; ++index) {
			const auto instance = vm.GetSlot(list, static_cast<std::int32_t>(index));
			if (objects.HasInstance(instance)) {
				objects.RequireCanRelease(instance);
			}
		}
	}

	static scene::DefaultDriverSceneServices ActualPreparedSceneServices(PreparedDriverCloseOwners owners, scene::LayerManagerFrameOperations actual_root_layers) {
		owners.Vm.RequireCanClose();
		owners.Scheduler.RequireCanShutdownNativeThreads();
		RequireActualThreadObjectRetirement(owners.Scheduler, owners.Vm, owners.Objects);
		owners.Layers.RequireCanReleaseAll(owners.Vm, owners.Objects, owners.Tree);
		if (owners.Vm.IsClosed() || owners.ResourceManager.IsClosed() || owners.ResourceMeter.IsClosed() || !owners.ResourceManager.CanClose() || !owners.ResourceMeter.CanClose() || !owners.Controller.Serial().CanClear() || !owners.Controller.Parallel().CanClear()) {
			throw std::runtime_error("prepared Driver construction requires its live concrete owners and idle embedded groups");
		}

		scene::DefaultDriverSceneServices services;
		services.ResourceManager.Update = [&manager = owners.ResourceManager]() {
			manager.Update();
		};
		services.ResourceManager.Draw = [&manager = owners.ResourceManager](std::uint32_t argument) {
			manager.Draw(argument);
		};
		services.ResourceManager.Retire = [&manager = owners.ResourceManager]() {
			manager.Close();
		};
		services.ResourceMeter = owners.ResourceMeter.DriverTaskOperations();
		services.RootLayers = std::move(actual_root_layers);

		return services;
	}

	PreparedDriverClose::PreparedDriverClose(PreparedDriverCloseOwners owners, scene::LayerManagerFrameOperations actual_root_layers, std::size_t max_tasks_per_group) : m_Controller(owners.Controller), m_Tree(owners.Tree), m_ResourceManager(owners.ResourceManager), m_ResourceMeter(owners.ResourceMeter), m_Scheduler(owners.Scheduler), m_Vm(owners.Vm), m_Objects(owners.Objects), m_Layers(owners.Layers), m_Logs(owners.Logs), m_Scene(owners.Controller, owners.Tree, ActualPreparedSceneServices(owners, std::move(actual_root_layers)), max_tasks_per_group) {
	}

	PreparedDriverClose::~PreparedDriverClose() noexcept {
		if (m_Phase != PreparedDriverClosePhase::Closed) {
			// DefaultDriverScene still captures these owners. Its destructor must not bypass Thread/VM admission.
			std::terminate();
		}
	}

	scene::DefaultDriverScene& PreparedDriverClose::Scene() {
		if (std::this_thread::get_id() != m_OwnerThread || m_Calling || m_CloseStarted) {
			throw std::runtime_error("prepared Driver scene access requires its open serialized owning scope");
		}

		return m_Scene;
	}

	PreparedDriverClosePhase PreparedDriverClose::Phase() const noexcept {
		return m_Phase;
	}

	void PreparedDriverClose::RequireCanRetireScene() const {
		if (!m_Controller.CanClear() || !m_Scene.CanClose()) {
			throw std::runtime_error("prepared Driver close requires inactive actual Common/scene/Layer ownership");
		}
		if (!m_ResourceManager.CanClose()) {
			throw std::runtime_error("prepared Driver close requires its actual ResourceManager receiving/worker preflight");
		}
		if (!m_ResourceMeter.CanClose()) {
			throw std::runtime_error("prepared Driver close requires its actual ResourceMeter hook/font preflight");
		}
	}

	void PreparedDriverClose::RequireCanNotifyThreads() const {
		RequireActualThreadObjectRetirement(m_Scheduler, m_Vm, m_Objects);
	}

	void PreparedDriverClose::RequirePendingOwners() const {
		RequireCanRetireScene();
		m_Vm.RequireCanClose();
		if (m_Phase == PreparedDriverClosePhase::Threads || m_Phase == PreparedDriverClosePhase::Vm) {
			if (m_Vm.IsClosed()) {
				throw std::runtime_error("prepared Driver VM retired before its actual Thread cleanup phase");
			}

			m_Scheduler.RequireCanShutdownNativeThreads();
			RequireCanNotifyThreads();
		} else if (!m_Vm.IsClosed()) {
			throw std::runtime_error("prepared Driver history/scene retirement must follow actual VM Close");
		}

		m_Layers.RequireCanReleaseAll(m_Vm, m_Objects, m_Tree);
	}

	void PreparedDriverClose::RequireCanClose() const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Calling) {
			throw std::runtime_error("prepared Driver close requires its inactive creating owner thread");
		}
		if (m_Phase == PreparedDriverClosePhase::Closed) {
			return;
		}

		RequirePendingOwners();
	}

	void PreparedDriverClose::RememberFailure(std::exception_ptr failure) noexcept {
		if (!m_Failure) {
			m_Failure = std::move(failure);
		}
	}

	void PreparedDriverClose::Close() {
		RequireCanClose();
		if (m_Phase == PreparedDriverClosePhase::Closed) {
			return;
		}

		const PreparedDriverCloseCall call(m_Calling);
		m_CloseStarted = true;
		if (m_Phase == PreparedDriverClosePhase::Threads) {
			m_Scheduler.ShutdownNativeThreads();
			m_Phase = PreparedDriverClosePhase::Vm;
		}

		if (m_Phase == PreparedDriverClosePhase::Vm) {
			// Notifications may have transferred hook/group leases; reject before VM or scene mutation.
			RequirePendingOwners();
			m_Vm.Close();
			m_Phase = PreparedDriverClosePhase::Histories;
			try {
				m_Vm.RethrowReleaseFailure();
			} catch (...) {
				RememberFailure(std::current_exception());
			}
		}

		if (m_Phase == PreparedDriverClosePhase::Histories) {
			// VM release hooks can change real scene owners while their backing remains enclosed.
			RequirePendingOwners();
			m_Logs.Close();
			m_Phase = PreparedDriverClosePhase::Scene;
		}

		if (m_Phase == PreparedDriverClosePhase::Scene) {
			RequirePendingOwners();
			try {
				m_Scene.Close();
			} catch (...) {
				RememberFailure(std::current_exception());
			}

			// Existing scene and Serial Clear finish admitted structural retirement before reporting errors.
			try {
				m_Controller.ClearSerial();
			} catch (...) {
				RememberFailure(std::current_exception());
			}
			if (!m_ResourceManager.IsClosed() || !m_ResourceMeter.IsClosed() || m_Controller.Parallel().Count() != 0 || m_Controller.Serial().Count() != 0) {
				throw std::runtime_error("prepared Driver scene retirement did not complete its actual callback owners");
			}

			m_Phase = PreparedDriverClosePhase::Closed;
		}

		if (const auto failure = std::exchange(m_Failure, {})) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::script
