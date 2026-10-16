#include "sgre/script/layer_raw_texture_bindings.hpp"

#include "raw_texture_fixture.hpp"
#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/native_math.hpp"
#include "sgre/script/psb_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"

#include <array>
#include <bit>
#include <fstream>
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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected LayerRawTex failure: " + std::string(message));
}

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, const char* name, std::initializer_list<SquirrelValue> parameters = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), parameters);

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

struct Services {
	sgre::scene::tests::TaskFixtureBackend Backend;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptBinaryBindings Binary;
	sgre::script::ScriptRawImageBindings Images;
	sgre::script::ScriptMathBindings Math;
	sgre::script::ScriptLayerRawTextureBindings Raw;

	static sgre::script::SquirrelVmLimits VmLimits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	explicit Services(sgre::script::ScriptLayerRawTextureLimits limits = {}) : Vm(VmLimits()), Objects(Vm), Psb(Vm, Objects), Cache([](auto, const auto&) {
		                                                                           return sgre::render::SpriteTextureHandle{};
	                                                                           },
	                                                                                                                              [](auto) {
	                                                                                                                              }),
	                                                                           Catalog(Cache), Layers(Vm, Objects, Tree), Binary(Vm, Objects), Images(Vm, Objects, Binary), Math(Vm), Raw(Vm, Objects, Layers, Images, Psb, Math, Tree, Backend, limits) {
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

	SquirrelValue Create(const SquirrelValue& parent, SquirrelValue width = std::int32_t{2}, SquirrelValue height = std::int32_t{1}, const SquirrelValue& derived = {}) {
		return Vm.Invoke(std::holds_alternative<std::monostate>(derived) ? Raw.LayerRawTextureClass() : derived, std::array<SquirrelValue, 5>{Vm.RootTable(), parent, width, height, std::string("ignored tail")}).Value;
	}

	void Update(const SquirrelValue& parent) {
		Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Update();
	}
};

static std::vector<std::uint8_t> Shadow(sgre::scene::RawTextureTask& task) {
	const auto mapping = task.Texture().Map(true);
	std::vector<std::uint8_t> bytes;
	for (const auto byte : mapping.Bytes) {
		bytes.push_back(std::to_integer<std::uint8_t>(byte));
	}

	task.Texture().Unmap();

	return bytes;
}

static void TestRealClassAndCapturedMissingArguments() {
	Services services;
	const auto parent = services.Parent();
	const auto raw = services.Create(parent, 2.75f, true);
	Require(services.Vm.IsInstanceOf(raw, services.Objects.ObjectClass()) && !services.Vm.IsInstanceOf(raw, services.Layers.LayerBaseClass()) && !services.Vm.IsInstanceOf(raw, services.Layers.MotionLayerBaseClass()), "LayerRawTex must derive directly from Object, retaining no LayerBase/LayerView alias");
	Require(services.Layers.MotionTasks(parent).Group(sgre::scene::MotionTaskGroup::First).Count() == 1 && services.Backend.Created.OriginalFormat == 87, "actual First owns one exclusive BGRA CPU-shadow task");
	Require(Call(services.Vm, raw, "getWidth") == SquirrelValue{std::int32_t(2)} && Call(services.Vm, raw, "getHeight") == SquirrelValue{std::int32_t(1)}, "native integer conversion truncates float and accepts boolean dimensions");
	Require(Call(services.Vm, raw, "getVisible") == SquirrelValue{false} && services.Raw.Task(raw).State().Task.DrawPaused, "original concrete task starts Active and drawing paused");
	Require(std::holds_alternative<std::monostate>(Call(services.Vm, raw, "setVisible")), "captured native setter returns zero values");
	Require(Call(services.Vm, raw, "getVisible") == SquirrelValue{true} && !services.Raw.Task(raw).State().Task.DrawPaused, "missing boolean argument reads actual captured userdata as truthy");
	static_cast<void>(Call(services.Vm, raw, "setOpacity", {std::int32_t(-1)}));
	Require(Call(services.Vm, raw, "getOpacity") == SquirrelValue{std::int32_t(-1)}, "opacity keeps all native DWORD bits");
	static_cast<void>(Call(services.Vm, raw, "setOpacity"));
	Require(Call(services.Vm, raw, "getOpacity") == SquirrelValue{std::int32_t(0)}, "missing integer conversion leaves initialized zero on real userdata");

	const std::array<SquirrelValue, 2> seed_captures{19.75f, 3.5f};
	const auto seed = services.Vm.CreateNativeClosureWithCaptures("seed actual inactive coordinate", [](auto&, auto) {
		return sgre::script::SquirrelNativeResult::NoValues();
	},
	    seed_captures);
	static_cast<void>(services.Vm.Invoke(seed, std::array<SquirrelValue, 1>{std::int32_t(17)}));
	static_cast<void>(Call(services.Vm, raw, "setOffset"));
	const auto state = services.Raw.ScaleView(raw).State();
	Require(state.Offset.X == 0 && state.Offset.Y == 19.75f && state.HasCamera, "zero-user two-coordinate thunk reads inactive third allocated cell rather than invented zero");
	static_cast<void>(Call(services.Vm, raw, "setOffset", {2.5f}));
	Require(services.Raw.ScaleView(raw).State().Offset.X == 2.5f && services.Raw.ScaleView(raw).State().Offset.Y == 0, "one user argument leaves captured userdata at native third coordinate index");
	static_cast<void>(Call(services.Vm, raw, "setCoord", {3.5f, -2.5f}));
	static_cast<void>(Call(services.Vm, raw, "setLeft", {true}));
	Require(Call(services.Vm, raw, "getLeft") == SquirrelValue{1.0f} && Call(services.Vm, raw, "getTop") == SquirrelValue{-2.5f}, "coordinate access keeps each original task field independent from camera offset");
	services.Raw.ReleaseInstance(raw);
	Require(services.Backend.Live, "wrapper destruction marks actual task without disposing its texture");
	services.Update(parent);
	Require(!services.Backend.Live, "actual First update retires original camera child and texture");
}

static void TestOriginalSynchronousSourcesAndSharedRandom() {
	Services services;
	const auto parent = services.Parent();
	const auto raw = services.Create(parent);
	const auto image = services.Vm.Invoke(services.Images.RawImageClass(), std::array<SquirrelValue, 3>{services.Vm.RootTable(), std::int32_t(2), std::int32_t(1)}).Value;
	const auto binary = services.Binary.WrapBuffer(std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
	static_cast<void>(Call(services.Vm, image, "restore", {binary}));
	static_cast<void>(Call(services.Vm, raw, "restore", {image}));
	Require(Shadow(services.Raw.Task(raw)) == std::vector<std::uint8_t>{3, 2, 1, 4, 7, 6, 5, 8}, "matching RawImage restore copies its actual BGRA synchronously");
	const auto smaller = services.Vm.Invoke(services.Images.RawImageClass(), std::array<SquirrelValue, 3>{services.Vm.RootTable(), std::int32_t(1), std::int32_t(1)}).Value;
	static_cast<void>(Call(services.Vm, raw, "restore", {smaller}));
	Require(Shadow(services.Raw.Task(raw)) == std::vector<std::uint8_t>{3, 2, 1, 4, 7, 6, 5, 8}, "native mismatched RawImage restore returns zero without copying");

	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->SerializedSize = 6;
	document->Chunks.push_back({0, 6});
	sgre::resource::PsbValue pixels;
	pixels.SerializedTag = 25;
	pixels.Data = sgre::resource::PsbChunkReference{};
	document->Root.Data = sgre::resource::PsbObject{{"pixels", pixels}};
	const auto backing = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{10, 20, 30, 40, 50, 60});
	const auto psb_owner = services.Psb.WrapDocument(document, backing);
	const auto root = Call(services.Vm, psb_owner, "getRoot");
	const auto chunk = services.Vm.ReadSlot(root, std::string("pixels"));
	const auto references = services.Vm.RetainedReferenceCount(psb_owner);
	static_cast<void>(Call(services.Vm, raw, "restore", {chunk}));
	Require(Shadow(services.Raw.Task(raw)) == std::vector<std::uint8_t>{30, 20, 10, 255, 60, 50, 40, 255}, "genuine PSBValue char chunk restore performs original RGB-to-BGRA alpha-FF conversion");
	Require(services.Vm.RetainedReferenceCount(psb_owner) == references, "synchronous restoration must not add a persistent document root");
	RequireFailure([&] {
		static_cast<void>(Call(services.Vm, raw, "restore", {binary}));
	},
	    "invalid argument.");
	RequireFailure([&] {
		static_cast<void>(Call(services.Vm, raw, "restore", {image, true}));
	},
	    "invalid argument.");

	services.Math.Seed(123);
	std::uint32_t expected_seed = 123;
	std::vector<std::uint8_t> expected;
	for (std::uint32_t index = 0; index != 2; ++index) {
		expected_seed = expected_seed * 214013u + 2531011u;
		const auto sample = static_cast<std::uint8_t>(((expected_seed >> 16) & 32767) / 128);
		expected.insert(expected.end(), {sample, sample, sample, 255});
	}

	static_cast<void>(Call(services.Vm, raw, "generateNoise"));
	Require(Shadow(services.Raw.Task(raw)) == expected, "noise consumes exactly one original shared CRT RNG value per pixel");
	expected_seed = expected_seed * 214013u + 2531011u;
	Require(services.Math.Random() == static_cast<std::int32_t>((expected_seed >> 16) & 32767), "noise must advance the same RNG seen by scripts");
}

static void TestOnProcessSelectionStatusAndQueueOrdering() {
	Services services;
	const auto parent = services.Parent();
	const auto raw = services.Create(parent);
	services.Update(parent);
	Require(services.Vm.LastError() == SquirrelValue{std::string("the index doesn't exist")}, "missing onProcess must preserve original failed lookup error instead of resetting it");
	services.Raw.ReleaseInstance(raw);
	services.Update(parent);

	std::uint32_t calls = 0;
	const auto derived = services.Vm.CreateClass(services.Raw.LayerRawTextureClass());
	services.Vm.NewSlot(derived, std::string("onProcess"), services.Vm.CreateNativeClosure("observed process", [&](auto&, auto arguments) {
		Require(arguments.size() == 1 && services.Vm.IsInstanceOf(arguments[0], derived), "onProcess must use exactly the genuine script receiver");
		++calls;

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	const auto active = services.Create(parent, std::int32_t(1), std::int32_t(1), derived);
	services.Update(parent);
	Require(calls == 1, "paused drawing must not pause the original onProcess update");
	std::vector<std::uint32_t> events;
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [&](auto& task) {
		events.push_back(1);
		task.SetStatus(sgre::scene::TaskStatus::Retire);
	};
	static_cast<void>(services.Raw.Task(active).SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Update = [&](auto) {
		events.push_back(2);
	};
	static_cast<void>(services.Raw.Task(active).ParallelTasks().Append(std::move(parallel)));
	services.Update(parent);
	Require(calls == 1 && events == std::vector<std::uint32_t>{1, 2}, "nonempty serial queue skips own process before native serial/parallel updates");
	services.Update(parent);
	Require(calls == 2, "own process resumes on the next frame after serial retirement");
	services.Raw.ReleaseInstance(active);
	services.Update(parent);

	const auto failure_class = services.Vm.CreateClass(services.Raw.LayerRawTextureClass());
	services.Vm.NewSlot(failure_class, std::string("onProcess"), services.Vm.CreateNativeClosure("process failure", [](auto&, auto) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("observed process error");
	}));
	const auto failing = services.Create(parent, std::int32_t(1), std::int32_t(1), failure_class);
	services.Update(parent);
	Require(services.Vm.LastError() == SquirrelValue{std::string("observed process error")} && services.Backend.Live, "VM call status is ignored while native task remains owned and last error persists");
	services.Raw.ReleaseInstance(failing);
	services.Update(parent);

	const auto ignored_class = services.Vm.CreateClass(services.Raw.LayerRawTextureClass());
	const auto callable_class = services.Vm.CreateClass();
	services.Vm.NewSlot(callable_class, std::string("_call"), services.Vm.CreateNativeClosure("must not call instance", [&](auto&, auto) {
		++calls;

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	services.Vm.NewSlot(ignored_class, std::string("onProcess"), services.Vm.CreateInstance(callable_class));
	const auto ignored = services.Create(parent, std::int32_t(1), std::int32_t(1), ignored_class);
	services.Update(parent);
	Require(calls == 2, "native onProcess ignores callable instances rather than invoking _call");
}

static void TestParentFirstDetachedSafetyAndClose() {
	Services services;
	auto parent = services.Parent();
	const auto raw = services.Create(parent);
	static_cast<void>(Call(services.Vm, raw, "setScale", {2.0f, 3.0f}));
	static_cast<void>(Call(services.Vm, raw, "setVisible", {true}));
	services.Layers.ReleaseInstance(parent);
	Require(!services.Backend.Live && services.Raw.ScaleView(raw).State().Detached, "parent first releases actual camera task/texture before retained wrapper detachment");
	Require(Call(services.Vm, raw, "getWidth") == SquirrelValue{std::int32_t(0)} && Call(services.Vm, raw, "getVisible") == SquirrelValue{false} && Call(services.Vm, raw, "getLeft") == SquirrelValue{0.0f}, "native detached integer/bool/float getters return exact defaults");
	static_cast<void>(Call(services.Vm, raw, "setCoord", {7.0f, 8.0f}));
	static_cast<void>(Call(services.Vm, raw, "generateNoise"));
	RequireFailure([&] {
		static_cast<void>(Call(services.Vm, raw, "restore", {parent}));
	},
	    "detached");
	RequireFailure([&] {
		static_cast<void>(Call(services.Vm, raw, "drawBlend2D"));
	},
	    "detached");
	services.Raw.ReleaseInstance(raw);
	parent = services.Parent();
	const auto closing = services.Create(parent);
	static_cast<void>(Call(services.Vm, closing, "setRot", {90.0f}));
	services.Vm.Close();
	services.Vm.RethrowReleaseFailure();
	Require(!services.Backend.Live, "true-reclaimer Close keeps native adapters alive through actual parent/wrapper retirement");
}

static void TestPendingShadowBudgetAndReleasePreflight() {
	sgre::script::ScriptLayerRawTextureLimits limits;
	limits.MaxShadowBytes = 4;
	Services services(limits);
	const auto parent = services.Parent();
	const auto raw = services.Create(parent, std::int32_t(1), std::int32_t(1));
	services.Raw.ReleaseInstance(raw);
	RequireFailure([&] {
		static_cast<void>(services.Create(parent, std::int32_t(1), std::int32_t(1)));
	},
	    "aggregate shadow");
	services.Update(parent);
	const auto replacement = services.Create(parent, std::int32_t(1), std::int32_t(1));
	services.Objects.Subscribe(replacement, parent, [&](const auto&) {
		RequireFailure([&] {
			services.Raw.ReleaseInstance(replacement);
		},
		    "notification");
		Require(services.Raw.Task(replacement).Texture().State().ShadowBytes == 4 && services.Backend.Live, "failed active-notification release must preserve native payload and registration");

		return false;
	});
	services.Objects.Notify(replacement);
	services.Raw.ReleaseInstance(replacement);
	services.Update(parent);
}

static void TestWeakProcessResolutionAndErrorHandler() {
	Services services;
	const auto parent = services.Parent();
	std::uint32_t calls = 0;
	std::uint32_t errors = 0;
	const auto live_class = services.Vm.CreateClass(services.Raw.LayerRawTextureClass());
	auto method = services.Vm.CreateNativeClosure("weak process", [&](auto&, auto) {
		++calls;

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	services.Vm.NewSlot(live_class, std::string("onProcess"), services.Vm.CreateWeakReference(method));
	const auto raw = services.Create(parent, std::int32_t(1), std::int32_t(1), live_class);
	services.Update(parent);
	Require(calls == 1, "live weak method is resolved before exact closure invocation");
	method = {};
	services.Update(parent);
	Require(calls == 1, "expired weak method must not be invoked or resurrected");
	services.Raw.ReleaseInstance(raw);
	services.Update(parent);

	const auto handler = services.Vm.CreateNativeClosure("observed native handler", [&](auto&, auto arguments) {
		++errors;
		Require(arguments.size() == 2 && arguments[1] == SquirrelValue{std::int32_t(77)}, "script onProcess raise flag must deliver original raw error payload to its base handler");

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	static_cast<void>(services.Vm.Invoke(services.Vm.GetSlot(services.Vm.RootTable(), std::string("seterrorhandler")), std::array<SquirrelValue, 2>{services.Vm.RootTable(), handler}));
	const auto failure_class = services.Vm.CreateClass(services.Raw.LayerRawTextureClass());
	services.Vm.NewSlot(failure_class, std::string("onProcess"), services.Vm.CreateNativeClosure("raised process", [](auto&, auto) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("raised process error");
	}));
	const auto failing = services.Create(parent, std::int32_t(1), std::int32_t(1), failure_class);
	services.Update(parent);
	Require(errors == 0 && services.Vm.LastError() == SquirrelValue{std::string("raised process error")}, "direct native failure retains LastError without invoking the script Execute handler");
	services.Raw.ReleaseInstance(failing);
	services.Update(parent);

	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = 2;
	program.MainFunction.SourceName = std::string("independent-onProcess-error");
	program.MainFunction.Name = std::string("onProcess");
	sgre::script::SquirrelInstruction load;
	load.Opcode = 2;
	load.Argument0 = 1;
	load.Argument1 = 77;
	program.MainFunction.Instructions.push_back(load);
	sgre::script::SquirrelInstruction raise;
	raise.Opcode = 58;
	raise.Argument0 = 1;
	program.MainFunction.Instructions.push_back(raise);
	const auto script_class = services.Vm.CreateClass(services.Raw.LayerRawTextureClass());
	services.Vm.NewSlot(script_class, std::string("onProcess"), services.Vm.CreateClosure(std::move(program)));
	const auto script = services.Create(parent, std::int32_t(1), std::int32_t(1), script_class);
	services.Update(parent);
	Require(errors == 1 && services.Vm.LastError() == SquirrelValue{std::int32_t(77)}, "genuine script Execute receives raise1 even though the original wrapper ignores failed call status");
	services.Raw.ReleaseInstance(script);
	services.Update(parent);
}

static void TestNoResultProcessOutputLifetime() {
	for (const bool suspend : {false, true}) {
		std::vector<std::uint32_t> events;
		Services services;
		const auto parent = services.Parent();
		SquirrelValue weak_output;
		const auto derived = services.Vm.CreateClass(services.Raw.LayerRawTextureClass());
		services.Vm.NewSlot(derived, std::string("onProcess"), services.Vm.CreateNativeClosure("process unused owned result", [&](auto& vm, auto) {
			const auto output = vm.CreateUserData(std::array<std::uint8_t, 1>{17});
			weak_output = vm.CreateWeakReference(output);
			vm.SetUserDataReleaseHook(output, [&](auto&, const auto&, auto) {
				events.push_back(1);
			});
			if (suspend) {
				vm.PushNativeValue(output);

				return sgre::script::SquirrelNativeResult::StackStatus(-666);
			}

			return sgre::script::SquirrelNativeResult::ReturnValue(output);
		}));
		const auto raw = services.Create(parent, std::int32_t(1), std::int32_t(1), derived);
		sgre::scene::ParallelTaskCallbacks child;
		child.Update = [&](auto) {
			Require(events == std::vector<std::uint32_t>{1}, "native retval0 drops unused output before embedded parallel update");
			events.push_back(2);
		};
		static_cast<void>(services.Raw.Task(raw).ParallelTasks().Append(std::move(child)));
		services.Update(parent);
		Require(events == std::vector<std::uint32_t>{1, 2} && services.Vm.Type(services.Vm.ResolveWeakReference(weak_output)) == sgre::script::SquirrelValueType::Null, "native process status does not create a facade or hidden strong output root");
		Require(!services.Vm.IsSuspended(), "direct native sq_call does not install Script Execute suspension from the raw-666 local flag");
		services.Vm.Close();
		services.Vm.RethrowReleaseFailure();
	}
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	input.read(static_cast<char*>(static_cast<void*>(bytes.data())), bytes.size());
	if (!input) {
		throw std::runtime_error("LayerRawTex native projection is truncated");
	}

	return bytes[0] | (std::uint32_t{bytes[1]} << 8) | (std::uint32_t{bytes[2]} << 16) | (std::uint32_t{bytes[3]} << 24);
}

static void TestNativeNoiseProjection(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(ReadWord(input) == 1000, "native noise corpus must contain exactly1000 bounded cases");
	Services services;
	const auto parent = services.Parent();
	for (std::uint32_t case_id = 0; case_id != 1000; ++case_id) {
		const auto width = ReadWord(input);
		const auto height = ReadWord(input);
		const auto seed = ReadWord(input);
		const auto final_seed = ReadWord(input);
		Require(width > 0 && width <= 16 && height > 0 && height <= 16, "native noise prepared extent is invalid");
		const auto raw = services.Create(parent, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
		services.Math.Seed(seed);
		static_cast<void>(Call(services.Vm, raw, "generateNoise"));
		const auto pixels = Shadow(services.Raw.Task(raw));
		for (const auto pixel : pixels) {
			Require(pixel == ReadWord(input), "native noise BGRA byte differs");
		}

		const auto next_seed = final_seed * 214013u + 2531011u;
		Require(services.Math.Random() == static_cast<std::int32_t>((next_seed >> 16) & 32767), "native noise shared CRT seed progression differs");
		services.Raw.ReleaseInstance(raw);
		services.Update(parent);
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native noise projection has unchecked trailing bytes");
}

static void TestNativeArgumentProjection(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(ReadWord(input) == 32, "native argument corpus must retain all32 real thunk cases");
	for (std::uint32_t case_id = 0; case_id != 32; ++case_id) {
		const auto method = ReadWord(input);
		const auto count = ReadWord(input);
		Require(method < 4 && count <= 2, "native argument method/count is invalid");
		Services services;
		const auto parent = services.Parent();
		const auto raw = services.Create(parent);
		static_cast<void>(Call(services.Vm, raw, "setOffset", {0.0f, 1.0f}));
		static_cast<void>(Call(services.Vm, raw, "setOffset", {0.0f, 0.0f}));
		std::vector<SquirrelValue> arguments{raw};
		for (std::uint32_t index = 0; index != count; ++index) {
			const auto kind = ReadWord(input);
			const auto bits = ReadWord(input);
			Require(kind < 3, "native argument kind is invalid");
			if (kind == 0) {
				arguments.push_back(std::bit_cast<float>(bits));
			} else if (kind == 1) {
				arguments.push_back(std::bit_cast<std::int32_t>(bits));
			} else {
				arguments.push_back(bits != 0);
			}
		}

		const auto seeded = ReadWord(input) != 0;
		const auto seed_word = ReadWord(input);
		const std::array<SquirrelValue, 2> captures{seeded ? SquirrelValue{std::bit_cast<float>(seed_word)} : SquirrelValue{}, 3.5f};
		const auto seed = services.Vm.CreateNativeClosureWithCaptures("seed original argument projection", [](auto&, auto) {
			return sgre::script::SquirrelNativeResult::NoValues();
		},
		    captures);
		static_cast<void>(services.Vm.Invoke(seed, std::array<SquirrelValue, 1>{std::int32_t(17)}));
		static const std::array<const char*, 4> names{"setOffset", "setRot", "setOpacity", "setVisible"};
		const auto result = services.Vm.Invoke(services.Vm.GetSlot(raw, std::string(names[method])), arguments);
		Require(std::holds_alternative<std::monostate>(result.Value), "original native setter status0 must return no value");
		std::array<std::uint32_t, 7> expected{};
		for (auto& word : expected) {
			word = ReadWord(input);
		}

		if (method == 0) {
			const auto wrapper = services.Raw.ScaleView(raw).State();
			const auto camera = services.Raw.ScaleView(raw).CameraState();
			Require(std::bit_cast<std::uint32_t>(wrapper.Offset.X) == expected[0] && std::bit_cast<std::uint32_t>(wrapper.Offset.Y) == expected[1] && std::bit_cast<std::uint32_t>(camera.Offset.X) == expected[2] && std::bit_cast<std::uint32_t>(camera.Offset.Y) == expected[3], "original fixed-index coordinate thunk differs");
		} else if (method == 1) {
			Require(std::bit_cast<std::uint32_t>(services.Raw.ScaleView(raw).CameraState().RotationRadians) == expected[4], "original fixed-index rotation thunk differs");
		} else if (method == 2) {
			Require(services.Raw.Task(raw).State().Opacity == expected[5], "original fixed-index integer thunk differs");
		} else {
			Require(services.Raw.ScaleView(raw).TaskState().DrawPaused == (expected[6] != 0), "original fixed-index truth thunk differs");
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native argument projection has unchecked trailing bytes");
}

int main(int argc, char** argv) {
	try {
		TestRealClassAndCapturedMissingArguments();
		TestOriginalSynchronousSourcesAndSharedRandom();
		TestOnProcessSelectionStatusAndQueueOrdering();
		TestParentFirstDetachedSafetyAndClose();
		TestPendingShadowBudgetAndReleasePreflight();
		TestWeakProcessResolutionAndErrorHandler();
		TestNoResultProcessOutputLifetime();
		if (argc == 3) {
			TestNativeNoiseProjection(argv[1]);
			TestNativeArgumentProjection(argv[2]);
		}
		std::cout << "Genuine LayerRawTex class, capture, lifetime and process fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
