#include "motion_receiver_fixture.hpp"

#include "sgre/core/application_clock_pump.hpp"
#include "sgre/core/application_event_control.hpp"
#include "sgre/core/looper_frame.hpp"
#include "sgre/platform/application_pump.hpp"
#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/graphics_display_frame.hpp"
#include "sgre/scene/serial_task_set.hpp"

#include "sprite_presentation_fixture.hpp"

#include <algorithm>
#include <atomic>
#include <optional>

enum class DisplayTestPhase {
	Pre,
	Update,
	WorkerPost,
	Draw,
	Post,
	AfterPresent
};

static std::shared_ptr<const sgre::resource::MotionResource> LongClockResource() {
	auto resource = std::make_shared<sgre::resource::MotionResource>(*Resource());
	// Authored test tracks remain visible across genuine host startup/drawable waits; game timing is not changed.
	static constexpr float kMiddleTime = 60000.0f;
	static constexpr float kLastTime = 120000.0f;
	for (auto& object : resource->Objects) {
		for (auto& timeline : object.Timelines) {
			timeline.LastTime = kLastTime;
			timeline.Layers.front().Frames[1].Time = kMiddleTime;
			timeline.Layers.front().Frames[2].Time = kLastTime;
			timeline.Priority[1].Time = kLastTime;
		}
	}

	return resource;
}

struct ClockSceneFixture {
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionResourceManager Manager;
	std::shared_ptr<const sgre::resource::MotionResource> Model = LongClockResource();
	std::array<std::uint8_t, 16> Bytes;
	sgre::scene::MotionProjectIdentity Identity;
	sgre::scene::MotionProjectId Project;
	const sgre::core::FrameClock& ActualClock;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed;
	std::vector<sgre::scene::MotionSourceAlias> Aliases;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	std::vector<std::unique_ptr<PlayerFixture>> Players;

	ClockSceneFixture(sgre::render::SpritePassRenderer& renderer, const sgre::core::FrameClock& actual_clock) : Cache(Upload(renderer), Release(renderer)), Manager(Cache), ActualClock(actual_clock) {
		Bytes.fill(255);
		for (std::size_t index = 0; index < Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index + 1);
		}
		Identity.DocumentInstance = Cache.AllocateDocumentIdentity();
		Identity.RootOffset = 100;
		Project = Manager.Register(Identity, Model, Bytes);
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project && Manager.Contains(project), "actual clock alias query lost its registered source owner");

			return std::span<const sgre::scene::MotionSourceAlias>(Aliases);
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, ActualClock, Seed, std::move(services), 8);
	}

	~ClockSceneFixture() noexcept {
		try {
			Runtime->Context().Clear();
			Players.clear();
			Runtime->Close();
			Manager.Close();
			Cache.Close();
		} catch (...) {
			std::terminate();
		}
	}

	PlayerFixture& Add(std::string_view character, sgre::resource::MotionPoint position, std::uint32_t filter = 0) {
		Players.push_back(std::make_unique<PlayerFixture>(*Runtime, character, position, filter));

		return *Players.back();
	}
};

struct ActualDisplayRoot {
	ClockSceneFixture* Scene = nullptr;
	PlayerFixture* Red = nullptr;
	PlayerFixture* Blue = nullptr;
	PlayerFixture* Unsupported = nullptr;
	Scratch ScratchSpace;
	std::vector<DisplayTestPhase> Trace;
	std::vector<float> ConsumedDeltas;
	std::vector<float> ActualClockSteps;
	std::vector<float> AdvancedDeltas;
	std::atomic<std::size_t> WorkerUpdates = 0;
	std::size_t WorkerPosts = 0;
	std::size_t Updates = 0;
	std::size_t Draws = 0;
	std::size_t Releases = 0;
	std::uint64_t WorkerRawCounter = 0;
	sgre::render::GraphicsDisplayFrame* Display = nullptr;
	sgre::core::LooperFrame* Looper = nullptr;
	sgre::platform::MacApplicationPump* Platform = nullptr;
	sgre::core::ApplicationClockPump* Clock = nullptr;
	sgre::core::ThreadTaskReceiver* Worker = nullptr;
	bool ThrowAfterQueue = false;
	bool ThrowDuringDraw = false;
	bool CheckDrawReentry = false;

	ActualDisplayRoot() {
		Trace.reserve(64);
		ConsumedDeltas.reserve(16);
		ActualClockSteps.reserve(16);
		AdvancedDeltas.reserve(16);
	}

	void InstallScene(ClockSceneFixture& actual_scene) {
		Require(Scene == nullptr && Red == nullptr && Blue == nullptr && Unsupported == nullptr, "typed root scene installation requires its actual unbound state");
		Scene = &actual_scene;
		Red = &Scene->Add("red", {-4, -4});
		Blue = &Scene->Add("blue", {0, -4});
		Unsupported = &Scene->Add("red", {-4, -4}, 1);
	}

	void RequirePhase(DisplayTestPhase expected) const {
		Require(!Trace.empty() && Trace.back() == expected, "real Display/Looper callback order differs from its containing phase");
	}

	sgre::scene::SerialTaskCallbacks Callbacks() {
		sgre::scene::SerialTaskCallbacks callbacks;
		callbacks.Update = [this](auto& actual_root) {
			Require(Scene != nullptr && Red != nullptr && Blue != nullptr && Display != nullptr && Looper != nullptr && Worker != nullptr && !actual_root.IsClosed() && !Looper->InWait(), "actual typed root Update lost its live containing owners");
			Require(&Scene->ActualClock == &Looper->Clock(), "actual selected runtime clock differs from the enclosing typed Looper");
			const auto expected_delta = Looper->Clock().VariableFrame() ? std::max(Looper->Clock().VariableStep(), 1.0f) : 1.0f;
			const auto consumed_delta = Scene->Runtime->ReadFrameDelta();
			Require(consumed_delta == expected_delta, "actual selected runtime did not consume its exact Looper clock delta");
			if (!AdvancedDeltas.empty()) {
				Require(consumed_delta == AdvancedDeltas.back(), "selected root Update did not consume the previous genuine OS clock sample");
			}
			ConsumedDeltas.push_back(consumed_delta);
			ActualClockSteps.push_back(Looper->Clock().VariableStep());
			RequirePhase(DisplayTestPhase::Pre);
			Require(!Display->Cycle().LastSubmission(), "actual Display Pre failed to invalidate the prior completed frame");
			Trace.push_back(DisplayTestPhase::Update);
			++Updates;
			Red->Player->Update();
			Blue->Player->Update();
			Worker->QueueUpdate();
			Worker->QueuePost();
			if (ThrowAfterQueue) {
				throw std::runtime_error("actual test root failed after queueing its genuine receiver");
			}
		};
		callbacks.Draw = [this](auto& actual_root, auto argument) {
			Require(Display != nullptr && !actual_root.IsClosed() && argument == 0, "actual typed root Draw lost its exact argument or Display owner");
			RequirePhase(DisplayTestPhase::WorkerPost);
			Require(WorkerUpdates.load() == Updates && WorkerPosts == Updates && Worker->CanClose() && WorkerRawCounter != 0, "actual root drawing preceded real worker/Post completion");
			Display->BeginRootDraw();
			Display->Cycle().ResetRootCamera();
			if (CheckDrawReentry) {
				RequireFailure([this] {
					static_cast<void>(Display->RunIteration());
				},
				    "inactive iteration");
				RequireFailure([this] {
					Platform->Present(false);
				},
				    "quiescent successful live encoding");
				RequireFailure([this] {
					Platform->UnbindPresentation();
				},
				    "inactive iteration");
				RequireFailure([this] {
					Display->Close();
				},
				    "detach first");
			}
			Red->Player->Draw();
			Display->Cycle().Draw(Scene->Runtime->Context(), ScratchSpace.Borrow());
			if (ThrowDuringDraw) {
				Unsupported->Player->Draw();
				Display->Cycle().Draw(Scene->Runtime->Context(), ScratchSpace.Borrow());
				throw std::runtime_error("actual unsupported packet unexpectedly completed its Draw");
			}
			Blue->Player->Draw();
			Display->Cycle().Draw(Scene->Runtime->Context(), ScratchSpace.Borrow());
			++Draws;
			Trace.push_back(DisplayTestPhase::Draw);
		};
		callbacks.CanRelease = [this] {
			return Worker != nullptr && Worker->CanClose() && (Scene == nullptr || (Scene->Runtime->ResourceBorrowsLive() && Scene->Runtime->Context().Size() == 0));
		};
		callbacks.Release = [this] {
			Require(Worker->CanClose() && (Scene == nullptr || (Scene->Runtime->ResourceBorrowsLive() && Scene->Runtime->Context().Size() == 0)), "actual root retirement preceded its concrete worker/context dependencies");
			++Releases;
		};

		return callbacks;
	}
};

struct DisplayTestCleanup {
	ActualDisplayRoot& Root;
	sgre::core::LooperFrame& Looper;
	sgre::core::ApplicationClockPump& Clock;
	sgre::platform::MacApplicationPump& Platform;
	sgre::core::ThreadTaskFrame& Workers;
	sgre::core::ThreadTaskReceiver& Worker;
	std::unique_ptr<sgre::render::GraphicsDisplayFrame>& Display;
	std::unique_ptr<ClockSceneFixture>& Scene;
	sgre::render::SpritePassPresentation& Surface;
	bool* ReleaseVeto = nullptr;
	bool Closed = false;

	void Close() {
		if (Closed) {
			return;
		}
		sgre::render::SpritePassPresentationFixtureBridge::SetSystemHook(Surface, {});
		Clock.ClearAuxiliary();
		Platform.UnbindPresentation();
		if (Scene) {
			Scene->Runtime->Context().Clear();
		}
		// Only these bounded authored test jobs are drained for exception cleanup; no game work is canceled or fabricated.
		if (!Worker.CanClose()) {
			Workers.RunFrame();
		}
		if (Display) {
			Display->Cycle().AbortFrame();
			Display->Close();
			Display.reset();
			Root.Display = nullptr;
		}
		if (ReleaseVeto != nullptr) {
			*ReleaseVeto = true;
		}
		Looper.Hooks().Group(sgre::scene::LooperHookPhase::PostLoop).Clear();
		Looper.Hooks().Group(sgre::scene::LooperHookPhase::PreLoop).Clear();
		Looper.ReleaseTask();
		Scene.reset();
		Root.Scene = nullptr;
		Root.Red = nullptr;
		Root.Blue = nullptr;
		Root.Unsupported = nullptr;
		Looper.Close();
		Clock.Close();
		Platform.Close();
		Worker.Close();
		Workers.Close();
		Closed = true;
	}

	~DisplayTestCleanup() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}
};

static sgre::scene::ParallelTaskCallbacks ObservePre(ActualDisplayRoot& root) {
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&root](auto) {
		Require(root.Display != nullptr && !root.Looper->InWait() && !root.Display->Cycle().LastSubmission(), "actual registered Pre observer ran before Display preparation or during wait");
		Require(root.Display->Cycle().CurrentCounters() == sgre::render::RenderFrameCounters{}, "real Pre failed to clear its contained record counters");
		root.Trace.push_back(DisplayTestPhase::Pre);
	};

	return callbacks;
}

static sgre::scene::ParallelTaskCallbacks ObservePost(ActualDisplayRoot& root) {
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&root](auto) {
		Require(root.Display != nullptr && !root.Looper->InWait() && root.Display->Cycle().LastSubmission().has_value(), "actual registered Post observer lacked the completed real frame before OS wait");
		Require(!root.Trace.empty() && (root.Trace.back() == DisplayTestPhase::Draw || root.Trace.back() == DisplayTestPhase::WorkerPost), "actual Post did not follow real drawn or draw-paused root work");
		root.Trace.push_back(DisplayTestPhase::Post);
	};

	return callbacks;
}

static void RequireHidden(const sgre::render::SpritePassPresentation& surface) {
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observed.ApplicationActive && !observed.WindowVisible && !observed.WindowKey && !observed.WindowMain && !observed.Presenting && observed.ViewAttached && observed.LayerAttached, "actual OS/Display join changed hidden window, activation or target ownership");
}

static void TestActualOSDisplay(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*renderer, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&counter] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	std::unique_ptr<ClockSceneFixture> scene_owner;
	ActualDisplayRoot actual_root;
	sgre::core::ThreadTaskReceiver worker(workers, [&](auto& actual_receiver) {
		Require(&actual_receiver == actual_root.Worker, "queued worker Update belongs to another concrete receiver");
		actual_receiver.RequireExecutingUpdate();
		actual_root.WorkerRawCounter = counter.ReadRawCounter();
		++actual_root.WorkerUpdates;
	},
	    [&](auto& actual_receiver) {
		    Require(&actual_receiver == actual_root.Worker && actual_root.WorkerUpdates.load() == actual_root.Updates, "live worker Post preceded the exact Update completion fence");
		    ++actual_root.WorkerPosts;
		    actual_root.Trace.push_back(DisplayTestPhase::WorkerPost);
	    });
	actual_root.Worker = &worker;
	sgre::core::ApplicationEventControl events;
	sgre::platform::MacApplicationPump platform(events, surface);
	sgre::core::ApplicationClockPump clock(events, platform);
	sgre::scene::SerialTask root(actual_root.Callbacks());
	sgre::core::LooperFrame looper(workers, root, clock, 4);
	actual_root.Looper = &looper;
	actual_root.Platform = &platform;
	actual_root.Clock = &clock;
	std::unique_ptr<sgre::render::GraphicsDisplayFrame> display;
	DisplayTestCleanup cleanup{actual_root, looper, clock, platform, workers, worker, display, scene_owner, surface};
	looper.Clock().SetVariableFrame(true);
	scene_owner = std::make_unique<ClockSceneFixture>(*renderer, looper.Clock());
	actual_root.InstallScene(*scene_owner);
	auto& scene = *scene_owner;
	auto& red = *actual_root.Red;
	display = std::make_unique<sgre::render::GraphicsDisplayFrame>(looper, graphics, surface, workers, scene.Cache, sgre::render::RenderFrameCounters{}, sgre::render::RenderFrameCounters{});
	actual_root.Display = display.get();
	const auto& row = *red.Player->PublishedRows()[0];
	display->Cycle().BindTexture(row.Slot.Index, row.Texture);
	static_cast<void>(looper.Hooks().Group(sgre::scene::LooperHookPhase::PreLoop).Append(ObservePre(actual_root)));
	static_cast<void>(looper.Hooks().Group(sgre::scene::LooperHookPhase::PostLoop).Append(ObservePost(actual_root)));
	RequireFailure([&] {
		platform.Present(false);
	},
	    "current-frame producer");
	platform.BindPresentation(*display, clock);
	RequireFailure([&] {
		platform.Present(false);
	},
	    "actual attached Looper wait");
	RequireFailure([&] {
		platform.BindPresentation(*display, clock);
	},
	    "unbound dispatcher");
	RequireFailure([&] {
		display->Close();
	},
	    "detach first");
	RequireFailure([&] {
		clock.Close();
	},
	    "retired Looper/presentation borrowers");
	RequireHidden(surface);
	std::size_t auxiliary_calls = 0;
	clock.SetAuxiliary([&](bool suspended) {
		Require(!suspended && looper.InWait(), "real host auxiliary observation did not execute in active Looper wait");
		actual_root.RequirePhase(DisplayTestPhase::Post);
		Require(&scene.ActualClock == &looper.Clock() && looper.Clock().VariableFrame(), "genuine OS clock advance lost the selected runtime's exact variable-mode owner");
		const auto advanced_delta = scene.Runtime->ReadFrameDelta();
		Require(advanced_delta == std::max(looper.Clock().VariableStep(), 1.0f), "selected runtime did not observe the real host-driven cached variable clock step");
		actual_root.AdvancedDeltas.push_back(advanced_delta);
		Require(clock.Pacing().CanClose(), "actual OS Present did not finish its genuine pacing/lateness cycle");
		RequireFailure([&] {
			platform.Present(false);
		},
		    "actual attached Looper wait");
		RequireFailure([&] {
			platform.UnbindPresentation();
		},
		    "quiescent platform borrower");
		actual_root.Trace.push_back(DisplayTestPhase::AfterPresent);
		++auxiliary_calls;
	},
	    [&] {
		    platform.RequireLive();
		    clock.RequireAttachment(looper, platform);
		    Require(scene.Runtime->ResourceBorrowsLive(), "actual auxiliary admission lost its resource owner");
	    });
	std::size_t policy_calls = 0;
	std::size_t transfer_calls = 0;
	std::size_t commit_calls = 0;
	sgre::render::SpritePassPresentationFixtureBridge::SetSystemHook(surface, [&](auto operation, auto moment) {
		if (moment != sgre::render::PresentationFixtureMoment::Before) {
			return;
		}
		Require(looper.InWait() && actual_root.Trace.back() == DisplayTestPhase::Post && display->Cycle().LastSubmission().has_value(), "real surface system operation preceded Display Post or its actual platform Present call");
		if (operation == sgre::render::PresentationSystemOperation::Policy) {
			++policy_calls;
		} else if (operation == sgre::render::PresentationSystemOperation::Transfer) {
			++transfer_calls;
		} else if (operation == sgre::render::PresentationSystemOperation::Commit) {
			++commit_calls;
		}
		RequireFailure([&] {
			static_cast<void>(display->RunIteration());
		},
		    "inactive iteration");
		RequireFailure([&] {
			platform.Present(false);
		},
		    "recursive actual transfer");
	});
	actual_root.CheckDrawReentry = true;
	const auto initial = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(display->RunIteration(), "actual typed root unexpectedly retired its first OS/Display frame");
	const auto drawn = display->Cycle().LastSubmission();
	Require(drawn && drawn->Owner == renderer.get() && auxiliary_calls == 1 && looper.FrameNumber() == 1, "actual completed frame or OS clock observation was not published exactly once");
	Require(actual_root.Trace == std::vector<DisplayTestPhase>{DisplayTestPhase::Pre, DisplayTestPhase::Update, DisplayTestPhase::WorkerPost, DisplayTestPhase::Draw, DisplayTestPhase::Post, DisplayTestPhase::AfterPresent}, "concrete Pre/Update/workers/root Draw/Post/OS wait sequence differs");
	const auto pixels = renderer->Capture(drawn->Identity);
	RequirePixel(pixels, 1, 1, {255, 0, 0, 255});
	RequirePixel(pixels, 5, 1, {0, 0, 255, 255});
	const auto first_present = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(first_present.DrawableRequests == initial.DrawableRequests + 1, "real OS pump did not request exactly one actual hidden drawable");
	Require(commit_calls <= 1 && transfer_calls == commit_calls, "actual transfer/commit observations disagree for the genuine drawable outcome");
	std::cout << "OBSERVATION first hidden OS Present: drawable requests=" << first_present.DrawableRequests << ", transfer calls=" << transfer_calls << ", commit calls=" << commit_calls << ", retained transfers=" << first_present.RetainedTransfers << ", policy calls=" << policy_calls << '\n';
	RequireHidden(surface);
	platform.UnbindPresentation();
	{
		// This is another actual typed Looper and clock on the existing Mac counter owner, never a replacement pump.
		sgre::core::ApplicationClockPump other_clock(events, platform);
		ActualDisplayRoot other_actual_root;
		other_actual_root.Scene = &scene;
		other_actual_root.Red = actual_root.Red;
		other_actual_root.Blue = actual_root.Blue;
		other_actual_root.Unsupported = actual_root.Unsupported;
		other_actual_root.Worker = &worker;
		sgre::scene::SerialTask other_root(other_actual_root.Callbacks());
		sgre::core::LooperFrame other_looper(workers, other_root, other_clock, 2);
		other_actual_root.Looper = &other_looper;
		try {
			RequireFailure([&] {
				platform.BindPresentation(*display, other_clock);
			},
			    "exact attached Looper");
			RequireFailure([&] {
				clock.RequireAttachment(other_looper, platform);
			},
			    "exact attached Looper");
		} catch (...) {
			const auto first = std::current_exception();
			other_looper.ReleaseTask();
			other_looper.Close();
			other_clock.Close();
			std::rethrow_exception(first);
		}
		other_looper.ReleaseTask();
		other_looper.Close();
		other_clock.Close();
		Require(other_root.IsClosed() && other_actual_root.Releases == 1 && other_clock.CanClose(), "rejected cross-Looper binding published an unexpected reciprocal clock borrower");
	}
	platform.BindPresentation(*display, clock);

	actual_root.Trace.clear();
	root.SetDrawPaused(true);
	Require(display->RunIteration(), "actual draw-paused root unexpectedly retired");
	const auto paused = display->Cycle().LastSubmission();
	Require(paused && paused->Identity > drawn->Identity && actual_root.Draws == 1 && auxiliary_calls == 2, "actual draw-paused Post did not produce a new genuine clear and OS cycle");
	Require(actual_root.Trace == std::vector<DisplayTestPhase>{DisplayTestPhase::Pre, DisplayTestPhase::Update, DisplayTestPhase::WorkerPost, DisplayTestPhase::Post, DisplayTestPhase::AfterPresent}, "paused root fabricated a Draw callback or skipped real worker/Post/OS phases");
	const auto cleared = renderer->Capture(paused->Identity);
	RequirePixel(cleared, 1, 1, {0, 0, 0, 255});
	RequirePixel(cleared, 5, 1, {0, 0, 0, 255});
	RequireHidden(surface);
	root.SetDrawPaused(false);
	actual_root.CheckDrawReentry = false;
	actual_root.Trace.clear();
	actual_root.ThrowDuringDraw = true;
	const auto requests_before_draw_failure = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface).DrawableRequests;
	RequireFailure([&] {
		static_cast<void>(display->RunIteration());
	},
	    "color-filter execution remains unsupported");
	Require(!looper.InWait() && !display->Cycle().LastSubmission() && clock.Pacing().CanClose() && looper.FrameNumber() == 2, "failed genuine draw published a completed frame or left actual wait/pacing active");
	Require(sgre::render::SpritePassPresentationFixtureBridge::Observe(surface).DrawableRequests == requests_before_draw_failure, "failed actual root Draw reached OS Present");
	scene.Runtime->Context().Clear();
	display->Cycle().AbortFrame();
	actual_root.ThrowDuringDraw = false;
	actual_root.Trace.clear();
	actual_root.ThrowAfterQueue = true;
	RequireFailure([&] {
		static_cast<void>(display->RunIteration());
	},
	    "failed after queueing");
	Require(worker.State().UpdatePending && worker.State().PostPending && !display->Cycle().LastSubmission() && !looper.InWait(), "failed actual Update drained its concrete pending receiver or published a nominal frame");
	workers.RunFrame();
	display->Cycle().AbortFrame();
	actual_root.ThrowAfterQueue = false;
	actual_root.Trace.clear();
	Require(display->RunIteration(), "actual OS/Display owner failed to recover after explicit bounded test-job cleanup");
	Require(auxiliary_calls == 3 && looper.FrameNumber() == 3, "recovered actual OS cycle counted an earlier failed frame");
	Require(actual_root.ConsumedDeltas.size() == 5 && actual_root.ActualClockSteps.size() == 5 && actual_root.AdvancedDeltas.size() == 3, "actual OS/Looper/Motion timing observations lost a real root Update or completed wait");
	Require(actual_root.ConsumedDeltas.front() == 1.0f && actual_root.ActualClockSteps.front() == 0.0f && actual_root.ConsumedDeltas[1] == actual_root.AdvancedDeltas[0], "actual Motion timing did not preserve native initial clamp or consume the first real host sample");
	std::cout << "OBSERVATION actual OS/Looper/Motion consumed deltas:";
	for (const auto delta : actual_root.ConsumedDeltas) {
		std::cout << ' ' << delta;
	}
	std::cout << '\n';
	RequireHidden(surface);
	sgre::render::SpritePassPresentationFixtureBridge::SetSystemHook(surface, {});
	clock.ClearAuxiliary();
	platform.UnbindPresentation();
	platform.Close();
	// A second real Mac dispatcher can borrow this same hidden window only after the first retires.
	{
		sgre::platform::MacApplicationPump other_platform(events, surface);
		RequireFailure([&] {
			other_platform.BindPresentation(*display, clock);
		},
		    "quiescent attached clock");
		RequireFailure([&] {
			clock.RequireAttachment(looper, other_platform);
		},
		    "exact attached Looper and platform");
		other_platform.Close();
	}
	cleanup.Close();
	Require(root.IsClosed() && actual_root.Releases == 1 && clock.CanClose(), "reciprocal presentation and clock/root retirement did not complete exactly once");
	events.Close();
}

static sgre::scene::ParallelTaskCallbacks VetoedClock(std::size_t& updates, std::size_t& releases, bool& release_allowed) {
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&](auto) {
		++updates;
	};
	callbacks.CanRelease = [&](auto) {
		return release_allowed;
	};
	callbacks.Release = [&](auto) {
		Require(release_allowed, "unrelated actual clock bypassed its retained dependent borrow");
		++releases;
	};

	return callbacks;
}

static void TestFullPostConstructorRollback(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*renderer, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&counter] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	std::unique_ptr<ClockSceneFixture> scene_owner;
	ActualDisplayRoot actual_root;
	sgre::core::ThreadTaskReceiver worker(workers, [&](auto& actual_receiver) {
		actual_receiver.RequireExecutingUpdate();
		actual_root.WorkerRawCounter = counter.ReadRawCounter();
		++actual_root.WorkerUpdates;
	},
	    [&](auto&) {
		    ++actual_root.WorkerPosts;
	    });
	actual_root.Worker = &worker;
	sgre::core::ApplicationEventControl events;
	sgre::platform::MacApplicationPump platform(events, surface);
	sgre::core::ApplicationClockPump clock(events, platform);
	sgre::scene::SerialTask root(actual_root.Callbacks());
	sgre::core::LooperFrame looper(workers, root, clock, 2);
	actual_root.Looper = &looper;
	std::unique_ptr<sgre::render::GraphicsDisplayFrame> display;
	std::size_t unrelated_updates = 0;
	std::size_t unrelated_releases = 0;
	bool release_allowed = false;
	DisplayTestCleanup cleanup{actual_root, looper, clock, platform, workers, worker, display, scene_owner, surface, &release_allowed};
	looper.Clock().SetVariableFrame(true);
	scene_owner = std::make_unique<ClockSceneFixture>(*renderer, looper.Clock());
	actual_root.InstallScene(*scene_owner);
	auto& scene = *scene_owner;
	auto& pre = looper.Hooks().Group(sgre::scene::LooperHookPhase::PreLoop);
	auto& post = looper.Hooks().Group(sgre::scene::LooperHookPhase::PostLoop);
	const auto retained_pre = pre.Append(VetoedClock(unrelated_updates, unrelated_releases, release_allowed));
	const auto retained_post_first = post.Append(VetoedClock(unrelated_updates, unrelated_releases, release_allowed));
	const auto retained_post_second = post.Append(VetoedClock(unrelated_updates, unrelated_releases, release_allowed));
	Require(!pre.CanClear() && !pre.CanExtract(retained_pre) && post.Count() == 2, "actual constructor rollback fixture did not establish full Post and unrelated Pre release veto");
	const auto before = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	RequireFailure([&] {
		sgre::render::GraphicsDisplayFrame rejected(looper, graphics, surface, workers, scene.Cache, {}, {});
	},
	    "capacity is exhausted");
	Require(pre.Count() == 1 && post.Count() == 2 && pre.Contains(retained_pre) && post.Contains(retained_post_first) && post.Contains(retained_post_second), "failed actual Display registration altered unrelated physical clock ownership");
	Require(!pre.CanClear() && !pre.CanExtract(retained_pre) && unrelated_updates == 0 && unrelated_releases == 0, "target-only constructor rollback queried, released or invoked unrelated vetoed clocks");
	looper.RequireIdleContainingPhase();
	Require(workers.CanClose() == false && worker.CanClose(), "failed Display constructor changed its actual worker receiver ownership");
	const auto after = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(after.DrawableRequests == before.DrawableRequests && !after.Presenting, "failed actual constructor produced a drawable or live GPU frame");
	std::array<sgre::scene::ParallelTaskHandle, 2> order{};
	Require(pre.WriteOrder(order) == 1 && order[0] == retained_pre, "constructor rollback left its partial Pre registration in the physical list");
	Require(post.WriteOrder(order) == 2 && order[0] == retained_post_first && order[1] == retained_post_second, "constructor rollback changed the full actual Post list order");
	release_allowed = true;
	cleanup.Close();
	Require(unrelated_releases == 3 && actual_root.Releases == 1 && root.IsClosed(), "explicit rollback-fixture retirement lost an actual owner or callback");
	events.Close();
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "Display OS fixture requires actual sprite and transfer shader sources");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				@autoreleasepool {
					TestActualOSDisplay(argv[1], argv[2]);
				}
				@autoreleasepool {
					TestFullPostConstructorRollback(argv[1], argv[2]);
				}
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "actual OS/Display tests retained a window or changed foreground activation");
				std::cout << "PASS actual typed root/selected packets/worker fence/Display clocks/host OS wait and Present, contrasting Metal pixels and paused clear, exact reciprocal retirement, reentry/failure cleanup and target-only full-Post rollback; hidden and silent\n";
			}

			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';

			return 1;
		}
	} @catch (NSException* exception) {
		std::cerr << "Display OS fixture Objective-C exception: " << exception.reason.UTF8String << '\n';

		return 1;
	}
}
