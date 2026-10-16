#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct NativeCleanupObservation {
	std::size_t Calls = 0;
	bool SelfAlive = false;
	bool NameExpired = false;
	bool EnvironmentExpired = false;
	std::exception_ptr Failure;
};

class NativeCallbackOwner {
public:
	NativeCallbackOwner(SquirrelVm& vm, const SquirrelValue& self_weak, const SquirrelValue& name_weak, const sgre::script::SquirrelObjectIdentity& environment_identity, NativeCleanupObservation& observation) : m_Vm(vm), m_SelfWeak(self_weak), m_NameWeak(name_weak), m_EnvironmentIdentity(environment_identity), m_Observation(observation) {
	}

	~NativeCallbackOwner() {
		++m_Observation.Calls;
		try {
			m_Observation.SelfAlive = !std::holds_alternative<std::monostate>(m_Vm.ResolveWeakReference(m_SelfWeak));
			m_Observation.NameExpired = std::holds_alternative<std::monostate>(m_Vm.ResolveWeakReference(m_NameWeak));
			m_Observation.EnvironmentExpired = !m_Vm.ResolveObjectIdentity(m_EnvironmentIdentity);
		} catch (...) {
			m_Observation.Failure = std::current_exception();
		}
	}

private:
	SquirrelVm& m_Vm;
	const SquirrelValue& m_SelfWeak;
	const SquirrelValue& m_NameWeak;
	const sgre::script::SquirrelObjectIdentity& m_EnvironmentIdentity;
	NativeCleanupObservation& m_Observation;
};

static void TestNativeMetadataBeforeHostCallableCleanup(bool collect_first) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelValue self_weak;
	SquirrelValue name_weak;
	sgre::script::SquirrelObjectIdentity environment_identity;
	NativeCleanupObservation observation;
	SquirrelVm vm(limits);
	const auto environment = vm.CreateTable();
	const auto environment_root = vm.Retain(environment);
	// Bound and unbound native closures share the same real host callback owner while cloning.
	auto owner = std::make_shared<NativeCallbackOwner>(vm, self_weak, name_weak, environment_identity, observation);
	sgre::script::SquirrelNativeSignature signature;
	signature.Name = "independent-native-callable-lifetime-name";
	signature.ParameterCheck = 1;
	SquirrelValue closure = vm.CreateNativeClosure("owned_native_callable", [owner](SquirrelVm&, std::span<const SquirrelValue>) {
		Require(static_cast<bool>(owner), "the native callback must retain its real host owner during invocation");
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t(11);

		return result;
	},
	    signature);
	closure = vm.BindEnvironment(closure, environment);
	owner.reset();
	self_weak = vm.CreateWeakReference(closure);
	name_weak = vm.CreateWeakReference(vm.CreateString(*signature.Name));
	SquirrelValue environment_wrapper = vm.CreateWeakReference(environment);
	environment_identity = vm.BorrowObjectIdentity(environment_wrapper);
	environment_wrapper = {};
	{
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 11, "the live recovered native callback path must remain invocable before retirement");
	}

	vm.ResetContext(vm.RootContext());
	if (collect_first) {
		Require(vm.CollectGarbage() == 1, "collection must finalize the unrooted native closure while its ordinary caller reference survives");
		vm.RethrowReleaseFailure();
		Require(observation.Calls == 0 && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(name_weak)) && vm.ResolveObjectIdentity(environment_identity), "native collection finalization must preserve metadata and host callback ownership until actual deletion");
	}

	closure = {};
	vm.RethrowReleaseFailure();
	if (observation.Failure) {
		std::rethrow_exception(observation.Failure);
	}

	Require(observation.Calls == 1 && observation.SelfAlive && observation.NameExpired && observation.EnvironmentExpired, "native name and weak environment must retire before host callable cleanup while the parent weak identity remains alive");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(self_weak)), "native closure base destruction must expire self after callback owner cleanup completes");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestNativeMetadataBeforeHostCallableCleanup(false);
		TestNativeMetadataBeforeHostCallableCleanup(true);
		std::cout << "Squirrel native closure metadata lifetime checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
