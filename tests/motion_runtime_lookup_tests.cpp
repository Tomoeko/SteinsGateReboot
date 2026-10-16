#include "sgre/script/motion_bindings.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include "motion_startup_observation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected runtime lookup/startup-service rejection was not reported");
}

static sgre::script::SquirrelVmLimits Limits() {
	sgre::script::SquirrelVmLimits result;
	result.ReclaimObjects = true;

	return result;
}

class Fixture {
public:
	Fixture() : Vm(Limits()), Objects(Vm), Psb(Vm, Objects), Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		            throw std::runtime_error("runtime lookup fixture unexpectedly requested texture publication");
	            },
	                                                             [](auto) {
		                                                             throw std::runtime_error("runtime lookup fixture unexpectedly released a texture");
	                                                             }),
	            Assets(Cache), Tree(8), Layers(Vm, Objects, Tree), Clock(0) {
		try {
			Vm.InstallRecoveredBaseLibrary();
			Layers.ConfigureMotionOwners(Psb, Assets, Cache);
			Leaf = Vm.Invoke(Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{Vm.RootTable()}).Value;
			for (std::size_t index = 0; index < Seed.size(); ++index) {
				// Explicit fixture input only; the original startup profile creates no application seed/runtime.
				Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
			}
		} catch (...) {
			const auto failure = std::current_exception();
			CloseOrTerminate();

			std::rethrow_exception(failure);
		}
	}

	~Fixture() noexcept {
		CloseOrTerminate();
	}

	void PrepareFixtureRuntimes() {
		if (Primary || Secondary) {
			throw std::runtime_error("runtime lookup fixture prepares its actual runtimes once");
		}

		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			return Layers.MotionResources(Leaf).SourceAliases(project);
		};
		Primary = std::make_unique<sgre::scene::MotionManagerRuntime>(Layers.MotionResources(Leaf).Primary(), Clock, Seed, services, 16);
		Secondary = std::make_unique<sgre::scene::MotionManagerRuntime>(Layers.MotionResources(Leaf).Secondary(), Clock, Seed, services, 16);
		Layers.AttachMotionRuntime(Leaf, *Primary);
		Layers.AttachMotionRuntime(Leaf, *Secondary);
	}

	void ReleaseLayer() {
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(Leaf) && Objects.HasInstance(Leaf)) {
			Layers.ReleaseInstance(Leaf);
		}
	}

	void Close() {
		if (m_Closed) {
			return;
		}

		ReleaseLayer();
		Leaf = {};
		if (Primary) {
			Primary->Close();
		}
		if (Secondary) {
			Secondary->Close();
		}
		Vm.Close();
		Cache.Close();
		m_Closed = true;
	}

	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Assets;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::core::FrameClock Clock;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::script::SquirrelValue Leaf;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Primary;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Secondary;

private:
	void CloseOrTerminate() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	bool m_Closed = false;
};

static void TestActualRegisteredLookupAndRetirement() {
	Fixture fixture;
	const auto& resources = fixture.Layers.MotionResources(fixture.Leaf);
	RequireFailure([&] {
		static_cast<void>(fixture.Layers.MotionRuntime(fixture.Leaf, resources.Secondary()));
	},
	    "unregistered");
	sgre::scene::MotionManagerRuntime::LayerBinding empty;
	RequireFailure([&] {
		static_cast<void>(empty.BorrowRuntime());
	},
	    "borrow is empty");
	fixture.PrepareFixtureRuntimes();
	const auto roots_before = fixture.Vm.RetainedReferenceCount(fixture.Leaf);
	Require(&fixture.Layers.MotionRuntime(fixture.Leaf, resources.Primary()) == fixture.Primary.get() && &fixture.Layers.MotionRuntime(fixture.Leaf, resources.Secondary()) == fixture.Secondary.get(), "actual per-slot registrations return their exact existing runtime identities");
	Require(fixture.Vm.RetainedReferenceCount(fixture.Leaf) == roots_before && fixture.Primary->PlayerCount() == 0 && fixture.Secondary->PlayerCount() == 0 && fixture.Layers.MotionTasks(fixture.Leaf).Group(sgre::scene::MotionTaskGroup::Second).Count() == 0, "observational lookup creates no receiver retain/player/task");
	Require(fixture.Primary->NextUnitFloat() == fixture.Secondary->NextUnitFloat(), "lookup does not consume either equal prepared fixture sampler");
	sgre::scene::MotionResourceManager foreign(fixture.Cache);
	RequireFailure([&] {
		static_cast<void>(fixture.Layers.MotionRuntime(fixture.Leaf, foreign));
	},
	    "owned Primary or Secondary");
	RequireFailure([&] {
		static_cast<void>(fixture.Layers.MotionRuntime(fixture.Vm.CreateTable(), resources.Primary()));
	},
	    "native LayerBase instance");
	fixture.Secondary->Close();
	RequireFailure([&] {
		static_cast<void>(fixture.Layers.MotionRuntime(fixture.Leaf, resources.Secondary()));
	},
	    "borrow is retired");
	Require(&fixture.Layers.MotionRuntime(fixture.Leaf, resources.Primary()) == fixture.Primary.get(), "a retired sibling registration does not replace its live actual manager slot");
	fixture.Layers.DetachMotionRuntime(fixture.Leaf, *fixture.Secondary);
	RequireFailure([&] {
		static_cast<void>(fixture.Layers.MotionRuntime(fixture.Leaf, resources.Secondary()));
	},
	    "unregistered");
	const auto retained_receiver = fixture.Leaf;
	fixture.ReleaseLayer();
	RequireFailure([&] {
		// Keep the manager argument genuinely alive; the retired receiver must reject before any slot lookup.
		static_cast<void>(fixture.Layers.MotionRuntime(retained_receiver, foreign));
	},
	    "no live native scene state");
}

static void TestRealClassRegistrationWithUnavailableApplicationOwners() {
	Fixture fixture;
	std::size_t runtime_lookups = 0;
	std::size_t mode_reads = 0;
	sgre::script::ScriptMotionBindingServices services;
	services.ResolveRuntime = [&](const auto& receiver, auto source) -> sgre::scene::MotionManagerRuntime& {
		++runtime_lookups;
		const auto& resources = fixture.Layers.MotionResources(receiver);
		const auto& manager = source == sgre::script::MotionLayerSource::Primary ? resources.Primary() : resources.Secondary();

		return fixture.Layers.MotionRuntime(receiver, manager);
	};
	services.ReadAsynchronousMode = [&]() -> bool {
		++mode_reads;
		throw std::runtime_error(std::string(sgre::test::kStartupMotionModeUnavailable));
	};
	sgre::script::ScriptMotionBindings bindings(fixture.Vm, fixture.Objects, fixture.Layers, std::move(services));
	Require(fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string("Motion")) == bindings.MotionClass() && fixture.Vm.Type(bindings.MotionClass()) == sgre::script::SquirrelValueType::Class && runtime_lookups == 0 && mode_reads == 0, "actual root class registration does not invoke unavailable application services");
	const auto derived = fixture.Vm.CreateClass(bindings.MotionClass());
	Require(fixture.Vm.GetSlot(derived, std::string("constructor")) == fixture.Vm.GetSlot(bindings.MotionClass(), std::string("constructor")) && fixture.Vm.GetSlot(derived, std::string("getEventEnabled")) == fixture.Vm.GetSlot(bindings.MotionClass(), std::string("getEventEnabled")) && runtime_lookups == 0 && mode_reads == 0, "actual derived class and inherited method lookups preserve closure identities without running its constructor/property body");
	auto uninitialized = fixture.Vm.CreateInstance(derived);
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(fixture.Vm.GetSlot(uninitialized, std::string("getEventEnabled")), std::array<sgre::script::SquirrelValue, 1>{uninitialized}));
	},
	    "genuine initialized native receiver");
	const auto weak = fixture.Vm.Invoke(fixture.Vm.GetSlot(uninitialized, std::string("weakref")), std::array<sgre::script::SquirrelValue, 1>{uninitialized}).Value;
	Require(fixture.Vm.ResolveWeakReference(weak) == uninitialized, "own native weakref needs the actual VM identity rather than a fabricated initialized Motion payload");
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(bindings.MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}));
	},
	    "unregistered");
	Require(runtime_lookups == 1 && mode_reads == 0 && bindings.NativeOwners().InstanceCount() == 0 && fixture.Layers.MotionTasks(fixture.Leaf).Group(sgre::scene::MotionTaskGroup::Second).Count() == 0, "unavailable runtime rejects real class construction before mode/player/task ownership is committed");
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(fixture.Vm.GetSlot(bindings.MotionClass(), std::string("getDefaultMultiThread")), std::array<sgre::script::SquirrelValue, 1>{bindings.MotionClass()}));
	},
	    sgre::test::kStartupMotionModeUnavailable);
	Require(mode_reads == 1, "explicit mode read invokes its unavailable service instead of returning a false fallback");
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(fixture.Vm.GetSlot(bindings.MotionClass(), std::string("setDefaultMultiThread")), std::array<sgre::script::SquirrelValue, 2>{bindings.MotionClass(), true}));
	},
	    "requires its actual containing application state");
	uninitialized = {};
	fixture.Vm.RethrowReleaseFailure();
	bindings.NativeOwners().Close();
}

static void TestBoundedNumericStartupObservation() {
	const auto first = sgre::test::ObserveStartupFailure("script/main.nut:main at instruction 214; script/basicpicture.nut:constructor at instruction 2");
	Require(first.Script == sgre::test::StartupFailureScript::Main && first.Instruction == 214, "script category belongs to the first numeric instruction location rather than another nested failure");
	const auto malformed = sgre::test::ObserveStartupFailure("script/basicpicture.nut:main at instruction 18446744073709551616");
	Require(malformed.Script == sgre::test::StartupFailureScript::BasicPicture && !malformed.Instruction && malformed.MalformedInstruction, "overflowed numeric locations remain explicitly malformed");
	sgre::test::MotionStartupObservation observation;
	observation.ActualClassRegistered = true;
	for (std::size_t index = 0; index < sgre::test::kStartupFailureSamples + 3; ++index) {
		observation.RecordFailure("private narrative must not escape: script/basicpicture.nut:main at instruction 315");
	}
	std::ostringstream output;
	sgre::test::WriteMotionStartupObservation(output, observation);
	Require(observation.FailureCount == sgre::test::kStartupFailureSamples + 3 && observation.StoredFailures == sgre::test::kStartupFailureSamples && output.str().size() <= sgre::test::kStartupObservationBytes && output.str().find("private narrative") == std::string::npos && output.str().find("acceptance=PENDING") != std::string::npos, "aggregate retains total counts and bounded numeric samples without exporting diagnostic/narrative text or declaring acceptance");
}

int main() {
	try {
		TestActualRegisteredLookupAndRetirement();
		TestRealClassRegistrationWithUnavailableApplicationOwners();
		TestBoundedNumericStartupObservation();
		std::cout << "Motion runtime lookup, real class registration and bounded observation fixture groups PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
