#include "sgre/script/layer_draw_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected LayerDraw failure: " + std::string(message));
}

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, const char* method, std::initializer_list<SquirrelValue> parameters = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), parameters);

	return vm.Invoke(vm.GetSlot(receiver, std::string(method)), arguments).Value;
}

static sgre::scene::LayerCameraFrameState FrameState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {1280, 720};
	state.View.Coefficients = {2.0f / 1280, 0, 0, -1, 0, -2.0f / 720, 0, 1, 0, 0, 1, 0};
	state.RawScissor = {0, 0, 1280, 720};
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

struct Services {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptDrawObjectBindings DrawObjects;
	sgre::scene::LayerCameraDrawScope Frame;
	sgre::script::ScriptLayerDrawBindings Proxies;

	static sgre::script::SquirrelVmLimits Limits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	explicit Services(bool camera = false) : Vm(Limits()), Objects(Vm), Psb(Vm, Objects), Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		                                         throw std::runtime_error("LayerDraw fixture unexpectedly requested asset upload");
	                                         },
	                                                                                          [](auto) {
	                                                                                          }),
	                                         Catalog(Cache), Layers(Vm, Objects, Tree), DrawObjects(Vm, Objects), Frame(FrameState(), {}, {}), Proxies(Vm, Objects, Layers, DrawObjects, {}, camera ? &Frame : nullptr) {
		Vm.InstallRecoveredBaseLibrary();
		Layers.ConfigureMotionOwners(Psb, Catalog, Cache);
	}

	~Services() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	SquirrelValue Parent() {
		return Vm.Invoke(Layers.LayerClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
	}

	SquirrelValue Create(const SquirrelValue& parent, const SquirrelValue& target) {
		return Vm.Invoke(Proxies.LayerDrawClass(), std::array<SquirrelValue, 4>{Vm.RootTable(), parent, target, std::string("ignored constructor tail")}).Value;
	}

	void Draw(const SquirrelValue& parent, std::uint32_t argument = 17) {
		Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Draw(argument);
	}

	void Update(const SquirrelValue& parent) {
		Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Update();
	}
};

static SquirrelValue AttachConcreteOwner(Services& services, sgre::script::ScriptNativeDrawObjectCallbacks callbacks) {
	const auto target = services.Vm.CreateInstance(services.Vm.CreateClass(services.DrawObjects.DrawObjectClass()));
	services.DrawObjects.AttachNativeDrawOwner(target, std::move(callbacks));

	return target;
}

static void TestRealClassCapturedParametersAndCamera() {
	Services services(true);
	const auto parent = services.Parent();
	std::size_t calls = 0;
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [&](std::uint32_t argument, std::uint32_t opacity) {
		Require(argument == 0x87654321u && opacity == 0xfedcba98u && services.Frame.Depth() == 1 && services.Frame.CurrentModel().Coefficients[0] == 2 && services.Frame.CurrentModel().Coefficients[5] == 3 && services.Frame.CurrentModel().Coefficients[3] == -7, "genuine camera-to-native-owner dispatch must preserve model and full DWORD arguments");
		++calls;
	};
	const auto target = AttachConcreteOwner(services, std::move(callbacks));
	const auto proxy = services.Create(parent, target);
	Require(services.Vm.IsInstanceOf(proxy, services.Objects.ObjectClass()) && !services.Vm.IsInstanceOf(proxy, services.Layers.LayerBaseClass()), "LayerDraw is a distinct direct Object subclass, not a LayerBase alias");
	Require(std::get<bool>(Call(services.Vm, proxy, "getVisible")) && std::get<std::int32_t>(Call(services.Vm, proxy, "getOpacity")) == 255, "DrawingProxy starts genuinely visible with opacity 255");
	static_cast<void>(Call(services.Vm, proxy, "setScale", {2, 3}));
	static_cast<void>(Call(services.Vm, proxy, "setOffset", {7, -2}));
	static_cast<void>(Call(services.Vm, proxy, "setOpacity", {std::bit_cast<std::int32_t>(0xfedcba98u)}));
	services.Draw(parent, 0x87654321u);
	Require(calls == 1 && services.Frame.Depth() == 0, "actual native owner dispatch must restore camera model depth");
	static_cast<void>(Call(services.Vm, proxy, "setVisible", {false}));
	services.Draw(parent, 0x87654321u);
	Require(calls == 1, "paused DrawingProxy must not call the concrete native owner");
	static_cast<void>(Call(services.Vm, proxy, "setVisible"));
	Require(std::get<bool>(Call(services.Vm, proxy, "getVisible")), "missing visible user argument must read truthy actual captured userdata");
	static_cast<void>(Call(services.Vm, proxy, "setOpacity"));
	Require(std::get<std::int32_t>(Call(services.Vm, proxy, "getOpacity")) == 0, "missing opacity user argument must fail numeric conversion of actual captured userdata to initialized zero");
	services.Proxies.ReleaseInstance(proxy);
	services.Update(parent);
}

static void TestCanonicalWeakRootAndDeferredTaskRetirement() {
	Services services;
	const auto parent = services.Parent();
	std::size_t releases = 0;
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [](auto, auto) {
	};
	callbacks.Release = [&] {
		++releases;
	};
	auto target = AttachConcreteOwner(services, std::move(callbacks));
	const auto weak = services.Vm.CreateWeakReference(target);
	const auto prior_weak_roots = services.Vm.RetainedReferenceCount(weak);
	const auto proxy = services.Create(parent, weak);
	Require(services.Vm.RetainedReferenceCount(weak) == prior_weak_roots + 1 && services.Vm.RetainedReferenceCount(target) == 0 && services.Vm.RetainedReferenceCount(proxy) == 0 && services.Vm.RetainedReferenceCount(parent) == 0, "proxy task must add exactly one canonical weak root without retaining target/self/parent");
	target = {};
	Require(releases == 1 && std::holds_alternative<std::monostate>(services.Vm.ResolveWeakReference(weak)), "proxy's rooted weak wrapper must not keep the native DrawObject target alive");
	services.Proxies.ReleaseInstance(proxy);
	Require(services.Vm.RetainedReferenceCount(weak) == 1, "wrapper retirement must leave the weak ObjectInfo owned by pending actual First task");
	services.Update(parent);
	Require(services.Vm.RetainedReferenceCount(weak) == 0, "actual task retirement must release weak ObjectInfo at the native destructor point");
	const auto expired = services.Create(parent, weak);
	Require(services.Vm.RetainedReferenceCount(weak) == 1, "constructor must retain original expired weak wrapper rather than resolved null");
	services.Proxies.ReleaseInstance(expired);
	services.Update(parent);
	Require(services.Vm.RetainedReferenceCount(weak) == 0, "expired-input weak root must also retire only with actual task ownership");
}

static void TestIgnoredTypeFailureAndExplicitRootContext() {
	Services services;
	const auto parent = services.Parent();
	std::size_t handler_calls = 0;
	const auto handler = services.Vm.CreateNativeClosure("must-not-handle-native-typecheck", [&](auto&, auto) {
		++handler_calls;

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	static_cast<void>(services.Vm.Invoke(services.Vm.GetSlot(services.Vm.RootTable(), std::string("seterrorhandler")), std::array<SquirrelValue, 2>{services.Vm.RootTable(), handler}));
	const auto null_proxy = services.Create(parent, {});
	services.Draw(parent);
	Require(services.Vm.LastError() == SquirrelValue{std::string("the object is not a class instance")} && handler_calls == 0, "native target conversion failure must skip draw while preserving exact error without invoking handler");
	services.Proxies.ReleaseInstance(null_proxy);
	services.Update(parent);
	const auto wrong_target = services.Vm.CreateInstance(services.Objects.ObjectClass());
	const auto wrong_proxy = services.Create(parent, wrong_target);
	services.Draw(parent);
	Require(services.Vm.LastError() == SquirrelValue{std::string("invalid type tag")} && handler_calls == 0, "native wrong-class lookup must preserve its distinct ignored type-tag error");
	services.Proxies.ReleaseInstance(wrong_proxy);
	services.Update(parent);
	const auto uninitialized = services.Vm.CreateInstance(services.DrawObjects.DrawObjectClass());
	const auto no_owner = services.Create(parent, uninitialized);
	services.Vm.SetLastErrorInContext(services.Vm.RootContext(), std::int32_t{123});
	services.Draw(parent);
	Require(services.Vm.LastError() == SquirrelValue{std::int32_t{123}}, "valid class with null native up pointer must silently skip without assigning a new error");
	services.Proxies.ReleaseInstance(no_owner);
	services.Update(parent);

	const auto child = services.Vm.CreateContext();
	services.Vm.SetLastErrorInContext(child, std::int32_t{91});
	const auto invalid = services.Create(parent, std::int32_t{42});
	const auto nested = services.Vm.CreateNativeClosure("nested-child-frame-dispatch", [&](auto&, auto) {
		services.Draw(parent);

		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{7});
	});
	Require(std::get<std::int32_t>(services.Vm.InvokeInContext(child, nested, std::array<SquirrelValue, 1>{services.Vm.RootTableInContext(child)}).Value) == 7 && services.Vm.LastErrorInContext(child) == SquirrelValue{std::int32_t{91}} && services.Vm.LastErrorInContext(services.Vm.RootContext()) == SquirrelValue{std::string("the object is not a class instance")}, "global native DrawingProxy lookup must change base error rather than currently running child's error");
	services.Proxies.ReleaseInstance(invalid);
	services.Update(parent);
	services.Vm.ReleaseContext(child);
}

static void TestParentFirstAndUnsafeDetachedOpacity() {
	Services services;
	const auto parent = services.Parent();
	const auto target = services.Vm.CreateInstance(services.DrawObjects.DrawObjectClass());
	const auto weak = services.Vm.CreateWeakReference(target);
	const auto proxy = services.Create(parent, target);
	services.Layers.ReleaseInstance(parent);
	Require(services.Vm.RetainedReferenceCount(weak) == 0 && services.Proxies.ScaleView(proxy).State().Detached && !std::get<bool>(Call(services.Vm, proxy, "getVisible")), "parent first must destroy task weak root before retained borrowed wrapper detaches");
	static_cast<void>(Call(services.Vm, proxy, "setOffset", {7, 9}));
	RequireFailure([&] {
		static_cast<void>(Call(services.Vm, proxy, "getOpacity"));
	},
	    "detached native task");
	services.Proxies.ReleaseInstance(proxy);
}

static void TestWrapperReleasePreflightAndMissingScope() {
	Services services;
	const auto parent = services.Parent();
	std::size_t calls = 0;
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [&](auto, auto) {
		++calls;
	};
	const auto target = AttachConcreteOwner(services, std::move(callbacks));
	const auto weak = services.Vm.CreateWeakReference(target);
	const auto prior_weak_roots = services.Vm.RetainedReferenceCount(weak);
	const auto proxy = services.Create(parent, target);
	const auto listener = services.Vm.Invoke(services.Objects.ObjectClass(), std::array<SquirrelValue, 1>{services.Vm.RootTable()}).Value;
	services.Objects.Subscribe(proxy, listener, [&](const auto&) {
		RequireFailure([&] {
			services.Proxies.ReleaseInstance(proxy);
		},
		    "completion notification");

		return true;
	});
	services.Objects.Notify(proxy);
	services.Draw(parent);
	Require(calls == 1 && services.Vm.RetainedReferenceCount(weak) == prior_weak_roots + 1 && std::get<bool>(Call(services.Vm, proxy, "getVisible")), "rejected release during notification must preserve native task and exact weak root");
	static_cast<void>(Call(services.Vm, proxy, "setOffset", {1, 0}));
	RequireFailure([&] {
		services.Draw(parent);
	},
	    "matrix/scissor scope");
	Require(calls == 1, "missing real camera service must never silently invoke an unscaled native target");
	services.Proxies.ReleaseInstance(proxy);
	services.Update(parent);
}

static void TestCloseRetiresNativeOwnersAndDeferredTargets() {
	Services services;
	const auto parent = services.Parent();
	std::size_t released = 0;
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [](auto, auto) {
	};
	callbacks.Release = [&] {
		++released;
	};
	const auto target = AttachConcreteOwner(services, std::move(callbacks));
	const auto weak = services.Vm.CreateWeakReference(target);
	const auto proxy = services.Create(parent, target);
	const auto weak_identity = services.Vm.BorrowObjectIdentity(weak);
	services.Vm.NewSlot(services.Vm.RootTable(), std::string("owned_layer_proxy"), proxy);
	services.Vm.NewSlot(services.Vm.RootTable(), std::string("owned_layer_parent"), parent);
	services.Vm.Close();
	services.Vm.RethrowReleaseFailure();
	Require(released == 1 && !services.Vm.ResolveObjectIdentity(weak_identity), "Close must release the native target and retire its deferred weak root before facade expiration");
}

static std::uint32_t ReadNativeWord(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	input.read(static_cast<char*>(static_cast<void*>(bytes.data())), bytes.size());
	Require(static_cast<bool>(input), "DrawingProxy native projection is truncated");

	return bytes[0] | (std::uint32_t{bytes[1]} << 8) | (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
}

static void TestNativeDrawingProxyProjection(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(ReadNativeWord(input) == 1000, "DrawingProxy native projection must retain the verified bounded case extent");
	Services services;
	const auto parent = services.Parent();
	const auto concrete_class = services.Vm.CreateClass(services.DrawObjects.DrawObjectClass());
	for (std::uint32_t case_id = 0; case_id != 1000; ++case_id) {
		std::array<std::uint32_t, 9> words{};
		for (auto& word : words) {
			word = ReadNativeWord(input);
		}

		const auto kind = words[0];
		Require(kind < 8, "DrawingProxy projection target kind exceeds the native prepared domain");
		SquirrelValue target;
		SquirrelValue source;
		std::optional<SquirrelValue> weak;
		if (kind == 1) {
			source = std::int32_t{42};
		} else if (kind == 2) {
			source = std::string("independent-proxy-target");
			weak = services.Vm.CreateWeakReference(source);
		} else if (kind == 3) {
			target = services.Vm.CreateInstance(services.Objects.ObjectClass());
			source = target;
			weak = services.Vm.CreateWeakReference(target);
		} else if (kind >= 4) {
			target = services.Vm.CreateInstance(concrete_class);
			weak = services.Vm.CreateWeakReference(target);
			source = kind >= 6 ? *weak : target;
			if (kind == 7) {
				target = {};
			}
		}

		std::vector<std::array<std::uint32_t, 2>> calls;
		if (kind == 4 || kind == 6) {
			sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
			callbacks.Draw = [&](std::uint32_t argument, std::uint32_t opacity) {
				calls.push_back({argument, opacity});
			};
			services.DrawObjects.AttachNativeDrawOwner(target, std::move(callbacks));
		}

		const auto prior_weak_roots = weak ? services.Vm.RetainedReferenceCount(*weak) : 0;
		const auto proxy = services.Create(parent, source);
		const auto task = services.Proxies.ScaleView(proxy).TaskState();
		Require(static_cast<std::uint32_t>(task.Status) == words[3] && static_cast<std::uint32_t>(task.DrawPaused) == words[4] && std::bit_cast<std::uint32_t>(std::get<std::int32_t>(Call(services.Vm, proxy, "getOpacity"))) == words[5], "DrawingProxy native constructor status/pause/opacity differs in case " + std::to_string(case_id));
		Require(!weak || services.Vm.RetainedReferenceCount(*weak) == prior_weak_roots + words[6], "DrawingProxy native canonical weak-root ownership differs");
		static_cast<void>(Call(services.Vm, proxy, "setOpacity", {std::bit_cast<std::int32_t>(words[2])}));
		services.Vm.SetLastErrorInContext(services.Vm.RootContext(), std::int32_t{123});
		services.Draw(parent, words[1]);
		Require(calls.size() == words[7] && (calls.empty() || calls[0] == std::array<std::uint32_t, 2>{words[1], words[2]}), "DrawingProxy native virtual dispatch selection/arguments differ");
		SquirrelValue expected_error = std::int32_t{123};
		if (words[8] == 1) {
			expected_error = std::string("the object is not a class instance");
		} else if (words[8] == 2) {
			expected_error = std::string("invalid type tag");
		}

		Require(services.Vm.LastError() == expected_error, "DrawingProxy ignored native error selection differs");
		services.Proxies.ReleaseInstance(proxy);
		Require(!weak || services.Vm.RetainedReferenceCount(*weak) == prior_weak_roots + words[6], "wrapper retirement must not perform the native task's weak-root release");
		services.Update(parent);
		Require(!weak || services.Vm.RetainedReferenceCount(*weak) == prior_weak_roots, "actual deferred task retirement must release exactly its native canonical weak root");
	}

	Require(input.peek() == std::char_traits<char>::eof(), "DrawingProxy native projection has unchecked trailing data");
}

int main(int argc, char** argv) {
	try {
		TestRealClassCapturedParametersAndCamera();
		TestCanonicalWeakRootAndDeferredTaskRetirement();
		TestIgnoredTypeFailureAndExplicitRootContext();
		TestParentFirstAndUnsafeDetachedOpacity();
		TestWrapperReleasePreflightAndMissingScope();
		TestCloseRetiresNativeOwnersAndDeferredTargets();
		if (argc == 2) {
			TestNativeDrawingProxyProjection(argv[1]);
		}
		std::cout << "Genuine LayerDraw proxy ownership fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
