#include "sgre/script/vector_font_bindings.hpp"

#include "vector_font_fixture.hpp"

#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("VectorFont script fixture expected failure");
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());

	return vm.Invoke(class_value, arguments).Value;
}

static sgre::script::SquirrelValue Invoke(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, std::string_view method, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), instance);

	return vm.Invoke(vm.GetSlot(instance, std::string(method)), arguments).Value;
}

static void TestLoadingAndLazyPrivateProviders() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	bool completed = false;
	std::size_t requests = 0;
	std::size_t polls = 0;
	sgre::script::ScriptVectorFontBindings fonts(vm, objects, [&](std::string_view name, auto mode) {
		Require(name == "font.otf" && mode == sgre::script::ScriptResourceRequestMode::BinaryWrapped, "VectorFont must acquire an exact wrapped-binary identity at construction");
		++requests;

		return [&, bytes]() -> std::optional<sgre::script::ScriptResourceAsset> {
			++polls;
			if (!completed) {
				return {};
			}

			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = bytes;

			return asset;
		};
	},
	    [](auto, auto) {
		    throw std::runtime_error("unexpected VectorFont resource error");
	    });
	Require(vm.GetSlot(vm.RootTable(), std::string("VectorFont")) == fonts.VectorFontClass(), "VectorFont registration identity differs");
	const auto first = Construct(vm, fonts.VectorFontClass(), {std::string("font.otf\0tail", 13)});
	const auto second = Construct(vm, fonts.VectorFontClass(), {std::string("font.otf"), std::int32_t{4}, std::string("ja\0tail", 7), std::int32_t{99}});
	Require(vm.IsInstanceOf(first, objects.ObjectClass()) && objects.HasInstance(first) && requests == 2 && polls == 0, "VectorFont must attach actual Object state and request without polling or parsing font bytes");
	Require(fonts.State(first).Mode == 0 && fonts.State(first).Language == "en" && fonts.State(second).Mode == 4 && fonts.State(second).Language == "ja", "VectorFont constructor defaults, CString ownership or ignored tail differs");
	Require(std::get<bool>(Invoke(vm, first, "getLoading")) && polls == 1 && !fonts.State(first).ProviderPrepared, "getLoading must observe resource readiness without building a font");
	RequireFailure([&] {
		static_cast<void>(Invoke(vm, first, "getSize"));
	},
	    "promoted completed resource");
	completed = true;
	Require(!std::get<bool>(Invoke(vm, first, "getLoading")) && polls == 2 && fonts.State(first).ResourceCompleted && !fonts.State(first).ProviderPrepared, "completed VectorFont resources must be promoted without decoding font data");
	Require(std::get<float>(Invoke(vm, first, "getSize")) == 4294967296.0f && fonts.State(first).ProviderPrepared, "getSize must lazily create the native sentinel-size provider");
	Require(!std::get<bool>(Invoke(vm, second, "getLoading")) && polls == 3, "second owner completion was not observed independently");
	static_cast<void>(Invoke(vm, first, "setSize", {24.9f, std::int32_t{99}}));
	Require(std::get<float>(Invoke(vm, first, "getSize")) == 24 && std::get<float>(Invoke(vm, second, "getSize")) == 4294967296.0f && &fonts.Provider(first) != &fonts.Provider(second), "separate wrappers must own independent mutable vector providers");
	Require(fonts.Provider(first).PrepareGlyph(0).PixelSize == 24 && fonts.Provider(second).Configuration().Direction == sgre::text::VectorFontDirection::TopToBottom, "actual CFF provider preparation or native vertical mode was lost");
	static_cast<void>(Invoke(vm, first, "setSize"));
	Require(fonts.Provider(first).SizeState().Word == 0 && fonts.Provider(first).PrepareGlyph(0).PixelSize == 1, "missing setter argument must preserve native numeric fallback zero/minimum sizing");
	static_cast<void>(Invoke(vm, first, "setSize", {true}));
	Require(std::get<float>(Invoke(vm, first, "getSize")) == 1, "native setter bool conversion differs");
	const auto weak = Invoke(vm, first, "weakref");
	Require(vm.ResolveWeakReference(weak) == first && std::get<std::int32_t>(Invoke(vm, first, "weakref", {std::int32_t{17}})) == 17, "registered VectorFont weakref must observe the native last stack argument");
	const auto subclass = vm.CreateClass(fonts.VectorFontClass());
	const auto derived = Construct(vm, subclass, {std::string("font.otf")});
	Require(fonts.State(derived).Language == "en", "VectorFont native constructor must permit a genuine derived class");
	fonts.ReleaseInstance(first);
	Require(!objects.HasInstance(first) && fonts.State(second).ProviderPrepared, "retiring one native font must preserve its independent sibling");
	RequireFailure([&] {
		static_cast<void>(fonts.Provider(first));
	},
	    "no live native owner");
	vm.Close();
}

static void TestCompletedFailureKindsAndConstructorGuards() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	std::size_t requests = 0;
	std::size_t errors = 0;
	sgre::script::ScriptVectorFontBindings fonts(vm, objects, [&](std::string_view name, auto) {
		++requests;
		const auto identity = std::string(name);

		return [identity]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			if (identity == "failed") {
				asset.Error = "original resource failure";
			} else if (identity == "psb") {
				asset.Document = std::make_shared<sgre::resource::PsbDocument>();
			} else {
				asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
				asset.Backing = std::make_shared<const std::vector<std::uint8_t>>(4, 0);
			}

			return asset;
		};
	},
	    [&](auto name, auto error) {
		    Require(name == "failed" && error == "original resource failure", "font resource error identity differs");
		    ++errors;
	    });
	for (const auto name : {"failed", "psb", "malformed"}) {
		const auto font = Construct(vm, fonts.VectorFontClass(), {std::string(name)});
		Require(!std::get<bool>(Invoke(vm, font, "getLoading")) && !fonts.State(font).ProviderPrepared, "completed failed/wrong-kind/malformed data must end loading without parsing a font");
		Require(!std::get<bool>(Invoke(vm, font, "getLoading")), "promoted resource readiness must remain complete");
		RequireFailure([&] {
			static_cast<void>(Invoke(vm, font, "getSize"));
		},
		    name == std::string_view("malformed") ? "sfnt" : "successful raw font bytes");
		fonts.ReleaseInstance(font);
	}

	Require(errors == 1 && requests == 3, "completed errors must be reported once without repeated resource acquisition");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.VectorFontClass()));
	},
	    "resource name");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.VectorFontClass(), {std::int32_t{17}}));
	},
	    "actual string");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.VectorFontClass(), {std::string("name"), std::int32_t{0}, false}));
	},
	    "actual string");
	Require(requests == 3, "unsafe string guard must run before resource acquisition");
	const auto fallback = Construct(vm, fonts.VectorFontClass(), {std::string("name"), vm.CreateTable()});
	Require(fonts.State(fallback).Mode == 0, "native failed shaping-mode integer conversion must preserve zero fallback");
	vm.Close();
}

static void TestRetirementBeforeNotificationAndAutomaticClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	std::vector<std::weak_ptr<const std::vector<std::uint8_t>>> leases;
	sgre::script::ScriptVectorFontBindings fonts(vm, objects, [&](auto, auto) {
		auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
		leases.push_back(bytes);

		return [bytes]() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
			asset.Backing = bytes;

			return asset;
		};
	},
	    [](auto, auto) {
	    });
	const auto font = Construct(vm, fonts.VectorFontClass(), {std::string("font")});
	static_cast<void>(Invoke(vm, font, "getLoading"));
	static_cast<void>(Invoke(vm, font, "getSize"));
	const auto listener = Construct(vm, objects.ObjectClass());
	objects.Subscribe(font, listener, [&](const auto&) -> bool {
		Require(leases[0].expired(), "font provider and raw manager reference must retire before Object completion notification");
		throw std::runtime_error("font completion fixture");
	});
	RequireFailure([&] {
		fonts.ReleaseInstance(font);
	},
	    "font completion fixture");
	Require(!objects.HasInstance(font) && leases[0].expired(), "failed notification left a font/provider/resource owner");
	auto pending = Construct(vm, fonts.VectorFontClass(), {std::string("pending")});
	const auto weak = vm.CreateWeakReference(pending);
	pending = {};
	const auto overwrite = vm.CreateNativeClosure("font overwrite", [](auto&, auto) {
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	static_cast<void>(vm.Invoke(overwrite, receiver));
	Require(leases[1].expired() && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "automatic VectorFont finalization retained a pending resource pin");
	const auto retained = Construct(vm, fonts.VectorFontClass(), {std::string("retained")});
	static_cast<void>(Invoke(vm, retained, "getLoading"));
	static_cast<void>(Invoke(vm, retained, "getSize"));
	vm.Close();
	Require(leases.back().expired(), "VM Close must retire vector providers before their resource pins");
}

static void TestBoundsFailuresAndReentrantDiagnostics() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	bool request_failure = false;
	bool empty_observer = false;
	std::size_t errors = 0;
	sgre::script::ScriptVectorFontBindings* adapter = nullptr;
	sgre::script::SquirrelValue observed;
	sgre::script::ScriptVectorFontLimits limits;
	limits.MaxInstances = 1;
	limits.MaxNameBytes = 8;
	limits.MaxTotalNameBytes = 12;
	sgre::script::ScriptVectorFontBindings fonts(vm, objects, [&](auto, auto) -> sgre::script::ScriptResourceBindings::LoadPoll {
		if (request_failure) {
			throw std::runtime_error("request fixture");
		}
		if (empty_observer) {
			return {};
		}

		return []() -> std::optional<sgre::script::ScriptResourceAsset> {
			sgre::script::ScriptResourceAsset asset;
			asset.Error = "diagnostic fixture";

			return asset;
		};
	},
	    [&](auto name, auto diagnostic) {
		    ++errors;
		    adapter->ReleaseInstance(observed);
		    Require(name == "bad" && diagnostic == "diagnostic fixture", "reentrant diagnostic consumer lost its borrowed identity/text");
	    },
	    limits);
	adapter = &fonts;
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.VectorFontClass(), {std::string(9, 'a')}));
	},
	    "bounded binding storage");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.VectorFontClass(), {std::string(8, 'a'), std::int32_t{0}, std::string(8, 'b')}));
	},
	    "bounded binding storage");
	request_failure = true;
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.VectorFontClass(), {std::string("fail")}));
	},
	    "request fixture");
	request_failure = false;
	empty_observer = true;
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.VectorFontClass(), {std::string("empty")}));
	},
	    "readiness observer");
	empty_observer = false;
	observed = Construct(vm, fonts.VectorFontClass(), {std::string("bad")});
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.VectorFontClass(), {std::string("full")}));
	},
	    "instance limit");
	Require(!std::get<bool>(Invoke(vm, observed, "getLoading")) && errors == 1 && !objects.HasInstance(observed), "reentrant error observation did not safely complete owner retirement");
	const auto replacement = Construct(vm, fonts.VectorFontClass(), {std::string("valid")});
	Require(fonts.State(replacement).ResourceName == "valid", "failed or retired constructor leaked instance/name capacity");
	vm.Close();
}

static void TestCrossOwnerActiveLookupKeepsOriginalRequestLease() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	std::weak_ptr<std::uint8_t> lease_observer;
	std::size_t polls = 0;
	std::size_t queries = 0;
	bool active = false;
	sgre::script::ScriptVectorFontBindings fonts(vm, objects, [&](auto, auto) {
		const auto lease = std::make_shared<std::uint8_t>();
		lease_observer = lease;

		return [&, lease]() -> std::optional<sgre::script::ScriptResourceAsset> {
			static_cast<void>(lease);
			++polls;

			return {};
		};
	},
	    [](auto, auto) {
	    },
	    {}, [&](auto name) -> std::optional<sgre::script::ScriptResourceAsset> {
		    Require(name == "shared.otf", "active font lookup changed its exact manager identity");
		    ++queries;
		    if (!active) {
			    return {};
		    }

		    sgre::script::ScriptResourceAsset asset;
		    asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
		    asset.Backing = bytes;

		    return asset;
	    });
	const auto font = Construct(vm, fonts.VectorFontClass(), {std::string("shared.otf")});
	RequireFailure([&] {
		static_cast<void>(fonts.Provider(font));
	},
	    "promoted completed resource");
	Require(queries == 1 && polls == 0 && !fonts.State(font).ResourceCompleted, "active lookup must not promote merely pending work or poll for completion");
	active = true;
	Require(fonts.Provider(font).Font().GlyphCount() == 3 && fonts.State(font).ResourceCompleted && fonts.State(font).ProviderPrepared && queries == 2 && polls == 0 && !lease_observer.expired(), "another owner may promote the resource while the VectorFont keeps its own acquired manager reference");
	Require(!std::get<bool>(Invoke(vm, font, "getLoading")) && polls == 0, "globally active font data must be complete without promoting this observer again");
	fonts.ReleaseInstance(font);
	Require(lease_observer.expired(), "active borrowed data must release the original request lease only after its private font owner retires");
	vm.Close();
}

static void TestOptionalRawProviderDoesNotPromoteOrMaskDecodeErrors() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	std::size_t polls = 0;
	bool completed = false;
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::script::ScriptVectorFontBindings fonts(vm, objects, [&](auto name, auto) {
		const auto identity = std::string(name);

		return [&, identity]() -> std::optional<sgre::script::ScriptResourceAsset> {
			++polls;
			if (!completed) {
				return {};
			}

			sgre::script::ScriptResourceAsset asset;
			if (identity == "failed") {
				asset.Error = "optional provider failed source";
			} else if (identity == "psb") {
				asset.Document = std::make_shared<sgre::resource::PsbDocument>();
			} else {
				asset.Kind = sgre::script::ScriptResourceDataKind::Binary;
				asset.Backing = identity == "malformed" ? std::make_shared<const std::vector<std::uint8_t>>(4, 0) : bytes;
			}

			return asset;
		};
	},
	    [](auto, auto) {
	    });
	const auto pending = Construct(vm, fonts.VectorFontClass(), {std::string("valid")});
	Require(!fonts.TryProvider(pending) && polls == 0 && !fonts.State(pending).ResourceCompleted, "optional FontInfo lookup must preserve pending ownership without polling or promotion");
	completed = true;
	static_cast<void>(Invoke(vm, pending, "getLoading"));
	Require(fonts.TryProvider(pending) == &fonts.Provider(pending) && polls == 1 && fonts.State(pending).ProviderPrepared, "optional ready lookup must reuse the actual private vector core");
	for (const auto name : {"failed", "psb"}) {
		const auto unavailable = Construct(vm, fonts.VectorFontClass(), {std::string(name)});
		static_cast<void>(Invoke(vm, unavailable, "getLoading"));
		Require(!fonts.TryProvider(unavailable) && !fonts.State(unavailable).ProviderPrepared, "optional FontInfo lookup must accept unavailable completed data without inventing a provider");
		RequireFailure([&] {
			static_cast<void>(fonts.Provider(unavailable));
		},
		    "successful raw font bytes");
	}

	const auto malformed = Construct(vm, fonts.VectorFontClass(), {std::string("malformed")});
	static_cast<void>(Invoke(vm, malformed, "getLoading"));
	RequireFailure([&] {
		static_cast<void>(fonts.TryProvider(malformed));
	},
	    "sfnt");
	Require(!fonts.State(malformed).ProviderPrepared, "optional provider lookup must retain explicit malformed-byte errors rather than mask them as pending");
	vm.Close();
}

int main() {
	try {
		TestLoadingAndLazyPrivateProviders();
		TestCompletedFailureKindsAndConstructorGuards();
		TestRetirementBeforeNotificationAndAutomaticClose();
		TestBoundsFailuresAndReentrantDiagnostics();
		TestCrossOwnerActiveLookupKeepsOriginalRequestLease();
		TestOptionalRawProviderDoesNotPromoteOrMaskDecodeErrors();
		std::cout << "VectorFont script loading, lazy providers, exact sizes and native lifetime passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
