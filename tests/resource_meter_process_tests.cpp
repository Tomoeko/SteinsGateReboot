#include "sgre/scene/resource_meter_process.hpp"

#include "resource_meter_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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

	throw std::runtime_error("ResourceMeter fixture expected rejection");
}

static sgre::scene::ParallelTaskHandle OnlyTask(const sgre::scene::ParallelTaskSet& group) {
	std::array<sgre::scene::ParallelTaskHandle, 1> order{};
	Require(group.WriteOrder(order) == 1, "expected the one actual clock task");

	return order.front();
}

static void TestRealFontCommonHooksAndDefaultDriverPause() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::LooperHookTasks hooks;
	std::weak_ptr<const sgre::resource::PsbDocument> document_lifetime;
	std::weak_ptr<const std::vector<std::uint8_t>> backing_lifetime;
	sgre::scene::TextureCache cache([&](auto format, const auto& pixels) {
		Require(format == sgre::resource::PixelFormat::Alpha8 && pixels.Pixels.size() == 4, "actual font sources must decode through the real cache");
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto handle) {
		    Require(hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 0 && hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == 0 && !document_lifetime.expired() && !backing_lifetime.expired(), "both clocks must retire before font disposal while its actual PSB/backing still survive");
		    releases.push_back(handle.Index);
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	auto document = Document();
	auto backing = Backing();
	document_lifetime = document;
	backing_lifetime = backing;
	auto asset = catalog.Prepare(document, backing);
	sgre::scene::ResourceMeterProcess meter(hooks, cache, asset, {100, 80});
	Require(meter.FontTextures().Asset().Document.get() != document.get() && meter.FontTextures().Asset().DocumentIdentity != asset->DocumentIdentity && meter.FontTextures().Asset().Backing.get() == backing.get(), "actual embedded construction must create a new descriptor identity while referring to the same resident bytes");
	document_lifetime = meter.FontTextures().Asset().Document;
	asset.reset();
	document.reset();
	backing.reset();
	Require(meter.Tasks().Serial().Count() == 0 && meter.Tasks().Parallel().Count() == 0 && meter.ProcessingCount() == 0 && hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 1 && hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == 1, "the actual default owner must have its own Common and two distinct physical clock registrations");
	Require(meter.Indicator().Elements().empty() && !meter.Indicator().Placement().Visible() && meter.Indicator().Placement().Bounds() == sgre::scene::LayerClip{-20, -20, -20, -20} && meter.Indicator().FontColor() == 0xffffffff && meter.Indicator().FontScale().Width == 1 && meter.Indicator().FontScale().Height == 1 && meter.FontTextures().Minification(0) == 1 && meter.FontTextures().Magnification(1) == 1, "default native font/Indicator/filter/centered renderer placement must remain concrete");
	hooks.Update(sgre::scene::LooperHookPhase::PreLoop);
	hooks.Update(sgre::scene::LooperHookPhase::PostLoop);
	Require(meter.ProcessingCount() == 0 && cache.Count() == 2 && releases.empty(), "native empty hook callbacks must neither increment the process counter nor retire physical font ownership");

	sgre::scene::ParallelTaskSet driver_parallel;
	const auto operations = meter.DriverTaskOperations();
	sgre::scene::ParallelTaskCallbacks root;
	root.Update = [&](auto) {
		operations.Update();
	};
	root.Draw = [&](auto, std::uint32_t argument) {
		operations.Draw(argument);
	};
	root.Release = [&](auto) {
		operations.Retire();
	};
	sgre::scene::ParallelTaskState state;
	state.DrawPaused = sgre::scene::ResourceMeterProcess::DefaultDrawPaused();
	const auto task = driver_parallel.Append(std::move(root), state);
	driver_parallel.Update();
	driver_parallel.Draw(0xfedcba98);
	Require(meter.ProcessingCount() == 1 && releases.empty(), "actual default Driver pause must suppress the native Meter Draw callback without a renderer substitute");
	driver_parallel.SetDrawPaused(task, false);
	RequireFailure([&]() {
		driver_parallel.Draw(7);
	},
	    "cached-width170");
	Require(meter.ProcessingCount() == 1 && cache.Count() == 2 && meter.CanClose(), "unsupported unpaused drawing must reject before changing the genuine owner");
	driver_parallel.SetDrawPaused(task, true);
	driver_parallel.Clear();
	Require(meter.IsClosed() && cache.Count() == 0 && releases == std::vector<std::uint32_t>{1, 2} && document_lifetime.expired() && backing_lifetime.expired(), "Driver retirement must close the same actual font/clock owner once without an extra PSB/resource root");
	catalog.Prune();
	Require(catalog.DocumentCount() == 0 && hooks.CanClose(), "no hidden catalog or hook membership must survive");
	hooks.Close();
}

static void TestAllOwnerPreflightBeforeAnyMutation() {
	std::uint32_t uploads = 0;
	std::uint32_t released = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++released;
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	sgre::scene::LooperHookTasks hooks;
	auto asset = catalog.Prepare(Document(), Backing());
	sgre::scene::ResourceMeterProcess meter(hooks, cache, asset, {});
	sgre::scene::ParallelTaskCallbacks observer;
	std::uint32_t calls = 0;
	observer.Update = [&](auto) {
		const auto before_pre = hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count();
		const auto before_post = hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count();
		Require(!meter.CanClose(), "active actual PostLoop traversal must reject owner retirement");
		RequireFailure([&]() {
			meter.Close();
		},
		    "preflights before mutation");
		Require(hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == before_pre && hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == before_post && cache.Count() == 2, "rejected PostLoop preflight must precede any earlier PreLoop or font mutation");
		++calls;
	};
	const auto observer_task = hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Append(std::move(observer));
	hooks.Update(sgre::scene::LooperHookPhase::PostLoop);
	Require(calls == 1 && !hooks.CanClose(), "the real containing groups must not clear live hook parents");
	RequireFailure([&]() {
		hooks.Close();
	},
	    "hook parents must retire");
	bool foreign_preflight = true;
	bool foreign_rejected = false;
	std::thread foreign([&]() {
		foreign_preflight = meter.CanClose();
		try {
			meter.Close();
		} catch (const std::runtime_error&) {
			foreign_rejected = true;
		}
	});
	foreign.join();
	Require(!foreign_preflight && foreign_rejected && hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 1 && cache.Count() == 2, "foreign retirement must reject before touching actual owner lists");
	auto observer_lease = hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Extract(observer_task);
	observer_lease.Release();
	meter.Close();
	Require(released == 2, "accepted retirement must dispose both actual font sources");
	hooks.Close();
}

static void TestTransferredClockAndFailureCompletingFontRetirement() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::LooperHookTasks hooks;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto handle) {
		    Require(hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 0 && hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == 0, "font failure may occur only after both clock removals");
		    releases.push_back(handle.Index);
		    if (handle.Index == 1) {
			    throw std::runtime_error("observed font release failure");
		    }
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	auto asset = catalog.Prepare(Document(), Backing());
	sgre::scene::ResourceMeterProcess meter(hooks, cache, asset, {});
	auto clock = hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Extract(OnlyTask(hooks.Group(sgre::scene::LooperHookPhase::PostLoop)));
	Require(!meter.CanClose(), "detached clock callbacks still borrow the actual Meter parent");
	RequireFailure([&]() {
		meter.Close();
	},
	    "preflights before mutation");
	Require(hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 1 && cache.Count() == 2, "transferred clock rejection must preserve earlier clock and actual font owners");
	clock.Release();
	RequireFailure([&]() {
		meter.Close();
	},
	    "observed font release failure");
	Require(meter.IsClosed() && releases == std::vector<std::uint32_t>{1, 2} && cache.Count() == 0 && hooks.CanClose(), "real font cleanup must complete later sources/Common before reporting its first failure");
	meter.Close();
	hooks.Close();
}

static void TestConstructorRollbackAndDwordCounterWrap() {
	std::uint32_t uploads = 0;
	std::uint32_t releases = 0;
	sgre::scene::LooperHookTasks hooks(1);
	const auto occupied = hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Append({});
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    Require(hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 0 && hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == 1, "failed second append must retire the first borrowed clock before its font");
		    ++releases;
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	auto asset = catalog.Prepare(Document(), Backing());
	RequireFailure([&]() {
		sgre::scene::ResourceMeterProcess meter(hooks, cache, asset, {});
	},
	    "capacity");
	Require(uploads == 2 && releases == 2 && cache.Count() == 0 && hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 0 && hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == 1, "failed construction must leave no callback capturing its retired object");
	auto occupied_lease = hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Extract(occupied);
	occupied_lease.Release();
	hooks.Close();

	for (const auto input : std::array<std::uint32_t, 8>{0, 1, 0x7ffffffe, 0x7fffffff, 0x80000000, 0xfffffffd, 0xfffffffe, 0xffffffff}) {
		sgre::scene::ResourceMeterCounter counter;
		counter.Value = input;
		counter.Advance();
		const auto independent = (std::uint64_t(input) + 1) & 0xffffffff;
		Require(counter.Value == independent, "native DWORD counter update must wrap independently of signed host width and scheduling");
	}
}

static void TestSeparateEmbeddedDescriptorsAndSharedResidentBytes() {
	std::uint32_t uploads = 0;
	std::vector<std::uint32_t> releases;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto handle) {
		    releases.push_back(handle.Index);
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	sgre::scene::LooperHookTasks hooks;
	auto asset = catalog.Prepare(Document(), Backing());
	sgre::scene::ResourceMeterProcess first(hooks, cache, asset, {});
	sgre::scene::ResourceMeterProcess second(hooks, cache, asset, {});
	const auto& first_asset = first.FontTextures().Asset();
	const auto& second_asset = second.FontTextures().Asset();
	Require(first_asset.Document.get() != second_asset.Document.get() && first_asset.DocumentIdentity != second_asset.DocumentIdentity && first_asset.Backing.get() == second_asset.Backing.get() && first_asset.Backing.get() == asset->Backing.get(), "each genuine embedded constructor must own a distinct referring PSB descriptor with the same resident backing");
	const auto first_source = first.FontTextures().Source(0);
	const auto second_source = second.FontTextures().Source(0);
	Require(uploads == 4 && cache.Count() == 4 && first_source.Texture() != second_source.Texture() && !first_source.SameIdentity(second_source), "separate embedded PSB descriptors must not silently share another constructor's physical font identity");
	first.Close();
	Require(!first_source.IsLive() && second_source.IsLive() && cache.Count() == 2 && hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 1 && hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == 1, "first Meter retirement must preserve the other actual owner and its remaining hooks/textures");
	second.Update();
	Require(second.ProcessingCount() == 1, "the remaining real Common owner must continue after independent sibling retirement");
	second.Close();
	Require(releases == std::vector<std::uint32_t>{1, 2, 3, 4} && hooks.CanClose(), "both embedded font providers must retain source order and retire only their own actual identities");
	hooks.Close();
}

static void TestBorrowedDriverCallbackExpiryWithoutParentRoot() {
	std::uint32_t uploads = 0;
	std::uint32_t released = 0;
	sgre::scene::LooperHookTasks hooks;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    ++released;
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	sgre::scene::DefaultDriverOwnedTaskOperations operations;
	std::weak_ptr<const sgre::resource::PsbDocument> document_lifetime;
	std::weak_ptr<const std::vector<std::uint8_t>> backing_lifetime;
	{
		auto document = Document();
		auto backing = Backing();
		document_lifetime = document;
		backing_lifetime = backing;
		auto asset = catalog.Prepare(document, backing);
		sgre::scene::ResourceMeterProcess meter(hooks, cache, asset, {});
		document_lifetime = meter.FontTextures().Asset().Document;
		operations = meter.DriverTaskOperations();
		asset.reset();
		document.reset();
		backing.reset();
		operations.Update();
		Require(meter.ProcessingCount() == 1 && !document_lifetime.expired() && !backing_lifetime.expired(), "live driver callback must borrow this exact parent and its existing backing");
	}
	Require(released == 2 && cache.Count() == 0 && document_lifetime.expired() && backing_lifetime.expired() && hooks.CanClose(), "copied driver callbacks must add no Meter or PSB/resource root");
	for (const auto operation : {0, 1, 2}) {
		RequireFailure([&]() {
			if (operation == 0) {
				operations.Update();
			} else if (operation == 1) {
				operations.Draw(0);
			} else {
				operations.Retire();
			}
		},
		    "parent has expired");
	}
	hooks.Close();
}

static void ExitAfterExpectedResourceMeterDestruction() {
	std::_Exit(0);
}

static void ExitAfterIsolatedResourceMeterDestruction() {
	std::_Exit(86);
}

static void ExpectDetachedClockDestructorFailure(bool verify) {
	std::uint32_t uploads = 0;
	sgre::scene::LooperHookTasks hooks;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    --uploads;
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	auto asset = catalog.Prepare(Document(), Backing());
	auto meter = std::make_unique<sgre::scene::ResourceMeterProcess>(hooks, cache, asset, sgre::scene::ResourceMeterViewport{});
	auto clock = hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Extract(OnlyTask(hooks.Group(sgre::scene::LooperHookPhase::PostLoop)));
	Require(!clock.IsEmpty() && !meter->CanClose() && uploads == 2 && hooks.Group(sgre::scene::LooperHookPhase::PreLoop).Count() == 1, "detached destructor fixture must reach the exact outstanding callback borrow");

	// Arm only after every fixture assertion; ordinary return is an unexpected failure.
	std::set_terminate(verify ? ExitAfterExpectedResourceMeterDestruction : ExitAfterIsolatedResourceMeterDestruction);
	meter.reset();
	throw std::runtime_error("detached callback destructor unexpectedly returned");
}

static void ExpectActiveDriverCallbackDestructorFailure(bool verify) {
	std::uint32_t uploads = 0;
	sgre::scene::LooperHookTasks hooks;
	std::unique_ptr<sgre::scene::ResourceMeterProcess> meter;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++uploads, 1, &uploads};
	},
	    [&](auto) {
		    meter.reset();
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	auto asset = catalog.Prepare(Document(), Backing());
	meter = std::make_unique<sgre::scene::ResourceMeterProcess>(hooks, cache, asset, sgre::scene::ResourceMeterViewport{});
	const auto operations = meter->DriverTaskOperations();
	Require(meter->CanClose() && uploads == 2 && hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Count() == 1, "active callback destructor fixture must reach the genuine ready retirement state");

	std::set_terminate(verify ? ExitAfterExpectedResourceMeterDestruction : ExitAfterIsolatedResourceMeterDestruction);
	operations.Retire();
	throw std::runtime_error("active callback destructor unexpectedly returned");
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--destroy-with-detached-clock") {
			ExpectDetachedClockDestructorFailure(false);
		} else if (argc == 2 && std::string_view(argv[1]) == "--destroy-active-driver-callback") {
			ExpectActiveDriverCallbackDestructorFailure(false);
		} else if (argc == 2 && std::string_view(argv[1]) == "--verify-destroy-with-detached-clock") {
			ExpectDetachedClockDestructorFailure(true);
		} else if (argc == 2 && std::string_view(argv[1]) == "--verify-destroy-active-driver-callback") {
			ExpectActiveDriverCallbackDestructorFailure(true);
		} else if (argc != 1) {
			throw std::runtime_error("unsupported ResourceMeter fixture arguments");
		}
		TestRealFontCommonHooksAndDefaultDriverPause();
		TestAllOwnerPreflightBeforeAnyMutation();
		TestTransferredClockAndFailureCompletingFontRetirement();
		TestConstructorRollbackAndDwordCounterWrap();
		TestBorrowedDriverCallbackExpiryWithoutParentRoot();
		TestSeparateEmbeddedDescriptorsAndSharedResidentBytes();
		std::cout << "six actual Meter/font/Common/hook/paused-owner/expiry/descriptor groups pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
