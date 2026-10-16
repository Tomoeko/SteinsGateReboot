#include "sgre/script/affine_layer_bindings.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
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
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected AffineLayer failure: " + std::string(message));
}

struct Services {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::LayerTree Tree;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptAffineMatrixBindings Matrices;
	sgre::script::ScriptAffineLayerBindings Affine;
	std::vector<sgre::script::SquirrelHostRoot> InstanceRoots;

	static sgre::script::SquirrelVmLimits Limits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	static sgre::script::ScriptLayerBindings& Configure(sgre::script::ScriptLayerBindings& layers, sgre::script::ScriptPsbBindings& psb, sgre::scene::MotionAssetCatalog& catalog, sgre::scene::TextureCache& cache) {
		layers.ConfigureMotionOwners(psb, catalog, cache);

		return layers;
	}

	Services()
	    : Vm(Limits()), Objects(Vm), Psb(Vm, Objects), Tree(32), Cache([](auto, const auto&) {
		      throw std::runtime_error("this affine fixture has no texture upload");

		      return sgre::render::SpriteTextureHandle{};
	      },
	                                                                 [](auto) {
	                                                                 }),
	      Catalog(Cache), Layers(Vm, Objects, Tree), Matrices(Vm), Affine(Vm, Objects, Configure(Layers, Psb, Catalog, Cache), Matrices, 16) {
		Vm.InstallRecoveredBaseLibrary();
	}

	~Services() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	SquirrelValue Create(const SquirrelValue& class_value = {}) {
		const auto instance = Vm.Invoke(std::holds_alternative<std::monostate>(class_value) ? Affine.AffineLayerClass() : class_value, std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
		InstanceRoots.push_back(Vm.Retain(instance));

		return instance;
	}

	SquirrelValue Call(const SquirrelValue& instance, std::string_view name, std::span<const SquirrelValue> parameters = {}) {
		std::vector<SquirrelValue> arguments{instance};
		arguments.insert(arguments.end(), parameters.begin(), parameters.end());

		return Vm.Invoke(Vm.GetSlot(instance, std::string(name)), arguments).Value;
	}

	void Set(const SquirrelValue& instance, std::string_view name, const SquirrelValue& value) {
		static_cast<void>(Call(instance, name, std::array<SquirrelValue, 1>{value}));
	}
};

static void CheckConstructionAndControls() {
	Services services;
	const auto instance = services.Create();
	Require(services.Vm.IsInstanceOf(instance, services.Layers.LayerClass()) && services.Objects.HasInstance(instance), "genuine AffineLayer derives the configured actual Layer and attaches Object once");
	Require(!services.Affine.TaskState(instance).MatrixInvalidated && services.Affine.PriorityOrderDirty(), "constructor does not invent initial matrix invalidation; membership marks priority order dirty");
	RequireFailure([&] {
		services.Affine.Process(instance, [] {
		});
	},
	    "observed invalidation");
	services.Set(instance, "setLeft", 42.0f);
	services.Set(instance, "setTop", -7.0f);
	services.Set(instance, "setDepth", 9.0f);
	Require(services.Call(instance, "getXpos") == SquirrelValue{42.0f} && services.Call(instance, "getYpos") == SquirrelValue{-7.0f} && services.Call(instance, "getZpos") == SquirrelValue{9.0f}, "aliases address the original scalar fields");
	services.Set(instance, "setZoom", 125.0f);
	Require(services.Call(instance, "getZoomx") == SquirrelValue{125.0f} && services.Call(instance, "getZoomy") == SquirrelValue{125.0f}, "zoom API remains percent while native state stores factors");
	static_cast<void>(services.Call(instance, "setFlipx"));
	Require(services.Call(instance, "getFlipx") == SquirrelValue{true}, "omitted boolean argument reads the actual captured userdata cell and Squirrel truth");
	services.Set(instance, "setFlipy", 256);
	Require(services.Call(instance, "getFlipy") == SquirrelValue{true}, "flip setter uses Squirrel truth rather than the hit getter's low-byte conversion");
	services.Set(instance, "setCenterx", 13.0f);
	services.Set(instance, "setRcenterx", 37.0f);
	services.Set(instance, "setRcenterx", {});
	Require(services.Call(instance, "getRcenterx") == SquirrelValue{13.0f}, "explicit null clears the optional pivot and restores the center fallback");
	static_cast<void>(services.Call(instance, "setRcenterx"));
	Require(services.Call(instance, "getRcenterx") == SquirrelValue{0.0f}, "omitted pivot sees nonnull captured userdata and enables a zero converted pivot");
	static_cast<void>(services.Call(instance, "setXpos"));
	Require(services.Call(instance, "getXpos") == SquirrelValue{0.0f}, "omitted float consumes captured userdata with native zero conversion");
	RequireFailure([&] {
		static_cast<void>(services.Vm.Invoke(services.Affine.AffineLayerClass(), std::array<SquirrelValue, 3>{services.Vm.RootTable(), {}, 1}));
	},
	    "invalid argument");
	RequireFailure([&] {
		services.Set(instance, "calcMatrix", {});
	},
	    "bad instance");
	services.Affine.ReleaseInstance(instance);
	Require(!services.Objects.HasInstance(instance), "derived release reaches common Object cleanup");
	RequireFailure([&] {
		static_cast<void>(services.Affine.Parameters(instance));
	},
	    "bad instance");
}

static void CheckLinksAndAssignment() {
	Services services;
	const auto parent = services.Create();
	const auto child = services.Create();
	const auto copy = services.Create();
	services.Set(parent, "setZpos", 10.0f);
	services.Set(parent, "setOffsetz", 2.0f);
	services.Set(parent, "setOriginz", 3.0f);
	services.Set(child, "setZpos", 4.0f);
	services.Set(child, "setLink", parent);
	Require(services.Call(child, "getLink") == parent && services.Call(child, "getCalczpos") == SquirrelValue{14.0f}, "weak parent resolves through getLink and recursive native z calculation");
	services.Set(copy, "assign", child);
	Require(services.Call(copy, "getZpos") == SquirrelValue{4.0f} && std::holds_alternative<std::monostate>(services.Call(copy, "getLink")), "assign copies scalar state without copying links");
	services.Set(copy, "assignLink", child);
	Require(services.Call(copy, "getLink") == parent, "assignLink uses separate link ownership");
	const auto weak = services.Vm.CreateWeakReference(child);
	RequireFailure([&] {
		services.Set(copy, "assign", weak);
	},
	    "live native source");
	services.Affine.ReleaseInstance(parent);
	Require(std::holds_alternative<std::monostate>(services.Call(child, "getLink")) && std::holds_alternative<std::monostate>(services.Call(copy, "getLink")), "parent release detaches both actual child links before its roots retire");
	services.Affine.ReleaseInstance(child);
	services.Affine.ReleaseInstance(copy);
}

static void CheckMatrixCallbacksAndErrors() {
	Services services;
	const auto derived = services.Vm.CreateClass(services.Affine.AffineLayerClass());
	std::vector<std::string> events;
	SquirrelValue saved_matrix;
	services.Vm.NewSlot(derived, std::string("calcMatrix"), services.Vm.CreateNativeClosure("native affine calc fixture", [&](auto& vm, const auto arguments) {
		Require(arguments.size() == 2 && vm.CurrentContext() == vm.RootContext(), "matrix helper invokes genuine receiver/wrapper in root VM");
		Require(vm.RetainedReferenceCount(arguments[1]) >= 4, "matrix root plus three scoped native ObjectInfo copies survive the call");
		saved_matrix = arguments[1];
		events.emplace_back("local");
		sgre::scene::TranslateAffineMatrix(services.Matrices.Matrix(arguments[1]), 5, 7);
		static_cast<void>(vm.CollectGarbage());
		Require(services.Objects.HasInstance(arguments[0]), "strong self survives collection inside matrix callback");

		return sgre::script::SquirrelNativeResult::ReturnValue(99);
	}));
	services.Vm.NewSlot(derived, std::string("calcImageMatrix"), services.Vm.CreateNativeClosure("native affine image fixture", [&](auto&, const auto arguments) {
		events.emplace_back("image");
		sgre::scene::ScaleAffineMatrix(services.Matrices.Matrix(arguments[1]), 2, 3);

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	services.Vm.NewSlot(derived, std::string("onScale"), services.Vm.CreateNativeClosure("native affine scale fixture", [&](auto& vm, const auto arguments) -> sgre::script::SquirrelNativeResult {
		Require(arguments.size() == 3 && arguments[1] == SquirrelValue{2.0f} && arguments[2] == SquirrelValue{3.0f}, "onScale sees prepared signed native camera scales");
		events.emplace_back("scale");
		vm.SetLastError(71);
		throw sgre::script::SquirrelVmException(71, "native scale failure fixture");
	}));
	const auto instance = services.Create(derived);
	static_cast<void>(services.Call(instance, "updateAffine"));
	services.Affine.Process(instance, [&] {
		events.emplace_back("common");
		Require(services.Affine.TaskState(instance).Camera.Offset.X == -5 && services.Affine.TaskState(instance).Camera.Offset.Y == -7, "camera stores native negative projected translation");
		RequireFailure([&] {
			services.Affine.ReleaseInstance(instance);
		},
		    "active");
	});
	Require(events == std::vector<std::string>{"local", "image", "scale", "common"} && services.Vm.LastErrorInContext(services.Vm.RootContext()) == SquirrelValue{71}, "ignored callback failure preserves root diagnostic and common task execution");
	events.clear();
	services.Affine.Process(instance, [&] {
		events.emplace_back("common");
	});
	Require(events == std::vector<std::string>{"local", "image", "common"} && services.Affine.TaskState(instance).MatrixInvalidated, "native dirty byte remains set while unchanged scale suppresses repeated onScale");
	const auto weak_matrix = services.Vm.CreateWeakReference(saved_matrix);
	saved_matrix = {};
	services.Affine.ReleaseInstance(instance);
	Require(std::holds_alternative<std::monostate>(services.Vm.ResolveWeakReference(weak_matrix)), "owned matrix wrappers retire after the callback copies leave");
}

static void CheckHitConversionAndInverse() {
	Services services;
	const auto derived = services.Vm.CreateClass(services.Affine.AffineLayerClass());
	std::int32_t hit_result = 256;
	services.Vm.NewSlot(derived, std::string("containsImage"), services.Vm.CreateNativeClosure("affine contains fixture", [&](auto&, const auto arguments) {
		Require(arguments.size() == 3 && arguments[1] == SquirrelValue{3.0f} && arguments[2] == SquirrelValue{4.0f}, "contains callback consumes stored native inverse coordinates");

		return sgre::script::SquirrelNativeResult::ReturnValue(hit_result);
	}));
	services.Vm.NewSlot(derived, std::string("containsShapeImage"), services.Vm.CreateNativeClosure("affine shape fixture", [](auto&, const auto arguments) {
		Require(arguments.size() == 4 && *sgre::script::TryStringText(arguments[1]) == "shape" && arguments[2] == SquirrelValue{3.0f} && arguments[3] == SquirrelValue{4.0f}, "shape string and transformed point preserve original callback order");

		return sgre::script::SquirrelNativeResult::ReturnValue(true);
	}));
	const auto instance = services.Create(derived);
	RequireFailure([&] {
		static_cast<void>(services.Call(instance, "contains", std::array<SquirrelValue, 2>{3.0f, 4.0f}));
	},
	    "observed matrix task");
	static_cast<void>(services.Call(instance, "updateAffine"));
	services.Affine.Process(instance, [] {
	});
	Require(services.Call(instance, "contains", std::array<SquirrelValue, 2>{3.0f, 4.0f}) == SquirrelValue{false}, "numeric 256 becomes native low-byte false");
	hit_result = 257;
	Require(services.Call(instance, "contains", std::array<SquirrelValue, 2>{3.0f, 4.0f}) == SquirrelValue{true}, "numeric 257 becomes native low-byte true");
	Require(services.Call(instance, "containsShape", std::array<SquirrelValue, 3>{std::string("shape"), 3.0f, 4.0f}) == SquirrelValue{true}, "actual string shape uses its native helper");
	RequireFailure([&] {
		static_cast<void>(services.Call(instance, "containsShape", std::array<SquirrelValue, 3>{17, 3.0f, 4.0f}));
	},
	    "generated-string");
	services.Affine.ReleaseInstance(instance);
}

static void CheckGlobalCallbackFromChild() {
	Services services;
	const auto derived = services.Vm.CreateClass(services.Affine.AffineLayerClass());
	const auto root = services.Vm.RootContext();
	services.Vm.NewSlot(derived, std::string("containsImage"), services.Vm.CreateNativeClosure("native global hit fixture", [&](auto& vm, const auto arguments) {
		Require(vm.CurrentContext() == root && arguments.size() == 3, "original contains helper selects the global root VM for its callback");
		vm.SetLastError(47);

		return sgre::script::SquirrelNativeResult::ReturnValue(true);
	}));
	const auto instance = services.Create(derived);
	static_cast<void>(services.Call(instance, "updateAffine"));
	services.Affine.Process(instance, [] {
	});
	const auto child = services.Vm.CreateContext();
	const auto child_root = services.Vm.CreateTable();
	services.Vm.SetRootTableInContext(child, child_root);
	services.Vm.SetLastErrorInContext(child, 93);
	const auto caller = services.Vm.CreateNativeClosure("native child hit caller", [&](auto& vm, const auto arguments) {
		Require(vm.CurrentContext() == child && vm.RootTable() == child_root && arguments.size() == 1, "native child caller enters its own root and receiver frame");
		const auto receiver = vm.NativeStackValue(1);
		const auto result = services.Call(instance, "contains", std::array<SquirrelValue, 2>{3.0f, 4.0f});
		Require(result == SquirrelValue{true} && vm.CurrentContext() == child && vm.RootTable() == child_root && vm.NativeStackSize() == 1 && vm.NativeStackValue(1) == receiver, "root hit helper returns to the unchanged native child frame");
		Require(vm.LastError() == SquirrelValue{93} && vm.LastErrorInContext(root) == SquirrelValue{47}, "native root callback diagnostic does not overwrite its child caller's error");

		return sgre::script::SquirrelNativeResult::ReturnValue(result);
	});
	const auto result = services.Vm.InvokeInContext(child, caller, std::array<SquirrelValue, 1>{child_root});
	Require(result.Value == SquirrelValue{true} && services.Vm.CurrentContext() == root, "global helper restores the outside root context after its child invocation");
	services.Vm.ReleaseContext(child);
	services.Affine.ReleaseInstance(instance);
}

static void CheckDrawDirtyAndSelfRoot() {
	Services services;
	const auto derived = services.Vm.CreateClass(services.Affine.AffineLayerClass());
	std::uint32_t draw_calls = 0;
	services.Vm.NewSlot(derived, std::string("onDraw"), services.Vm.CreateNativeClosure("native draw dirty fixture", [&](auto& vm, const auto arguments) -> sgre::script::SquirrelNativeResult {
		Require(arguments.size() == 1, "native onDraw receives only its receiver");
		++draw_calls;
		services.Set(arguments[0], "setXpos", 17.0f);
		vm.SetLastError(83);
		throw sgre::script::SquirrelVmException(83, "native draw failure fixture");
	}));
	const auto instance = services.Create(derived);
	services.Set(instance, "setXpos", 5.0f);
	const auto references = services.Vm.RetainedReferenceCount(instance);
	services.Affine.Process(instance, [&] {
		Require(services.Vm.RetainedReferenceCount(instance) == references + 1, "native Process holds one additional strong self through its common phase");
		services.InstanceRoots.clear();
		static_cast<void>(services.Vm.CollectGarbage());
		Require(services.Objects.HasInstance(instance), "common-phase collection preserves the strong Process receiver");
		services.InstanceRoots.push_back(services.Vm.Retain(instance));
	});
	std::uint32_t common_calls = 0;
	services.Affine.Draw(instance, 0xff112233, [&](std::uint32_t argument) {
		++common_calls;
		Require(argument == 0xff112233 && !services.Affine.TaskState(instance).MatrixInvalidated, "full draw argument survives and the dirty byte clears before common drawing");
		Require(services.Vm.RetainedReferenceCount(instance) == references, "native Draw adds no Process-style self ObjectInfo root");
		RequireFailure([&] {
			services.Affine.ReleaseInstance(instance);
		},
		    "active");
	});
	Require(draw_calls == 1 && common_calls == 1 && services.Vm.LastError() == SquirrelValue{83}, "draw callback failure still reaches common drawing and preserves its diagnostic");
	Require(services.Affine.Parameters(instance).PositionX == 17 && services.Affine.TaskState(instance).Projected.TranslationX == 5, "onDraw invalidation is cleared without an invented matrix recomputation");
	services.Affine.Process(instance, [] {
	});
	Require(services.Affine.TaskState(instance).Projected.TranslationX == 5, "clean update retains prior transform after drawing");
	services.Affine.Draw(instance, 7, [&](std::uint32_t argument) {
		Require(argument == 7, "clean draw passes its original argument");
		++common_calls;
	});
	Require(draw_calls == 1 && common_calls == 2, "clean draw suppresses repeated onDraw while keeping common traversal");
	services.Set(instance, "setXpos", 21.0f);
	services.Affine.Process(instance, [] {
	});
	Require(services.Affine.TaskState(instance).Projected.TranslationX == 21, "later external invalidation computes a fresh matrix");
	services.Affine.ReleaseInstance(instance);
}

static void CheckBorrowedCleanupAndClose() {
	Services services;
	const auto instance = services.Create();
	static_cast<void>(services.Call(instance, "updateAffine"));
	services.Affine.Process(instance, [] {
	});
	std::uint32_t releases = 0;
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&](auto) {
		RequireFailure([&] {
			services.Affine.ReleaseInstance(instance);
		},
		    "task groups is active");
		Require(services.Affine.TaskState(instance).TransformPrepared, "failed preflight leaves all derived state live");
	};
	callbacks.Release = [&](auto) {
		++releases;
	};
	static_cast<void>(services.Layers.MotionTasks(instance).Group(sgre::scene::MotionTaskGroup::First).Append(std::move(callbacks)));
	services.Layers.MotionTasks(instance).Group(sgre::scene::MotionTaskGroup::First).Update();
	Require(releases == 0, "active task cannot destroy borrowed derived state");
	services.Vm.Close();
	services.Vm.RethrowReleaseFailure();
	Require(releases == 1 && services.Tree.ChildCount() == 0, "VM Close invokes genuine release bridge and all common tasks exactly once");
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<std::uint8_t, 4> bytes{};
	input.read(reinterpret_cast<char*>(bytes.data()), 4);
	Require(input.good(), "truncated affine process vector");

	return std::uint32_t{bytes[0]} | std::uint32_t{bytes[1]} << 8 | std::uint32_t{bytes[2]} << 16 | std::uint32_t{bytes[3]} << 24;
}

static void CheckNativeProcess(std::string_view plan_path, std::string_view expected_path) {
	std::ifstream plan(std::string(plan_path), std::ios::binary);
	std::ifstream expected(std::string(expected_path), std::ios::binary);
	Require(ReadWord(plan) == 0x504d4753, "affine process vector magic");
	const auto count = ReadWord(plan);
	Require(count > 0 && count <= 1000, "bounded affine process cases");
	static constexpr std::array<const char*, 22> scalar_names{"Xpos", "Ypos", "Zpos", "Rotate", "Zoomx", "Zoomy", "Slantx", "Slanty", "Offsetx", "Offsety", "Offsetz", "Centerx", "Centery", "Originx", "Originy", "Originz", "AbsoluteBase", "BaseScale", "Imagex", "Imagey", "Imagerotate", "Imagezoom"};
	static constexpr std::array<const char*, 4> pivot_names{"Zcenterx", "Zcentery", "Rcenterx", "Rcentery"};
	std::uint32_t words = 0;
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		Services services;
		const auto derived = services.Vm.CreateClass(services.Affine.AffineLayerClass());
		std::uint32_t scale_calls = 0;
		services.Vm.NewSlot(derived, std::string("onScale"), services.Vm.CreateNativeClosure("original process scale observer", [&](auto&, const auto) {
			++scale_calls;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		const auto instance = services.Create(derived);
		const auto parent = services.Create();
		for (const auto* name : scalar_names) {
			services.Set(instance, std::string("set") + name, std::bit_cast<float>(ReadWord(plan)));
		}
		services.Set(instance, "setFlipx", ReadWord(plan) != 0);
		services.Set(instance, "setFlipy", ReadWord(plan) != 0);
		for (const auto* name : pivot_names) {
			const auto value = std::bit_cast<float>(ReadWord(plan));
			const auto enabled = ReadWord(plan) != 0;
			services.Set(instance, std::string("set") + name, enabled ? SquirrelValue{value} : SquirrelValue{});
		}
		const auto linked = ReadWord(plan) != 0;
		services.Set(parent, "setXpos", std::bit_cast<float>(ReadWord(plan)));
		services.Set(parent, "setYpos", std::bit_cast<float>(ReadWord(plan)));
		if (linked) {
			services.Set(instance, "setLink", parent);
		}
		for (std::uint32_t frame = 0; frame < 2; ++frame) {
			services.Affine.Process(instance, [] {
			});
			const auto state = services.Affine.TaskState(instance);
			std::array<float, 17> values{state.Projected.XX, state.Projected.YX, state.Projected.XY, state.Projected.YY, state.Projected.TranslationX, state.Projected.TranslationY, state.Inverse.XX, state.Inverse.YX, state.Inverse.XY, state.Inverse.YY, state.Inverse.TranslationX, state.Inverse.TranslationY, state.Camera.Offset.X, state.Camera.Offset.Y, state.Camera.Scale.X, state.Camera.Scale.Y, state.Camera.RotationRadians};
			for (const auto value : values) {
				const auto native = ReadWord(expected);
				if (std::bit_cast<std::uint32_t>(value) != native) {
					throw std::runtime_error("affine process word mismatch at case=" + std::to_string(case_index) + " frame=" + std::to_string(frame) + " word=" + std::to_string(words));
				}
				++words;
			}
			for (const auto slot : {sgre::script::AffineLayerMatrixSlot::Composite, sgre::script::AffineLayerMatrixSlot::Local, sgre::script::AffineLayerMatrixSlot::Image}) {
				for (const auto value : services.Affine.MatrixValue(instance, slot).Rows) {
					Require(std::bit_cast<std::uint32_t>(value) == ReadWord(expected), "native default matrix word mismatch");
					++words;
				}
			}
			Require(ReadWord(expected) == static_cast<std::uint32_t>(state.MatrixInvalidated) && ReadWord(expected) == scale_calls, "native persistent dirty/onScale gate mismatch");
			words += 2;
		}
		services.Affine.ReleaseInstance(instance);
		services.Affine.ReleaseInstance(parent);
	}
	Require(plan.peek() == std::char_traits<char>::eof() && expected.peek() == std::char_traits<char>::eof(), "affine process vector has trailing bytes");
	std::cout << "Matched " << count << " native affine owner cases and " << words << " selected words\n";
}

static sgre::script::SquirrelInstruction DrawInstruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument1;
	instruction.Argument2 = argument2;
	instruction.Argument3 = argument3;

	return instruction;
}

static SquirrelValue CreateDrawScript(sgre::script::SquirrelVm& vm) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-affine-draw-fixture");
	program.MainFunction.Name = std::string("draw_fixture");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Literals = {std::string("drawProbe")};
	program.MainFunction.Instructions = {DrawInstruction(9, 2, 0, 0), DrawInstruction(10, 3, 0), DrawInstruction(6, 255, 2, 3, 1), DrawInstruction(19, 255)};

	return vm.CreateClosure(std::move(program));
}

static void CheckNativeDraw(std::string_view plan_path, std::string_view expected_path) {
	std::ifstream plan(std::string(plan_path), std::ios::binary);
	std::ifstream expected(std::string(expected_path), std::ios::binary);
	Require(ReadWord(plan) == 0x44414753, "affine draw vector magic");
	const auto count = ReadWord(plan);
	Require(count > 0 && count <= 1000, "bounded affine draw cases");
	std::uint32_t words = 0;
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		const auto dirty = ReadWord(plan);
		const auto mode = ReadWord(plan);
		const auto argument = ReadWord(plan);
		Require(dirty <= 1 && mode < 6, "affine draw input domain");
		Services services;
		const auto derived = services.Vm.CreateClass(services.Affine.AffineLayerClass());
		std::uint32_t callback_calls = 0;
		std::uint32_t common_calls = 0;
		const auto callback = services.Vm.CreateNativeClosure("affine draw differential callback", [&](auto& vm, const auto arguments) -> sgre::script::SquirrelNativeResult {
			Require(arguments.size() == 1 && vm.CurrentContext() == vm.RootContext(), "draw callback receives only its actual receiver on the root VM");
			++callback_calls;
			static_cast<void>(services.Call(arguments[0], "updateAffine"));
			vm.SetLastError(73);
			throw sgre::script::SquirrelVmException(73, "affine draw differential failure");
		});
		auto callback_owner = services.Vm.Retain(callback);
		if (mode == 1) {
			services.Vm.NewSlot(derived, std::string("onDraw"), 17);
		} else if (mode == 2) {
			services.Vm.NewSlot(derived, std::string("drawProbe"), callback);
			services.Vm.NewSlot(derived, std::string("onDraw"), CreateDrawScript(services.Vm));
		} else if (mode == 3) {
			services.Vm.NewSlot(derived, std::string("onDraw"), callback);
		} else if (mode == 4) {
			services.Vm.NewSlot(derived, std::string("onDraw"), services.Vm.CreateWeakReference(callback));
		} else if (mode == 5) {
			const auto expired = services.Vm.CreateTable();
			auto expired_owner = services.Vm.Retain(expired);
			services.Vm.NewSlot(derived, std::string("onDraw"), services.Vm.CreateWeakReference(expired));
			expired_owner = {};
		}
		const auto instance = services.Create(derived);
		if (dirty != 0) {
			static_cast<void>(services.Call(instance, "updateAffine"));
		}
		for (std::uint32_t draw = 0; draw < 2; ++draw) {
			services.Affine.Draw(instance, argument, [&](const auto actual_argument) {
				Require(actual_argument == argument && !services.Affine.TaskState(instance).MatrixInvalidated, "common draw receives the full original DWORD after dirty clear");
				++common_calls;
			});
			const auto expected_dirty = ReadWord(expected);
			const auto expected_callbacks = ReadWord(expected);
			Require(expected_dirty == static_cast<std::uint32_t>(services.Affine.TaskState(instance).MatrixInvalidated) && expected_callbacks == callback_calls, "native dirty/type/weak callback gate mismatch at case=" + std::to_string(case_index) + " draw=" + std::to_string(draw) + " mode=" + std::to_string(mode) + " expected=" + std::to_string(expected_callbacks) + " actual=" + std::to_string(callback_calls));
			words += 2;
		}
		Require(common_calls == 2 && callback_calls == static_cast<std::uint32_t>(dirty != 0 && mode >= 2 && mode <= 4), "repeat clean draw keeps common dispatch and suppresses callback");
		services.Affine.ReleaseInstance(instance);
	}
	Require(plan.peek() == std::char_traits<char>::eof() && expected.peek() == std::char_traits<char>::eof(), "affine draw vector has trailing bytes");
	std::cout << "Matched " << count << " native affine Draw cases and " << words << " selected words\n";
}

static sgre::scene::LayerCameraFrameState PublicationFrameState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {1280, 720};
	state.View.Coefficients = {2.0f / 1280, 0, 0, -1, 0, -2.0f / 720, 0, 1, 0, 0, 1, 0};
	state.RawScissor = {0, 0, 1280, 720};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

static void CheckCommonCameraPublicationAndDraw() {
	const std::array<sgre::scene::MotionScissorMatrix, 1> initial_models{};
	sgre::scene::LayerCameraDrawScope camera(PublicationFrameState(), initial_models, [](const auto&) {
	});
	Services services;
	const auto derived = services.Vm.CreateClass(services.Affine.AffineLayerClass());
	std::vector<std::string> phases;
	bool old_camera_seen = false;
	services.Vm.NewSlot(derived, std::string("calcMatrix"), services.Vm.CreateNativeClosure("publication actual local matrix", [&](auto&, const auto arguments) {
		phases.emplace_back("local");
		sgre::scene::TranslateAffineMatrix(services.Matrices.Matrix(arguments[1]), 5, 7);

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	services.Vm.NewSlot(derived, std::string("calcImageMatrix"), services.Vm.CreateNativeClosure("publication actual image matrix", [&](auto&, const auto arguments) {
		phases.emplace_back("image");
		sgre::scene::ScaleAffineMatrix(services.Matrices.Matrix(arguments[1]), 2, 3);

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	services.Vm.NewSlot(derived, std::string("onScale"), services.Vm.CreateNativeClosure("publication original scale callback order", [&](auto& vm, const auto arguments) -> sgre::script::SquirrelNativeResult {
		phases.emplace_back("scale");
		const auto handle = services.Layers.Handle(arguments[0]);
		const auto before = services.Tree.State(handle);
		old_camera_seen = before.Transform.Scale == sgre::scene::LayerPoint{7, 8} && before.Transform.Offset == sgre::scene::LayerPoint{70, 80} && before.Transform.AngleRadians == 0.75f;
		services.Tree.SetScale(handle, {9, 10});
		services.Tree.SetOffset(handle, {90, 100});
		services.Tree.SetCenter(handle, {16, -5});
		services.Tree.SetClip(handle, {0, 0}, {100, 100});
		vm.SetLastError(81);
		throw sgre::script::SquirrelVmException(81, "original ignored onScale failure before camera publication");
	}));
	const auto instance = services.Create(derived);
	const auto handle = services.Layers.Handle(instance);
	services.Tree.SetCenter(handle, {4, -3});
	services.Tree.SetScale(handle, {7, 8});
	services.Tree.SetOffset(handle, {70, 80});
	services.Tree.SetAngleRadians(handle, 0.75f);
	services.Tree.SetBaseClip(handle, {0, 0}, {1280, 720});
	services.Tree.SetClip(handle, {10, 20}, {30, 40});
	sgre::script::ScriptMotionFrameServices frame_services;
	frame_services.State = [&](const auto& receiver) {
		return services.Layers.MotionFrameState(receiver);
	};
	frame_services.SecondaryDraw = [](const auto&, auto) {
	};
	frame_services.PrimaryDraw = [](const auto&, auto) {
	};
	services.Layers.AttachMotionFrame(instance, camera, std::move(frame_services));
	std::array<float, 12> drawn_model{};
	std::size_t draws = 0;
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto argument) {
		Require(argument == 0x87654321u, "affine camera publication changed the real Motion frame draw argument");
		drawn_model = camera.CurrentModel().Coefficients;
		++draws;
	};
	static_cast<void>(services.Layers.MotionTasks(instance).Group(sgre::scene::MotionTaskGroup::First).Append(std::move(child)));
	static_cast<void>(services.Call(instance, "updateAffine"));
	bool common_saw_publication = false;
	services.Layers.DispatchMotionFrame(handle, [&](const auto& receiver, auto& frame) {
		services.Affine.Process(receiver, [&] {
			phases.emplace_back("common");
			const auto state = services.Layers.MotionFrameState(receiver);
			common_saw_publication = state.Camera.Scale.X == 2 && state.Camera.Scale.Y == 3 && state.Camera.RotationRadians == 0 && state.Camera.Offset.X == -5 && state.Camera.Offset.Y == -7;
			frame.Update();
		});
	});
	Require(old_camera_seen && common_saw_publication && phases == std::vector<std::string>{"local", "image", "scale", "common"}, "actual affine publication must follow onScale and precede common Process even after an ignored script failure");
	Require(services.Vm.LastErrorInContext(services.Vm.RootContext()) == SquirrelValue{81}, "camera publication replaced the original callback failure diagnostic");
	const auto state = services.Tree.State(handle);
	Require(state.Transform.Center == sgre::scene::LayerPoint{16, -5} && state.Clip == sgre::scene::LayerClip{0, 0, 100, 100} && state.BaseClip == sgre::scene::LayerClip{0, 0, 1280, 720}, "publishing affine camera fields must retain the actual center and independent clip owners");
	services.Layers.DispatchMotionFrame(handle, [&](const auto& receiver, auto& frame) {
		services.Affine.Draw(receiver, 0x87654321u, [&](auto argument) {
			frame.Draw(argument);
		});
	});
	// Pivot(16,-5), scale(2,3), negative offset(-5,-7): T(pivot)*S*T(-pivot)-offset.
	const std::array<float, 12> expected{2, 0, 0, -11, 0, 3, 0, 17, 0, 0, 1, 0};
	Require(draws == 1 && drawn_model == expected && camera.Depth() == 1 && camera.CurrentModel().Coefficients == initial_models[0].Coefficients, "actual Motion camera child did not consume the published common transform or restore its scope");
	services.Affine.ReleaseInstance(instance);
}

static void CheckCommonCameraLastWriterAndRetirement() {
	Services services;
	const auto instance = services.Create();
	const auto handle = services.Layers.Handle(instance);
	services.Set(instance, "setXpos", 5.0f);
	services.Set(instance, "setRotate", 30.0f);
	services.Affine.Process(instance, [] {
	});
	const auto calculated = services.Affine.TaskState(instance).Camera;
	const auto published = services.Layers.MotionFrameState(instance).Camera;
	Require(calculated.RotationRadians != 0 && std::bit_cast<std::uint32_t>(published.RotationRadians) == std::bit_cast<std::uint32_t>(calculated.RotationRadians), "computed native radians must publish without a degree conversion");
	Require(published.Offset.X == calculated.Offset.X && published.Offset.Y == calculated.Offset.Y && published.Scale.X == calculated.Scale.X && published.Scale.Y == calculated.Scale.Y, "common camera must receive the exact completed derived words");
	services.Affine.Draw(instance, 0, [](auto) {
	});
	Require(!services.Affine.TaskState(instance).MatrixInvalidated, "real Draw must establish the clean matrix phase before the last-writer check");
	// Exercise real inherited Layer setters, independently of Affine's own scalar properties.
	static_cast<void>(services.Vm.Invoke(services.Vm.GetSlot(services.Layers.LayerBaseClass(), std::string("setOffset")), std::array<SquirrelValue, 3>{instance, 8.0f, 9.0f}));
	static_cast<void>(services.Vm.Invoke(services.Vm.GetSlot(services.Layers.LayerBaseClass(), std::string("setAngleRad")), std::array<SquirrelValue, 2>{instance, -0.25f}));
	services.Affine.Process(instance, [] {
	});
	const auto unchanged = services.Layers.MotionFrameState(instance).Camera;
	Require(unchanged.Offset.X == 8 && unchanged.Offset.Y == 9 && unchanged.RotationRadians == -0.25f, "clean affine Process or read-time overlay must not overwrite later genuine common setters");
	services.Set(instance, "setXpos", 6.0f);
	services.Affine.Process(instance, [] {
	});
	const auto updated = services.Layers.MotionFrameState(instance).Camera;
	const auto next = services.Affine.TaskState(instance).Camera;
	Require(updated.Offset.X == next.Offset.X && updated.Offset.Y == next.Offset.Y && updated.RotationRadians == next.RotationRadians && updated.Offset.X != 8, "next actual invalidation must republish the derived common transform");
	Require(!services.Vm.HasSlot(services.Affine.AffineLayerClass(), std::string("PublishAffineCamera")), "native containing-owner publication must not invent a script method");
	services.Affine.ReleaseInstance(instance);
	Require(!services.Tree.Contains(handle), "affine camera publication must not retain a released native Layer");
	RequireFailure([&] {
		services.Layers.PublishAffineCamera(instance, next);
	},
	    "no live native scene state");
}

int main(int argc, char** argv) {
	try {
		if (argc == 4 && std::string_view(argv[1]) == "--draw") {
			CheckNativeDraw(argv[2], argv[3]);
		} else if (argc == 3) {
			CheckNativeProcess(argv[1], argv[2]);
		} else {
			Require(argc == 1, "expected zero arguments, native process plan/expected paths, or --draw plan/expected paths");
			std::cout << "Checking CheckConstructionAndControls" << std::endl;
			CheckConstructionAndControls();
			std::cout << "Checking CheckLinksAndAssignment" << std::endl;
			CheckLinksAndAssignment();
			std::cout << "Checking CheckMatrixCallbacksAndErrors" << std::endl;
			CheckMatrixCallbacksAndErrors();
			std::cout << "Checking CheckHitConversionAndInverse" << std::endl;
			CheckHitConversionAndInverse();
			CheckGlobalCallbackFromChild();
			CheckDrawDirtyAndSelfRoot();
			std::cout << "Checking CheckBorrowedCleanupAndClose" << std::endl;
			CheckBorrowedCleanupAndClose();
			CheckCommonCameraPublicationAndDraw();
			CheckCommonCameraLastWriterAndRetirement();
		}
		std::cout << "AffineLayer binding tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
