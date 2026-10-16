#include "sgre/script/layer_bindings.hpp"

#include "sgre/script/motion_resource_owner.hpp"

#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected native motion resource owner failure was not reported");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue atom;
	atom.Data = std::move(value);

	return atom;
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{1})}, {"height", Atom(std::int64_t{1})}, {"truncated_width", Atom(std::int64_t{1})}, {"truncated_height", Atom(std::int64_t{1})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	auto source = Atom(PsbObject{{"texture", std::move(texture)}, {"icon", Atom(PsbObject{})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 127;
	document->SerializedSize = 128;
	document->Chunks.push_back({127, 1});
	document->Root = Atom(PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Synthetic"))}, {"easing", Atom(PsbArray{})}, {"source", Atom(PsbObject{{"source", std::move(source)}})}, {"object", Atom(PsbObject{{"z", Atom(PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(PsbObject{})}})}, {"a", Atom(PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(PsbObject{})}})}})}, {"sourceIconRenameMap", Atom(PsbObject{{"alias", Atom(PsbArray{Atom(std::string("source")), Atom(std::string("icon"))})}})}});
	document->Root.SerializedOffset = document->Header.RootOffset;

	return document;
}

static std::shared_ptr<std::vector<std::uint8_t>> Backing() {
	auto bytes = std::make_shared<std::vector<std::uint8_t>>(128);
	bytes->back() = 128;

	return bytes;
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, std::string_view name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), instance);

	return vm.Invoke(vm.GetSlot(instance, std::string(name)), arguments).Value;
}

static void TestMotionClassAndNativeResourceMethods() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++releases;
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(layers.MotionLayerBaseClass(), receiver));
	},
	    "can't create instance");
	RequireFailure([&] {
		static_cast<void>(layers.LayerClass());
	},
	    "configured resource services");
	layers.ConfigureMotionOwners(psb, catalog, cache);
	Require(vm.GetSlot(vm.RootTable(), std::string("Layer")) == layers.LayerClass() && vm.GetSlot(vm.RootTable(), std::string("MotionLayerBase")) == layers.MotionLayerBaseClass(), "native motion classes must use their recovered root identities");
	RequireFailure([&] {
		layers.ConfigureMotionOwners(psb, catalog, cache);
	},
	    "one PSB/catalog/cache");
	const auto folder = vm.Invoke(layers.LayerFolderClass(), receiver).Value;
	const std::array<sgre::script::SquirrelValue, 2> parent_receiver{vm.RootTable(), folder};
	const auto leaf = vm.Invoke(layers.LayerClass(), parent_receiver).Value;
	Require(vm.IsInstanceOf(leaf, layers.MotionLayerBaseClass()) && vm.IsInstanceOf(leaf, layers.LayerBaseClass()) && vm.IsInstanceOf(leaf, objects.ObjectClass()) && !vm.IsInstanceOf(leaf, layers.LayerFolderClass()), "native Layer must have genuine MotionLayerBase inheritance, separate from LayerFolder");
	const auto handle = layers.Handle(leaf);
	Require(tree.State(handle).Kind == sgre::scene::LayerKind::Motion && tree.State(handle).Owner == layers.Handle(folder) && layers.MotionResources(leaf).Count() == 0, "native Layer constructor must create common motion leaf state and both empty resource managers");
	Require(!std::get<bool>(Call(vm, leaf, "getMotionCoordAlignmentToInteger")) && !std::get<bool>(Call(vm, leaf, "getMotionMaskRegionClipping")) && layers.MotionProperties(leaf).ObjectClip == sgre::scene::LayerClip{}, "motion alignment/mask/object-clip constructor defaults differ");
	RequireFailure([&] {
		static_cast<void>(Call(vm, folder, "setOwner", {leaf}));
	},
	    "LayerFolder child manager");
	Require(!tree.State(layers.Handle(folder)).Owner && tree.State(handle).Owner == layers.Handle(folder), "invalid motion destination must preserve both native scene memberships");
	const std::array<sgre::script::SquirrelValue, 3> invalid_receiver{vm.RootTable(), folder, {}};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(layers.LayerClass(), invalid_receiver));
	},
	    "invalid arguments");
	const auto foreign_receiver = std::array<sgre::script::SquirrelValue, 1>{folder};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.GetSlot(layers.LayerClass(), std::string("getMotionMaskRegionClipping")), foreign_receiver));
	},
	    "live Layer leaf");
	const auto document = Document();
	const auto backing = Backing();
	const auto first = psb.WrapDocument(document, backing);
	const auto second = psb.WrapDocument(document, backing);
	const auto project_value = Call(vm, leaf, "registerMotionResource", {first});
	const sgre::scene::MotionProjectId project{std::get<std::int32_t>(project_value)};
	Require(Call(vm, leaf, "registerMotionResource", {second}) == project_value && layers.MotionResources(leaf).ReferenceCount(project) == 2 && layers.MotionResources(leaf).RegisteredPsbOwner(project) == first && uploads == 1, "Layer registration must connect both actual managers and retain only the first PSBObject");
	const auto texture = layers.MotionResources(leaf).Primary().FindSource(project, "source")->Texture;
	Require(cache.Get(texture).References == 2, "Layer's two source managers must share one physical texture with two wrapper references");
	const auto names = Call(vm, leaf, "getMotionCharaNameList", {project_value});
	Require(vm.Length(names) == 2 && sgre::script::StringText(vm.GetSlot(names, std::int32_t{0})) == "a" && sgre::script::StringText(vm.GetSlot(names, std::int32_t{1})) == "z", "Layer character names must be fresh native arrays in byte-key map order");
	Require(vm.Length(Call(vm, leaf, "getMotionCharaNameList", {std::int32_t{-1}})) == 0, "missing native project name list must remain empty");
	static_cast<void>(Call(vm, leaf, "renameMotionChara", {project_value, std::string("a\0ignored", 9), std::string("B")}));
	Require(layers.MotionResources(leaf).Primary().FindCharacter(project, "B") && layers.MotionResources(leaf).Secondary().FindCharacter(project, "B") && !layers.MotionResources(leaf).Primary().FindCharacter(project, "a"), "native character rename must truncate script names at NUL and update both managers");
	static_cast<void>(Call(vm, leaf, "removeMotionChara", {project_value, std::string("z")}));
	Require(vm.Length(Call(vm, leaf, "getMotionCharaNameList", {project_value})) == 1, "native character removal must reach both manager maps");
	bool view_filtered = false;
	bool view_detached = false;
	auto view = layers.BorrowMotionView(leaf, [&](bool smoothing) {
		Require(smoothing && tree.State(handle).Smoothing && cache.Get(texture).Minification == 0 && cache.Get(texture).Magnification == 0, "borrowed views must filter after common state changes and before either source manager");
		view_filtered = true;
	},
	    [&] {
		    view_detached = true;
	    });
	static_cast<void>(Call(vm, leaf, "setSmoothing", {true}));
	Require(view_filtered, "native Layer smoothing must reach its borrowed view");
	Require(cache.Get(texture).Minification == 1 && cache.Get(texture).Magnification == 1 && std::get<bool>(Call(vm, leaf, "getSmoothing")), "concrete Layer smoothing must update common state and both motion texture samplers");
	static_cast<void>(Call(vm, leaf, "setMotionCoordAlignmentToInteger", {std::int32_t{1}}));
	static_cast<void>(Call(vm, leaf, "setMotionMaskRegionClipping", {true}));
	static_cast<void>(Call(vm, leaf, "setClip", {10.0f, 20.0f, 30.0f, 40.0f}));
	static_cast<void>(Call(vm, leaf, "setObjectClip", {8.0f, 9.0f, -2.0f, 4.0f}));
	const auto properties = layers.MotionProperties(leaf);
	Require(properties.CoordinateAlignmentToInteger && properties.MaskRegionClipping && properties.ObjectClip == sgre::scene::LayerClip{8, 9, 6, 13} && tree.State(handle).Clip == sgre::scene::LayerClip{10, 20, 40, 60}, "native object clip must remain independent from inherited layer clips");
	static_cast<void>(Call(vm, leaf, "resetObjectClip"));
	Require(layers.MotionProperties(leaf).ObjectClip == sgre::scene::LayerClip{}, "object clip reset must restore native sentinel");
	static_cast<void>(Call(vm, leaf, "unregisterMotionResource", {project_value}));
	Require(layers.MotionResources(leaf).ReferenceCount(project) == 1 && releases == 0, "intermediate Layer resource release must retain both source managers");
	static_cast<void>(Call(vm, leaf, "unregisterMotionResource", {std::string("failed integer conversion")}));
	Require(layers.MotionResources(leaf).Count() == 0 && releases == 1 && cache.Count() == 0, "native failed project integer conversion must use initialized zero before final paired release");
	RequireFailure([&] {
		static_cast<void>(Call(vm, leaf, "unregisterMotionResource", {project_value}));
	},
	    "invalid resource id");
	layers.ReleaseInstance(leaf);
	Require(!tree.Contains(handle) && !objects.HasInstance(leaf) && view_detached && !view.IsAttached(), "explicit motion Layer release must retire both resource managers, borrowed views and common state");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestAutomaticLayerReleaseOrder() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::vector<std::string> events;
	sgre::script::SquirrelValue weak_psb;
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    Require(!std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_psb)), "native Layer shutdown must retain registered PSB owners through GPU retirement");
		    events.push_back("gpu");
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, catalog, cache);
	vm.NewSlot(psb.ObjectClass(), std::string("destructor"), vm.CreateNativeClosure("PSB destructor fixture", [&](auto&, auto) {
		events.push_back("psb");

		return sgre::script::SquirrelNativeResult{};
	}));
	vm.NewSlot(layers.LayerClass(), std::string("destructor"), vm.CreateNativeClosure("Layer destructor fixture", [&](auto&, auto) {
		events.push_back("layer script");

		return sgre::script::SquirrelNativeResult{};
	}));
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	auto folder = vm.Invoke(layers.LayerFolderClass(), receiver).Value;
	sgre::script::SquirrelValue leaf;
	{
		const std::array<sgre::script::SquirrelValue, 2> arguments{vm.RootTable(), folder};
		leaf = vm.Invoke(layers.LayerClass(), arguments).Value;
	}

	const auto handle = layers.Handle(leaf);
	const auto weak_leaf = vm.CreateWeakReference(leaf);
	{
		const auto script_owner = psb.WrapDocument(Document(), Backing());
		weak_psb = vm.CreateWeakReference(script_owner);
		static_cast<void>(Call(vm, leaf, "registerMotionResource", {script_owner}));
	}

	auto view = layers.BorrowMotionView(leaf, [](bool) {
	},
	    [&] {
		    Require(cache.Count() == 0 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_psb)), "native view detachment must follow paired GPU cleanup and registered PSB retirement");
		    events.push_back("view");
	    });
	folder = {};
	Require(tree.Contains(handle) && !tree.State(handle).Attached && std::holds_alternative<std::monostate>(Call(vm, leaf, "getOwner")), "last-strong folder retirement must detach the retained actual motion leaf");
	leaf = {};
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::string>{"layer script", "gpu", "psb", "view"} && !view.IsAttached() && !tree.Contains(handle) && cache.Count() == 0 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_leaf)), "automatic Layer release must run its script destructor before both manager cleanup and final PSB retirement");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestLayerVmCloseAfterClassFinalization() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++releases;
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	sgre::scene::LayerTree tree(4);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, catalog, cache);
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	const auto derived = vm.CreateClass(layers.LayerClass());
	const auto leaf = vm.Invoke(derived, receiver).Value;
	const auto handle = layers.Handle(leaf);
	const auto script_owner = psb.WrapDocument(Document(), Backing());
	static_cast<void>(Call(vm, leaf, "registerMotionResource", {script_owner}));
	static_cast<void>(Call(vm, leaf, "setSmoothing", {true}));
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && !tree.Contains(handle) && cache.Count() == 0 && releases == 1, "VM close must retire actual motion owners with checked identities after class metadata finalizes");
}

static void TestActualLayerTaskGroupRetirement() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::vector<std::string> events;
	sgre::script::SquirrelValue weak_psb;
	std::uint32_t uploads = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    Require(!std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_psb)), "registered native PSB root must survive all concrete task and GPU retirement");
		    events.push_back("gpu");
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, catalog, cache);
	vm.NewSlot(psb.ObjectClass(), std::string("destructor"), vm.CreateNativeClosure("PSB task fixture", [&](auto&, auto) {
		events.push_back("psb");

		return sgre::script::SquirrelNativeResult{};
	}));
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	const auto leaf = vm.Invoke(layers.LayerClass(), receiver).Value;
	const auto handle = layers.Handle(leaf);
	sgre::scene::MotionProjectId project;
	{
		const auto owner = psb.WrapDocument(Document(), Backing());
		weak_psb = vm.CreateWeakReference(owner);
		project.Value = std::get<std::int32_t>(Call(vm, leaf, "registerMotionResource", {owner}));
	}

	const auto& resources = layers.MotionResources(leaf);
	auto& task_owner = layers.MotionTasks(leaf);
	const auto append = [&](sgre::scene::MotionTaskGroup group, std::string name, bool between_managers) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Release = [&, name = std::move(name), between_managers](auto) {
			Require(resources.Primary().Contains(project) && resources.Secondary().Contains(project) != between_managers && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_psb)), "concrete task disposal must observe the recovered source-manager phase and live PSB roots");
			events.push_back(name);
		};

		return task_owner.Group(group).Append(std::move(callbacks));
	};
	const auto common_first = append(sgre::scene::MotionTaskGroup::CommonFirst, "common first", false);
	const auto common_second = append(sgre::scene::MotionTaskGroup::CommonSecond, "common second", false);
	const auto second = append(sgre::scene::MotionTaskGroup::Second, "second", false);
	const auto first = append(sgre::scene::MotionTaskGroup::First, "first", true);
	Require(common_first.Owner != common_second.Owner && common_first.Owner != first.Owner && first.Owner != second.Owner, "four actual nested groups must own separate task identities");
	auto view = layers.BorrowMotionView(leaf, [](bool) {
	},
	    [&] {
		    Require(cache.Count() == 0 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_psb)), "borrowed views detach only after all actual tasks, managers and registered owners retire");
		    events.push_back("view");
	    });
	layers.ReleaseInstance(leaf);
	Require(events == std::vector<std::string>{"common first", "common second", "second", "first", "gpu", "psb", "view"} && !tree.Contains(handle) && !view.IsAttached(), "actual Layer teardown must preserve measured nested group/GPU/PSB/view ordering");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestTaskFailuresStillRetireLayer() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++releases;
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	sgre::scene::LayerTree tree(4);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	layers.ConfigureMotionOwners(psb, catalog, cache);
	const auto leaf = vm.Invoke(layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{vm.RootTable()}).Value;
	const auto handle = layers.Handle(leaf);
	const auto owner = psb.WrapDocument(Document(), Backing());
	static_cast<void>(Call(vm, leaf, "registerMotionResource", {owner}));
	std::vector<std::uint32_t> phases;
	for (const auto group : std::array{sgre::scene::MotionTaskGroup::CommonFirst, sgre::scene::MotionTaskGroup::CommonSecond, sgre::scene::MotionTaskGroup::Second, sgre::scene::MotionTaskGroup::First}) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Release = [&, group](auto) {
			phases.push_back(static_cast<std::uint32_t>(group));
			if (group == sgre::scene::MotionTaskGroup::CommonFirst) {
				throw std::runtime_error("first task failure");
			}
			if (group == sgre::scene::MotionTaskGroup::First) {
				throw std::runtime_error("between managers task failure");
			}
		};
		static_cast<void>(layers.MotionTasks(leaf).Group(group).Append(std::move(callbacks)));
	}

	bool detached = false;
	auto view = layers.BorrowMotionView(leaf, [](bool) {
	},
	    [&] {
		    detached = true;
	    });
	RequireFailure([&] {
		layers.ReleaseInstance(leaf);
	},
	    "first task failure");
	Require(phases == std::vector<std::uint32_t>{0, 1, 3, 2} && detached && releases == 1 && cache.Count() == 0 && !tree.Contains(handle) && !view.IsAttached(), "authored failure cleanup must attempt all nested tasks, source managers, views and native parent before reporting the first error");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestActualLayerTaskGroupRetirement();
		TestTaskFailuresStillRetireLayer();
		TestMotionClassAndNativeResourceMethods();
		TestAutomaticLayerReleaseOrder();
		TestLayerVmCloseAfterClassFinalization();
		std::cout << "Native motion Layer binding and paired resource lifetime fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
