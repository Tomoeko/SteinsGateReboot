#pragma once

#include "sgre/scenario/calendar_restore.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <memory>
#include <string>

namespace sgre::script {

	enum class OriginalCalendarRestorePhase {
		Installing,
		Armed,
		Applying,
		Applied,
		Preserved,
		Cancelled,
		Failed
	};

	struct OriginalCalendarRestoreObservation {
		OriginalCalendarRestorePhase Phase = OriginalCalendarRestorePhase::Installing;
		std::string Reason;
	};

	class OriginalCalendarRestore {
	public:
		// Explicit cold-jump completion only. The VM outlives this owner.
		OriginalCalendarRestore(SquirrelVm& vm, scenario::ScenarioCalendarCommand command);
		~OriginalCalendarRestore();
		OriginalCalendarRestore(const OriginalCalendarRestore&) = delete;
		OriginalCalendarRestore& operator=(const OriginalCalendarRestore&) = delete;
		// Repeat at the existing idle module-publication boundary until installed.
		// Installation must precede MyPlayer derivation and instance/class locking.
		void InstallAvailableHook();
		void Cancel() noexcept;
		[[nodiscard]] const OriginalCalendarRestoreObservation& Observation() const noexcept;

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
