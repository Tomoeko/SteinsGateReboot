#include "raw_texture_fixture.hpp"
#include "sgre/scene/mutable_texture_owner.hpp"
#include "sgre/scene/raw_layer_owner.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected raw common owner safety rejection");
}

static void TestActualRawLeafAndOrderedOwnership() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::LayerTree tree(8);
	const auto folder = tree.CreateFolder();
	const auto sibling = tree.CreateFolder(folder);
	std::vector<std::string> events;
	events.reserve(16);
	bool background_released = false;
	sgre::scene::ParallelTaskSet driver;
	sgre::scene::ParallelTaskCallbacks background_callbacks;
	background_callbacks.Release = [&](sgre::scene::ParallelTaskHandle) {
		background_released = true;
		events.push_back("background");
	};
	const auto background = driver.Append(std::move(background_callbacks));
	sgre::scene::LayerHandle raw;
	sgre::scene::RawLayerRetirement retirement;
	retirement.DropParent = [&] {
		Require(driver.State(background).Status == sgre::scene::TaskStatus::Retire && tree.ChildCount(folder) == 1 && tree.Contains(raw), "background retirement and unlink precede actual weak parent disposal");
		Require(!tree.State(raw).Attached && !tree.State(raw).Owner && backend.Live, "unlinked typed leaf survives with live texture through parent disposal");
		events.push_back("parent");
	};
	retirement.ReleaseObject = [&] {
		Require(!backend.Live && tree.Contains(raw), "native Object phase follows resources while common identity remains retained");
		events.push_back("Object");
	};
	backend.OnTextureRelease = [&] {
		events.push_back("texture");
	};
	sgre::scene::RawLayerAttachment attachment;
	attachment.Folder = folder;
	sgre::scene::RawLayerOwner owner(tree, std::make_unique<sgre::scene::RawTextureTask>(backend, sgre::resource::MotionPoint{2, 2}), driver, background, std::move(retirement), attachment);
	raw = owner.Handle();
	Require(tree.State(raw).Kind == sgre::scene::LayerKind::RawTexture && tree.ChildCount(folder) == 2, "raw view uses genuine distinct common leaf membership");
	RequireError([&] {
		static_cast<void>(tree.CreateFolder(raw));
	},
	    "folder child manager");
	tree.SetZoom(folder, 2);
	tree.SetOffset(raw, {3, 4});
	Require(tree.WorldTransform(raw).XX == 2 && tree.WorldTransform(raw).TranslationX == -6 && tree.WorldTransform(raw).TranslationY == -8, "raw common leaf retains the actual inherited logical camera transform");
	for (const auto group : {sgre::scene::RawLayerTaskGroup::CommonFirst, sgre::scene::RawLayerTaskGroup::CommonSecond}) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Release = [&, group](sgre::scene::ParallelTaskHandle) {
			Require(backend.Live, "common task groups precede camera-owned raw texture disposal");
			events.push_back(group == sgre::scene::RawLayerTaskGroup::CommonFirst ? "first" : "second");
		};
		static_cast<void>(owner.Group(group).Append(std::move(callbacks)));
	}
	sgre::scene::ParallelTaskCallbacks inner_parallel;
	inner_parallel.Release = [&](sgre::scene::ParallelTaskHandle) {
		Require(!backend.Live, "inner raw tasks follow texture disposal");
		events.push_back("raw-parallel");
	};
	static_cast<void>(owner.TextureTask().ParallelTasks().Append(std::move(inner_parallel)));
	sgre::scene::SerialTaskCallbacks inner_serial;
	inner_serial.Release = [&] {
		events.push_back("raw-serial");
	};
	static_cast<void>(owner.TextureTask().SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(inner_serial))));
	sgre::scene::SerialTaskCallbacks common_serial;
	common_serial.Release = [&] {
		events.push_back("common-serial");
	};
	static_cast<void>(owner.SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(common_serial))));
	owner.Close();
	Require(events == std::vector<std::string>{"parent", "first", "second", "texture", "raw-parallel", "raw-serial", "common-serial", "Object"}, "common raw ownership phases differ from unchanged wrapper/base teardown");
	Require(!tree.Contains(raw) && tree.Contains(sibling) && !background_released && driver.Count() == 1, "common identity retires without deleting peer or prematurely deleting driver-owned background task");
	owner.Close();
	Require(events.size() == 8, "common Close cannot repeat ownership disposal");
	driver.Update();
	Require(background_released && driver.Count() == 0 && events.back() == "background", "actual background driver retires its marked owner later");
}

static void TestCallbackPreflightBeforeAnyMutation() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::LayerTree tree;
	sgre::scene::ParallelTaskSet driver;
	const auto background = driver.Append({});
	sgre::scene::RawLayerRetirement retirement;
	retirement.DropParent = [] {
	};
	retirement.ReleaseObject = [] {
	};
	sgre::scene::RawLayerOwner owner(tree, std::make_unique<sgre::scene::RawTextureTask>(backend, sgre::resource::MotionPoint{1, 1}), driver, background, std::move(retirement));
	const auto handle = owner.Handle();
	sgre::scene::ParallelTaskCallbacks common;
	common.Update = [&](sgre::scene::ParallelTaskHandle) {
		Require(!owner.CanClose(), "common callback cannot safely retire its ancestor owner");
		RequireError([&] {
			owner.Close();
		},
		    "active task callback");
		Require(driver.State(background).Status == sgre::scene::TaskStatus::Active && tree.State(handle).Attached && backend.Live && !owner.IsClosed(), "rejected common close precedes every native ownership phase");
	};
	static_cast<void>(owner.Group(sgre::scene::RawLayerTaskGroup::CommonFirst).Append(std::move(common)));
	owner.Group(sgre::scene::RawLayerTaskGroup::CommonFirst).Update();
	sgre::scene::SerialTaskCallbacks raw;
	raw.Update = [&](sgre::scene::SerialTask&) {
		RequireError([&] {
			owner.Close();
		},
		    "active task callback");
		Require(!owner.IsClosed() && backend.Live && tree.State(handle).Attached, "borrowed raw callback also preserves complete common ownership on rejection");
	};
	static_cast<void>(owner.TextureTask().SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(raw))));
	owner.TextureTask().SerialTasks().Update();
	Require(owner.CanClose(), "all callback lifetimes restore normal common Close availability");
	owner.Close();
	Require(!tree.Contains(handle) && !backend.Live, "common retirement succeeds after guarded callbacks return");
}

static void TestFailureStillRetiresEveryOwner() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::LayerTree tree;
	sgre::scene::ParallelTaskSet driver;
	const auto background = driver.Append({});
	std::vector<std::string> events;
	events.reserve(8);
	sgre::scene::RawLayerRetirement retirement;
	retirement.DropParent = [&] {
		events.push_back("parent");
		throw std::runtime_error("first failure");
	};
	retirement.ReleaseObject = [&] {
		events.push_back("Object");
		throw std::runtime_error("later failure");
	};
	backend.OnTextureRelease = [&] {
		events.push_back("texture");
	};
	sgre::scene::RawLayerOwner owner(tree, std::make_unique<sgre::scene::RawTextureTask>(backend, sgre::resource::MotionPoint{1, 1}), driver, background, std::move(retirement));
	sgre::scene::SerialTaskCallbacks serial;
	serial.Release = [&] {
		events.push_back("serial");
	};
	static_cast<void>(owner.SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	RequireError([&] {
		owner.Close();
	},
	    "first failure");
	Require(events == std::vector<std::string>{"parent", "texture", "serial", "Object"} && owner.IsClosed() && !backend.Live && !tree.Contains(owner.Handle()), "reported bridge failure cannot interrupt valid resource/base/identity retirement");
	Require(driver.State(background).Status == sgre::scene::TaskStatus::Retire, "failure cleanup still marks the real driver-owned task for retirement");
}

static void TestScreenAttachmentAndDetachedIdentity() {
	sgre::scene::tests::TaskFixtureBackend backend;
	sgre::scene::LayerTree tree(4);
	const auto screen = tree.CreateScreenRoot();
	sgre::scene::ParallelTaskSet driver;
	const auto background = driver.Append({});
	sgre::scene::RawLayerRetirement retirement;
	retirement.DropParent = [] {
	};
	retirement.ReleaseObject = [] {
	};
	sgre::scene::RawLayerAttachment attachment;
	attachment.Screen = screen;
	sgre::scene::RawLayerOwner owner(tree, std::make_unique<sgre::scene::RawTextureTask>(backend, sgre::resource::MotionPoint{1, 1}), driver, background, std::move(retirement), attachment);
	const auto handle = owner.Handle();
	Require(tree.State(handle).ScreenOwner == screen && tree.ScreenChildCount(screen) == 1, "genuine raw leaf attaches to a Screen child manager");
	tree.Detach(handle);
	Require(tree.Contains(handle) && !tree.State(handle).Attached && !tree.State(handle).ScreenOwner && tree.ScreenChildCount(screen) == 0, "common unlink retains exact typed identity without registering it at the root");
	tree.Detach(handle);
	tree.SetScreenOwner(handle, screen);
	Require(tree.ScreenChildCount(screen) == 1 && tree.State(handle).Kind == sgre::scene::LayerKind::RawTexture, "detached common identity can reattach with the same genuine kind");
	owner.Close();
	const auto reused = tree.CreateFolder();
	Require(reused.Index == handle.Index && reused.Generation != handle.Generation, "common release advances generation before record reuse");
}

int main() {
	try {
		TestActualRawLeafAndOrderedOwnership();
		TestCallbackPreflightBeforeAnyMutation();
		TestFailureStillRetiresEveryOwner();
		TestScreenAttachmentAndDetachedIdentity();
		std::cout << "genuine raw common ownership and ordered retirement fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
