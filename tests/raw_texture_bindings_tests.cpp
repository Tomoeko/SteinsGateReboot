#include "sgre/script/raw_texture_bindings.hpp"

#include "raw_texture_fixture.hpp"
#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"

#include <array>
#include <bit>
#include <iostream>
#include <memory>
#include <span>
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

template <class Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual RawTex binding safety rejection");
}

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, std::string_view name, std::initializer_list<SquirrelValue> tail = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), tail);

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

struct Services {
	sgre::scene::tests::TaskFixtureBackend Backend;
	sgre::scene::LayerTree Tree{32};
	sgre::scene::ParallelTaskSet Driver;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptBinaryBindings Binary;
	sgre::script::ScriptRawImageBindings Images;
	sgre::script::ScriptRawTextureBindings Raw;

	static sgre::script::SquirrelVmLimits VmLimits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	Services() : Vm(VmLimits()), Objects(Vm), Layers(Vm, Objects, Tree), Binary(Vm, Objects), Images(Vm, Objects, Binary), Raw(Vm, Objects, Layers, Images, Binary, Tree, Backend, Driver) {
		Driver.SetRetireWhenEmpty(false);
	}

	~Services() {
		try {
			Vm.Close();
			Driver.Clear();
		} catch (...) {
		}
	}

	SquirrelValue Create(std::initializer_list<SquirrelValue> values, const SquirrelValue& class_value = {}) {
		std::vector<SquirrelValue> arguments{Vm.RootTable()};
		arguments.insert(arguments.end(), values);

		return Vm.Invoke(std::holds_alternative<std::monostate>(class_value) ? Raw.RawTextureClass() : class_value, arguments).Value;
	}
};

static std::vector<std::uint8_t> Shadow(sgre::scene::RawTextureTask& task) {
	const auto mapping = task.Texture().Map(true);
	std::vector<std::uint8_t> result;
	result.reserve(mapping.Bytes.size());
	for (const auto byte : mapping.Bytes) {
		result.push_back(std::to_integer<std::uint8_t>(byte));
	}
	task.Texture().Unmap();

	return result;
}

static void TestGenuineConstructorAndScalarMethods() {
	Services services;
	const auto derived = services.Vm.CreateClass(services.Raw.RawTextureClass());
	const auto raw = services.Create({SquirrelValue{}, std::int32_t(2), true, true}, derived);
	const auto handle = services.Raw.Owner(raw).Handle();
	Require(services.Vm.IsInstanceOf(raw, services.Raw.RawTextureClass()) && services.Vm.IsInstanceOf(raw, services.Layers.LayerBaseClass()) && !services.Vm.IsInstanceOf(raw, services.Layers.MotionLayerBaseClass()) && services.Objects.HasInstance(raw), "genuine RawTex derives directly from LayerBase with actual Object state");
	Require(services.Tree.State(handle).Kind == sgre::scene::LayerKind::RawTexture && services.Driver.Count() == 1 && services.Backend.Created.OriginalFormat == 87, "constructor creates exclusive raw texture and distinct driver-owned background group");
	Require(Call(services.Vm, raw, "getWidth") == SquirrelValue{std::int32_t(2)} && Call(services.Vm, raw, "getHeight") == SquirrelValue{std::int32_t(1)} && Call(services.Vm, raw, "getOpacity") == SquirrelValue{std::int32_t(255)} && Call(services.Vm, raw, "getRunning") == SquirrelValue{false}, "native integer/scalar getter statuses and empty group predicate differ");
	Require(!services.Raw.Owner(raw).TextureTask().State().Task.DrawPaused && services.Backend.Created.MipLevels == 1, "wrapper enables real drawing and preserves native one-mip allocation");
	services.Driver.Update();
	Require(services.Driver.Count() == 1 && !std::get<bool>(Call(services.Vm, raw, "getRunning")), "actual raw background group does not auto-retire empty");
	Require(std::holds_alternative<std::monostate>(Call(services.Vm, raw, "setOpacity", {std::int32_t(-1), std::string("tail")})), "native opacity setter returns no value and ignores tail");
	Require(Call(services.Vm, raw, "getOpacity") == SquirrelValue{std::int32_t(-1)} && services.Raw.Owner(raw).TextureTask().State().Opacity == 0xffffffff, "opacity keeps original full unsigned DWORD");
	static_cast<void>(Call(services.Vm, raw, "setOpacity", {std::string("invalid")}));
	Require(Call(services.Vm, raw, "getOpacity") == SquirrelValue{std::int32_t(0)}, "failed opacity conversion preserves initialized zero");
	static_cast<void>(Call(services.Vm, raw, "setColor", {true, 0.5f, std::string("bad"), std::string("tail")}));
	Require(services.Raw.Owner(raw).TextureTask().State().PackedRgb == 0xff7f0000, "typed native color conversion uses boolean/float/missing-zero independently");
	static_cast<void>(Call(services.Vm, raw, "setSmoothing", {true}));
	Require(services.Raw.Owner(raw).TextureTask().Texture().State().DesiredSampler.FilterWord == 21, "inherited smoothing changes actual raw owner sampler intent");
	services.Layers.ReleaseInstance(raw);
	Require(!services.Tree.Contains(handle) && !services.Backend.Live && services.Driver.Count() == 1, "wrapper retirement releases common/texture but retains pending driver ownership");
	services.Driver.Update();
	Require(services.Driver.Count() == 0, "real driver later retires marked background group");
}

static void TestActualImageOptionsAndBinaryRestoration() {
	Services services;
	const std::array<SquirrelValue, 1> root{services.Vm.RootTable()};
	const auto folder = services.Vm.Invoke(services.Layers.LayerFolderClass(), root).Value;
	const std::array<SquirrelValue, 3> image_arguments{services.Vm.RootTable(), std::int32_t(2), std::int32_t(1)};
	const auto image = services.Vm.Invoke(services.Images.RawImageClass(), image_arguments).Value;
	services.Images.StoreThumbnail(image, std::array<std::uint8_t, 8>{3, 2, 1, 4, 7, 6, 5, 8});
	const auto options = services.Vm.CreateTable();
	services.Vm.NewSlot(options, std::string("specFlag"), std::string("not-bool"));
	services.Vm.NewSlot(options, std::string("mipCount"), std::string("auto"));
	services.Vm.NewSlot(options, std::string("colorMode"), std::string("8888_RGBA\0unused", 16));
	const auto raw = services.Create({folder, image, options});
	Require(services.Tree.State(services.Raw.Owner(raw).Handle()).Owner == services.Layers.Handle(folder) && Call(services.Vm, raw, "getOwner") == folder, "native image constructor attaches to actual Folder owner");
	Require(services.Backend.Created.OriginalFormat == 28 && services.Backend.Created.MipLevels == 1 && services.Backend.UploadCount == 0, "colorMode C-string is consumed, spec/mip metadata does not invent physical mip allocation or early upload");
	Require(Shadow(services.Raw.Owner(raw).TextureTask()) == std::vector<std::uint8_t>{3, 2, 1, 4, 7, 6, 5, 8}, "actual RawImage copy preserves BGRA bytes even with RGBA physical format");
	const auto rgb = services.Binary.WrapBuffer(std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{10, 20, 30, 40, 50, 60}));
	Require(std::holds_alternative<std::monostate>(Call(services.Vm, raw, "restore", {rgb})), "actual Binary RGB restore returns native status zero");
	Require(Shadow(services.Raw.Owner(raw).TextureTask()) == std::vector<std::uint8_t>{30, 20, 10, 255, 60, 50, 40, 255}, "real restoration performs RGB-to-BGRA alpha-FF conversion");
	const auto weak = Call(services.Vm, raw, "weakref", {image});
	Require(services.Vm.ResolveWeakReference(weak) == image, "native weakref targets the last stack argument");
	services.Raw.ReleaseInstance(raw);
}

static void TestAutomaticAndVmCloseLifetime() {
	Services services;
	SquirrelValue weak;
	sgre::scene::LayerHandle handle;
	{
		const auto raw = services.Create({std::int32_t(1), std::int32_t(1)});
		handle = services.Raw.Owner(raw).Handle();
		weak = services.Vm.CreateWeakReference(raw);
		static_cast<void>(Call(services.Vm, raw, "getWidth"));
	}
	services.Vm.RethrowReleaseFailure();
	Require(!services.Tree.Contains(handle) && !services.Backend.Live && std::holds_alternative<std::monostate>(services.Vm.ResolveWeakReference(weak)), "binding does not retain a strong self through its native callbacks");
	services.Driver.Update();
	const auto replacement = services.Create({std::int32_t(1), std::int32_t(1)});
	handle = services.Raw.Owner(replacement).Handle();
	services.Vm.Close();
	services.Vm.RethrowReleaseFailure();
	Require(!services.Tree.Contains(handle) && !services.Backend.Live, "VM Close keeps actual adapters/backend alive through native RawTex retirement");
}

static void TestTypedErrorsAndBorrowedCallbackPreflight() {
	Services services;
	RequireError([&] {
		static_cast<void>(services.Create({}));
	},
	    "screen not found");
	RequireError([&] {
		static_cast<void>(services.Create({std::int32_t(1)}));
	},
	    "invalid argument");
	RequireError([&] {
		static_cast<void>(services.Create({std::string("bad"), std::int32_t(1)}));
	},
	    "positive extent");
	const auto raw = services.Create({std::int32_t(2), std::int32_t(1)});
	const auto handle = services.Raw.Owner(raw).Handle();
	RequireError([&] {
		static_cast<void>(Call(services.Vm, raw, "restore"));
	},
	    "invalid argument");
	RequireError([&] {
		static_cast<void>(Call(services.Vm, raw, "restore", {services.Vm.CreateTable()}));
	},
	    "invalid argument");
	const auto short_rgb = services.Binary.WrapBuffer(std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{1, 2, 3}));
	RequireError([&] {
		static_cast<void>(Call(services.Vm, raw, "restore", {short_rgb}));
	},
	    "byte count");
	bool rejected = false;
	sgre::scene::ParallelTaskCallbacks task;
	task.Update = [&](sgre::scene::ParallelTaskHandle) {
		RequireError([&] {
			services.Layers.ReleaseInstance(raw);
		},
		    "its own task callback");
		rejected = true;
	};
	static_cast<void>(services.Raw.Owner(raw).Group(sgre::scene::RawLayerTaskGroup::CommonFirst).Append(std::move(task)));
	services.Raw.Owner(raw).Group(sgre::scene::RawLayerTaskGroup::CommonFirst).Update();
	Require(rejected && services.Tree.State(handle).Attached && services.Backend.Live && services.Objects.HasInstance(raw), "release preflight preserves all ownership for a borrowed common callback");
	services.Raw.ReleaseInstance(raw);
}

int main() {
	try {
		TestGenuineConstructorAndScalarMethods();
		TestActualImageOptionsAndBinaryRestoration();
		TestAutomaticAndVmCloseLifetime();
		TestTypedErrorsAndBorrowedCallbackPreflight();
		std::cout << "PASS genuine RawTex binding: four fixture groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
