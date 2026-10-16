#include "sgre/script/application_finish_bindings.hpp"

#include <array>
#include <bit>
#include <span>
#include <stdexcept>
#include <utility>

namespace sgre::script {

	class ScriptApplicationFinishBindings::Implementation {
	public:
		explicit Implementation(SquirrelVm& vm) : Vm(vm), Callback(vm.Retain({})) {
		}

		SquirrelVm& Vm;
		SquirrelHostRoot Callback;

		void SetCallback(const SquirrelValue& callback) {
			if (Vm.IsClosed()) {
				throw std::runtime_error("application callback VM is closed");
			}

			// 2276d0 replaces the retained ObjectInfo. Keep the supplied object, including a weakref or null.
			Callback = Vm.Retain(callback);
		}

		ApplicationFinishEvaluation Evaluate(std::int32_t reason, std::uint64_t instruction_budget) {
			if (Vm.IsClosed()) {
				throw std::runtime_error("application callback VM is closed");
			}
			if (instruction_budget == 0) {
				throw std::runtime_error("application callback requires a positive instruction budget");
			}

			ApplicationFinishEvaluation evaluation;
			if (reason != 1 && reason != 2) {
				evaluation.Outcome = ApplicationFinishOutcome::SoundShutdownRequired;

				return evaluation;
			}

			auto callback = Callback.Value();
			if (Vm.Type(callback) == SquirrelValueType::WeakReference) {
				callback = Vm.ResolveWeakReference(callback);
			}
			if (std::holds_alternative<std::monostate>(callback)) {
				return evaluation;
			}

			const auto context = Vm.RootContext();
			const std::array<SquirrelValue, 2> arguments = {Vm.RootTableInContext(context), reason};
			try {
				static_cast<void>(Vm.InvokeObserveCompletedResultInContext(context, callback, arguments, [&](const SquirrelValue& output) {
					// 227b30 reads the native getter before 048b4c pops this same completed output.
					const auto boolean = TryNativeInteger(output);
					if (!boolean) {
						evaluation.Outcome = ApplicationFinishOutcome::ApprovedAfterCallbackFailure;
						evaluation.CallbackFailure = "native callback result is not boolean or numeric";
					} else {
						evaluation.Outcome = *boolean != 0 ? ApplicationFinishOutcome::ApprovedByCallback : ApplicationFinishOutcome::RejectedByCallback;
					}
				},
				    instruction_budget, true));
			} catch (const SquirrelVmException& error) {
				// Native sq_call failures have a typed VM exception; authored budgets/admission guards propagate.
				// 048b1a keeps the native default true for an actual callback failure.
				evaluation.Outcome = ApplicationFinishOutcome::ApprovedAfterCallbackFailure;
				evaluation.CallbackFailure = error.what();
			}

			return evaluation;
		}
	};

	ScriptApplicationFinishBindings::ScriptApplicationFinishBindings(SquirrelVm& vm) : m_Implementation(std::make_shared<Implementation>(vm)) {
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto closure = vm.CreateNativeClosure("entryRequestFinishCallback", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("application finish callback bindings have expired");
			}
			if (arguments.size() < 2) {
				throw std::runtime_error("entryRequestFinishCallback requires its callback argument");
			}

			// The native one-object adapter reads slot 2 and returns zero results; additional slots are unused.
			owner->SetCallback(arguments[1]);

			return SquirrelNativeResult::NoValues();
		});
		vm.NewSlot(vm.RootTableInContext(vm.RootContext()), std::string("entryRequestFinishCallback"), closure);
	}

	ScriptApplicationFinishBindings::~ScriptApplicationFinishBindings() = default;

	ApplicationFinishEvaluation ScriptApplicationFinishBindings::Evaluate(std::int32_t native_reason, std::uint64_t instruction_budget) {
		const auto owner = m_Implementation;

		return owner->Evaluate(native_reason, instruction_budget);
	}

	bool ScriptApplicationFinishBindings::EvaluateCloseRequest(std::uint32_t native_reason, std::uint64_t instruction_budget) {
		const auto evaluation = Evaluate(std::bit_cast<std::int32_t>(native_reason), instruction_budget);
		switch (evaluation.Outcome) {
		case ApplicationFinishOutcome::ApprovedByCallback:
		case ApplicationFinishOutcome::ApprovedAfterCallbackFailure:
			return true;
		case ApplicationFinishOutcome::RejectedByCallback:
			return false;
		case ApplicationFinishOutcome::ConfirmationRequired:
			throw std::runtime_error("application close requires its actual platform confirmation; no registered live script callback handled it");
		case ApplicationFinishOutcome::SoundShutdownRequired:
			throw std::runtime_error("application close reason requires its actual Sound shutdown before approval");
		}

		throw std::runtime_error("application close evaluation returned an unknown outcome");
	}

} // namespace sgre::script
