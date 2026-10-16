#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace sgre::script {

	enum class ApplicationFinishOutcome {
		ApprovedByCallback,
		RejectedByCallback,
		ApprovedAfterCallbackFailure,
		ConfirmationRequired,
		SoundShutdownRequired
	};

	struct ApplicationFinishEvaluation {
		ApplicationFinishOutcome Outcome = ApplicationFinishOutcome::ConfirmationRequired;
		std::optional<std::string> CallbackFailure;
	};

	class ScriptApplicationFinishBindings {
	public:
		// The VM encloses registration and evaluation. Destruction releases the retained callback.
		explicit ScriptApplicationFinishBindings(SquirrelVm& vm);
		~ScriptApplicationFinishBindings();
		ScriptApplicationFinishBindings(const ScriptApplicationFinishBindings&) = delete;
		ScriptApplicationFinishBindings& operator=(const ScriptApplicationFinishBindings&) = delete;
		ScriptApplicationFinishBindings(ScriptApplicationFinishBindings&&) = delete;
		ScriptApplicationFinishBindings& operator=(ScriptApplicationFinishBindings&&) = delete;

		// Reasons 1 and 2 are the observed native callback branch. Other reasons require real sound shutdown.
		// Confirmation and shutdown are work for the host; this method does not perform or claim either.
		[[nodiscard]] ApplicationFinishEvaluation Evaluate(std::int32_t native_reason, std::uint64_t instruction_budget = 1000000);
		// Actual application close transport. Unperformed platform confirmation or Sound work rejects explicitly.
		[[nodiscard]] bool EvaluateCloseRequest(std::uint32_t native_reason, std::uint64_t instruction_budget = 1000000);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
