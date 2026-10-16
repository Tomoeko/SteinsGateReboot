#include "sgre/script/driver_phases.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::script {

	static std::string_view DriverCString(std::string_view text) {
		return text.substr(0, text.find('\0'));
	}

	class ScriptDriverPhaseCall {
	public:
		explicit ScriptDriverPhaseCall(bool& calling) : m_Calling(calling) {
			if (m_Calling) {
				throw std::runtime_error("script driver phase cannot reenter");
			}

			m_Calling = true;
		}

		~ScriptDriverPhaseCall() {
			m_Calling = false;
		}

	private:
		bool& m_Calling;
	};

	ScriptDriverPhases::ScriptDriverPhases(SquirrelVm& vm, ScriptScheduler& scheduler, std::string_view filename, std::span<const std::string_view> arguments, ScriptDriverPhaseServices services, ScriptDriverPhaseLimits limits) : m_Vm(vm), m_Scheduler(scheduler), m_Arguments(arguments), m_Services(std::move(services)), m_Limits(limits) {
		if (!m_Services.Setup || !m_Services.ReadFrameStep || limits.StartupInstructions == 0 || arguments.size() > limits.MaxArguments || arguments.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - 3)) {
			throw std::runtime_error("script driver phases require actual setup/clock services and bounded startup arguments");
		}
		if (vm.IsClosed()) {
			throw std::runtime_error("script driver phases require their live bootstrapped VM");
		}

		const auto name = DriverCString(filename);
		if (name.size() > limits.MaxStartupBytes) {
			throw std::runtime_error("script driver startup filename exceeds byte bound");
		}

		std::size_t bytes = name.size();
		for (const auto argument : arguments) {
			const auto count = DriverCString(argument).size();
			if (count > limits.MaxStartupBytes - bytes) {
				throw std::runtime_error("script driver startup arguments exceed byte bound");
			}

			bytes += count;
		}

		m_Filename = name;
	}

	void ScriptDriverPhases::OnProcess() {
		if (m_Failed || m_Vm.IsClosed()) {
			throw std::runtime_error("script driver phase owner is failed or its VM is closed");
		}

		const ScriptDriverPhaseCall call(m_Calling);
		try {
			switch (m_Phase) {
			case ScriptDriverPhase::ArmSetup:
				m_Phase = ScriptDriverPhase::Setup;
				return;
			case ScriptDriverPhase::Setup:
				m_Services.Setup();
				m_Phase = ScriptDriverPhase::Startup;
				return;
			case ScriptDriverPhase::Startup:
				if (!m_Filename.empty()) {
					m_StartupSucceeded = StartRootThread();
				}

				m_Phase = ScriptDriverPhase::Steady;
				m_Arguments = {};
				return;
			case ScriptDriverPhase::Steady:
				AdvanceScheduler();
				return;
			}
		} catch (...) {
			// Authored safety boundary: a partial setup or service failure is never silently retried.
			m_Failed = true;
			throw;
		}
	}

	bool ScriptDriverPhases::StartRootThread() {
		const auto context = m_Vm.RootContext();
		if (m_Vm.CurrentContext() != context || m_Vm.ContextState(context) != SquirrelContextState::Idle) {
			throw std::runtime_error("script driver startup requires the idle root VM outside script execution");
		}

		const auto root = m_Vm.RootTableInContext(context);
		try {
			const auto thread = m_Vm.ReadSlotInContext(context, root, std::string("Thread"), m_Limits.StartupInstructions);
			std::vector<SquirrelValue> parameters;
			parameters.reserve(m_Arguments.size() + 3);
			parameters.push_back(root);
			parameters.emplace_back();
			parameters.emplace_back(m_Filename);
			for (const auto argument : m_Arguments) {
				parameters.emplace_back(std::string(DriverCString(argument)));
			}

			static_cast<void>(m_Vm.InvokeThenDiscardResultInContext(context, thread, parameters, m_Limits.StartupInstructions, true));
		} catch (const SquirrelVmException&) {
			return false;
		}

		return true;
	}

	void ScriptDriverPhases::AdvanceScheduler() {
		const auto step = m_Services.ReadFrameStep();
		if (!std::isfinite(step) || step < 0.0f || step >= 2147483648.0f) {
			throw std::runtime_error("script driver cached frame step is outside the supported CVTTSS2SI domain");
		}

		static_cast<void>(m_Scheduler.Advance(static_cast<std::int32_t>(step)));
	}

	ScriptDriverPhase ScriptDriverPhases::Phase() const noexcept {
		return m_Phase;
	}

	std::optional<bool> ScriptDriverPhases::StartupSucceeded() const noexcept {
		return m_StartupSucceeded;
	}

	bool ScriptDriverPhases::Failed() const noexcept {
		return m_Failed;
	}

} // namespace sgre::script
