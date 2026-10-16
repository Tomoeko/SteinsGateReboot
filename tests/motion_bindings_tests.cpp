#include "sgre/script/motion_bindings.hpp"

#include "motion_root_opacity_native_vectors.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

	throw std::runtime_error("expected Motion adapter rejection containing '" + std::string(expected) + "' was not reported");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Integer(std::int64_t value) {
	return Atom(value);
}

static sgre::resource::PsbValue Frame(float time, std::int64_t type, sgre::resource::PsbValue content = {}) {
	return Atom(sgre::resource::PsbObject{{"time", Atom(static_cast<double>(time))}, {"type", Integer(type)}, {"content", std::move(content)}});
}

static std::shared_ptr<const sgre::resource::PsbDocument> EventDocument() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Integer(16)}, {"height", Integer(16)}, {"truncated_width", Integer(16)}, {"truncated_height", Integer(16)}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(PsbObject{{"left", Integer(0)}, {"top", Integer(0)}, {"width", Integer(16)}, {"height", Integer(16)}, {"originX", Integer(0)}, {"originY", Integer(0)}, {"attr", Integer(0)}});
	const auto source = Atom(PsbObject{{"texture", std::move(texture)}, {"icon", Atom(PsbObject{{"tile", icon}})}});
	const auto attributes = Atom(PsbObject{{"mask", Integer(3 | 0x40000)}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(PsbArray{Integer(0), Integer(0), Integer(0)})}, {"ox", Integer(0)}, {"oy", Integer(0)}, {"act", Atom(std::string("frame\0ignored", 13))}});
	const auto layer = Atom(PsbObject{{"label", Atom(std::string("sprite"))}, {"type", Integer(0)}, {"inheritMask", Integer(4)}, {"coordinate", Integer(0)}, {"transformOrder", Atom(PsbArray{Integer(0), Integer(3), Integer(2), Integer(1)})}, {"children", Atom(PsbArray{})}, {"frameList", Atom(PsbArray{Frame(0, 2, attributes), Frame(4, 2, attributes), Frame(8, 0)})}, {"stencilType", Integer(0)}, {"objTriPriority", Integer(0)}, {"meshTransform", Integer(0)}, {"joinTarget", Integer(0)}, {"groundCorrection", Integer(0)}, {"parameterize", sgre::resource::PsbValue{}}});
	const auto sync = Atom(PsbObject{{"label", Atom(std::string("sync\0suffix", 11))}, {"sync", Integer(1)}, {"align", Integer(0)}, {"action", Atom(std::string("tag\0suffix", 10))}});
	const auto later_label = Atom(PsbObject{{"label", Atom(std::string("sync"))}, {"sync", Integer(0)}, {"align", Integer(0)}, {"action", Atom(std::string{})}});
	const auto timeline = Atom(PsbObject{{"type", Integer(0)}, {"bounds", Atom(PsbObject{{"left", Integer(0)}, {"top", Integer(0)}, {"right", Integer(16)}, {"bottom", Integer(16)}})}, {"lastTime", Integer(8)}, {"loopTime", Integer(-1)}, {"layer", Atom(PsbArray{layer})}, {"priority", Atom(PsbArray{Frame(0, 1, Atom(PsbArray{Integer(0)})), Frame(8, 1)})}, {"tag", Atom(PsbArray{Frame(0, 0), Frame(2, 1, sync), Frame(4, 1, later_label), Frame(8, 0)})}, {"parameter", Atom(PsbArray{})}, {"variable", Atom(PsbArray{})}, {"parameterize", sgre::resource::PsbValue{}}});
	const auto character = Atom(PsbObject{{"type", Integer(0)}, {"motion", Atom(PsbObject{{"main", timeline}})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 127;
	document->SerializedSize = 383;
	document->Chunks.push_back({127, 256});
	document->Root = Atom(PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Adapter fixture"))}, {"easing", Atom(PsbArray{})}, {"source", Atom(PsbObject{{"atlas", source}})}, {"object", Atom(PsbObject{{"character", character}})}, {"sourceIconRenameMap", Atom(PsbObject{})}});
	document->Root.SerializedOffset = 44;

	return document;
}

static sgre::script::SquirrelVmLimits VmLimits() {
	sgre::script::SquirrelVmLimits result;
	result.ReclaimObjects = true;

	return result;
}

enum class ConstructorFailure {
	None,
	AfterPrimaryAssociation,
	InvalidConstantsAfterAssociations
};

struct RollbackObservation {
	std::size_t Associations = 0;
	std::size_t RemainingLayers = 1;
	bool CatchEntered = false;
	bool PrimaryClosedByLayer = false;
	bool SecondaryClosedByLayer = false;
	bool PrimaryClosed = false;
	bool SecondaryClosed = false;
	bool CleanupCompleted = false;
};

static bool IsRuntimeClosed(const sgre::scene::MotionManagerRuntime& runtime) {
	try {
		static_cast<void>(runtime.Manager());
	} catch (const std::runtime_error& error) {
		if (std::string_view(error.what()) == "motion manager runtime is closed") {
			return true;
		}

		throw;
	}

	return false;
}

class Fixture {
public:
	explicit Fixture(ConstructorFailure failure = ConstructorFailure::None, RollbackObservation* observation = nullptr, bool writable_mode = true, bool complete_scene = false)
	    : Vm(VmLimits()), Objects(Vm), Psb(Vm, Objects), Cache([this](auto, const auto&) {
		      return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	      },
	                                                         [this](auto) {
		                                                         ++Releases;
	                                                         }),
	      Assets(Cache), Tree(8), Layers(Vm, Objects, Tree), Clock(0) {
		m_RollbackObservation = observation;
		try {
			Vm.InstallRecoveredBaseLibrary();
			Layers.ConfigureMotionOwners(Psb, Assets, Cache);
			Leaf = Vm.Invoke(Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{Vm.RootTable()}).Value;
			for (std::size_t index = 0; index < Seed.size(); ++index) {
				// Supplied synthetic624-word input; no default application entropy or looper is manufactured.
				Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
			}
			sgre::scene::MotionManagerRuntimeServices services;
			services.ReadSourceAliases = [this](auto project) {
				return Layers.MotionResources(Leaf).SourceAliases(project);
			};
			if (complete_scene) {
				services.SceneAssets = &Assets;
				services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
					return Layers.MotionResources(Leaf).BorrowAsset(project);
				};
			}
			Primary = std::make_unique<sgre::scene::MotionManagerRuntime>(Layers.MotionResources(Leaf).Primary(), Clock, Seed, services, 16);
			Secondary = std::make_unique<sgre::scene::MotionManagerRuntime>(Layers.MotionResources(Leaf).Secondary(), Clock, Seed, services, 16);
			Layers.AttachMotionRuntime(Leaf, *Primary);
			if (m_RollbackObservation) {
				m_RollbackObservation->Associations = 1;
			}
			if (failure == ConstructorFailure::AfterPrimaryAssociation) {
				throw std::runtime_error("fixture failure after actual Primary association");
			}
			Layers.AttachMotionRuntime(Leaf, *Secondary);
			if (m_RollbackObservation) {
				m_RollbackObservation->Associations = 2;
			}
			Constants = Vm.CreateTable();
			Vm.NewSlot(Constants, std::string("extra"), std::int32_t{17});
			Vm.NewSlot(Constants, std::string("FORCE"), std::int32_t{99});
			Vm.NewSlot(Vm.ConstantTable(), std::string("MOTION"), failure == ConstructorFailure::InvalidConstantsAfterAssociations ? sgre::script::SquirrelValue{std::int32_t{0}} : Constants);
			sgre::script::ScriptMotionBindingServices binding_services;
			binding_services.ResolveRuntime = [this](const auto& leaf, auto source) -> sgre::scene::MotionManagerRuntime& {
				Require(leaf == Leaf, "resolver must receive the actual concrete Layer");
				Selections.push_back(source);
				if (OnResolve) {
					OnResolve();
				}

				return source == sgre::script::MotionLayerSource::Primary ? *Primary : *Secondary;
			};
			binding_services.ReadAsynchronousMode = [this] {
				return Asynchronous;
			};
			if (writable_mode) {
				binding_services.WriteAsynchronousMode = [this](bool mode) {
					Asynchronous = mode;
				};
			}
			Bindings = std::make_unique<sgre::script::ScriptMotionBindings>(Vm, Objects, Layers, std::move(binding_services));
		} catch (...) {
			const auto original_failure = std::current_exception();
			if (m_RollbackObservation) {
				m_RollbackObservation->CatchEntered = true;
			}
			try {
				Close();
			} catch (...) {
				// Rejected retirement cannot safely leave a Layer borrowing members about to unwind.
				std::terminate();
			}

			std::rethrow_exception(original_failure);
		}
	}

	~Fixture() noexcept {
		try {
			Close();
		} catch (...) {
			// Normal explicit Close reports failures; unwinding must not destroy attached runtimes first.
			std::terminate();
		}
	}

	void Close() {
		if (m_Closed) {
			return;
		}
		// Release actual invocation-result roots while the Layer and task owners still enclose them.
		Vm.ResetExecution();
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(Leaf)) {
			// Keep the real script receiver and every enclosing service alive through actual Layer retirement.
			if (Objects.HasInstance(Leaf)) {
				Layers.ReleaseInstance(Leaf);
			}
			if (m_RollbackObservation) {
				m_RollbackObservation->RemainingLayers = Tree.ChildCount();
				m_RollbackObservation->PrimaryClosedByLayer = m_RollbackObservation->Associations >= 1 && Primary && IsRuntimeClosed(*Primary);
				m_RollbackObservation->SecondaryClosedByLayer = m_RollbackObservation->Associations >= 2 && Secondary && IsRuntimeClosed(*Secondary);
			}
			Leaf = {};
		}
		if (Bindings) {
			Bindings->NativeOwners().Close();
		}
		if (Primary) {
			Primary->Close();
			if (m_RollbackObservation) {
				m_RollbackObservation->PrimaryClosed = IsRuntimeClosed(*Primary);
			}
		}
		if (Secondary) {
			Secondary->Close();
			if (m_RollbackObservation) {
				m_RollbackObservation->SecondaryClosed = IsRuntimeClosed(*Secondary);
			}
		}
		Vm.Close();
		Cache.Close();
		m_Closed = true;
		if (m_RollbackObservation) {
			m_RollbackObservation->CleanupCompleted = true;
		}
	}

	[[nodiscard]] sgre::script::SquirrelValue New(std::initializer_list<sgre::script::SquirrelValue> extras = {}) {
		std::vector<sgre::script::SquirrelValue> arguments{Vm.RootTable(), Leaf};
		arguments.insert(arguments.end(), extras.begin(), extras.end());

		return Vm.Invoke(Bindings->MotionClass(), arguments).Value;
	}

	[[nodiscard]] sgre::script::SquirrelValue Call(const sgre::script::SquirrelValue& instance, std::string_view name, std::initializer_list<sgre::script::SquirrelValue> extras = {}) {
		std::vector<sgre::script::SquirrelValue> arguments{instance};
		arguments.insert(arguments.end(), extras.begin(), extras.end());

		return Vm.Invoke(Vm.GetSlot(instance, std::string(name)), arguments).Value;
	}

	[[nodiscard]] sgre::scene::ParallelTaskSet& Second() {
		return Layers.MotionTasks(Leaf).Group(sgre::scene::MotionTaskGroup::Second);
	}

	[[nodiscard]] std::int32_t RegisterEvents() {
		auto bytes = std::make_shared<std::vector<std::uint8_t>>(383, 128);
		const auto object = Psb.WrapDocument(EventDocument(), bytes);
		const auto id = Call(Leaf, "registerMotionResource", {object});

		return std::get<std::int32_t>(id);
	}

	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	std::uint32_t Uploads = 0;
	std::uint32_t Releases = 0;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Assets;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::core::FrameClock Clock;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::script::SquirrelValue Leaf;
	sgre::script::SquirrelValue Constants;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Primary;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Secondary;
	bool Asynchronous = false;
	std::vector<sgre::script::MotionLayerSource> Selections;
	std::function<void()> OnResolve;
	std::unique_ptr<sgre::script::ScriptMotionBindings> Bindings;

private:
	RollbackObservation* m_RollbackObservation = nullptr;
	bool m_Closed = false;
};

static void TestConstructionRollbackAfterAssociation() {
	RollbackObservation primary_only;
	RequireFailure([&] {
		Fixture fixture(ConstructorFailure::AfterPrimaryAssociation, &primary_only);
	},
	    "fixture failure after actual Primary association");
	Require(primary_only.CatchEntered && primary_only.Associations == 1 && primary_only.RemainingLayers == 0 && primary_only.PrimaryClosedByLayer && !primary_only.SecondaryClosedByLayer && primary_only.PrimaryClosed && primary_only.SecondaryClosed && primary_only.CleanupCompleted, "actual Layer closes committed Primary before source disposal; constructed unattached Secondary closes explicitly before member unwind");

	RollbackObservation both;
	RequireFailure([&] {
		Fixture fixture(ConstructorFailure::InvalidConstantsAfterAssociations, &both);
	},
	    "existing actual constant-table entry");
	Require(both.CatchEntered && both.Associations == 2 && both.RemainingLayers == 0 && both.PrimaryClosedByLayer && both.SecondaryClosedByLayer && both.PrimaryClosed && both.SecondaryClosed && both.CleanupCompleted, "actual binding-installation error survives complete Layer-first retirement of both associations");
}

static void TestActualFactoryAndConstantTable() {
	Fixture fixture;
	Require(fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string("Motion")) == fixture.Bindings->MotionClass(), "native rootMotion must be the actual Object-derived class");
	Require(!fixture.Vm.HasSlot(fixture.Vm.RootTable(), std::string("MOTION")) && fixture.Vm.GetSlot(fixture.Vm.ConstantTable(), std::string("MOTION")) == fixture.Constants, "MOTION uses the separate existing shared constant table");
	Require(std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Constants, std::string("extra"))) == 17 && std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Constants, std::string("FORCE"))) == 1 && std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Constants, std::string("INSERT"))) == 1, "exact constants overwrite their keys while retaining existing table identity and unrelated fields");
	auto first = fixture.New({std::string("not numeric"), false});
	auto second = fixture.New({std::int32_t{-1}, std::int32_t{-1}});
	Require(fixture.Vm.FindRawSlot(fixture.Bindings->MotionClass(), std::string("weakref")).has_value(), "Motion installs its own native weakref slot rather than relying on a default delegate");
	const auto weak = fixture.Call(first, "weakref");
	Require(fixture.Vm.ResolveWeakReference(weak) == first, "the own native weakref preserves the actual last receiver identity");
	Require(std::get<std::int32_t>(fixture.Call(first, "weakref", {std::int32_t{5}})) == 5, "native capture0 weakref uses the last explicit argument and passes nonref primitives unchanged");
	const auto first_task = fixture.Bindings->NativeOwners().Task(first);
	const auto second_task = fixture.Bindings->NativeOwners().Task(second);
	std::array<sgre::scene::ParallelTaskHandle, 2> order;
	Require(fixture.Selections == std::vector<sgre::script::MotionLayerSource>{sgre::script::MotionLayerSource::Secondary, sgre::script::MotionLayerSource::Primary} && fixture.Second().WriteOrder(order) == 2 && order[0] == second_task.Handle() && order[1] == first_task.Handle(), "wrong integer type defaults0, nonzero manager and insertion preserve actual Primary/Second identities");
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), first}));
	},
	    "full selected-state clone owner");
	Require(!std::get<bool>(fixture.Call(fixture.Bindings->MotionClass(), "getDefaultMultiThread")), "actual application mode reader reports the supplied synchronous state");
	fixture.Asynchronous = true;
	Require(std::get<bool>(fixture.Call(fixture.Bindings->MotionClass(), "getDefaultMultiThread")), "static mode reader preserves true even though new async construction remains unsupported");
	RequireFailure([&] {
		static_cast<void>(fixture.New());
	},
	    "synchronous mode");
	Require(fixture.Bindings->NativeOwners().InstanceCount() == 2 && fixture.Second().Count() == 2, "rejected clone/async leave no constructed placeholder or task");
	first = {};
	second = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Second().Update();
	fixture.Asynchronous = false;
	fixture.OnResolve = [&] {
		RequireFailure([&] {
			static_cast<void>(fixture.New());
		},
		    "cannot reenter");
	};
	auto guarded = fixture.New();
	Require(fixture.Bindings->NativeOwners().InstanceCount() == 1 && fixture.Second().Count() == 1, "authored service reentry guard precedes any nested real wrapper/task construction");
	fixture.OnResolve = {};
	guarded = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Second().Update();
}

static void TestPrimitiveConversionsAndExplicitGates() {
	Fixture fixture;
	auto instance = fixture.New();
	static_cast<void>(fixture.Call(instance, "setChara", {std::int32_t{27}}));
	Require(sgre::script::StringText(fixture.Call(instance, "getChara")) == "27", "primitive name conversion uses actual VM tostring");
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("actual\0suffix", 13)}));
	Require(sgre::script::StringText(fixture.Call(instance, "getChara")) == "actual", "native string boundary truncates embeddedNUL only after VM conversion");
	static_cast<void>(fixture.Call(instance, "setSpeed", {std::string("not numeric")}));
	Require(std::get<float>(fixture.Call(instance, "getSpeed")) == 0, "float template defaults0 for wrong types rather than parsing strings");
	fixture.Vm.AssignSlot(instance, std::string("eventEnabled"), std::string{});
	Require(std::get<bool>(fixture.Vm.ReadSlot(instance, std::string("eventEnabled"))), "emptystring truth uses a real nonnull string value");
	fixture.Vm.AssignSlot(instance, std::string("eventEnabled"), std::monostate{});
	Require(!std::get<bool>(fixture.Vm.ReadSlot(instance, std::string("eventEnabled"))), "null boolean conversion is false");
	static_cast<void>(fixture.Call(instance, "setEventEnabled", {std::numeric_limits<float>::quiet_NaN()}));
	Require(std::get<bool>(fixture.Call(instance, "getEventEnabled")), "native truth treats unordered float as true");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("plain"), 1.0f}));
	Require(std::get<float>(fixture.Call(instance, "getVariable", {std::string("plain")})) == 1, "actual Motion variables use the task-owned local value map");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setChara", {fixture.Vm.CreateTable()}));
	},
	    "reentrant VM owner");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setTickCount", {std::monostate{}}));
	},
	    "invalid argument");
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Second().Update();
}

static void RethrowCallbackFailure(const std::exception_ptr& failure) {
	if (failure) {
		std::rethrow_exception(failure);
	}
}

static void TestNativeSyncControls() {
	Fixture fixture;
	static_cast<void>(fixture.RegisterEvents());
	const auto instance = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	const auto root = fixture.Vm.Retain(instance);
	Require(!std::get<bool>(fixture.Call(instance, "getSyncWaiting", {std::string("ignored")})), "unselected actual player starts outside a sync wait");
	static_cast<void>(fixture.Call(instance, "skipToSync", {true}));
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(instance, "play", {std::string("main")}));
	auto task = fixture.Bindings->NativeOwners().Task(instance);
	Require(std::get<float>(fixture.Call(instance, "getLoopTime", {true})) == -1, "actual selected non-looping timeline returns its raw retained loop word");
	const auto composition_pending = task.Commands().ClockState().InitialCompositionPending;
	static_cast<void>(fixture.Call(instance, "skipToSync", {std::int32_t{99}}));
	Require(task.Commands().ClockState().TickCount == 2 && task.Commands().ClockState().EvaluationTime == 2 && task.Commands().ClockState().InitialCompositionPending == composition_pending && !task.Commands().ClockState().InitialBoundaryTraversalPending && !std::get<bool>(fixture.Call(instance, "getSyncWaiting")), "skip selects the actual next sync without composing, queuing its event or adding initial-boundary traversal");
	fixture.Second().Update();
	Require(std::get<bool>(fixture.Call(instance, "getSyncWaiting")) && task.Commands().ClockState().TickCount == 2, "the genuine next update reaches the retained sync marker");
	static_cast<void>(fixture.Call(instance, "skipToSync"));
	Require(task.Commands().ClockState().TickCount == 2 && std::get<bool>(fixture.Call(instance, "getSyncWaiting")), "equal-time skip retains the current sync wait");
	const auto before_release = task.Commands().ClockState();
	static_cast<void>(fixture.Call(instance, "releaseSyncWait", {false, std::string("ignored")}));
	Require(!std::get<bool>(fixture.Call(instance, "getSyncWaiting")) && task.Commands().ClockState().TickCount == before_release.TickCount && task.Commands().ClockState().InitialTagDispatchPending == before_release.InitialTagDispatchPending, "release changes only the actual wait flag and ignores extra operands");
	fixture.Second().Update();
	Require(std::get<bool>(fixture.Call(instance, "getSyncWaiting")), "the retained pending seek can encounter the same sync again after release");
	static_cast<void>(fixture.Call(instance, "releaseSyncWait"));
	fixture.Second().Update();
	Require(task.Commands().ClockState().TickCount == 3 && !std::get<bool>(fixture.Call(instance, "getSyncWaiting")), "the following genuine delta advances after the pending sync traversal has been consumed");
	static_cast<void>(fixture.Call(instance, "skipToSync"));
	Require(task.Commands().ClockState().TickCount == 8, "no later sync falls back to actual timeline end");
	static_cast<void>(fixture.Call(instance, "stop"));
	static_cast<void>(fixture.Call(instance, "setTickCount", {5.0f}));
	static_cast<void>(fixture.Call(instance, "skipToSync"));
	Require(task.Commands().ClockState().TickCount == 5, "stopped player skip is a native no-op");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(std::get<float>(fixture.Call(instance, "getLoopTime", {true})) == 0, "detached loop query is native positive zero");
	Require(!std::get<bool>(fixture.Call(instance, "getSyncWaiting", {true})), "detached sync query does not access the retired player");
	static_cast<void>(fixture.Call(instance, "skipToSync", {true}));
	static_cast<void>(fixture.Call(instance, "releaseSyncWait", {true}));
	fixture.Bindings->NativeOwners().ReleaseInstance(instance);
}

static void TestSelectedActionSyncAndLabelInputs() {
	Fixture fixture;
	const auto project = fixture.RegisterEvents();
	const auto derived = fixture.Vm.CreateClass(fixture.Bindings->MotionClass());
	sgre::script::SquirrelObjectIdentity identity{};
	std::vector<std::string> delivered;
	std::exception_ptr callback_failure;
	const auto action = fixture.Vm.CreateNativeClosure("fixture.onAction", [&](auto& vm, auto arguments) {
		try {
			Require(arguments.size() == 3 && vm.BorrowObjectIdentity(arguments[0]) == identity && vm.RetainedReferenceCount(arguments[0]) == 0, "native action resolves its real receiver and temporary owned arguments without a persistent self root");
			delivered.push_back("action:" + sgre::script::StringText(arguments[1]) + ":" + sgre::script::StringText(arguments[2]));
			RequireFailure([&] {
				static_cast<void>(fixture.Call(fixture.Leaf, "unregisterMotionResource", {project}));
			},
			    "borrowed event delivery");
		} catch (...) {
			// Native-compatible delivery swallows VM callback failures; the fixture must retain its own first assertion.
			if (!callback_failure) {
				callback_failure = std::current_exception();
			}
		}

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const auto sync = fixture.Vm.CreateNativeClosure("fixture.onSync", [&](auto& vm, auto arguments) {
		try {
			Require(arguments.size() == 1 && vm.BorrowObjectIdentity(arguments[0]) == identity, "sync owns only the actual receiver argument");
			delivered.push_back("sync");
		} catch (...) {
			if (!callback_failure) {
				callback_failure = std::current_exception();
			}
		}

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	// Native callbacks are class methods, installed before the first derived instance locks its class.
	fixture.Vm.NewSlot(derived, std::string("onAction"), action);
	fixture.Vm.NewSlot(derived, std::string("onSync"), sync);
	auto instance = fixture.Vm.Invoke(derived, std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	identity = fixture.Vm.BorrowObjectIdentity(instance);
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(instance, "play", {std::string("main")}));
	fixture.Second().Update();
	RethrowCallbackFailure(callback_failure);
	Require(delivered.empty(), "disabled event forwarding consumes actual first selected layer action without a callback");
	fixture.Vm.AssignSlot(instance, std::string("eventEnabled"), true);
	fixture.Second().Update();
	RethrowCallbackFailure(callback_failure);
	Require(delivered.empty(), "the first unassigned delta reaches tick1 without the later sync tag");
	fixture.Second().Update();
	RethrowCallbackFailure(callback_failure);
	auto task = fixture.Bindings->NativeOwners().Task(instance);
	Require(delivered == std::vector<std::string>{"sync", "action::tag"} && task.Commands().ClockState().SyncWaiting && fixture.Layers.MotionResources(fixture.Leaf).Count() == 1, "actual sync precedes tag action and guarded unregister changes no resource ownership");
	task.PostUpdate();
	RethrowCallbackFailure(callback_failure);
	Require(delivered.size() == 2, "separate PostUpdate drains the already empty synchronous queue");
	static_cast<void>(fixture.Call(instance, "setTickCount", {std::string("sync")}));
	Require(std::get<float>(fixture.Call(instance, "getTickCount")) == 2, "string tick selects the first matching actual label tag, including C-string truncation");
	static_cast<void>(fixture.Call(instance, "setTickCount", {std::string("absent")}));
	Require(std::get<float>(fixture.Call(instance, "getTickCount")) == 2, "missing label performs no timing assignment");
	static_cast<void>(fixture.Call(instance, "play", {std::string("main"), std::int32_t{8}}));
	Require(std::get<float>(fixture.Call(instance, "getTickCount")) == 2, "same-name JOIN without Force preserves the selected clock through native early return");
	// Force bypasses the same-name no-op; the real assigned tick bypasses outgoing capture before ordinary reset.
	const auto join_serial = task.Commands().SelectionSerial();
	Require(task.Commands().ClockState().TickAssigned, "actual label assignment must reach the native JOIN snapshot bypass");
	static_cast<void>(fixture.Call(instance, "play", {std::string("main"), std::int32_t{9}}));
	Require(task.Commands().SelectionSerial() == join_serial + 1 && std::get<float>(fixture.Call(instance, "getTickCount")) == 0 && task.Commands().ClockState().Playing && task.Commands().ClockState().TickAssigned && !task.Commands().ClockState().SyncWaiting, "real VM Force JOIN must install the numeric player and reset its assigned clock/sync state");
	fixture.Second().Update();
	RethrowCallbackFailure(callback_failure);
	Require(task.Player().Rows().size() == 1 && fixture.Uploads == 1, "selected callback fixture owns a real prepared sprite and one registered atlas");
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Second().Update();
	RethrowCallbackFailure(callback_failure);
}

static void TestActualVariableMethods() {
	Fixture fixture;
	auto instance = fixture.New();
	const auto instance_root = fixture.Vm.Retain(instance);
	const auto leaf_root = fixture.Vm.Retain(fixture.Leaf);
	Require(std::get<float>(fixture.Call(instance, "getVariable", {std::string("missing"), std::int32_t{99}})) == 0, "native local getter returns zero after a miss and ignores extras");
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setVariable", {std::string("disable"), true})), "native immediate setter returns no values");
	Require(std::get<float>(fixture.Call(instance, "getVariable", {std::string("disable")})) == 1, "native numeric conversion accepts bool before the genuine local write");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("disable"), std::string("7"), fixture.Vm.CreateTable()}));
	Require(std::get<float>(fixture.Call(instance, "getVariable", {std::string("disable")})) == 0, "failed float/integer conversions default zero instead of parsing strings or treating objects as true");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::int32_t{27}, std::int32_t{-4}, 2.75f}));
	Require(std::get<float>(fixture.Call(instance, "getVariable", {std::int32_t{27}})) == -4, "both variable methods use actual VM tostring for primitive names");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("name\0suffix", 11), 6.25f}));
	Require(std::get<float>(fixture.Call(instance, "getVariable", {std::string("name")})) == 6.25f, "native variable names truncate at the first NUL");
	Require(fixture.Vm.Length(fixture.Call(instance, "getVariableKeys", {std::int32_t{99}})) == 0, "local writes without selected parameter registrations do not manufacture variable keys");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string{}, 13.0f}));
	Require(std::get<float>(fixture.Call(instance, "getVariable")) == 0, "missing getter arg2 stringifies its actual member capture rather than pretending an empty name");
	for (const auto count : {0u, 1u, 4u}) {
		std::vector<sgre::script::SquirrelValue> arguments(count + 1, std::int32_t{1});
		arguments[0] = instance;
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(fixture.Vm.GetSlot(instance, std::string("setVariable")), arguments));
		},
		    "invalid argument");
	}
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("child::value"), 1.0f}));
	Require(std::get<float>(fixture.Call(instance, "getVariable", {std::string("child/value")})) == 1 && std::get<float>(fixture.Call(instance, "getVariable", {std::string("unmatched::value")})) == 0, "real descriptor getter shares the native canonical slash alias without creating or substituting a child owner");

	std::size_t conversions = 0;
	const auto type = fixture.Vm.CreateClass();
	const auto large_call = fixture.Vm.CreateNativeClosure("actual_variable_operand_growth", [](auto&, auto arguments) {
		Require(arguments.size() == 32768, "nested conversion must grow genuine VM operand storage");
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const auto large_call_root = fixture.Vm.Retain(large_call);
	fixture.Vm.NewSlot(type, std::string("_tostring"), fixture.Vm.CreateNativeClosure("actual_variable_name", [&](auto& vm, auto) {
		++conversions;
		std::vector<sgre::script::SquirrelValue> many(32768, std::int32_t{1});
		static_cast<void>(vm.Invoke(large_call, many));
		static_cast<void>(fixture.Call(instance, "setVariable", {std::string("inner"), 3.0f}));
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("converted\0suffix", 16));
	}));
	const auto name = fixture.Vm.CreateInstance(type);
	const auto name_root = fixture.Vm.Retain(name);
	static_cast<void>(fixture.Call(instance, "setVariable", {name, 7.25f, std::int32_t{-1}}));
	Require(conversions == 1 && std::get<float>(fixture.Call(instance, "getVariable", {std::string("converted")})) == 7.25f && std::get<float>(fixture.Call(instance, "getVariable", {std::string("inner")})) == 3, "tostring reentry must preserve snapshotted receiver/value/control operands through real nested storage growth");
	Require(std::get<float>(fixture.Call(instance, "getVariable", {name, false})) == 7.25f && conversions == 2, "getter conversion precedes the exact current local lookup and ignores extras");

	const auto task = fixture.Bindings->NativeOwners().Task(instance);
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(fixture.Bindings->NativeOwners().IsDetached(instance) && !task.IsLive(), "real Layer retirement must expire the variable-bearing native task");
	static_cast<void>(fixture.Call(instance, "setVariable", {name}));
	Require(conversions == 2, "detached raw setter skips count validation and name conversion");
	Require(std::get<float>(fixture.Call(instance, "getVariable", {name})) == 0 && conversions == 3, "typed getter still stringifies before returning its detached zero");
	Require(fixture.Vm.Length(fixture.Call(instance, "getVariableKeys")) == 0, "detached native key getter returns an actual empty array");
	RequireFailure([&] {
		static_cast<void>(task.GetVariable("disable"));
	},
	    "expired");
	fixture.Leaf = {};
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
}

static void TestActualVariableAnimationOwner() {
	Fixture fixture;
	auto instance = fixture.New();
	const auto instance_root = fixture.Vm.Retain(instance);
	const auto leaf_root = fixture.Vm.Retain(fixture.Leaf);
	auto& first = fixture.Layers.MotionTasks(fixture.Leaf).Group(sgre::scene::MotionTaskGroup::CommonFirst);
	const auto task = fixture.Bindings->NativeOwners().Task(instance);
	Require(first.Count() == 0, "animation task group is allocated only at actual first request");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("value"), 0.0f}));
	static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("value"), 63.0f, std::int32_t{4}, std::int32_t{0}}));
	Require(first.Count() == 1 && task.GetVariable("value") == 0, "scheduling attaches one actual CommonFirst nested set without publishing an immediate replacement value");
	fixture.Second().Update();
	Require(task.GetVariable("value") == 0, "actual Motion Second update is not the animation's recovered CommonFirst owner");
	// Independently calculated integer vectors: native count=duration*16, last=63.
	const std::array<std::int32_t, 5> curves{0, 1, -1, 11, -11};
	const std::array<std::array<float, 4>, 5> samples{{{15, 31, 47, 63}, {3, 15, 35, 63}, {27, 47, 59, 63}, {7, 31, 55, 63}, {23, 31, 39, 63}}};
	for (std::size_t curve = 0; curve != curves.size(); ++curve) {
		static_cast<void>(fixture.Call(instance, "setVariable", {std::string("value"), 0.0f}));
		static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("value"), 63.0f, std::int32_t{4}, curves[curve], std::int32_t{999}}));
		for (const auto expected : samples[curve]) {
			first.Update();
			Require(std::abs(task.GetVariable("value") - expected) < 0.0001f, "actual CommonFirst animation differs from the independently calculated native integer curve samples");
		}
		first.Update();
		Require(task.GetVariable("value") == 63 && first.Count() == 1, "completed handlers retire while their actual lazy parallel set remains owned");
	}
	static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("value"), 99.0f, std::int32_t{10}}));
	static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("value"), 7.0f, std::int32_t{0}}));
	Require(task.GetVariable("value") == 63, "a same-name replacement captures the retained value but does not update it early");
	first.Draw(19);
	Require(task.GetVariable("value") == 63, "native animation Draw is inert");
	first.Update();
	Require(task.GetVariable("value") == 7, "native same-name replacement retires the predecessor before delivering the newly appended zero-duration handler");
	static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("child::value"), 9.0f, std::int32_t{0}}));
	first.Update();
	Require(task.GetVariable("child/value") == 9, "animation publishes through the actual descriptor setter and canonical controller instead of creating a plain-name fallback");
	static_cast<void>(fixture.Call(instance, "animateVariable", {std::int32_t{27}, true, fixture.Vm.CreateTable(), fixture.Vm.CreateTable()}));
	first.Update();
	Require(task.GetVariable("27") == 1, "native name tostring/bool numeric and failed integer conversion reach the real zero-duration task");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("value"), 1.0f, std::int32_t{-1}}));
	},
	    "bounded native integer arithmetic domain");
	Require(task.GetVariable("value") == 7 && first.Count() == 1, "unsupported integer duration rejects before replacing the actual animation/value owner");

	fixture.Clock.SetVariableFrame(true);
	Require(fixture.Clock.Advance(100) == 6 && fixture.Secondary->ReadFrameDelta() == 6, "variable-frame sample must come from the real retained FrameClock");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("clock"), 0.0f}));
	static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("clock"), 159.0f, std::int32_t{10}}));
	first.Update();
	Require(std::abs(task.GetVariable("clock") - 95) < 0.0001f, "variable animation uses actual manager frame delta rather than a separate observer clock");
	first.Update();
	Require(task.GetVariable("clock") == 159, "actual elapsed integer delta clamps at native duration and publishes the final endpoint");
	Require(fixture.Clock.Advance(1000100) == 60000, "large actual clock sample must retain the original counter cadence");
	static_cast<void>(fixture.Call(instance, "setVariable", {std::string("wrapped"), 0.0f}));
	static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("wrapped"), 159999.0f, std::int32_t{10000}, std::int32_t{1}}));
	first.Update();
	// Native IMUL EAX: low32(159999^2)=4124843521; IDIV159999=25780, even at the final input ordinal.
	Require(task.GetVariable("wrapped") == 25780, "native curve multiplication truncates its product to32bits rather than substituting an algebraic endpoint or64-bit power");
	fixture.Clock.SetVariableFrame(false);

	std::size_t conversions = 0;
	const auto type = fixture.Vm.CreateClass();
	const auto many_call = fixture.Vm.CreateNativeClosure("actual_animation_operand_growth", [](auto&, auto arguments) {
		Require(arguments.size() == 32768, "animation conversion must exercise genuine VM operand growth");
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const auto many_root = fixture.Vm.Retain(many_call);
	fixture.Vm.NewSlot(type, std::string("_tostring"), fixture.Vm.CreateNativeClosure("actual_animation_name", [&](auto& vm, auto) {
		++conversions;
		static_cast<void>(fixture.Call(instance, "setVariable", {std::string("converted"), 20.0f}));
		std::vector<sgre::script::SquirrelValue> many(32768, std::int32_t{1});
		static_cast<void>(vm.Invoke(many_call, many));
		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("converted"));
	}));
	const auto name = fixture.Vm.CreateInstance(type);
	const auto name_root = fixture.Vm.Retain(name);
	static_cast<void>(fixture.Call(instance, "animateVariable", {name, 51.0f, std::int32_t{2}, std::int32_t{0}}));
	Require(conversions == 1 && task.GetVariable("converted") == 20, "native typed argument conversion precedes initial-value lookup and genuine task creation");
	first.Update();
	Require(std::abs(task.GetVariable("converted") - 35) < 0.0001f, "animate operands remain actual snapshots through tostring reentry and genuine stack growth");
	first.Update();
	Require(task.GetVariable("converted") == 51, "genuine converted-name task reaches its captured target");

	static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("pending"), 42.0f, std::int32_t{100}}));
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && fixture.Bindings->NativeOwners().IsDetached(instance), "Layer retirement must close CommonFirst handlers before actual Motion/task/runtime owners");
	static_cast<void>(fixture.Call(instance, "animateVariable", {name, fixture.Vm.CreateTable(), std::int32_t{-1}, std::int32_t{999}}));
	Require(conversions == 2, "detached typed animation still converts its name before skipping actual task creation and unsupported duration checks");
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Leaf = {};
}

static void TestActualAnimationRetiresWithMotion() {
	Fixture fixture;
	auto instance = fixture.New();
	const auto instance_root = fixture.Vm.Retain(instance);
	const auto leaf_root = fixture.Vm.Retain(fixture.Leaf);
	auto& first = fixture.Layers.MotionTasks(fixture.Leaf).Group(sgre::scene::MotionTaskGroup::CommonFirst);
	static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("pending"), 42.0f, std::int32_t{100}}));
	Require(first.Count() == 1, "pending real animation owns one concrete first-group registration");
	const auto task = fixture.Bindings->NativeOwners().Task(instance);
	fixture.Bindings->NativeOwners().ReleaseInstance(instance);
	Require(fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Retire, "native wrapper release marks actual Motion task for retirement");
	first.Update();
	fixture.Second().Update();
	Require(first.Count() == 0 && !task.IsLive(), "native wrapper release retires both actual nested first-group owner and Second task without touching an expired runtime");
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
}

static void TestDetachedDefaults() {
	Fixture fixture;
	auto instance = fixture.New();
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("before detach")}));
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(fixture.Bindings->NativeOwners().IsDetached(instance), "real Layer close detaches wrapper after concrete task retirement");
	Require(sgre::script::StringText(fixture.Call(instance, "getChara")).empty() && !std::get<bool>(fixture.Call(instance, "getPlaying")) && std::get<float>(fixture.Call(instance, "getTickCount")) == 0, "detached methods return their actual native defaults");
	static_cast<void>(fixture.Call(instance, "play", {fixture.Vm.CreateTable()}));
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setChara", {fixture.Vm.CreateTable()}));
	},
	    "reentrant VM owner");
	static_cast<void>(fixture.Call(instance, "setEventEnabled", {true}));
	Require(std::get<bool>(fixture.Call(instance, "getEventEnabled")), "wrapper-owned forwarding flag survives detachment");
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Leaf = {};
}

static void TestActualMutableDefaultMode() {
	Fixture fixture;
	const auto motion = fixture.Bindings->MotionClass();
	const std::array<std::pair<sgre::script::SquirrelValue, bool>, 12> cases{{
	    {{}, false},
	    {false, false},
	    {true, true},
	    {std::int32_t{0}, false},
	    {std::int32_t{256}, true},
	    {std::int32_t{-1}, true},
	    {0.0f, false},
	    {-0.0f, false},
	    {0.25f, true},
	    {std::numeric_limits<float>::quiet_NaN(), true},
	    {fixture.Vm.CreateString(""), true},
	    {fixture.Vm.CreateTable(), true},
	}};
	for (const auto& [value, expected] : cases) {
		Require(std::holds_alternative<std::monostate>(fixture.Call(motion, "setDefaultMultiThread", {value, false})), "native static mode setter returns no values and ignores extra arguments");
		Require(fixture.Asynchronous == expected && std::get<bool>(fixture.Call(motion, "getDefaultMultiThread", {false})) == expected, "mode getter reads the actual same mutable state and ignores extra arguments");
	}

	static_cast<void>(fixture.Call(motion, "setDefaultMultiThread"));
	Require(fixture.Asynchronous, "without an explicit user operand, original stack2 is the actual nonnull captured userdata and therefore tests true");
	Require(std::get<bool>(fixture.Call(motion, "getDefaultMultiThread")), "the actual capture-only getter preserves the mutated shared state");

	static_cast<void>(fixture.Call(motion, "setDefaultMultiThread", {true}));
	RequireFailure([&] {
		static_cast<void>(fixture.New());
	},
	    "synchronous mode");
	Require(fixture.Bindings->NativeOwners().InstanceCount() == 0 && fixture.Second().Count() == 0, "setting the original default byte does not manufacture async task support");
	static_cast<void>(fixture.Call(motion, "setDefaultMultiThread", {false}));
	const auto setter = fixture.Vm.GetSlot(motion, "setDefaultMultiThread");
	fixture.Bindings.reset();
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(setter, std::array<sgre::script::SquirrelValue, 2>{motion, true}));
	},
	    "owner has expired");

	Fixture read_only(ConstructorFailure::None, nullptr, false);
	RequireFailure([&] {
		static_cast<void>(read_only.Call(read_only.Bindings->MotionClass(), "setDefaultMultiThread", {true}));
	},
	    "actual containing application state");
	Require(!read_only.Asynchronous, "missing mutable application service rejects before state mutation");
}

static void TestActualRootCoordinates() {
	Fixture fixture;
	auto instance = fixture.New();
	auto task = fixture.Bindings->NativeOwners().Task(instance);
	Require(std::get<float>(fixture.Call(instance, "getLeft")) == 0 && std::get<float>(fixture.Call(instance, "getTop")) == 0 && std::get<float>(fixture.Call(instance, "getZ")) == 0, "root coordinate getters read initial genuine override fields");
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setCoord3", {1.25f, -2.5f, 7.0f, std::int32_t{99}})), "typed coordinate setters return no values and ignore extra operands");
	Require(task.Root().Overrides().Coordinate == std::array<float, 3>{1.25f, -2.5f, 7}, "coordinate3 consumes actual XYZ values without scaling or changing the owner");
	static_cast<void>(fixture.Call(instance, "setCoord", {std::int32_t{10}, std::int32_t{20}}));
	Require(task.Root().Overrides().Coordinate == std::array<float, 3>{10, 20, 7}, "coordinate2 changes only XY and preserves actual Z");
	static_cast<void>(fixture.Call(instance, "setLeft", {true}));
	static_cast<void>(fixture.Call(instance, "setTop", {std::string("5.25")}));
	static_cast<void>(fixture.Call(instance, "setZ", {-3.5f}));
	Require(std::get<float>(fixture.Call(instance, "getLeft", {false})) == 1 && std::get<float>(fixture.Call(instance, "getTop", {std::string("ignored")})) == 0 && std::get<float>(fixture.Call(instance, "getZ", {std::int32_t{8}})) == -3.5f, "single-axis controls preserve other axes, ignore getter extras and use numeric defaultzero rather than parsing strings");
	static_cast<void>(fixture.Call(instance, "setCoord", {4.0f}));
	Require(task.Root().Overrides().Coordinate == std::array<float, 3>{4, 0, -3.5f}, "missing coordinate2 Y uses the bridge's initialized zero without changing Z");
	static_cast<void>(fixture.Call(instance, "setCoord3", {4.0f, 5.0f}));
	Require(task.Root().Overrides().Coordinate == std::array<float, 3>{4, 5, 0}, "missing final coordinate reads captured userdata and leaves that numeric conversion zero");
	// CreateNativeClosureWithCaptures pops pushed captures in reverse; slot2 is overwritten by the real reader descriptor.
	const std::array<sgre::script::SquirrelValue, 3> retained_cells{3.5f, 19.75f, 0.0f};
	const auto seed = fixture.Vm.CreateNativeClosureWithCaptures(
	    "actual inactive coordinate cells", [](auto& active, auto) {
		    Require(active.NativeStackSize() == 4 && std::get<float>(active.NativeAllocatedStackValue(2)) == 0 && std::get<float>(active.NativeAllocatedStackValue(3)) == 19.75f && std::get<float>(active.NativeAllocatedStackValue(4)) == 3.5f, "seed must expose the actual reverse-pushed capture slots before the genuine coordinate invocation");
		    return sgre::script::SquirrelNativeResult::NoValues();
	    },
	    retained_cells);
	const std::array<sgre::script::SquirrelValue, 1> seed_arguments{instance};
	static_cast<void>(fixture.Vm.Invoke(seed, seed_arguments));
	static_cast<void>(fixture.Call(instance, "setCoord3"));
	Require(task.Root().Overrides().Coordinate == std::array<float, 3>{0, 19.75f, 3.5f}, "short native calls read their real retained inactive allocated coordinates rather than inventing zero operands");
	static_cast<void>(fixture.Call(instance, "setCoord3", {std::string("8"), fixture.Vm.CreateTable(), true}));
	Require(task.Root().Overrides().Coordinate == std::array<float, 3>{0, 0, 1}, "wrong-type coordinates defaultzero and bool converts to its original numeric value");
	static_cast<void>(fixture.Call(instance, "setZ"));
	Require(std::get<float>(fixture.Call(instance, "getZ")) == 0, "missing single-axis operand remains zero");
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc12345});
	static_cast<void>(fixture.Call(instance, "setCoord3", {1.0f, 2.0f, nan}));
	static_cast<void>(fixture.Call(instance, "setCoord", {3.0f, 4.0f}));
	Require(std::bit_cast<std::uint32_t>(task.Root().Overrides().Coordinate[2]) == 0x7fc12345, "coordinate2 retains the actual untouched Z representation even outside the finite composition domain");
	static_cast<void>(fixture.Call(instance, "setCoord3", {0.0f, 0.0f, 0.0f}));
	static_cast<void>(fixture.Call(instance, "setCoord", {-0.0f, -0.0f}));
	Require(!std::signbit(task.Root().Overrides().Coordinate[0]) && !std::signbit(task.Root().Overrides().Coordinate[1]), "equal signed-zero coordinates preserve native stored representations");
	static_cast<void>(fixture.RegisterEvents());
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(instance, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	static_cast<void>(fixture.Call(instance, "setCoord3", {2.5f, -6.0f, 9.0f}));
	fixture.Second().Update();
	Require(task.Root().Frame().Composed && task.Root().Frame().Hierarchy.Attributes.Coordinate == std::array<float, 3>{2.5f, -6, 9} && !task.Root().Frame().Hierarchy.Attributes.Hidden && !task.Root().Overrides().Dirty, "actual selected owner update consumes script coordinates and visibility before clearing root dirty");
	static_cast<void>(fixture.Call(instance, "setCoord3", {2.5f, -6.0f, 9.0f}));
	Require(!task.Root().Overrides().Dirty, "repeated actual coordinate controls do not dirty an unchanged composed root");
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setLeft", {11.0f}));
	},
	    "outside queued row borrows");
	const auto first_count = fixture.Layers.MotionTasks(fixture.Leaf).Group(sgre::scene::MotionTaskGroup::CommonFirst).Count();
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "animateVariable", {std::string("queued"), 1.0f, std::int32_t{0}}));
	},
	    "outside queued row borrows");
	Require(fixture.Layers.MotionTasks(fixture.Leaf).Group(sgre::scene::MotionTaskGroup::CommonFirst).Count() == first_count, "queued actual rows reject animation before any nested group or handler is created");
	fixture.Secondary->Context().Clear();
	static_cast<void>(fixture.Call(instance, "setLeft", {11.0f}));
	Require(std::get<float>(fixture.Call(instance, "getLeft")) == 11, "actual queue release restores the same weak task's coordinate admission");
	const auto callback = fixture.Vm.GetSlot(instance, std::string("getLeft"));
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 1>{fixture.Vm.CreateTable()}));
	},
	    "bad instance");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && fixture.Bindings->NativeOwners().IsDetached(instance), "actual Layer retirement expires the task while preserving native detached wrapper behavior");
	for (const auto name : {"getLeft", "getTop", "getZ"}) {
		Require(std::get<float>(fixture.Call(instance, name, {true})) == 0, "detached root getters return native floatzero without acquiring a retired task");
	}
	for (const auto name : {"setCoord", "setCoord3", "setLeft", "setTop", "setZ"}) {
		Require(std::holds_alternative<std::monostate>(fixture.Call(instance, name, {fixture.Vm.CreateTable()})), "detached coordinate setters retain native no-op and no-value behavior");
	}
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Leaf = {};
}

static void TestActualRootZoom() {
	Fixture fixture;
	auto instance = fixture.New();
	auto task = fixture.Bindings->NativeOwners().Task(instance);
	Require(std::get<float>(fixture.Call(instance, "getZoomX")) == 1 && std::get<float>(fixture.Call(instance, "getZoomY")) == 1, "native new Motion owns unit raw zoom factors");
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setZoom", {2.0f, -3.0f, std::int32_t{99}})), "typed setZoom returns no values and ignores extra operands");
	Require(task.Root().Overrides().ZoomX == 2 && task.Root().Overrides().ZoomY == -3 && !task.Root().Frame().Composed, "zoom stores raw factors without percent conversion or an implicit numeric update");
	static_cast<void>(fixture.Call(instance, "setZoomX", {true}));
	static_cast<void>(fixture.Call(instance, "setZoomY", {std::string("2")}));
	Require(std::get<float>(fixture.Call(instance, "getZoomX", {false})) == 1 && std::get<float>(fixture.Call(instance, "getZoomY", {std::int32_t{99}})) == 0, "zoom axes preserve the other real factor, convert bool and default unsupported numeric types to zero");
	static_cast<void>(fixture.Call(instance, "setZoom", {4.0f}));
	Require(task.Root().Overrides().ZoomX == 4 && task.Root().Overrides().ZoomY == 0, "missing second zoom operand reads actual captured userdata as numeric zero, never duplicates X");
	const std::array<sgre::script::SquirrelValue, 2> retained_cells{19.75f, 0.0f};
	const auto seed = fixture.Vm.CreateNativeClosureWithCaptures(
	    "retained zoom allocated cell", [](auto& active, auto) {
		    Require(active.NativeStackSize() == 3 && std::get<float>(active.NativeAllocatedStackValue(3)) == 19.75f, "short zoom seed must occupy the real inactive Y cell");
		    return sgre::script::SquirrelNativeResult::NoValues();
	    },
	    retained_cells);
	static_cast<void>(fixture.Vm.Invoke(seed, std::array<sgre::script::SquirrelValue, 1>{instance}));
	static_cast<void>(fixture.Call(instance, "setZoom"));
	Require(task.Root().Overrides().ZoomX == 0 && task.Root().Overrides().ZoomY == 19.75f, "short two-float bridge must read real retained allocated cells beyond logical top");
	static_cast<void>(fixture.Call(instance, "setZoom", {0.0f, 1.0f}));
	fixture.Second().Update();
	static_cast<void>(fixture.Call(instance, "setZoom", {-0.0f, 1.0f}));
	Require(!task.Root().Overrides().Dirty && std::bit_cast<std::uint32_t>(task.Root().Overrides().ZoomX) == 0, "ordered equal signed zeros do not dirty or overwrite native retained zoom");
	static_cast<void>(fixture.Call(instance, "setZoom", {-0.0f, 2.0f}));
	Require(task.Root().Overrides().Dirty && std::bit_cast<std::uint32_t>(task.Root().Overrides().ZoomX) == 0x80000000 && task.Root().Overrides().ZoomY == 2, "changing either native zoom axis stores both supplied words including the other signed zero");
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc12345});
	static_cast<void>(fixture.Call(instance, "setZoomY", {nan}));
	Require(std::bit_cast<std::uint32_t>(std::get<float>(fixture.Call(instance, "getZoomY"))) == 0x7fc12345 && task.Root().Overrides().Dirty, "native unordered zoom write retains its raw NaN payload until genuine composition admission");
	RequireFailure([&] {
		task.Progress(0);
	},
	    "finite coordinates");
	static_cast<void>(fixture.Call(instance, "setZoom", {2.0f, 3.0f}));
	static_cast<void>(fixture.RegisterEvents());
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(instance, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	fixture.Second().Update();
	Require(task.Root().Frame().Hierarchy.Transform.XX == 2 && task.Root().Frame().Hierarchy.Transform.YY == 3 && !task.Root().Overrides().Dirty, "actual selected update must compose the recovered zoom factors into its real root transform");
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setZoomX", {8.0f}));
	},
	    "queued row borrows");
	Require(std::get<float>(fixture.Call(instance, "getZoomX")) == 2 && task.Root().Frame().Hierarchy.Transform.XX == 2, "rejected queued zoom mutation preserves completed actual output while getters remain observational");
	fixture.Secondary->Context().Clear();
	const auto callback = fixture.Vm.GetSlot(instance, std::string("setZoom"));
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 1>{fixture.Vm.CreateTable()}));
	},
	    "bad instance");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(std::get<float>(fixture.Call(instance, "getZoomX")) == 0 && std::get<float>(fixture.Call(instance, "getZoomY")) == 0 && std::holds_alternative<std::monostate>(fixture.Call(instance, "setZoom", {true, fixture.Vm.CreateTable()})), "detached native zoom returns floatzero and void without borrowing a retired task");
}

static void TestActualRootAngles() {
	Fixture fixture;
	auto instance = fixture.New();
	auto task = fixture.Bindings->NativeOwners().Task(instance);
	Require(std::get<float>(fixture.Call(instance, "getAngleDeg")) == 0 && std::get<float>(fixture.Call(instance, "getAngleRad", {true})) == 0, "actual Motion angle getters own initial floatzero and ignore extras");
	Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setAngleDeg", {-1080.25f, std::int32_t{77}})) && task.Root().Overrides().Angle == -1080.25f, "degree setter returns void and keeps raw unnormalized ordinary degrees");
	static_cast<void>(fixture.Call(instance, "setAngleRad", {1.57079637f, false}));
	Require(std::get<float>(fixture.Call(instance, "getAngleDeg")) == 90 && std::bit_cast<std::uint32_t>(std::get<float>(fixture.Call(instance, "getAngleRad"))) == 0x3fc90fdb, "all four actual typed angle methods must share native separate float32 conversions");
	static_cast<void>(fixture.Call(instance, "setAngleDeg", {true}));
	Require(task.Root().Overrides().Angle == 1, "native fixed-slot angle reader converts bool numerically");
	static_cast<void>(fixture.Call(instance, "setAngleRad", {std::string("2")}));
	Require(task.Root().Overrides().Angle == 0, "native angle reader does not parse strings");
	static_cast<void>(fixture.Call(instance, "setAngleDeg", {fixture.Vm.CreateTable()}));
	static_cast<void>(fixture.Call(instance, "setAngleDeg", {12.0f}));
	static_cast<void>(fixture.Call(instance, "setAngleRad"));
	Require(task.Root().Overrides().Angle == 0, "missing angle operand reads real captured16-byte userdata in physical slot2 as numericzero");
	fixture.Second().Update();
	static_cast<void>(fixture.Call(instance, "setAngleDeg", {-0.0f}));
	Require(!task.Root().Overrides().Dirty && std::bit_cast<std::uint32_t>(task.Root().Overrides().Angle) == 0, "ordered equal signedzero must preserve the old representation without dirtying the genuine root");
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc12345});
	static_cast<void>(fixture.Call(instance, "setAngleDeg", {nan}));
	Require(task.Root().Overrides().Dirty && std::bit_cast<std::uint32_t>(std::get<float>(fixture.Call(instance, "getAngleDeg"))) == 0x7fc12345, "unordered angle retains the actual raw payload until numeric composition admission");
	RequireFailure([&] {
		task.Progress(0);
	},
	    "finite coordinates");
	static_cast<void>(fixture.Call(instance, "setAngleDeg", {90.0f}));
	static_cast<void>(fixture.RegisterEvents());
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(instance, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	fixture.Second().Update();
	const auto completed = task.Root().Frame().Hierarchy.Transform;
	Require(task.Root().Frame().Hierarchy.Attributes.Angle == 90 && !task.Root().Overrides().Dirty, "actual selected update must compose the accepted angle into the same owned root");
	fixture.Second().Draw(0);
	for (const auto name : {"setAngleDeg", "setAngleRad"}) {
		RequireFailure([&] {
			static_cast<void>(fixture.Call(instance, name, {3.0f}));
		},
		    "queued row borrows");
	}
	Require(std::get<float>(fixture.Call(instance, "getAngleDeg")) == 90 && task.Root().Frame().Hierarchy.Transform.XX == completed.XX, "queued angle rejection must precede retained or completed-output mutation while getters remain observational");
	fixture.Secondary->Context().Clear();
	for (const auto name : {"setAngleDeg", "getAngleDeg", "setAngleRad", "getAngleRad"}) {
		const auto callback = fixture.Vm.GetSlot(instance, std::string(name));
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.CreateTable(), true}));
		},
		    "bad instance");
	}
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive(), "angle-bearing actual task must retire with its genuine containing Layer");
	for (const auto name : {"getAngleDeg", "getAngleRad"}) {
		Require(std::bit_cast<std::uint32_t>(std::get<float>(fixture.Call(instance, name))) == 0, "detached typed angle getters return positive floatzero");
	}
	for (const auto name : {"setAngleDeg", "setAngleRad"}) {
		Require(std::holds_alternative<std::monostate>(fixture.Call(instance, name, {nan})), "detached angle setters convert the operand then skip arithmetic and the retired player");
	}
	const auto callback = fixture.Vm.GetSlot(instance, std::string("getAngleRad"));
	fixture.Bindings->NativeOwners().ReleaseInstance(instance);
	Require(fixture.Bindings->NativeOwners().CanClose(), "the actual detached wrapper must complete before testing adapter expiry");
	fixture.Bindings.reset();
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 1>{instance}));
	},
	    "owner has expired");
}

static void TestActualRootOpacity() {
	Fixture fixture;
	auto instance = fixture.New();
	auto task = fixture.Bindings->NativeOwners().Task(instance);
	Require(std::get<std::int32_t>(fixture.Call(instance, "getOpacity", {true})) == 255, "actual Motion opacity getter returns integer255 and ignores extras");
	for (const auto& record : kNativeMotionOpacityCases) {
		if (record.Detached) {
			continue;
		}
		static_cast<void>(fixture.Call(instance, "setOpacity", {std::bit_cast<std::int32_t>(record.Initial)}));
		sgre::script::SquirrelValue value;
		if (record.Type == 0x5000002u) {
			value = std::bit_cast<std::int32_t>(record.Input);
		} else if (record.Type == 0x5000004u) {
			value = std::bit_cast<float>(record.Input);
		} else if (record.Type == 0x1000008u) {
			value = record.Input != 0;
		} else if (record.Type == 0xa000080u) {
			value = fixture.Vm.CreateUserData(std::array<std::uint8_t, 16>{});
		}
		Require(std::holds_alternative<std::monostate>(fixture.Call(instance, "setOpacity", {value, std::int32_t{91}})), "actual integer opacity setter returns void and ignores extras");
		Require(task.Root().Overrides().Opacity == record.Expected && std::bit_cast<std::uint32_t>(std::get<std::int32_t>(fixture.Call(instance, "getOpacity"))) == record.Getter, "actual VM fixed opacity conversion and retained word must match original instructions");
	}
	static_cast<void>(fixture.Call(instance, "setOpacity", {std::string("128")}));
	Require(task.Root().Overrides().Opacity == 0, "opacity integer bridge must not parse strings or invoke script conversion");
	static_cast<void>(fixture.Call(instance, "setOpacity", {std::int32_t{128}}));
	static_cast<void>(fixture.Call(instance, "setOpacity"));
	Require(task.Root().Overrides().Opacity == 0, "missing opacity operand reads actual final descriptor in physical2 as failed conversion retaining initialized zero");
	static_cast<void>(fixture.Call(instance, "setOpacity", {std::int32_t{128}}));
	fixture.Second().Update();
	Require(!task.Root().Overrides().Dirty && task.Root().Frame().Hierarchy.Attributes.Opacity == 128, "actual ordinary update consumes opacity without a synthetic extra compose");
	static_cast<void>(fixture.Call(instance, "setOpacity", {128.9f}));
	Require(!task.Root().Overrides().Dirty && std::get<std::int32_t>(fixture.Vm.ReadSlot(instance, std::string("opacity"))) == 128, "integer truncation and real Object property getter share unchanged native word");
	static_cast<void>(fixture.RegisterEvents());
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(instance, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(instance, "setVisible", {true}));
	fixture.Second().Update();
	fixture.Second().Draw(0);
	RequireFailure([&] {
		static_cast<void>(fixture.Call(instance, "setOpacity", {std::int32_t{64}}));
	},
	    "queued row borrows");
	Require(std::get<std::int32_t>(fixture.Call(instance, "getOpacity")) == 128 && task.Root().Frame().Hierarchy.Attributes.Opacity == 128, "queued mutation rejection precedes retained/completed opacity writes");
	fixture.Secondary->Context().Clear();
	const auto callback = fixture.Vm.GetSlot(instance, std::string("setOpacity"));
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.CreateTable(), true}));
	},
	    "bad instance");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && std::get<std::int32_t>(fixture.Call(instance, "getOpacity")) == 255 && std::holds_alternative<std::monostate>(fixture.Call(instance, "setOpacity", {std::bit_cast<float>(std::uint32_t{0x7fc12345})})), "detached actual opacity methods retain native255/void defaults after conversion");
	const auto getter = fixture.Vm.GetSlot(instance, std::string("getOpacity"));
	fixture.Bindings->NativeOwners().ReleaseInstance(instance);
	Require(fixture.Bindings->NativeOwners().CanClose(), "the actual detached opacity wrapper must complete before adapter expiry");
	fixture.Bindings.reset();
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(getter, std::array<sgre::script::SquirrelValue, 1>{instance}));
	},
	    "owner has expired");
}

static void TestDestinationLayerCloneFactory() {
	Fixture fixture(ConstructorFailure::None, nullptr, true, true);
	static_cast<void>(fixture.RegisterEvents());
	auto source = fixture.New();
	static_cast<void>(fixture.Call(source, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(source, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(source, "setLeft", {13.0f}));
	static_cast<void>(fixture.Call(source, "setTop", {-7.0f}));
	static_cast<void>(fixture.Call(source, "setOpacity", {std::int32_t{128}}));
	static_cast<void>(fixture.Call(source, "setEventEnabled", {true}));
	fixture.Second().Update();
	const auto source_task = fixture.Bindings->NativeOwners().Task(source);
	const auto source_identity = fixture.Vm.BorrowObjectIdentity(source);
	const auto source_clock = source_task.Commands().ClockState();
	const auto* source_scene = source_task.GenericPlayer()->Scene();
	Require(source_scene && !source_scene->PacketRows().empty(), "clone fixture requires the genuine selected source's completed frame");
	auto clone = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 3>{fixture.Vm.RootTable(), fixture.Leaf, source}).Value;
	const auto cloned_task = fixture.Bindings->NativeOwners().Task(clone);
	const auto* cloned_scene = cloned_task.GenericPlayer()->Scene();
	Require(cloned_task.Handle() != source_task.Handle() && cloned_scene && cloned_scene != source_scene && cloned_scene->PacketRows().size() == source_scene->PacketRows().size(), "Layer+Motion must publish a distinct selected scene before its first update");
	Require(cloned_task.Commands().ClockState().TickCount == source_clock.TickCount && cloned_task.Commands().ClockState().EvaluationTime == source_clock.EvaluationTime && cloned_task.Commands().Names().CurrentMotion.empty() && cloned_task.Commands().Names().RequestedMotion == "main", "native copy preserves the clock and selected records while destination+90 remains its initialized empty string");
	Require(!fixture.Bindings->NativeOwners().EventEnabled(clone) && std::get<float>(fixture.Call(clone, "getLeft")) == 13 && std::get<float>(fixture.Call(clone, "getTop")) == -7 && std::get<std::int32_t>(fixture.Call(clone, "getOpacity")) == 128, "the clone copies actual root state and installs its own disabled event listener");
	fixture.Second().Draw(0);
	Require(fixture.Secondary->SceneContext().Size() > 0, "both actual selected tasks draw their completed frame before clone Update");
	fixture.Secondary->SceneContext().Clear();

	// Argument4 selects the destination manager. It cannot select insertion because factory arity ends here.
	auto primary_clone = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 4>{fixture.Vm.RootTable(), fixture.Leaf, source, std::int32_t{1}}).Value;
	const auto primary_task = fixture.Bindings->NativeOwners().Task(primary_clone);
	Require(&primary_task.Commands().Manager() == &fixture.Primary->Manager() && &cloned_task.Commands().Manager() == &fixture.Secondary->Manager(), "Layer clone resolves the exact selected destination manager and registered immutable alias");
	std::array<sgre::scene::ParallelTaskHandle, 3> order;
	Require(fixture.Second().WriteOrder(order) == 3 && order[0] == source_task.Handle() && order[1] == cloned_task.Handle() && order[2] == primary_task.Handle(), "both supported Layer clone forms append after their actual source");
	static_cast<void>(fixture.Call(clone, "setLeft", {91.0f}));
	Require(std::get<float>(fixture.Call(source, "getLeft")) == 13 && std::get<float>(fixture.Call(primary_clone, "getLeft")) == 13, "mutable clone roots never alias the source or another clone");
	fixture.Vm.ResetExecution();
	source = {};
	fixture.Vm.RethrowReleaseFailure();
	Require(!fixture.Vm.ResolveObjectIdentity(source_identity) && cloned_task.IsLive() && primary_task.IsLive(), "clone construction retains no source VM receiver");
	fixture.Second().Update();
	Require(!source_task.IsLive() && cloned_task.IsLive() && primary_task.IsLive(), "normal source-first task retirement preserves both independently owned clones");
	clone = {};
	primary_clone = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Second().Update();
	Require(fixture.Second().Count() == 0 && fixture.Primary->PlayerCount() == 0 && fixture.Secondary->PlayerCount() == 0, "Layer clone wrapper retirement releases every copied task through the genuine task group");
}

static void TestActualLastTime() {
	Fixture fixture;
	auto instance = fixture.New();
	const auto task = fixture.Bindings->NativeOwners().Task(instance);
	Require(std::get<float>(fixture.Call(instance, "getLastTime", {false, std::string("ignored")})) == 0 && std::get<float>(fixture.Vm.ReadSlot(instance, std::string("lastTime"))) == 0, "actual unselected player owns an initial float-zero lastTime and ignores getter extras");
	static_cast<void>(fixture.RegisterEvents());
	static_cast<void>(fixture.Call(instance, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(instance, "play", {std::string("main")}));
	Require(std::get<float>(fixture.Vm.ReadSlot(instance, std::string("lastTime"))) == 8 && task.Commands().ClockState().TickCount == 0, "ordinary Object property resolution reads the selected player's actual terminal time before its first update");
	fixture.Second().Update();
	fixture.Second().Draw(0);
	Require(std::get<float>(fixture.Call(instance, "getLastTime")) == 8, "observing lastTime is valid while the real completed frame is borrowed for drawing");
	fixture.Secondary->Context().Clear();
	static_cast<void>(fixture.Call(instance, "setTickCount", {500.0f}));
	static_cast<void>(fixture.Call(instance, "stop"));
	Require(std::get<float>(fixture.Call(instance, "getLastTime")) == 8 && task.Commands().ClockState().TickCount == 500 && !task.Commands().ClockState().Playing, "lastTime is the retained terminal word, independent of overshot tick count and stopped status");
	static_cast<void>(fixture.Call(instance, "play", {std::string("missing")}));
	Require(std::get<float>(fixture.Call(instance, "getLastTime")) == 8, "failed selection preserves the actual cached terminal time");
	const auto callback = fixture.Vm.GetSlot(instance, std::string("getLastTime"));
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(callback, std::array<sgre::script::SquirrelValue, 1>{fixture.Vm.CreateTable()}));
	},
	    "bad instance");
	fixture.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && std::get<float>(fixture.Call(instance, "getLastTime", {true})) == 0 && std::get<float>(fixture.Vm.ReadSlot(instance, std::string("lastTime"))) == 0, "actual Layer retirement returns detached floatzero without borrowing the expired task");
	instance = {};
	fixture.Leaf = {};
	fixture.Vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestConstructionRollbackAfterAssociation();
		TestActualFactoryAndConstantTable();
		TestPrimitiveConversionsAndExplicitGates();
		TestNativeSyncControls();
		TestSelectedActionSyncAndLabelInputs();
		TestDetachedDefaults();
		TestActualMutableDefaultMode();
		TestActualRootCoordinates();
		TestActualRootZoom();
		TestActualRootAngles();
		TestActualRootOpacity();
		TestActualLastTime();
		TestDestinationLayerCloneFactory();
		TestActualVariableMethods();
		TestActualVariableAnimationOwner();
		TestActualAnimationRetiresWithMotion();
		std::cout << "Motion class new-only adapter fixture groups PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
