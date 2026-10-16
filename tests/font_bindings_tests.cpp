#include "sgre/script/font_bindings.hpp"

#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("Font binding fixture expected failure: " + std::string(expected));
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());

	return vm.Invoke(class_value, arguments).Value;
}

static void TestActualClassesAndUnparsedOwnerLifetime() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptFontBindings fonts(vm, objects, psb);
	Require(vm.GetSlot(vm.RootTable(), std::string("Font")) == fonts.FontClass() && vm.GetSlot(vm.RootTable(), std::string("PSBFont")) == fonts.PsbFontClass(), "Font registration names or class identities differ");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.FontClass()));
	},
	    "can't create instance");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.PsbFontClass()));
	},
	    "PSBObject");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.PsbFontClass(), {vm.CreateTable()}));
	},
	    "PSBObject");
	const auto document = std::make_shared<sgre::resource::PsbDocument>();
	const auto owner = psb.WrapDocument(document);
	const auto first = Construct(vm, fonts.PsbFontClass(), {owner, std::string("ignored constructor tail")});
	const auto second = Construct(vm, fonts.PsbFontClass(), {owner});
	Require(vm.IsInstanceOf(first, fonts.PsbFontClass()) && vm.IsInstanceOf(first, fonts.FontClass()) && vm.IsInstanceOf(first, objects.ObjectClass()) && objects.HasInstance(first), "PSBFont must retain actual Font/Object ancestry and native base state");
	Require(fonts.PsbOwner(first) == owner && fonts.PsbOwner(second) == owner && vm.RetainedReferenceCount(owner) == 2, "each unparsed provider must retain its underlying PSBObject once");
	const std::array<sgre::script::SquirrelValue, 1> receiver{first};
	const auto weak = vm.Invoke(vm.GetSlot(first, std::string("weakref")), receiver).Value;
	Require(vm.Type(weak) == sgre::script::SquirrelValueType::WeakReference && vm.ResolveWeakReference(weak) == first, "native registered Font weakref must refer to the actual wrapper");
	const std::array<sgre::script::SquirrelValue, 2> weak_tail{first, std::int32_t{17}};
	Require(std::get<std::int32_t>(vm.Invoke(vm.GetSlot(first, std::string("weakref")), weak_tail).Value) == 17, "registered Font weakref must use the native topmost argument and preserve a primitive");
	const auto subclass = vm.CreateClass(fonts.PsbFontClass());
	const auto derived = Construct(vm, subclass, {owner});
	Require(fonts.PsbOwner(derived) == owner && vm.RetainedReferenceCount(owner) == 3, "PSBFont constructor must accept a genuine derived receiver");
	fonts.ReleaseInstance(first);
	Require(!objects.HasInstance(first) && vm.RetainedReferenceCount(owner) == 2 && fonts.PsbOwner(second) == owner, "PSBFont deletion must release only its own root and base state");
	RequireFailure([&] {
		static_cast<void>(fonts.PsbOwner(first));
	},
	    "no live provider");
	fonts.ReleaseInstance(second);
	fonts.ReleaseInstance(derived);
	Require(vm.RetainedReferenceCount(owner) == 0, "last provider deletion left an underlying document root");
	vm.Close();
}

static void TestInstanceLimitsAndRetainedUnderlyingDocument() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptFontBindings fonts(vm, objects, psb, 1);
	RequireFailure([&] {
		static_cast<void>(sgre::script::ScriptFontBindings(vm, objects, psb, 0));
	},
	    "nonzero");
	const auto owner = psb.WrapDocument(std::make_shared<sgre::resource::PsbDocument>());
	const auto font = Construct(vm, fonts.PsbFontClass(), {owner});
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.PsbFontClass(), {owner}));
	},
	    "limit");
	Require(vm.RetainedReferenceCount(owner) == 1, "failed bounded construction leaked an ObjectInfo root");
	auto consumer = vm.Retain(fonts.PsbOwner(font));
	Require(vm.RetainedReferenceCount(owner) == 2 && vm.RetainedReferenceCount(font) == 0, "provider consumer must copy the underlying document rather than retain the Font wrapper");
	fonts.ReleaseInstance(font);
	Require(vm.RetainedReferenceCount(owner) == 1 && consumer.Value() == owner, "independent consumer document root did not survive provider deletion");
	consumer = {};
	Require(vm.RetainedReferenceCount(owner) == 0, "consumer root was not retired exactly once");
	vm.Close();
}

static void TestReleaseFailureCompletesDerivedAndBaseCleanup() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptFontBindings fonts(vm, objects, psb);
	const auto owner = psb.WrapDocument(std::make_shared<sgre::resource::PsbDocument>());
	const auto font = Construct(vm, fonts.PsbFontClass(), {owner});
	const auto listener = Construct(vm, objects.ObjectClass());
	objects.Subscribe(font, listener, [&](const auto&) -> bool {
		Require(vm.RetainedReferenceCount(owner) == 0, "PSBFont root must retire before native Object completion notification");
		throw std::runtime_error("Font completion fixture");
	});
	RequireFailure([&] {
		fonts.ReleaseInstance(font);
	},
	    "Font completion fixture");
	Require(!objects.HasInstance(font) && vm.RetainedReferenceCount(owner) == 0, "failed Font completion left derived/base native state or document ownership");
	RequireFailure([&] {
		static_cast<void>(fonts.PsbOwner(font));
	},
	    "no live provider");
	vm.Close();
}

static void TestAutomaticReclamationAndVmClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptFontBindings fonts(vm, objects, psb);
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	const std::weak_ptr<const sgre::resource::PsbDocument> weak_document = document;
	auto owner = psb.WrapDocument(document);
	document.reset();
	auto font = Construct(vm, fonts.PsbFontClass(), {owner});
	const auto weak_owner = vm.CreateWeakReference(owner);
	const auto weak_font = vm.CreateWeakReference(font);
	owner = {};
	Require(!weak_document.expired(), "retained PSBFont did not keep its borrowed PSB wrapper alive");
	font = {};
	const auto overwrite = vm.CreateNativeClosure("Font fixture overwrite", [](auto&, const auto) {
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	static_cast<void>(vm.Invoke(overwrite, receiver));
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_font)) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_owner)) && weak_document.expired(), "automatic provider deletion failed to release its PSB wrapper/document");
	const auto retained_owner = psb.WrapDocument(std::make_shared<sgre::resource::PsbDocument>());
	const auto retained_font = Construct(vm, fonts.PsbFontClass(), {retained_owner});
	Require(vm.RetainedReferenceCount(retained_owner) == 1 && objects.HasInstance(retained_font), "live provider fixture lacks native ownership");
	vm.Close();
	Require(vm.IsClosed(), "VM close did not retire native Font adapters");
}

static void TestExactUnavailableSystemFontSemantics() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptFontBindings fonts(vm, objects, psb);
	Require(vm.GetSlot(vm.RootTable(), std::string("SystemFont")) == fonts.SystemFontClass(), "SystemFont must retain its original registration name");
	const auto basic = Construct(vm, fonts.SystemFontClass());
	Require(vm.IsInstanceOf(basic, fonts.FontClass()) && vm.IsInstanceOf(basic, objects.ObjectClass()) && objects.HasInstance(basic), "SystemFont must retain real Font/Object state");
	Require(fonts.SystemState(basic).Scale == 1 && fonts.SystemState(basic).Name.empty(), "native parameterless SystemFont defaults differ");
	const std::array<sgre::script::SquirrelValue, 1> receiver{basic};
	Require(!std::get<bool>(vm.Invoke(vm.GetSlot(basic, std::string("ready")), receiver).Value), "native SystemFont ready must remain the original constant false");
	RequireFailure([&] {
		static_cast<void>(fonts.PsbOwner(basic));
	},
	    "no native font provider");
	const auto invalid_number = Construct(vm, fonts.SystemFontClass(), {vm.CreateTable()});
	Require(fonts.SystemState(invalid_number).Scale == 0, "nonnumeric SystemFont scale must retain native zero conversion fallback");
	const auto name = Construct(vm, fonts.SystemFontClass(), {true, std::string("Named\0ignored", 13)});
	Require(fonts.SystemState(name).Scale == 1 && fonts.SystemState(name).Name == "Named", "SystemFont bool scale or C-string name termination differs");
	const auto number_name = Construct(vm, fonts.SystemFontClass(), {2.0f, std::int32_t{17}});
	Require(fonts.SystemState(number_name).Scale == 2 && fonts.SystemState(number_name).Name == "17", "SystemFont name must use the actual VM tostring conversion");
	for (const auto bits : std::array<std::uint32_t, 4>{0x80000000, 0x7f800000, 0x7fc00001, 0xffc00001}) {
		const auto value = Construct(vm, fonts.SystemFontClass(), {std::bit_cast<float>(bits)});
		Require(std::bit_cast<std::uint32_t>(fonts.SystemState(value).Scale) == bits, "SystemFont signed-zero/infinity/NaN storage differs from the original ordered comparison");
		fonts.ReleaseInstance(value);
	}
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.SystemFontClass(), {-1.0f}));
	},
	    "negative");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.SystemFontClass(), {1.0f, std::string("Name"), std::int32_t{7}}));
	},
	    "invalid argument");
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.SystemFontClass(), {1.0f, std::string(65537, 'A')}));
	},
	    "bounded binding storage");
	fonts.ReleaseInstance(name);
	Require(!objects.HasInstance(name), "SystemFont explicit deletion left Object base state");
	RequireFailure([&] {
		static_cast<void>(fonts.SystemState(name));
	},
	    "no live owner");
	vm.Close();
}

static void TestSystemFontSharedCapacityAndAutomaticReclamation() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptFontBindings fonts(vm, objects, psb, 1);
	auto font = Construct(vm, fonts.SystemFontClass(), {1.0f, std::string(48, 'N')});
	const auto weak = vm.CreateWeakReference(font);
	const auto psb_owner = psb.WrapDocument(std::make_shared<sgre::resource::PsbDocument>());
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fonts.PsbFontClass(), {psb_owner}));
	},
	    "limit");
	Require(vm.RetainedReferenceCount(psb_owner) == 0, "mixed Font capacity rejection leaked a PSB root");
	font = {};
	const auto overwrite = vm.CreateNativeClosure("SystemFont overwrite", [](auto&, const auto) {
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	static_cast<void>(vm.Invoke(overwrite, receiver));
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "SystemFont native wrapper/string ownership was not reclaimed");
	const auto replacement = Construct(vm, fonts.SystemFontClass());
	Require(fonts.SystemState(replacement).Name.empty(), "reclaimed SystemFont did not release combined instance capacity");
	vm.Close();
}

int main() {
	try {
		TestActualClassesAndUnparsedOwnerLifetime();
		TestInstanceLimitsAndRetainedUnderlyingDocument();
		TestReleaseFailureCompletesDerivedAndBaseCleanup();
		TestAutomaticReclamationAndVmClose();
		TestExactUnavailableSystemFontSemantics();
		TestSystemFontSharedCapacityAndAutomaticReclamation();
		std::cout << "Font/PSBFont native class, ObjectInfo lifetime and provider roots passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
