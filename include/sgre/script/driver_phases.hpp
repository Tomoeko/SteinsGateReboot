#pragma once

#include "sgre/script/script_scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sgre::script {

	enum class ScriptDriverPhase {
		ArmSetup,
		Setup,
		Startup,
		Steady
	};

	struct ScriptDriverPhaseLimits {
		std::size_t MaxArguments = 256;
		std::size_t MaxStartupBytes = 64 * 1024;
		std::uint64_t StartupInstructions = 1000000;
	};

	struct ScriptDriverPhaseServices {
		// Publishes the genuine DefaultDriverScene and then initializes the concrete logger pair.
		// Real resource, metric, renderer and VM bootstrap owners remain caller supplied.
		std::function<void()> Setup;
		// Reads the real clock's already prepared selected step; it must not advance the clock a second time.
		std::function<float()> ReadFrameStep;
	};

	class ScriptDriverPhases {
	public:
		// The VM/scheduler/services outlive this owner. Argument views borrow immutable NUL-prefix bytes until Startup finishes.
		// The native constructor owns its filename and borrows argv; this owner follows that relationship.
		ScriptDriverPhases(SquirrelVm& vm, ScriptScheduler& scheduler, std::string_view filename, std::span<const std::string_view> arguments, ScriptDriverPhaseServices services, ScriptDriverPhaseLimits limits = {});
		ScriptDriverPhases(const ScriptDriverPhases&) = delete;
		ScriptDriverPhases& operator=(const ScriptDriverPhases&) = delete;
		ScriptDriverPhases(ScriptDriverPhases&&) = delete;
		ScriptDriverPhases& operator=(ScriptDriverPhases&&) = delete;

		// Called by the actual CommonProcessTasks onProcess, after its serial-empty gate and before parallel traversal.
		void OnProcess();
		[[nodiscard]] ScriptDriverPhase Phase() const noexcept;
		[[nodiscard]] std::optional<bool> StartupSucceeded() const noexcept;
		[[nodiscard]] bool Failed() const noexcept;

	private:
		[[nodiscard]] bool StartRootThread();
		void AdvanceScheduler();
		SquirrelVm& m_Vm;
		ScriptScheduler& m_Scheduler;
		std::string m_Filename;
		std::span<const std::string_view> m_Arguments;
		ScriptDriverPhaseServices m_Services;
		ScriptDriverPhaseLimits m_Limits;
		ScriptDriverPhase m_Phase = ScriptDriverPhase::ArmSetup;
		std::optional<bool> m_StartupSucceeded;
		bool m_Calling = false;
		bool m_Failed = false;
	};

} // namespace sgre::script
