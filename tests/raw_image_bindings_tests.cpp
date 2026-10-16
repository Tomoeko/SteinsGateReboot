#include "sgre/script/raw_image_bindings.hpp"

#include "sgre/script/binary_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::script::SquirrelValue;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected RawImage binding rejection");
}

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, std::string_view name, std::initializer_list<SquirrelValue> tail = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), tail);

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

static void TestGenuineClassesAndMutablePixels() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptRawImageBindings images(vm, objects, binary);
	const auto derived = vm.CreateClass(images.RawImageClass());
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), std::int32_t(2), std::int32_t(1)};
	const auto image = vm.Invoke(derived, arguments).Value;
	Require(vm.IsInstanceOf(image, objects.ObjectClass()) && vm.IsInstanceOf(image, images.RawImageClass()), "genuine RawImage derives from Object and supports a real derived constructor");
	Require(Call(vm, image, "getWidth") == SquirrelValue{std::int32_t(2)} && Call(vm, image, "getHeight") == SquirrelValue{std::int32_t(1)} && Call(vm, image, "getUpdate") == SquirrelValue{true}, "native scalar getters return one value and initial update is true");
	Require(std::holds_alternative<std::monostate>(Call(vm, image, "clearUpdate")), "clearUpdate returns status zero with no value");
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{1, 2, 3, 0, 10, 20, 30, 64});
	const auto input = binary.WrapBuffer(bytes);
	Require(std::holds_alternative<std::monostate>(Call(vm, image, "restore", {input})), "restore consumes real retained Binary bytes and returns no value");
	const auto color = std::bit_cast<std::int32_t>(std::uint32_t{0xaabbccdd});
	static_cast<void>(Call(vm, image, "fill", {std::int32_t(0), std::int32_t(0), std::int32_t(2), std::int32_t(1), color, std::string("false")}));
	Require(images.Image(image).StorePixels(true) == std::vector<std::uint8_t>{1, 2, 3, 0, 0xaa, 0xbb, 0xcc, 64}, "native truth conversion treats a nonempty string as true and preserves alpha/transparent pixels");
	const auto weak = Call(vm, image, "weakref", {input});
	Require(vm.ResolveWeakReference(weak) == input, "registered native weakref observes the last argument");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestBackgroundFilterCallbacks() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptRawImageBindings images(vm, objects, binary);
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), std::int32_t(3), std::int32_t(1)};
	const auto image = vm.Invoke(images.RawImageClass(), arguments).Value;
	static_cast<void>(Call(vm, image, "fill", {std::int32_t(0), std::int32_t(0), std::int32_t(3), std::int32_t(1), std::int32_t(-1)}));
	Require(std::holds_alternative<std::monostate>(Call(vm, image, "boxBlur", {std::int32_t(1), std::int32_t(0)})), "native blur defaults to one iteration and returns no values");
	Require(images.Image(image).StorePixels(true)[4] == 254, "blur callback must update the actual retained image");
	Require(std::holds_alternative<std::monostate>(Call(vm, image, "fillAlpha")), "fillAlpha requires only its receiver and returns no values");
	Require(images.Image(image).StorePixels(true)[7] == 255, "fillAlpha callback must update the same image's alpha");
	Require(std::holds_alternative<std::monostate>(Call(vm, image, "adjustLevel", {std::int32_t(32), std::int32_t(224), std::int32_t(32), std::int32_t(224), std::int32_t(32), std::int32_t(224)})), "six-endpoint level callback returns no values");
	Require(images.Image(image).StorePixels(true) == std::vector<std::uint8_t>{224, 224, 224, 255, 223, 223, 223, 255, 224, 224, 224, 255}, "level callback preserves alpha while mapping each actual RGB channel");
	RequireError([&] {
		static_cast<void>(Call(vm, image, "fillAlpha", {std::int32_t(1)}));
	},
	    "invalid argument");
	RequireError([&] {
		static_cast<void>(Call(vm, image, "boxBlur", {std::int32_t(-1), std::int32_t(0)}));
	},
	    "nonnegative radii");
	RequireError([&] {
		static_cast<void>(Call(vm, image, "boxBlur", {std::int32_t(0), std::int32_t(0), std::int32_t(0)}));
	},
	    "positive iteration");
	RequireError([&] {
		static_cast<void>(Call(vm, image, "adjustLevel", {std::int32_t(32), std::int32_t(224)}));
	},
	    "invalid argument");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestLimitsErrorsAndExplicitRelease() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptRawImageLimits limits;
	limits.MaxInstances = 1;
	limits.MaxPixelBytes = 8;
	sgre::script::ScriptRawImageBindings images(vm, objects, binary, limits);
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), std::int32_t(2), std::int32_t(1)};
	const auto image = vm.Invoke(images.RawImageClass(), arguments).Value;
	RequireError([&] {
		static_cast<void>(vm.Invoke(images.RawImageClass(), arguments));
	},
	    "storage domain");
	RequireError([&] {
		const std::array<SquirrelValue, 2> incomplete{vm.RootTable(), std::int32_t(1)};
		static_cast<void>(vm.Invoke(images.RawImageClass(), incomplete));
	},
	    "invalid argument");
	const auto input = binary.WrapBuffer(std::make_shared<const std::vector<std::uint8_t>>(5, 0));
	RequireError([&] {
		static_cast<void>(Call(vm, image, "restore", {input}));
	},
	    "byte count");
	RequireError([&] {
		static_cast<void>(Call(vm, image, "fill", {std::int32_t(-1), std::int32_t(0), std::int32_t(1), std::int32_t(1), std::int32_t(0)}));
	},
	    "rectangle");
	Require(images.Image(image).IsUpdated(), "failed mutation preserves the original update state");
	images.ReleaseInstance(image);
	RequireError([&] {
		static_cast<void>(images.Image(image));
	},
	    "live pixel owner");
	const auto replacement = vm.Invoke(images.RawImageClass(), arguments).Value;
	Require(images.Image(replacement).BgraPixels().size() == 8, "actual release returns both instance and aggregate pixel capacity");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestAutomaticNativeRetirement() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptRawImageLimits image_limits;
	image_limits.MaxInstances = 1;
	image_limits.MaxPixelBytes = 4;
	sgre::script::ScriptRawImageBindings images(vm, objects, binary, image_limits);
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), std::int32_t(1), std::int32_t(1)};
	SquirrelValue weak;
	{
		const auto image = vm.Invoke(images.RawImageClass(), arguments).Value;
		weak = vm.CreateWeakReference(image);
		static_cast<void>(Call(vm, image, "getWidth"));
	}

	vm.RethrowReleaseFailure();
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "last native image reference retires its actual pixels through the Object release bridge");
	const auto replacement = vm.Invoke(images.RawImageClass(), arguments).Value;
	Require(images.Image(replacement).Width() == 1, "automatic native retirement frees bounded registry/storage capacity");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestBackgroundFilterCallbacks();
		TestGenuineClassesAndMutablePixels();
		TestLimitsErrorsAndExplicitRelease();
		TestAutomaticNativeRetirement();
		std::cout << "genuine RawImage binding and native pixel ownership passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
