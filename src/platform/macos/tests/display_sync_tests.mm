#include "sgre/render/presentation_synchronization.hpp"

#include "sgre/core/looper_frame.hpp"
#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/sprite_presentation.hpp"
#include "sprite_presentation_bridge.hpp"
#include "sprite_presentation_fixture.hpp"

#import <QuartzCore/CAMetalLayer.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sgre::render {

	class PresentationSynchronizationFixtureBridge {
	public:
		// CPU-only comparison of the private adoption store; no production presentation service is supplied.
		static void ObserveNativeAdoption(PresentationSynchronization& synchronization, const SpritePassPresentation& presentation) {
			synchronization.Presented(presentation);
		}
	};

} // namespace sgre::render

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}
	throw std::runtime_error("expected rejection: " + std::string(message));
}

static std::uint64_t Word(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::size_t count) {
	Require(count <= 8 && offset <= bytes.size() && count <= bytes.size() - offset, "bounded native synchronization word");
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < count; ++index) {
		value |= static_cast<std::uint64_t>(bytes[offset++]) << (index * 8);
	}

	return value;
}

static void TestNative(const std::filesystem::path& path, sgre::core::LooperFrame& looper, sgre::render::SpritePassPresentation& presentation) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	Require(input && input.tellg() == 6152, "DSY1 exact bounded native fixture length");
	std::vector<std::uint8_t> bytes(6152);
	input.seekg(0);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Require(input && std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "DSY1", "DSY1 native fixture header");
	std::size_t offset = 4;
	Require(Word(bytes, offset, 4) == 128, "DSY1 bounded native case count");
	sgre::render::PresentationSynchronization synchronization(looper, presentation);
	Require(synchronization.Current() == 1 && synchronization.Requested() == 1, "original display constructor stores");
	for (std::size_t index = 0; index < 128; ++index) {
		const auto request = static_cast<std::uint32_t>(Word(bytes, offset, 4));
		const auto force = Word(bytes, offset, 4);
		const auto before = Word(bytes, offset, 4);
		const auto after = Word(bytes, offset, 4);
		const auto interval = Word(bytes, offset, 4);
		const auto changes_during_present = Word(bytes, offset, 4);
		const auto callback_request = static_cast<std::uint32_t>(Word(bytes, offset, 4));
		const auto heap_count = Word(bytes, offset, 4);
		const auto heap_frame = Word(bytes, offset, 8);
		const auto native_allocation_sum = Word(bytes, offset, 8);
		Require(force <= 1 && changes_during_present <= 1 && heap_count >= 1 && heap_count <= 4, "DSY1 case domains");
		// The native heap observations remain expected-only evidence, without an authored GPU allocation producer.
		static_cast<void>(heap_frame);
		static_cast<void>(native_allocation_sum);
		Require(synchronization.Current() == before, "native current before request");
		synchronization.Request(request);
		Require(synchronization.Current() == before && synchronization.Requested() == request, "request preserves current word");
		Require(synchronization.Interval(force != 0) == interval, "native current/base/forced interval selection");
		if (changes_during_present != 0) {
			synchronization.Request(callback_request);
		}
		sgre::render::PresentationSynchronizationFixtureBridge::ObserveNativeAdoption(synchronization, presentation);
		Require(synchronization.Current() == after && synchronization.Requested() == after, "native private post-present adoption");
	}
	Require(offset == bytes.size(), "DSY1 no trailing native data");
}

static void Drain(sgre::render::SpritePassPresentation& presentation) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!presentation.PollTransfers()) {
		Require(std::chrono::steady_clock::now() < deadline, "actual hidden transfer bounded completion");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

class RealQueueGate {
public:
	explicit RealQueueGate(const sgre::render::MetalSpritePresentationSource& source) {
		m_Event = [source.Device newSharedEvent];
		m_Commands = [source.Queue commandBuffer];
		Require(m_Event != nil && m_Commands != nil, "actual GPU queue gate owners");
		[m_Commands encodeWaitForEvent:m_Event value:1];
		[m_Commands commit];
	}
	~RealQueueGate() {
		m_Event.signaledValue = 1;
		[m_Commands waitUntilCompleted];
	}

private:
	id<MTLSharedEvent> m_Event = nil;
	id<MTLCommandBuffer> m_Commands = nil;
};

static void TestActual(sgre::core::LooperFrame& looper, const std::filesystem::path& sprite_shader, const std::filesystem::path& transfer_shader, const std::filesystem::path& native_fixture) {
	const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
	sgre::render::SpritePassDescription description;
	description.Width = 8;
	description.Height = 8;
	description.MaxVertices = 16;
	description.MaxIndices = 16;
	description.MaxDraws = 4;
	auto renderer = std::make_unique<sgre::render::SpritePassRenderer>(description, sprite_shader);
	sgre::render::SpritePassPresentation presentation(*renderer, transfer_shader);
	TestNative(native_fixture, looper, presentation);
	sgre::render::PresentationSynchronization synchronization(looper, presentation);
	const auto objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(presentation);
	CAMetalLayer* layer = objects.Layer;
	Require(layer != nil && layer.displaySyncEnabled, "actual initial synchronized owned layer");
	const auto empty = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	synchronization.Request(0);
	RequireError([&] {
		static_cast<void>(presentation.TryPresentCurrent(synchronization, true));
	},
	    "actual initialized Submit");
	Require(sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation).DrawableRequests == empty.DrawableRequests && synchronization.Current() == 1 && layer.displaySyncEnabled, "missing frame rejects before drawable and synchronization mutation");
	const std::array<float, 16> projection{};
	const std::array<float, 4> clear{0.1F, 0.2F, 0.3F, 1};
	const auto first = renderer->Submit({}, projection, clear);
	const auto first_current = presentation.CurrentSubmission();
	Require(first_current.Owner == renderer.get() && first_current.Identity == first, "current identity comes from exact actual Submit owner");
	const auto latest = renderer->Submit({}, projection, clear);
	Require(presentation.CurrentSubmission().Identity == latest && latest != first, "new actual Submit replaces current identity");
	RequireError([&] {
		static_cast<void>(presentation.TryPresentLatest(first_current));
	},
	    "current presentation path");
	RequireError([&] {
		sgre::render::PresentationSynchronization duplicate(looper, presentation);
	},
	    "exclusive actual synchronization owner");
	Require(synchronization.Requested() == 0 && synchronization.Current() == 1, "failed duplicate registration preserves actual existing scalar owner");
	std::exception_ptr foreign_failure;
	std::thread foreign([&] {
		try {
			RequireError([&] {
				synchronization.Request(1);
			},
			    "owner thread");
			RequireError([&] {
				static_cast<void>(presentation.CurrentSubmission());
			},
			    "main thread");
		} catch (...) {
			foreign_failure = std::current_exception();
		}
	});
	foreign.join();
	if (foreign_failure) {
		std::rethrow_exception(foreign_failure);
	}
	const std::array<std::uint32_t, 4> requests{0, 1, 1, 1};
	const std::array<bool, 4> forced{false, false, true, false};
	const std::array<bool, 4> expected_layer{true, false, false, true};
	for (std::size_t index = 0; index < requests.size(); ++index) {
		synchronization.Request(requests[index]);
		Require(presentation.TryPresentCurrent(synchronization, forced[index]) == sgre::render::SpritePresentationStatus::QueuedHidden, "actual fixture requires genuine hidden queued drawable");
		Require(synchronization.Current() == requests[index] && static_cast<bool>(layer.displaySyncEnabled) == expected_layer[index], "current/request/actual layer interval ordering");
		Drain(presentation);
	}
	const auto source = sgre::render::SpritePassPresentationBridge::CreateSource(*renderer);
	{
		RealQueueGate gate(source);
		for (std::size_t index = 0; index < 3; ++index) {
			Require(presentation.TryPresentCurrent(synchronization, false) == sgre::render::SpritePresentationStatus::QueuedHidden, "three genuine blocked transfers");
		}
		const auto before = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
		synchronization.Request(0);
		Require(presentation.TryPresentCurrent(synchronization, true) == sgre::render::SpritePresentationStatus::Busy, "actual bounded slots report Busy");
		Require(synchronization.Current() == 1 && layer.displaySyncEnabled && sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation).DrawableRequests == before.DrawableRequests, "Busy restores layer and current without drawable request");
	}
	Drain(presentation);
	@autoreleasepool {
		NSMutableArray<id<CAMetalDrawable>>* held = [NSMutableArray arrayWithCapacity:3];
		for (std::size_t index = 0; index < 3; ++index) {
			id<CAMetalDrawable> drawable = [layer nextDrawable];
			Require(drawable != nil, "fixture requires three actual pool drawables");
			[held addObject:drawable];
		}
		const auto pool_status = presentation.TryPresentCurrent(synchronization, false);
		std::cout << "Actual held drawables=" << held.count << " distinctTextures=" << (held[0].texture != held[1].texture && held[0].texture != held[2].texture && held[1].texture != held[2].texture) << " layerMaximum=" << layer.maximumDrawableCount << " syncCurrent=" << synchronization.Current() << " syncRequested=" << synchronization.Requested() << " layerSync=" << static_cast<bool>(layer.displaySyncEnabled) << " status=" << static_cast<int>(pool_status) << '\n';
		Require(pool_status != sgre::render::SpritePresentationStatus::Busy, "drained software slots cannot report Busy");
		if (pool_status == sgre::render::SpritePresentationStatus::DrawableUnavailable) {
			Require(synchronization.Current() == 1 && layer.displaySyncEnabled, "actual nil drawable preserves current and restores layer");
		} else {
			Require(synchronization.Current() == 0 && layer.displaySyncEnabled, "another genuine queued drawable adopts requested word");
			std::cout << "Three retained distinct pool drawables did not prove exhaustion; nil branch and cross-interval pool retirement remain OPEN\n";
		}
	}
	Drain(presentation);
	const auto before_detach = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	NSView* view = objects.View;
	view.layer = nil;
	RequireError([&] {
		static_cast<void>(presentation.CurrentSubmission());
	},
	    "physical geometry changed");
	view.layer = layer;
	Require(sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation).DrawableRequests == before_detach.DrawableRequests, "detached namespace rejects before drawable request");
	Require(presentation.CurrentSubmission().Identity == latest, "restored namespace keeps exact current Submit");
	renderer.reset();
	RequireError([&] {
		static_cast<void>(presentation.CurrentSubmission());
	},
	    "renderer has closed");
	Require(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "hidden synchronization preserves foreground application");
	const auto final = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(!final.ApplicationActive && !final.WindowVisible && !final.WindowKey && !final.WindowMain, "actual target stays hidden and inactive");
	std::cout << "Native128 scalar transitions; actual current Submit/latest replacement/expiry; seven genuine hidden transfers and Busy restoration PASS; actual held-pool availability recorded\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 4, "native fixture and two actual shader paths required");
		@autoreleasepool {
			sgre::platform::HostPerformanceCounter counter;
			sgre::core::ThreadTaskFrame workers([&] {
				return counter.ReadRawCounter();
			},
			    1);
			sgre::core::LooperTaskState task_state;
			bool retired = false;
			sgre::core::LooperTaskOperations task;
			task.ReadState = [&] {
				return task_state;
			};
			task.Update = [] {
				throw std::runtime_error("diagnostic must not substitute actual root Update");
			};
			task.Draw = [](std::uint32_t) {
				throw std::runtime_error("diagnostic must not substitute actual root Draw");
			};
			task.CanRetire = [&] {
				return !retired;
			};
			task.Retire = [&] {
				retired = true;
			};
			sgre::core::LooperFrame looper(workers, std::move(task), [](auto&) {
				throw std::runtime_error("diagnostic must not substitute actual535f20 pump");
			},
			    0);
			try {
				Require(looper.FixedBaseCount() == 1 && looper.RemainingFixedTicks() == 1, "actual immutable Looper constructor base");
				looper.ConsumeFixedTick();
				looper.Clock().SetVariableFrame(true);
				static_cast<void>(looper.Clock().Advance(40));
				Require(looper.FixedBaseCount() == 1 && looper.RemainingFixedTicks() == 0 && looper.Clock().VariableStep() != 1, "fixed base differs from residual and variable sample");
				TestActual(looper, argv[2], argv[3], argv[1]);
			} catch (...) {
				looper.ReleaseTask();
				looper.Close();
				throw;
			}
			looper.ReleaseTask();
			looper.Close();
			RequireError([&] {
				static_cast<void>(looper.FixedBaseCount());
			},
			    "live serialized");
			workers.Close();
			Require(retired, "actual containing diagnostic root retirement");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
