#include "sgre/script/layer_proxy_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/draw_object_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/layer_draw_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using SquirrelValue = sgre::script::SquirrelValue;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(!std::string_view(error.what()).empty(), "actual proxy failure must retain a useful diagnostic");

		return;
	}

	throw std::runtime_error("expected actual LayerProxy rejection: " + std::string(message));
}

static sgre::script::SquirrelVmLimits VmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static sgre::scene::LayerCameraFrameState CameraState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {128, 72};
	state.RawScissor = {0, 0, 128, 72};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

class ProxyFixture {
public:
	ProxyFixture()
	    : Vm(VmLimits()), Objects(Vm), Layers(Vm, Objects, Tree), Psb(Vm, Objects), Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		      throw std::runtime_error("LayerProxy fixture unexpectedly requested asset upload");
	      },
	                                                                                    [](auto) {
		                                                                                    throw std::runtime_error("LayerProxy fixture unexpectedly retired an uploaded asset");
	                                                                                    }),
	      Catalog(Cache), DrawObjects(Vm, Objects), Camera(CameraState(), Models, [](const auto&) {
	      }),
	      DrawObjectsInLayer(Vm, Objects, Layers, DrawObjects, {}, &Camera), Proxies(Vm, Objects, Layers, Tree, Services()) {
		Vm.InstallRecoveredBaseLibrary();
		Layers.ConfigureMotionOwners(Psb, Catalog, Cache);
		TargetClass = Vm.CreateClass(Layers.LayerBaseClass());
		TargetClassRoot = Vm.Retain(TargetClass);
		Vm.NewSlot(TargetClass, std::string("constructor"), Vm.CreateNativeClosure("genuine fixture SQLayerBase constructor", [this](auto&, auto arguments) {
			Layers.ConstructBaseInstance(arguments);

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
	}

	~ProxyFixture() {
		try {
			Close();
		} catch (const std::exception& error) {
			std::cerr << "LayerProxy fixture cleanup: " << error.what() << '\n';
		}
	}

	void Close() {
		Vm.Close();
		Vm.RethrowReleaseFailure();
		Require(Tree.ChildCount() == 0, "closed actual proxy fixture must release all native root memberships");
	}

	SquirrelValue Call(const SquirrelValue& receiver, std::string_view name, std::initializer_list<SquirrelValue> parameters = {}) {
		std::vector<SquirrelValue> arguments{receiver};
		arguments.insert(arguments.end(), parameters);
		const auto result = Vm.Invoke(Vm.GetSlot(receiver, std::string(name)), arguments).Value;
		Vm.ResetExecution();

		return result;
	}

	SquirrelValue Folder() {
		const auto instance = Vm.Invoke(Layers.LayerFolderClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
		Vm.ResetExecution();

		return instance;
	}

	SquirrelValue Base(const SquirrelValue& parent = {}) {
		const auto instance = Vm.Invoke(TargetClass, std::array<SquirrelValue, 2>{Vm.RootTable(), parent}).Value;
		Vm.ResetExecution();
		Layers.AttachBaseFrame(instance, Camera);

		return instance;
	}

	SquirrelValue Motion() {
		const auto instance = Vm.Invoke(Layers.LayerClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
		Vm.ResetExecution();
		sgre::script::ScriptMotionFrameServices services;
		services.State = [this](const auto& receiver) {
			return Layers.MotionFrameState(receiver);
		};
		// The real target has empty original source registries; concrete child tasks still draw.
		services.SecondaryDraw = [](const auto&, auto) {
		};
		services.PrimaryDraw = [](const auto&, auto) {
		};
		Layers.AttachMotionFrame(instance, Camera, std::move(services));

		return instance;
	}

	SquirrelValue Proxy(const SquirrelValue& target, const SquirrelValue& parent = {}, bool fast = false) {
		std::vector<SquirrelValue> arguments{Vm.RootTable()};
		if (!fast) {
			arguments.push_back(parent);
		}
		arguments.push_back(target);
		const auto instance = Vm.Invoke(Proxies.LayerProxyClass(), arguments).Value;
		Vm.ResetExecution();
		Layers.AttachBaseFrame(instance, Camera);

		return instance;
	}

	void Update(const SquirrelValue& proxy) {
		Layers.DispatchBaseFrame(Layers.Handle(proxy), [](const auto&, auto& frame) {
			frame.Update();
		});
	}

	sgre::script::ScriptLayerProxyDrawServices Services() {
		sgre::script::ScriptLayerProxyDrawServices services;
		services.State = [this](auto handle) {
			++TargetStateReads;
			Events.push_back(20);

			return Layers.ProcessState(handle);
		};
		services.Draw = [this](auto handle, auto argument) {
			++TargetDispatches;
			Events.push_back(21);
			if (OnTargetDispatch) {
				OnTargetDispatch(handle, argument);
			}
			if (Proxies.Owns(handle)) {
				Proxies.Draw(handle, argument);
			} else if (Tree.State(handle).Kind == sgre::scene::LayerKind::Motion) {
				Layers.DispatchMotionFrame(handle, [argument](const auto&, auto& frame) {
					frame.Draw(argument);
				});
			} else {
				Layers.DispatchBaseFrame(handle, [argument](const auto&, auto& frame) {
					frame.Draw(argument);
				});
			}
		};

		return services;
	}

	std::vector<int> Events;
	std::size_t TargetStateReads = 0;
	std::size_t TargetDispatches = 0;
	std::function<void(sgre::scene::LayerHandle, std::uint32_t)> OnTargetDispatch;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	sgre::script::ScriptDrawObjectBindings DrawObjects;
	std::array<sgre::scene::MotionScissorMatrix, 1> Models;
	sgre::scene::LayerCameraDrawScope Camera;
	sgre::script::ScriptLayerDrawBindings DrawObjectsInLayer;
	sgre::script::ScriptLayerProxyBindings Proxies;
	SquirrelValue TargetClass;
	sgre::script::SquirrelHostRoot TargetClassRoot;
};

static void TestGenuineRootClassConstructorAndInheritedState() {
	ProxyFixture fixture;
	const auto parent = fixture.Folder();
	const auto target = fixture.Base();
	const auto target_handle = fixture.Layers.Handle(target);
	const auto proxy = fixture.Proxy(target, parent);
	const auto handle = fixture.Layers.Handle(proxy);
	Require(fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string("LayerProxy")) == fixture.Proxies.LayerProxyClass() && fixture.Vm.IsInstanceOf(proxy, fixture.Layers.LayerBaseClass()) && fixture.Vm.IsInstanceOf(proxy, fixture.Objects.ObjectClass()), "actual registered LayerProxy must inherit genuine LayerBase/Object");
	Require(fixture.Proxies.Owns(handle) && fixture.Tree.State(handle).Kind == sgre::scene::LayerKind::Base && fixture.Tree.State(handle).Owner == fixture.Layers.Handle(parent) && !fixture.Tree.State(target_handle).Owner, "constructor attaches only proxy to optional owner and never reparents its real target");
	Require(fixture.Layers.BaseTasks(proxy).Children().Count() == 1 && fixture.Layers.BaseTasks(proxy).CommonFirst().Count() == 0 && fixture.Layers.BaseTasks(proxy).CommonSecond().Count() == 0, "genuine proxy owns exactly one actual camera drawing task");
	fixture.Call(proxy, "setPriority", {13.5f});
	fixture.Call(proxy, "setOffset", {7.0f, -2.0f});
	fixture.Call(proxy, "setScale", {2.0f, 3.0f});
	Require(fixture.Call(proxy, "getOwner") == parent && fixture.Call(proxy, "getPriority") == SquirrelValue{13.5f} && fixture.Call(proxy, "getOffsetX") == SquirrelValue{7.0f} && fixture.Call(proxy, "getScaleY") == SquirrelValue{3.0f}, "inherited actual properties reach the proxy's own common native layer");
	const auto weak = fixture.Call(proxy, "weakref");
	Require(fixture.Vm.Type(weak) == sgre::script::SquirrelValueType::WeakReference && fixture.Vm.ResolveWeakReference(weak) == proxy, "own native weakref slot returns canonical weak instance");
	const auto fast = fixture.Proxy(target, {}, true);
	Require(!fixture.Tree.State(fixture.Layers.Handle(fast)).Owner, "one-target constructor fast path keeps native default root owner");
	const auto before = fixture.Tree.ChildCount();
	const auto weak_target = fixture.Vm.CreateWeakReference(target);
	const auto wrong_class = fixture.Vm.CreateInstance(fixture.Objects.ObjectClass());
	const auto no_native = fixture.Vm.CreateInstance(fixture.TargetClass);
	for (const auto& invalid : std::array<SquirrelValue, 5>{SquirrelValue{}, std::int32_t{9}, weak_target, wrong_class, no_native}) {
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(fixture.Proxies.LayerProxyClass(), std::array<SquirrelValue, 3>{fixture.Vm.RootTable(), parent, invalid}));
		},
		    "non-layer, weak or native-null mandatory target");
		fixture.Vm.ResetExecution();
		fixture.Vm.RethrowReleaseFailure();
		Require(fixture.Tree.ChildCount() == before, "invalid native target fails before proxy layer allocation/publication");
	}
	RequireFailure([&] {
		static_cast<void>(fixture.Vm.Invoke(fixture.Proxies.LayerProxyClass(), std::array<SquirrelValue, 4>{fixture.Vm.RootTable(), parent, target, std::string("not ignored")}));
	},
	    "extra constructor argument");
	fixture.Vm.ResetExecution();
	fixture.Vm.RethrowReleaseFailure();
	Require(fixture.Tree.ChildCount() == before, "extra argument rejection preserves exact actual layer membership");
	fixture.Close();
}

static void TestOwnUpdateAndOrderedDrawWithoutTargetAdvance() {
	ProxyFixture fixture;
	const auto target = fixture.Base();
	const auto proxy = fixture.Proxy(target);
	std::size_t target_updates = 0;
	std::size_t own_updates = 0;
	sgre::scene::ParallelTaskCallbacks target_callbacks;
	target_callbacks.Update = [&](auto) {
		++target_updates;
	};
	target_callbacks.Draw = [&](auto, auto argument) {
		Require(argument == 0x87654321u && fixture.Camera.Depth() == 3, "actual target camera receives the full draw argument inside proxy camera");
		fixture.Events.push_back(30);
	};
	static_cast<void>(fixture.Layers.BaseTasks(target).Children().Append(std::move(target_callbacks)));
	for (const auto index : {0, 1}) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [&](auto) {
			++own_updates;
		};
		callbacks.Draw = [&, index](auto, auto argument) {
			Require(argument == 0x87654321u && fixture.Camera.Depth() == 1, "own common groups draw before actual outer proxy camera");
			fixture.Events.push_back(index + 10);
		};
		auto& group = index == 0 ? fixture.Layers.BaseTasks(proxy).CommonFirst() : fixture.Layers.BaseTasks(proxy).CommonSecond();
		static_cast<void>(group.Append(std::move(callbacks)));
	}
	fixture.Update(proxy);
	Require(own_updates == 2 && target_updates == 0, "proxy updates its own actual common graph while MDrawingProxy never updates target");
	fixture.Events.clear();
	fixture.Proxies.Draw(fixture.Layers.Handle(proxy), 0x87654321u);
	Require(fixture.Events == std::vector<int>{10, 11, 20, 21, 30} && fixture.Camera.Depth() == 1, "own common groups precede target state/draw and all real camera scopes restore");
	fixture.Close();
}

static void TestConcreteMotionDrawPreservesOpacityArgumentAndNestedCamera() {
	ProxyFixture fixture;
	const auto target = fixture.Motion();
	const auto proxy = fixture.Proxy(target);
	std::size_t calls = 0;
	const auto releases = std::make_shared<std::size_t>(0);
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [&](auto argument, auto opacity) {
		Require(argument == 0x87654321u && opacity == 0xfedcba98u && fixture.Camera.Depth() == 3, "actual proxy to Motion to LayerDraw to native DrawObject keeps full argument/opacity and nested camera");
		const auto& model = fixture.Camera.CurrentModel().Coefficients;
		Require(model[0] == 10 && model[5] == 21 && model[3] == -29 && model[7] == -37, "actual target camera composes beneath proxy camera with recovered integer transforms");
		++calls;
	};
	callbacks.Release = [releases] {
		++*releases;
	};
	const auto draw_object = fixture.Vm.CreateInstance(fixture.Vm.CreateClass(fixture.DrawObjects.DrawObjectClass()));
	fixture.DrawObjects.AttachNativeDrawOwner(draw_object, std::move(callbacks));
	const auto drawing = fixture.Vm.Invoke(fixture.DrawObjectsInLayer.LayerDrawClass(), std::array<SquirrelValue, 3>{fixture.Vm.RootTable(), target, draw_object}).Value;
	fixture.Vm.ResetExecution();
	fixture.Call(drawing, "setOpacity", {std::bit_cast<std::int32_t>(0xfedcba98u)});
	fixture.Call(proxy, "setScale", {2.0f, 3.0f});
	fixture.Call(proxy, "setOffset", {7.0f, -2.0f});
	fixture.Call(target, "setScale", {5.0f, 7.0f});
	fixture.Call(target, "setOffset", {11.0f, 13.0f});
	fixture.OnTargetDispatch = [&](auto handle, auto argument) {
		Require(handle == fixture.Layers.Handle(target) && argument == 0x87654321u && fixture.Camera.Depth() == 2, "native target virtual dispatch begins inside genuine proxy camera");
		const auto& model = fixture.Camera.CurrentModel().Coefficients;
		Require(model[0] == 2 && model[5] == 3 && model[3] == -7 && model[7] == 2, "outer camera is the real proxy transform rather than a target substitute");
	};
	fixture.Proxies.Draw(fixture.Layers.Handle(proxy), 0x87654321u);
	Require(calls == 1 && *releases == 0 && fixture.Camera.Depth() == 1, "actual nested concrete draw returns without retiring target or leaking camera state");
	fixture.Close();
	Require(*releases == 1, "actual native DrawObject retires once with its enclosing VM");
}

static void TestTargetVisibilityStatusAndWholeGraphExpirationGate() {
	ProxyFixture fixture;
	auto target = fixture.Base();
	const auto proxy = fixture.Proxy(target);
	const auto proxy_root = fixture.Vm.Retain(proxy);
	const auto proxy_handle = fixture.Layers.Handle(proxy);
	std::size_t own_draws = 0;
	std::size_t target_draws = 0;
	sgre::scene::ParallelTaskCallbacks own;
	own.Draw = [&](auto, auto) {
		++own_draws;
	};
	static_cast<void>(fixture.Layers.BaseTasks(proxy).CommonFirst().Append(std::move(own)));
	sgre::scene::ParallelTaskCallbacks target_callbacks;
	target_callbacks.Draw = [&](auto, auto) {
		++target_draws;
	};
	static_cast<void>(fixture.Layers.BaseTasks(target).Children().Append(std::move(target_callbacks)));
	fixture.Call(target, "setVisible", {false});
	fixture.Proxies.Draw(proxy_handle, 1);
	Require(own_draws == 1 && target_draws == 0, "live hidden target skips only the MDrawingProxy dispatch, retaining own base graph draw");
	fixture.Call(target, "setVisible", {true});
	sgre::scene::LayerFrameProcessState state;
	state.Status = sgre::scene::TaskStatus::Retire;
	fixture.Layers.SetProcessState(target, state);
	fixture.Proxies.Draw(proxy_handle, 2);
	Require(own_draws == 2 && target_draws == 0, "target native status2 skips dispatch without inventing target retirement");
	state.Status = sgre::scene::TaskStatus::Complete;
	fixture.Layers.SetProcessState(target, state);
	fixture.Proxies.Draw(proxy_handle, 3);
	Require(own_draws == 3 && target_draws == 1, "target status1 remains drawable under exact native status!=2 predicate");
	const auto weak = fixture.Vm.CreateWeakReference(target);
	const auto target_handle = fixture.Layers.Handle(target);
	target = {};
	fixture.Vm.ResetExecution();
	static_cast<void>(fixture.Vm.CollectGarbage());
	fixture.Vm.RethrowReleaseFailure();
	Require(!fixture.Tree.Contains(target_handle) && std::holds_alternative<std::monostate>(fixture.Vm.ResolveWeakReference(weak)), "canonical weak proxy never keeps source target native wrapper alive");
	const auto reads = fixture.TargetStateReads;
	fixture.Proxies.Draw(proxy_handle, 4);
	Require(own_draws == 3 && target_draws == 1 && fixture.TargetStateReads == reads && fixture.Camera.Depth() == 1, "expired target skips entire own common graph before services/camera dispatch");
	fixture.Close();
}

static void TestNativeNullTargetAndWeakDropBeforeBaseRetirement() {
	ProxyFixture fixture;
	const auto target = fixture.Base();
	const auto weak = fixture.Vm.CreateWeakReference(target);
	const auto baseline_weak_roots = fixture.Vm.RetainedReferenceCount(weak);
	const auto proxy = fixture.Proxy(target);
	const auto handle = fixture.Layers.Handle(proxy);
	Require(fixture.Vm.RetainedReferenceCount(weak) == baseline_weak_roots + 1 && fixture.Vm.RetainedReferenceCount(target) == 0 && fixture.Vm.RetainedReferenceCount(proxy) == 0, "actual proxy roots one canonical weak wrapper and neither target nor self");
	std::size_t own_draws = 0;
	const auto base_releases = std::make_shared<std::size_t>(0);
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Draw = [&](auto, auto) {
		++own_draws;
	};
	callbacks.Release = [&fixture, weak, handle, base_releases](auto) {
		Require(fixture.Vm.RetainedReferenceCount(weak) == 0 && fixture.Tree.Contains(handle) && !fixture.Tree.State(handle).Attached, "derived target weak drops before native Base task deletion and final Object/tree release");
		++*base_releases;
	};
	static_cast<void>(fixture.Layers.BaseTasks(proxy).CommonFirst().Append(std::move(callbacks)));
	fixture.Layers.ReleaseInstance(target);
	Require(fixture.Vm.ResolveWeakReference(weak) == target, "explicit target-native retirement leaves genuine weak referent wrapper distinct from expiration");
	const auto reads = fixture.TargetStateReads;
	fixture.Proxies.Draw(handle, 19);
	Require(own_draws == 0 && fixture.TargetStateReads == reads, "live target wrapper with null native owner skips entire proxy graph");
	fixture.Proxies.ReleaseInstance(proxy);
	Require(*base_releases == 1 && !fixture.Tree.Contains(handle) && !fixture.Proxies.Owns(handle), "proxy-first native cleanup releases weak before actual Base graph and retires once");
	fixture.Close();
}

static void TestParentFirstAndProxyFirstIndependentLifetimes() {
	for (const bool parent_first : {false, true}) {
		ProxyFixture fixture;
		const auto parent = fixture.Folder();
		const auto target = fixture.Base();
		const auto weak = fixture.Vm.CreateWeakReference(target);
		const auto baseline_weak_roots = fixture.Vm.RetainedReferenceCount(weak);
		const auto proxy = fixture.Proxy(target, parent);
		const auto handle = fixture.Layers.Handle(proxy);
		if (parent_first) {
			fixture.Layers.ReleaseInstance(parent);
			Require(fixture.Tree.Contains(handle) && !fixture.Tree.State(handle).Attached && !fixture.Tree.State(handle).Owner && fixture.Proxies.Owns(handle) && fixture.Vm.RetainedReferenceCount(weak) == baseline_weak_roots + 1, "parent manager retirement detaches retained proxy without destroying its independent weak target graph");
		}
		fixture.Proxies.ReleaseInstance(proxy);
		Require(!fixture.Tree.Contains(handle) && fixture.Tree.Contains(fixture.Layers.Handle(target)) && fixture.Vm.RetainedReferenceCount(weak) == baseline_weak_roots, "proxy native retirement never releases an independently live target process");
		if (!parent_first) {
			fixture.Layers.ReleaseInstance(parent);
		}
		fixture.Close();
	}
}

static void TestDrawFailureRestoresScopesAndActualLifetimeGuards() {
	ProxyFixture fixture;
	const auto target = fixture.Base();
	const auto proxy = fixture.Proxy(target);
	const auto handle = fixture.Layers.Handle(proxy);
	bool fail = true;
	std::size_t calls = 0;
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Draw = [&](auto, auto) {
		RequireFailure([&] {
			fixture.Proxies.ReleaseInstance(proxy);
		},
		    "active proxy retirement");
		RequireFailure([&] {
			fixture.Layers.ReleaseInstance(target);
		},
		    "active target retirement");
		RequireFailure([&] {
			fixture.Proxies.Draw(handle, 7);
		},
		    "same proxy draw recursion");
		Require(fixture.Proxies.Owns(handle) && fixture.Tree.Contains(fixture.Layers.Handle(target)), "real active lifetime failures preserve native owners before mutation");
		++calls;
		if (fail) {
			throw std::runtime_error("prepared actual target drawing failure");
		}
	};
	static_cast<void>(fixture.Layers.BaseTasks(target).Children().Append(std::move(callbacks)));
	RequireFailure([&] {
		fixture.Proxies.Draw(handle, 7);
	},
	    "actual target draw failure");
	Require(calls == 1 && fixture.Camera.Depth() == 1, "actual target exception restores both proxy and target camera/frame scopes");
	fail = false;
	fixture.Proxies.Draw(handle, 7);
	Require(calls == 2 && fixture.Camera.Depth() == 1, "post-failure genuine draw remains usable with preserved independent owners");
	fixture.Close();
}

static void TestDetachedCameraChildBlocksUnsafeProxyRetirement() {
	ProxyFixture fixture;
	const auto target = fixture.Base();
	const auto weak = fixture.Vm.CreateWeakReference(target);
	const auto baseline_weak_roots = fixture.Vm.RetainedReferenceCount(weak);
	const auto proxy = fixture.Proxy(target);
	const auto handle = fixture.Layers.Handle(proxy);
	auto& children = fixture.Layers.BaseTasks(proxy).Children();
	std::array<sgre::scene::ParallelTaskHandle, 1> order;
	Require(children.WriteOrder(order) == 1, "actual proxy has exactly one owned native camera task");
	auto detached = children.Extract(order[0]);
	RequireFailure([&] {
		fixture.Proxies.ReleaseInstance(proxy);
	},
	    "extracted native child still owns borrowed proxy callbacks");
	Require(fixture.Proxies.Owns(handle) && fixture.Tree.Contains(handle) && fixture.Vm.RetainedReferenceCount(weak) == baseline_weak_roots + 1, "failed child-lease preflight preserves proxy and canonical weak owner before mutation");
	detached.Release();
	fixture.Proxies.ReleaseInstance(proxy);
	Require(!fixture.Tree.Contains(handle) && fixture.Vm.RetainedReferenceCount(weak) == baseline_weak_roots, "actual detached child release acknowledgement permits safe proxy destruction");
	fixture.Close();
}

static void TestTargetRetirementInsideOwnCommonGroupsRejectsBeforeNativeDispatch() {
	for (const bool first_group : {true, false}) {
		ProxyFixture fixture;
		const auto target = fixture.Base();
		const auto target_handle = fixture.Layers.Handle(target);
		const auto proxy = fixture.Proxy(target);
		const auto proxy_root = fixture.Vm.Retain(proxy);
		const auto handle = fixture.Layers.Handle(proxy);
		std::size_t own_calls = 0;
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Draw = [&](auto, auto) {
			++own_calls;
			fixture.Layers.ReleaseInstance(target);
		};
		auto& group = first_group ? fixture.Layers.BaseTasks(proxy).CommonFirst() : fixture.Layers.BaseTasks(proxy).CommonSecond();
		static_cast<void>(group.Append(std::move(callbacks)));
		RequireFailure([&] {
			fixture.Proxies.Draw(handle, 0x87654321u);
		},
		    "target native retirement during borrowed own common draw");
		Require(own_calls == 1 && !fixture.Tree.Contains(target_handle) && fixture.Proxies.Owns(handle) && fixture.TargetStateReads == 0 && fixture.TargetDispatches == 0, "mid-borrow target loss rejects before any target state read or virtual dispatch");
		Require(fixture.Camera.Depth() == 1 && fixture.Layers.BaseTasks(proxy).CanClear() && fixture.Layers.CommonTasks(proxy).CanClear(), "target-loss failure restores camera/frame/traversal guards without weakening retirement");
		fixture.Proxies.Draw(handle, 0x87654321u);
		Require(own_calls == 1 && fixture.TargetStateReads == 0 && fixture.TargetDispatches == 0, "subsequent native-null weak resolution skips the entire proxy graph");
		fixture.Close();
	}
}

static void TestIndirectProxyRecursionRejectsAndRestoresRealFrames() {
	ProxyFixture fixture;
	const auto target = fixture.Base();
	const auto first = fixture.Proxy(target);
	const auto second = fixture.Proxy(first);
	const std::array<sgre::script::SquirrelHostRoot, 3> roots{fixture.Vm.Retain(target), fixture.Vm.Retain(first), fixture.Vm.Retain(second)};
	const auto first_handle = fixture.Layers.Handle(first);
	const auto second_handle = fixture.Layers.Handle(second);
	bool recurse = true;
	std::size_t target_calls = 0;
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Draw = [&](auto, auto argument) {
		++target_calls;
		if (recurse) {
			fixture.Proxies.Draw(second_handle, argument);
		}
	};
	static_cast<void>(fixture.Layers.BaseTasks(target).Children().Append(std::move(callbacks)));
	RequireFailure([&] {
		fixture.Proxies.Draw(second_handle, 91);
	},
	    "second proxy to first proxy to target to second proxy recursion");
	Require(target_calls == 1 && fixture.Camera.Depth() == 1 && fixture.Proxies.Owns(first_handle) && fixture.Proxies.Owns(second_handle), "indirect proxy recursion is bounded and restores all real outer/inner camera scopes");
	Require(fixture.Layers.BaseTasks(first).CanClear() && fixture.Layers.BaseTasks(second).CanClear() && fixture.Layers.BaseTasks(target).CanClear(), "recursive rejection releases each actual owned traversal guard");
	recurse = false;
	fixture.Proxies.Draw(second_handle, 91);
	Require(target_calls == 2 && fixture.Camera.Depth() == 1, "genuine nested proxy draw remains usable after rejected indirect recursion");
	fixture.Close();
}

struct ConfiguredScreenOwnerState {
	ProxyFixture* Fixture = nullptr;
	sgre::scene::LayerScreenRootHandle Root;
	sgre::script::SquirrelObjectIdentity Instance;
	bool Released = false;
};

static void ReleaseConfiguredScreenOwner(const std::shared_ptr<ConfiguredScreenOwnerState>& state, const SquirrelValue& receiver) {
	if (state->Released) {
		return;
	}
	auto& fixture = *state->Fixture;
	Require(fixture.Vm.BorrowObjectIdentity(receiver) == state->Instance, "configured Screen retirement receives its exact real native Object identity");
	fixture.Layers.DetachScreenOwners(receiver);
	fixture.Objects.ReleaseInstance(receiver);
	fixture.Tree.ReleaseScreenRoot(state->Root);
	state->Released = true;
}

static void TestLeadingConfiguredScreenPreservesCurrentContextTypeProbeError() {
	for (const bool child_context : {false, true}) {
		ProxyFixture fixture;
		const auto state = std::make_shared<ConfiguredScreenOwnerState>();
		state->Fixture = &fixture;
		state->Root = fixture.Tree.CreateScreenRoot();
		const auto screen_class = fixture.Vm.CreateClass(fixture.Objects.ObjectClass());
		const auto screen = fixture.Vm.CreateInstance(screen_class);
		fixture.Objects.AttachInstance(screen);
		state->Instance = fixture.Vm.BorrowObjectIdentity(screen);
		const auto screen_root = fixture.Vm.Retain(screen);
		fixture.Layers.ConfigureScreenOwners(screen_class, [state](const auto& receiver) {
			Require(!state->Released && state->Fixture->Vm.BorrowObjectIdentity(receiver) == state->Instance, "configured genuine Screen resolver uses its exact native Object and live tree root");

			return state->Root;
		});
		fixture.Objects.ConfigureNativeRelease(screen, [state](const auto& receiver) {
			ReleaseConfiguredScreenOwner(state, receiver);
		});
		const auto target = fixture.Base();
		const auto child = fixture.Vm.CreateContext();
		fixture.Vm.SetLastErrorInContext(fixture.Vm.RootContext(), std::int32_t{71});
		fixture.Vm.SetLastErrorInContext(child, std::int32_t{91});
		const auto actual_context = child_context ? child : fixture.Vm.RootContext();
		const auto proxy = fixture.Vm.InvokeInContext(actual_context, fixture.Proxies.LayerProxyClass(), std::array<SquirrelValue, 3>{fixture.Vm.RootTableInContext(actual_context), screen, target}).Value;
		const auto expected_error = SquirrelValue{std::string("invalid type tag")};
		Require(fixture.Vm.LastErrorInContext(actual_context) == expected_error, "accepted leading Screen retains failed Folder trial error in the actual constructor context");
		Require(fixture.Vm.LastErrorInContext(child_context ? fixture.Vm.RootContext() : child) == SquirrelValue{std::int32_t{child_context ? 71 : 91}}, "successful Screen parent probe does not overwrite another VM context's LastError");
		const auto handle = fixture.Layers.Handle(proxy);
		Require(fixture.Tree.State(handle).ScreenOwner == state->Root && !fixture.Tree.State(handle).Owner && fixture.Call(proxy, "getOwner") == screen && fixture.TargetDispatches == 0 && fixture.TargetStateReads == 0, "real LayerProxy constructor uses configured Screen root ownership without drawing or replacing the actual Base target");
		fixture.Proxies.ReleaseInstance(proxy);
		fixture.Vm.ReleaseContext(child);
		ReleaseConfiguredScreenOwner(state, screen);
		fixture.Close();
		Require(state->Released && !fixture.Tree.ContainsScreenRoot(state->Root), "configured real native Screen/root retirement balances without allocating a renderer or device");
	}
}
int main() {
	try {
		TestGenuineRootClassConstructorAndInheritedState();
		TestOwnUpdateAndOrderedDrawWithoutTargetAdvance();
		TestConcreteMotionDrawPreservesOpacityArgumentAndNestedCamera();
		TestTargetVisibilityStatusAndWholeGraphExpirationGate();
		TestNativeNullTargetAndWeakDropBeforeBaseRetirement();
		TestParentFirstAndProxyFirstIndependentLifetimes();
		TestDrawFailureRestoresScopesAndActualLifetimeGuards();
		TestDetachedCameraChildBlocksUnsafeProxyRetirement();
		TestTargetRetirementInsideOwnCommonGroupsRejectsBeforeNativeDispatch();
		TestIndirectProxyRecursionRejectsAndRestoresRealFrames();
		TestLeadingConfiguredScreenPreservesCurrentContextTypeProbeError();
		std::cout << "PASS 11 actual LayerProxy VM/Base/Motion draw and weak owner groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
