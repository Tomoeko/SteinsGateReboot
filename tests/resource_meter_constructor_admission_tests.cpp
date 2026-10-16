#include "sgre/scene/resource_meter_process.hpp"

#include "resource_meter_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
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
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("missing actual ResourceMeter constructor rejection");
}

struct BorrowedClockOwner {
	std::size_t Updates = 0;
	std::size_t Releases = 0;
	std::size_t ReleaseQueries = 0;
	bool ReleaseAllowed = false;

	sgre::scene::ParallelTaskCallbacks Operations() {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [this](auto) {
			++Updates;
		};
		callbacks.CanRelease = [this](auto) {
			++ReleaseQueries;

			return ReleaseAllowed;
		};
		callbacks.Release = [this](auto) {
			Require(ReleaseAllowed, "unrelated actual borrowed clock still retains its concrete owner");
			++Releases;
		};

		return callbacks;
	}
};

struct MeterAdmissionFixture {
	BorrowedClockOwner Sibling;
	sgre::scene::LooperHookTasks Hooks;
	std::size_t Uploads = 0;
	std::size_t Releases = 0;
	sgre::scene::TextureCache Cache;
	sgre::scene::BitmapFontAssetCatalog Catalog;
	std::shared_ptr<const sgre::scene::LoadedBitmapFontAsset> Asset;
	std::unique_ptr<sgre::scene::ResourceMeterProcess> Meter;

	explicit MeterAdmissionFixture(std::size_t capacity) : Hooks(capacity), Cache([this](auto format, const auto& pixels) {
		                                                       Require(format == sgre::resource::PixelFormat::Alpha8 && pixels.Pixels.size() == 4, "actual authored bitmap source did not pass through the real format/cache owner");
		                                                       ++Uploads;

		                                                       // CPU owner admission only: this handle identifies the concrete fixture upload; it claims no Metal texture.
		                                                       return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(Uploads), 1, this};
	                                                       },
	                                                                            [this](auto handle) {
		                                                                            Require(handle.Owner == this && handle.Index > 0 && handle.Index <= Uploads, "actual Meter font released a foreign physical fixture texture");
		                                                                            ++Releases;
	                                                                            }),
	                                                       Catalog(Cache), Asset(Catalog.Prepare(Document(), Backing())) {
	}

	void Construct() {
		Meter = std::make_unique<sgre::scene::ResourceMeterProcess>(Hooks, Cache, Asset, sgre::scene::ResourceMeterViewport{100, 80});
	}

	~MeterAdmissionFixture() noexcept {
		try {
			if (Meter) {
				Meter->Close();
				Meter.reset();
			}
			Sibling.ReleaseAllowed = true;
			Hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Clear();
			Hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Clear();
			Hooks.Close();
			Asset.reset();
			Catalog.Prune();
			Cache.Close();
		} catch (...) {
			std::terminate();
		}
	}
};

static std::vector<sgre::scene::ParallelTaskHandle> Order(const sgre::scene::ParallelTaskSet& group) {
	std::vector<sgre::scene::ParallelTaskHandle> result(group.Count());
	Require(group.WriteOrder(result) == result.size(), "actual physical clock list order exceeded the bounded fixture");

	return result;
}

static void TestActualCommonSetupWithBorrowedClocks() {
	MeterAdmissionFixture fixture(4);
	auto& pre = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PreLoop);
	auto& post = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PostLoop);
	const auto retained_pre = pre.Append(fixture.Sibling.Operations());
	const auto retained_post = post.Append(fixture.Sibling.Operations());
	Require(!pre.CanClear() && !post.CanClear(), "actual siblings did not establish their dependent-owner retirement veto");
	const auto queries_before = fixture.Sibling.ReleaseQueries;
	sgre::scene::CommonProcessTasks controller;
	std::size_t setup_visits = 0;
	// Genuine Common onProcess runs while Common is calling; these physical hook groups are independently idle.
	fixture.Hooks.Update(sgre::scene::LooperHookPhase::PreLoop);
	controller.Update([&] {
		Require(!controller.CanClear(), "actual setup did not execute inside the containing Common traversal");
		pre.RequireIdleTraversal();
		post.RequireIdleTraversal();
		fixture.Construct();
		++setup_visits;
	});
	Require(setup_visits == 1 && fixture.Meter && fixture.Uploads == 2 && fixture.Releases == 0 && fixture.Cache.Count() == 2, "actual Common setup did not construct its real bitmap/font/clock owner");
	Require(fixture.Sibling.ReleaseQueries == queries_before && fixture.Sibling.Releases == 0, "constructor admission queried or retired unrelated borrowed siblings");
	const auto pre_order = Order(pre);
	const auto post_order = Order(post);
	Require(pre_order.size() == 2 && post_order.size() == 2 && pre_order.front() == retained_pre && post_order.front() == retained_post, "genuine Meter clock append changed existing physical sibling order");
	Require(fixture.Meter->ProcessingCount() == 0, "construction or native empty clock callbacks invented a process count");
	fixture.Meter->Update();
	fixture.Hooks.Update(sgre::scene::LooperHookPhase::PreLoop);
	fixture.Hooks.Update(sgre::scene::LooperHookPhase::PostLoop);
	Require(fixture.Meter->ProcessingCount() == 1 && fixture.Sibling.Updates == 3, "native empty Meter clocks changed processing or suppressed actual sibling delivery");
	fixture.Meter->Close();
	Require(fixture.Meter->IsClosed() && fixture.Releases == 2 && fixture.Cache.Count() == 0 && fixture.Sibling.ReleaseQueries == queries_before && fixture.Sibling.Releases == 0, "target-only Meter retirement altered unrelated actual clock borrows");
	Require(Order(pre) == std::vector<sgre::scene::ParallelTaskHandle>{retained_pre} && Order(post) == std::vector<sgre::scene::ParallelTaskHandle>{retained_post}, "actual Meter retirement unlinked another owner's physical clock");
	controller.Clear();
}

static void TestActiveHookTraversalRejectsBeforeFontPublication() {
	for (const auto phase : {sgre::scene::LooperHookPhase::PreLoop, sgre::scene::LooperHookPhase::PostLoop}) {
		MeterAdmissionFixture fixture(4);
		std::size_t rejected_calls = 0;
		sgre::scene::ParallelTaskCallbacks actual_callback;
		actual_callback.Update = [&](auto) {
			const auto pre_count = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count();
			const auto post_count = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count();
			RequireFailure([&] {
				fixture.Construct();
			},
			    "traversal");
			Require(!fixture.Meter && fixture.Uploads == 0 && fixture.Releases == 0 && fixture.Cache.Count() == 0, "active hook rejection published a font or concrete Meter owner");
			Require(fixture.Hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == pre_count && fixture.Hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == post_count, "active hook rejection changed earlier physical clock ownership");
			++rejected_calls;
		};
		static_cast<void>(fixture.Hooks.Group(phase).Append(std::move(actual_callback)));
		fixture.Hooks.Update(phase);
		Require(rejected_calls == 1, "actual active hook callback did not exercise constructor admission");
	}
}

static void TestActualAppendCapacityAndPartialRollback() {
	for (const bool full_pre : {false, true}) {
		MeterAdmissionFixture fixture(2);
		auto& pre = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PreLoop);
		auto& post = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PostLoop);
		static_cast<void>(pre.Append(fixture.Sibling.Operations()));
		static_cast<void>(post.Append(fixture.Sibling.Operations()));
		static_cast<void>((full_pre ? pre : post).Append(fixture.Sibling.Operations()));
		const auto pre_before = Order(pre);
		const auto post_before = Order(post);
		RequireFailure([&] {
			fixture.Construct();
		},
		    "capacity is exhausted");
		Require(!fixture.Meter && fixture.Uploads == 2 && fixture.Releases == 2 && fixture.Cache.Count() == 0, "failed physical append did not retire every already prepared font source");
		Require(Order(pre) == pre_before && Order(post) == post_before && fixture.Sibling.Releases == 0 && fixture.Sibling.ReleaseQueries == 0, "partial Meter construction rollback depended on unrelated sibling release or changed its physical order");
		pre.RequireIdleTraversal();
		post.RequireIdleTraversal();
	}
}

int main() {
	try {
		TestActualCommonSetupWithBorrowedClocks();
		TestActiveHookTraversalRejectsBeforeFontPublication();
		TestActualAppendCapacityAndPartialRollback();
		std::cout << "PASS actual Common setup with idle borrowed sibling clocks, no unrelated retirement gate, native empty callbacks, active-traversal rejection and exact physical-capacity rollback; CPU owner admission only\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
