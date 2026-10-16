#include "sgre/script/text_render_bindings.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "sgre/scene/layer_scale_view.hpp"
#include "sgre/scene/motion_assets.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
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
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("TextRender fixture expected an explicit unsupported/lifetime error");
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());

	return vm.Invoke(class_value, arguments).Value;
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::string_view name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), receiver);

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

struct Fixture {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	std::uint32_t Uploads = 0;
	sgre::scene::TextureCache Textures{[this](auto, const auto&) {
		                                   return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	                                   },
	    [](auto) {
	    }};
	sgre::scene::MotionAssetCatalog Motions{Textures};
	sgre::scene::LayerTree Tree{16};
	sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};
	text_atlas_test::Backend Backend;
	sgre::script::ScriptTextRenderBindings Text{Vm, Objects, Layers, Tree, Backend};

	Fixture() {
		Layers.ConfigureMotionOwners(Psb, Motions, Textures);
	}
	~Fixture() {
		Vm.Close();
	}
};

static void TestActualClassFirstTaskAndDelayedRetirement() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	auto layer = Construct(vm, fixture.Layers.LayerClass());
	auto first = Construct(vm, fixture.Text.TextRenderClass(), {layer, std::string("ignored tail")});
	auto second = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	Require(vm.GetSlot(vm.RootTable(), std::string("TextRender")) == fixture.Text.TextRenderClass() && vm.IsInstanceOf(first, fixture.Objects.ObjectClass()) && !vm.IsInstanceOf(first, fixture.Layers.LayerBaseClass()), "TextRender must retain its real direct Object ancestry");
	Require(vm.RetainedReferenceCount(layer) == 0 && vm.RetainedReferenceCount(first) == 0 && fixture.Backend.Observed.Creates == 0 && fixture.Uploads == 0, "constructed text task borrows its Layer and caches weak self without creating replacement assets");
	auto& group = fixture.Layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First);
	std::array<sgre::scene::ParallelTaskHandle, 2> order;
	Require(group.Count() == 2 && group.WriteOrder(order) == 2 && group.State(order[0]).DrawPaused && group.State(order[0]).Status == sgre::scene::TaskStatus::Active, "genuine text tasks must append hidden/active in the original First group");
	static_cast<void>(Call(vm, first, "setVisible", {true}));
	static_cast<void>(Call(vm, first, "setVertical", {std::int32_t{2}}));
	static_cast<void>(Call(vm, first, "setTimeScale", {-3.0f}));
	static_cast<void>(Call(vm, first, "setFontScale", {std::int32_t{0}}));
	static_cast<void>(Call(vm, first, "setShowCount", {std::int32_t{-1}}));
	Require(std::get<bool>(Call(vm, first, "getVisible")) && std::get<bool>(Call(vm, first, "getVertical")) && std::get<float>(Call(vm, first, "getTimeScale")) == -3 && std::get<float>(Call(vm, first, "getFontScale")) == 0 && fixture.Text.Context(first).State().ShownElementCount == 0xffffffff, "actual script scalar conversions and task pause must propagate without clamps");
	static_cast<void>(Call(vm, layer, "setSmoothing", {true}));
	Require(fixture.Text.Context(first).Atlas().State().Filtering && fixture.Text.Context(second).Atlas().State().Filtering, "actual Layer borrowed registry must filter every text context");
	group.Update();
	Require(fixture.Text.Context(first).State().ShownElementCount == 0xffffffff, "ordinary native process update does not finalize text");
	static_cast<void>(Call(vm, first, "done"));
	Require(fixture.Text.Context(first).State().ShownElementCount == 0, "explicit native done finalizes the empty core and copies its zero count");
	fixture.Text.ReleaseInstance(first);
	Require(group.Count() == 2 && group.State(order[0]).Status == sgre::scene::TaskStatus::Retire && group.State(order[1]).Status == sgre::scene::TaskStatus::Active, "wrapper destruction must mark rather than immediately delete its owned task");
	group.Update();
	Require(group.Count() == 1, "actual parent update must retire only the marked text task");
	fixture.Layers.ReleaseInstance(layer);
	Require(!std::get<bool>(Call(vm, second, "getVisible")) && !std::get<bool>(Call(vm, second, "getVertical")) && std::get<float>(Call(vm, second, "getTimeScale")) == 1 && std::get<float>(Call(vm, second, "getFontScale")) == 1, "parent-first disposal must make retained wrappers inert with native detached defaults");
	static_cast<void>(Call(vm, second, "setVertical", {true}));
	RequireFailure([&] {
		static_cast<void>(Call(vm, second, "getClipMode"));
	},
	    "live task");
	RequireFailure([&] {
		static_cast<void>(fixture.Text.Context(second));
	},
	    "detached");
	fixture.Text.ReleaseInstance(second);
}

static void TestReceiverIdentityAndUnsupportedMethods() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fixture.Text.TextRenderClass()));
	},
	    "actual Layer");
	const auto folder = Construct(vm, fixture.Layers.LayerFolderClass());
	RequireFailure([&] {
		static_cast<void>(Construct(vm, fixture.Text.TextRenderClass(), {folder}));
	},
	    "actual Layer");
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto instance = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	static_cast<void>(Call(vm, instance, "setTimeScale", {std::string("bad numeric input")}));
	Require(std::get<float>(Call(vm, instance, "getTimeScale")) == 0, "Sqrat numeric conversion leaves the original zero output on type mismatch");
	static_cast<void>(Call(vm, instance, "setClipMode", {std::int32_t{2}}));
	static_cast<void>(Call(vm, instance, "setCoord", {10.0f, 20.0f}));
	static_cast<void>(Call(vm, instance, "setClipOffset", {3.0f, 4.0f}));
	Require(fixture.Text.Context(instance).State().ClippingBounds.Left == 13 && std::get<float>(Call(vm, instance, "getLeft")) == 10 && std::get<float>(Call(vm, instance, "getWidth")) == 0, "actual empty view placement must stay distinct from font rendering");
	RequireFailure([&] {
		static_cast<void>(Call(vm, instance, "render", {std::string("unrecovered application"), std::int32_t{0}, std::int32_t{0}, std::int32_t{0}, false}));
	},
	    "configured provider services");
	Require(fixture.Text.Context(instance).Content().ElementCount() == 0 && fixture.Text.Context(instance).Content().Lines().empty() && fixture.Uploads == 0 && fixture.Backend.Observed.Creates == 0, "registered render without real font services must reject before publishing content or fabricating providers");
	const auto uninitialized = vm.CreateInstance(fixture.Text.TextRenderClass());
	RequireFailure([&] {
		static_cast<void>(Call(vm, uninitialized, "getVisible"));
	},
	    "live native wrapper");
	fixture.Text.ReleaseInstance(instance);
	RequireFailure([&] {
		static_cast<void>(Call(vm, instance, "getVisible"));
	},
	    "live native wrapper");
}

static void TestCachedWeakSelfDoesNotRetainWrapper() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::scene::TextureCache textures([](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{};
	},
	    [](auto) {
	    });
	sgre::scene::MotionAssetCatalog motions(textures);
	sgre::scene::LayerTree tree(4);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, motions, textures);
	text_atlas_test::Backend backend;
	sgre::script::ScriptTextRenderBindings text(vm, objects, layers, tree, backend);
	const auto layer = Construct(vm, layers.LayerClass());
	auto instance = Construct(vm, text.TextRenderClass(), {layer});
	static_cast<void>(Call(vm, instance, "getVisible"));
	const auto weak = vm.CreateWeakReference(instance);
	auto& group = layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First);
	instance = {};
	vm.RethrowReleaseFailure();
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)) && group.Count() == 1, "native cached weak self must allow wrapper finalization before task retirement");
	group.Update();
	Require(group.Count() == 0 && backend.Observed.Creates == 0, "retiring an empty text task must release its cached self and avoid resource fabrication");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestActualCapturedMethodArgumentPositions() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto instance = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	static_cast<void>(Call(vm, instance, "setVertical"));
	static_cast<void>(Call(vm, instance, "setVisible"));
	Require(std::get<bool>(Call(vm, instance, "getVertical")) && std::get<bool>(Call(vm, instance, "getVisible")), "missing native bool argument must coerce the appended actual userdata capture to true");
	static_cast<void>(Call(vm, instance, "setTimeScale"));
	Require(std::get<float>(Call(vm, instance, "getTimeScale")) == 0, "missing native numeric argument sees userdata and retains its initialized zero output");
	static_cast<void>(Call(vm, instance, "setCoord", {19.75f}));
	Require(fixture.Text.Context(instance).State().Coordinate.X == 19.75f && fixture.Text.Context(instance).State().Coordinate.Y == 0, "one user argument places captured userdata at the second numeric index");
	const std::array<sgre::script::SquirrelValue, 2> captured_history{19.75f, 3.5f};
	const auto seed = vm.CreateNativeClosureWithCaptures("retain_native_argument_history", [](auto&, auto) {
		return sgre::script::SquirrelNativeResult::NoValues();
	},
	    captured_history);
	const std::array<sgre::script::SquirrelValue, 1> receiver{std::int32_t{17}};
	static_cast<void>(vm.Invoke(seed, receiver));
	static_cast<void>(Call(vm, instance, "setCoord"));
	Require(fixture.Text.Context(instance).State().Coordinate.X == 0 && fixture.Text.Context(instance).State().Coordinate.Y == 19.75f, "zero user arguments preserve the actual inactive third float rather than inventing a zero default");
}

static void TestSharedScaleCameraOwnsActualTextTask() {
	Fixture fixture;
	auto& vm = fixture.Vm;
	const auto layer = Construct(vm, fixture.Layers.LayerClass());
	const auto instance = Construct(vm, fixture.Text.TextRenderClass(), {layer});
	auto& group = fixture.Layers.MotionTasks(layer).Group(sgre::scene::MotionTaskGroup::First);
	std::array<sgre::scene::ParallelTaskHandle, 1> before;
	Require(group.WriteOrder(before) == 1, "text task must begin directly in the original First group");
	static_cast<void>(Call(vm, instance, "setOffset", {0.0f, 0.0f}));
	static_cast<void>(Call(vm, instance, "setScale", {1.0f, 1.0f}));
	static_cast<void>(Call(vm, instance, "setRot", {0.0f}));
	Require(!fixture.Text.ScaleView(instance).State().HasCamera && group.Contains(before[0]), "native identity setters must preserve the actual task without allocating a camera");
	static_cast<void>(Call(vm, instance, "setVisible", {true}));
	static_cast<void>(Call(vm, instance, "setOffset", {3.0f, -4.0f}));
	const auto& view = fixture.Text.ScaleView(instance);
	std::array<sgre::scene::ParallelTaskHandle, 1> after;
	Require(group.WriteOrder(after) == 1 && !group.Contains(before[0]) && after[0] != before[0] && view.State().HasCamera, "lazy camera must move the same owned text task into its actual child group");
	Require(group.State(after[0]).Status == sgre::scene::TaskStatus::Active && !group.State(after[0]).DrawPaused && !view.TaskState().DrawPaused, "camera and child retain their distinct native pause fields");
	static_cast<void>(Call(vm, instance, "setScale", {-2.0f, 5.0f}));
	static_cast<void>(Call(vm, instance, "setRot", {90.0f}));
	const auto camera = view.CameraState();
	Require(camera.Offset.X == 3 && camera.Offset.Y == -4 && camera.Scale.X == -2 && camera.Scale.Y == 5 && camera.RotationRadians == 90.0f * 0.0174532924f, "genuine text scale methods must use recovered common camera fields and multiplication order");
	static_cast<void>(Call(vm, instance, "setPrimaryScale", {2.5f}));
	Require(view.CameraState().Scale.X == 2.5f && view.CameraState().Scale.Y == 2.5f, "primary scale must write both components of the actual camera");
	group.Draw(17);
	Require(fixture.Backend.Observed.Creates == 0, "drawing the genuine empty camera task must not create synthetic text resources");
	fixture.Text.ReleaseInstance(instance);
	Require(group.Count() == 1 && group.State(after[0]).Status == sgre::scene::TaskStatus::Retire, "wrapper destruction must retire the camera without deleting its child immediately");
	group.Update();
	Require(group.Count() == 0 && fixture.Backend.Observed.Creates == 0, "actual First retirement releases the camera and text task exactly through ownership");
	fixture.Layers.ReleaseInstance(layer);

	const auto second_layer = Construct(vm, fixture.Layers.LayerClass());
	const auto retained = Construct(vm, fixture.Text.TextRenderClass(), {second_layer});
	static_cast<void>(Call(vm, retained, "setScale", {2.0f, 3.0f}));
	fixture.Layers.ReleaseInstance(second_layer);
	Require(fixture.Text.IsDetached(retained) && fixture.Text.ScaleView(retained).State().Detached && !fixture.Text.ScaleView(retained).State().HasCamera, "parent-first retirement must expire shared camera and task before borrowed wrapper detach");
	static_cast<void>(Call(vm, retained, "setRot", {3.0f}));
	fixture.Text.ReleaseInstance(retained);
}

int main() {
	try {
		TestActualClassFirstTaskAndDelayedRetirement();
		TestReceiverIdentityAndUnsupportedMethods();
		TestCachedWeakSelfDoesNotRetainWrapper();
		TestActualCapturedMethodArgumentPositions();
		TestSharedScaleCameraOwnsActualTextTask();
		std::cout << "Genuine TextRender Object class, First-group empty task and weak wrapper lifetime passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
