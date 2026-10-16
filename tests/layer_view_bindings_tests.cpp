#include "sgre/script/layer_bindings.hpp"

#include "sgre/scene/bitmap_indicator.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <iostream>
#include <optional>
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

static void SetSmoothing(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, bool smoothing) {
	static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("setSmoothing")), std::array<sgre::script::SquirrelValue, 2>{instance, smoothing}));
}

struct ViewFixture {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::script::ScriptPsbBindings Psb{Vm, Objects};
	sgre::scene::TextureCache Cache{[](auto, const auto&) {
		                                return sgre::render::SpriteTextureHandle{};
	                                },
	    [](auto) {
	    }};
	sgre::scene::MotionAssetCatalog Catalog{Cache};
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers{Vm, Objects, Tree};

	explicit ViewFixture(bool reclaim = false) : Vm([reclaim] {
		                                             sgre::script::SquirrelVmLimits limits;
		                                             limits.ReclaimObjects = reclaim;

		                                             return limits;
	                                             }()) {
		Layers.ConfigureMotionOwners(Psb, Catalog, Cache);
	}

	~ViewFixture() {
		Vm.Close();
	}

	sgre::script::SquirrelValue CreateLeaf() {
		return Vm.Invoke(Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{Vm.RootTable()}).Value;
	}
};

static void TestOrderedBorrowingAndMoveOnlyTokens() {
	ViewFixture fixture;
	const auto leaf = fixture.CreateLeaf();
	std::vector<std::uint32_t> calls;
	std::array<bool, 3> detached{};
	auto first = fixture.Layers.BorrowMotionView(leaf, [&](bool smooth) {
		Require(smooth, "borrowed view must receive the current native bool");
		calls.push_back(1);
	},
	    [&] {
		    detached[0] = true;
	    });
	auto middle = fixture.Layers.BorrowMotionView(leaf, [&](bool) {
		calls.push_back(2);
	},
	    [&] {
		    detached[1] = true;
	    });
	auto last = fixture.Layers.BorrowMotionView(leaf, [&](bool) {
		calls.push_back(3);
	},
	    [&] {
		    detached[2] = true;
	    });
	Require(calls.empty() && first.IsAttached() && middle.IsAttached() && last.IsAttached(), "native common view registration must borrow without applying initial font filtering itself");
	middle.Reset();
	auto moved = std::move(first);
	Require(!first.IsAttached() && moved.IsAttached() && !middle.IsAttached() && !detached[1], "moving transfers the singular registration; ordinary view destruction unlinks without a parent-detach callback");
	SetSmoothing(fixture.Vm, leaf, true);
	Require(calls == std::vector<std::uint32_t>{1, 3}, "native borrowed-view traversal must preserve survivor registration order");
	fixture.Layers.ReleaseInstance(leaf);
	Require(detached == std::array<bool, 3>{true, false, true} && !moved.IsAttached() && !last.IsAttached(), "native Layer retirement must detach only its remaining borrowed views");
	moved.Reset();
	last.Reset();
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestBorrowedRegistrationDoesNotRootParent() {
	ViewFixture fixture(true);
	sgre::resource::BitmapFont font;
	font.MaxWidth = 16;
	font.MaxHeight = 12;
	sgre::scene::BitmapIndicator indicator(std::move(font));
	auto leaf = fixture.CreateLeaf();
	const auto handle = fixture.Layers.Handle(leaf);
	const auto weak_leaf = fixture.Vm.CreateWeakReference(leaf);
	auto registration = fixture.Layers.BorrowMotionView(leaf, [](bool) {
	},
	    [&] {
		    indicator.Placement().Detach();
	    });
	// The host invocation return cell is a separate native root; clear it before dropping the caller value.
	SetSmoothing(fixture.Vm, leaf, false);
	leaf = {};
	fixture.Vm.RethrowReleaseFailure();
	Require(!fixture.Tree.Contains(handle) && !registration.IsAttached() && indicator.Placement().IsDetached() && std::holds_alternative<std::monostate>(fixture.Vm.ResolveWeakReference(weak_leaf)), "a retained native view registration must not keep its owning Layer alive");
	registration.Reset();
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestDeferredUnlinkDuringCallback() {
	ViewFixture fixture;
	const auto leaf = fixture.CreateLeaf();
	std::vector<std::uint32_t> calls;
	std::optional<sgre::script::ScriptLayerBindings::BorrowedViewRegistration> second;
	auto first = fixture.Layers.BorrowMotionView(leaf, [&](bool) {
		calls.push_back(1);
		second.reset();
	},
	    [] {
	    });
	second = fixture.Layers.BorrowMotionView(leaf, [&](bool) {
		calls.push_back(2);
	},
	    [] {
	    });
	auto third = fixture.Layers.BorrowMotionView(leaf, [&](bool) {
		calls.push_back(3);
	},
	    [] {
	    });
	SetSmoothing(fixture.Vm, leaf, true);
	SetSmoothing(fixture.Vm, leaf, false);
	Require(calls == std::vector<std::uint32_t>{1, 3, 1, 3} && first.IsAttached() && third.IsAttached(), "authored deferred unlink must preserve a stable borrowed callback and skip the retired entry");
	fixture.Layers.ReleaseInstance(leaf);
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

static void TestCheckedTargetAndRetirementFailure() {
	ViewFixture fixture;
	const auto folder = fixture.Vm.Invoke(fixture.Layers.LayerFolderClass(), std::array<sgre::script::SquirrelValue, 1>{fixture.Vm.RootTable()}).Value;
	bool folder_rejected = false;
	try {
		static_cast<void>(fixture.Layers.BorrowMotionView(folder, [](bool) {
		},
		    [] {
		    }));
	} catch (const std::runtime_error&) {
		folder_rejected = true;
	}

	Require(folder_rejected, "native borrowed views require an actual motion Layer rather than a folder child manager");
	const auto leaf = fixture.CreateLeaf();
	const auto handle = fixture.Layers.Handle(leaf);
	bool later_detached = false;
	auto first = fixture.Layers.BorrowMotionView(leaf, [](bool) {
	},
	    [] {
		    throw std::runtime_error("authored detach fixture");
	    });
	auto second = fixture.Layers.BorrowMotionView(leaf, [](bool) {
	},
	    [&] {
		    later_detached = true;
	    });
	bool failure_reported = false;
	try {
		fixture.Layers.ReleaseInstance(leaf);
	} catch (const std::runtime_error& error) {
		failure_reported = std::string_view(error.what()).find("authored detach fixture") != std::string_view::npos;
	}

	Require(failure_reported && later_detached && !fixture.Tree.Contains(handle) && !first.IsAttached() && !second.IsAttached(), "authored release failure must retire the remaining borrowed views and native parent before reporting the first error");
	fixture.Layers.ReleaseInstance(folder);
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestOrderedBorrowingAndMoveOnlyTokens();
		TestBorrowedRegistrationDoesNotRootParent();
		TestDeferredUnlinkDuringCallback();
		TestCheckedTargetAndRetirementFailure();
		std::cout << "Native borrowed Layer view lifetime fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
