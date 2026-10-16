#include "sgre/platform/application_pump.hpp"

#include "sgre/core/window_frame_pacing.hpp"
#include "sgre/render/sprite_presentation.hpp"
#include "sprite_presentation_fixture.hpp"

#include <IOKit/hidsystem/IOLLEvent.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

using Platform = sgre::platform::MacApplicationPump;
using Events = sgre::core::ApplicationEventControl;
using Pacing = sgre::core::WindowFramePacing;

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

@interface SGREApplicationPumpProbeResponder : NSResponder {
@public
	Platform* ActualPlatform;
	Events* ActualEvents;
	Pacing* ActualPacing;
	std::uint32_t Delivered;
	std::exception_ptr Failure;
}
@end

@implementation SGREApplicationPumpProbeResponder
- (BOOL)acceptsFirstResponder {
	return YES;
}
- (void)keyUp:(NSEvent*)event {
	try {
		Require(event.type == NSEventTypeKeyUp, "actual AppKit responder received a different event type");
		++Delivered;
		RequireError([&] {
			ActualPlatform->DispatchEvents(*ActualEvents, *ActualPacing);
		},
		    "nonrecursive");
		RequireError([&] {
			ActualPlatform->Close();
		},
		    "during actual OS dispatch");
		RequireError([&] {
			static_cast<void>(ActualPlatform->ReadMouseFrame());
		},
		    "completed actual OS dispatch");
	} catch (...) {
		// No authored exception crosses the OS responder callback.
		Failure = std::current_exception();
	}
}
@end

static void TestPlatform(const char* sprite_shader, const char* transfer_shader) {
	Require([NSThread isMainThread], "fixture requires the actual main thread");
	const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
	static_assert(NSF24FunctionKey - NSF1FunctionKey == 23);
	Events events;
	std::unique_ptr<Platform> platform_owner;
	std::unique_ptr<Pacing> pacing_owner;
	sgre::render::SpritePresentationFixtureObjects weak_objects;
	@autoreleasepool {
		sgre::render::SpritePassDescription description;
		description.Width = 8;
		description.Height = 8;
		description.MaxVertices = 4;
		description.MaxIndices = 4;
		description.MaxDraws = 1;
		description.MaxTextures = 1;
		description.MaxPipelines = 1;
		description.MaxTextureBytes = 1024;
		description.MaxTargetBytes = 16384;
		sgre::render::SpritePassRenderer renderer(description, sprite_shader);
		auto presentation = std::make_unique<sgre::render::SpritePassPresentation>(renderer, transfer_shader);
		weak_objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(*presentation);
		platform_owner = std::make_unique<Platform>(events, *presentation);
		auto& platform = *platform_owner;
		RequireError([&] {
			Platform duplicate(events, *presentation);
		},
		    "exclusive");
		const auto actual_keys = platform.ReadFunctionKeySample();
		Require(!actual_keys.Available && std::all_of(actual_keys.VirtualKeys.begin(), actual_keys.VirtualKeys.end(), [](auto value) {
			return value == 0;
		}),
		    "hidden real window focus gate must produce actual unavailable input, not activate the app");
		sgre::input::KeyMappings actual_mappings;
		const auto actual_controller_count = platform.ObservedControllerCount();
		if (actual_controller_count == 0) {
			const auto actual_hub_frame = platform.ReadHubFrame(actual_mappings);
			Require(actual_hub_frame.CursorExtent.has_value() && actual_hub_frame.CursorExtent->Width > 0 && actual_hub_frame.CursorExtent->Height > 0, "actual Hub cursor extent must come from the real owned backing bounds");
			for (const auto& device : actual_hub_frame.Devices) {
				Require(!device.Connected && device.Buttons == 0 && device.LeftTrigger == 0 && device.RightTrigger == 0 && std::all_of(device.PhysicalKeys.begin(), device.PhysicalKeys.end(), [](auto key) {
					return key == 0;
				}),
				    "actual hidden keyboard-only frame must publish the native focus-lost state for all eight devices");
			}
		} else {
			RequireError([&] {
				static_cast<void>(platform.ReadHubFrame(actual_mappings));
			},
			    "connected HID controller domain");
		}
		const auto actual_mouse = platform.ReadMouseFrame();
		const auto retained_mouse = platform.ReadMouseFrame();
		Require(actual_mouse.Client.Right > 0 && actual_mouse.Client.Bottom > 0 && !actual_mouse.TargetWindowMatches && !actual_mouse.EnterTracking && actual_mouse.Buttons == 0 && actual_mouse.WheelTicks == 0, "actual hidden mouse owner must retain real client bounds without claiming an owned pointer hit or manufactured buttons/wheel");
		Require(retained_mouse.WheelTicks == actual_mouse.WheelTicks && retained_mouse.Buttons == actual_mouse.Buttons, "independent mouse clocks borrow the retained Hub poll result without consuming wheel again");
		sgre::input::MouseInput native_mouse;
		native_mouse.Advance(actual_mouse);
		Require(native_mouse.Position().X == 32768 && native_mouse.Position().Y == 32768 && !native_mouse.Inside(), "actual hidden window sample must reach native outside-position/enter semantics");
		std::cout << "actual HID controller collections " << actual_controller_count << '\n';
		Require(platform.Frequency() > 0 && platform.ReadRawCounter() > 0 && platform.HasBoundPresentationTarget(), "counter and attached target must be actual owner outputs");
		weak_objects.View.layer = nil;
		RequireError([&] {
			static_cast<void>(platform.HasBoundPresentationTarget());
		},
		    "attached target identity changed");
		weak_objects.View.layer = weak_objects.Layer;
		Require(platform.HasBoundPresentationTarget(), "restored actual target must retain its exact identity");
		pacing_owner = std::make_unique<Pacing>(events, platform.Frequency(), platform.ReadRawCounter());
		auto& pacing = *pacing_owner;
		Require(pacing.UsesControl(events), "actual pacing control identity differs");
		Events foreign_events;
		Pacing foreign_pacing(foreign_events, platform.Frequency(), platform.ReadRawCounter());
		RequireError([&] {
			platform.DispatchEvents(foreign_events, pacing);
		},
		    "exact");
		RequireError([&] {
			platform.DispatchEvents(events, foreign_pacing);
		},
		    "exact");
		RequireError([&] {
			platform.Present(false);
		},
		    "actual current-frame producer");
		RequireError([&] {
			platform.Present(true);
		},
		    "actual current-frame producer");
		Require(sgre::render::SpritePassPresentationFixtureBridge::Observe(*presentation).DrawableRequests == 0, "unsupported presentation must never request a drawable");
		for (std::uint32_t sample = 0; sample < 8; ++sample) {
			const auto first = platform.ReadRawCounter();
			platform.PrecisionWaitOneMillisecond();
			const auto second = platform.ReadRawCounter();
			platform.SuspendedWaitSixteenMilliseconds();
			const auto third = platform.ReadRawCounter();
			const auto first_seconds = static_cast<double>(second - first) / static_cast<double>(platform.Frequency());
			const auto second_seconds = static_cast<double>(third - second) / static_cast<double>(platform.Frequency());
			Require(first_seconds >= 0.001 && second_seconds >= 0.016, "actual waits returned before their requested durations");
			std::cout << "wait " << sample << ' ' << first_seconds << ' ' << second_seconds << '\n';
		}
		std::exception_ptr foreign_failure;
		std::thread foreign([&] {
			try {
				static_cast<void>(platform.ReadFunctionKeySample());
			} catch (...) {
				foreign_failure = std::current_exception();
			}
		});
		foreign.join();
		Require(static_cast<bool>(foreign_failure), "foreign thread was admitted to actual window state");
		SGREApplicationPumpProbeResponder* responder = [[SGREApplicationPumpProbeResponder alloc] init];
		responder->ActualPlatform = &platform;
		responder->ActualEvents = &events;
		responder->ActualPacing = &pacing;
		responder.nextResponder = weak_objects.View;
		Require([weak_objects.Window makeFirstResponder:responder] && weak_objects.Window.firstResponder == responder, "actual hidden first responder could not be installed");
		NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:weak_objects.Window.windowNumber context:nil characters:@"a" charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:0];
		Require(event != nil && event.window == weak_objects.Window, "fixture event must belong to the exact actual hidden window");
		[NSApp postEvent:event atStart:YES];
		platform.DispatchEvents(events, pacing);
		Require(responder->Delivered == 1 && !responder->Failure, "actual AppKit queue did not dispatch through the owned responder");
		Require(!platform.ReadFunctionKeySample().Available, "delivering an owned key event must not invent focus for a hidden window");
		Require([weak_objects.Window makeFirstResponder:weak_objects.View], "actual responder cleanup failed");
		responder.nextResponder = nil;
		event = nil;
		const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(*presentation);
		Require(!observation.ApplicationActive && !observation.WindowVisible && !observation.WindowKey && !observation.WindowMain && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "hidden actual queue dispatch changed foreground or window visibility");
		Require(platform.HasBoundPresentationTarget(), "missing current frame must not masquerade as an absent target");
		presentation.reset();
		RequireError([&] {
			platform.RequireLive();
		},
		    "owner");
		foreign_pacing.Close();
		foreign_events.Close();
	}
	Require(weak_objects.Window == nil && weak_objects.View == nil, "dispatcher must not retain retired actual window/view owners");
	const auto deferred_layer = weak_objects.Layer != nil;
	platform_owner->Close();
	std::cout << "CoreAnimation deferred layer observed " << deferred_layer << '\n';
	RequireError([&] {
		static_cast<void>(platform_owner->ReadFunctionKeySample());
	},
	    "live actual hidden owner");
	RequireError([&] {
		static_cast<void>(platform_owner->ReadMouseFrame());
	},
	    "live actual hidden owner");
	RequireError([&] {
		static_cast<void>(platform_owner->ObservedControllerCount());
	},
	    "live actual hidden owner");
	pacing_owner->Close();
	events.Close();
	std::cout << "actual hidden queue/responder, waits/counter, lifetime and unsupported presentation PASS\n";
}

static NSEvent* CreateUnownedControlEvent() {
	for (NSInteger number = 1; number <= 64; ++number) {
		if ([NSApp windowWithWindowNumber:number] != nil) {
			continue;
		}

		NSEvent* event = [NSEvent otherEventWithType:NSEventTypeSystemDefined location:NSZeroPoint modifierFlags:0 timestamp:9 windowNumber:number context:nil subtype:NX_SUBTYPE_AUX_CONTROL_BUTTONS data1:68096 data2:-1];
		if (event != nil && event.window == nil && event.windowNumber == number) {
			return event;
		}
	}

	throw std::runtime_error("could not prepare a bounded actual windowless event with a nonzero unowned identity");
}

static void TestBoundedQueue(const char* sprite_shader, const char* transfer_shader) {
	const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
	static_assert(NSF24FunctionKey - NSF1FunctionKey == 23);
	sgre::render::SpritePassDescription description;
	description.Width = 8;
	description.Height = 8;
	description.MaxVertices = 4;
	description.MaxIndices = 4;
	description.MaxDraws = 1;
	description.MaxTextures = 1;
	description.MaxPipelines = 1;
	description.MaxTextureBytes = 1024;
	description.MaxTargetBytes = 16384;
	sgre::render::SpritePassRenderer renderer(description, sprite_shader);
	sgre::render::SpritePassPresentation presentation(renderer, transfer_shader);
	const auto objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(presentation);
	Events events;
	const auto previous_mouse_moved = objects.Window.acceptsMouseMovedEvents;
	const auto previous_tracking_count = objects.View.trackingAreas.count;
	Platform platform(events, presentation, 1);
	Require(objects.Window.acceptsMouseMovedEvents && objects.View.trackingAreas.count == previous_tracking_count + 1, "actual window sampler must own its real movement/enter/leave registration without activating the window");
	Pacing pacing(events, platform.Frequency(), platform.ReadRawCounter());
	SGREApplicationPumpProbeResponder* responder = [[SGREApplicationPumpProbeResponder alloc] init];
	responder->ActualPlatform = &platform;
	responder->ActualEvents = &events;
	responder->ActualPacing = &pacing;
	responder.nextResponder = objects.View;
	Require([objects.Window makeFirstResponder:responder] && objects.Window.firstResponder == responder, "bounded responder installation failed");
	for (std::uint16_t key = 0; key < 2; ++key) {
		NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:objects.Window.windowNumber context:nil characters:@"a" charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:key];
		Require(event != nil && event.window == objects.Window, "bounded queue event owner differs");
		[NSApp postEvent:event atStart:YES];
	}
	RequireError([&] {
		platform.DispatchEvents(events, pacing);
	},
	    "bounded actual OS queue work");
	Require(responder->Delivered == 1 && !responder->Failure, "bounded queue must dispatch exactly one genuine event");
	platform.DispatchEvents(events, pacing);
	Require(responder->Delivered == 2 && !responder->Failure, "remaining actual queue event must survive the bounded failure");
	NSEvent* following_key = [NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0 timestamp:6 windowNumber:objects.Window.windowNumber context:nil characters:@"a" charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:2];
	NSEvent* auxiliary = [NSEvent otherEventWithType:NSEventTypeSystemDefined location:NSZeroPoint modifierFlags:0 timestamp:7 windowNumber:0 context:nil subtype:NX_SUBTYPE_AUX_CONTROL_BUTTONS data1:68096 data2:-1];
	Require(following_key != nil && following_key.window == objects.Window && auxiliary != nil && auxiliary.window == nil && auxiliary.windowNumber == 0, "windowless auxiliary/owned-key queue fixture differs");
	[NSApp postEvent:following_key atStart:YES];
	[NSApp postEvent:auxiliary atStart:YES];
	RequireError([&] {
		platform.DispatchEvents(events, pacing);
	},
	    "bounded actual OS queue work");
	NSEvent* pending_key = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:NO];
	Require(pending_key != nil && pending_key.type == NSEventTypeKeyUp && pending_key.timestamp == 6 && pending_key.window == objects.Window && pending_key.windowNumber == objects.Window.windowNumber && pending_key.keyCode == 2 && [pending_key.characters isEqualToString:@"a"] && [pending_key.charactersIgnoringModifiers isEqualToString:@"a"] && !pending_key.isARepeat && pending_key.modifierFlags == 0 && responder->Delivered == 2 && !responder->Failure, "auxiliary notification must consume exactly one budget unit without delivering or replacing the following key");
	const auto control_keys = platform.ReadFunctionKeySample();
	Require(!control_keys.Available && std::all_of(control_keys.VirtualKeys.begin(), control_keys.VirtualKeys.end(), [](auto value) {
		return value == 0;
	}),
	    "windowless media notification must not publish game keys or manufacture focus");
	const auto control_mouse = platform.ReadMouseFrame();
	Require(!control_mouse.TargetWindowMatches && !control_mouse.EnterTracking && control_mouse.Buttons == 0 && control_mouse.WheelTicks == 0, "windowless media notification must not mutate mouse enter/button/wheel input");
	sgre::input::MouseInput hidden_mouse;
	hidden_mouse.Advance(control_mouse);
	Require(hidden_mouse.Position().X == 32768 && hidden_mouse.Position().Y == 32768 && !hidden_mouse.Inside(), "windowless media notification must preserve native hidden outside-cursor semantics");
	const auto control_events = events.State();
	Require(!control_events.SuspendPending && !control_events.ResumePending && !control_events.ClosePending && !control_events.WaitBreakPending && !control_events.QuitPending && !control_events.ResizePending && control_events.ResumeRequests == 0 && control_events.ResizeRequests == 0, "windowless media notification must not publish application control requests");
	const auto control_observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(!control_observation.ApplicationActive && !control_observation.WindowVisible && !control_observation.WindowKey && !control_observation.WindowMain && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "windowless media notification changed hidden/foreground state");
	platform.DispatchEvents(events, pacing);
	Require(responder->Delivered == 3 && !responder->Failure, "owned key behind the consumed auxiliary notification did not dispatch in its original next budget");
	NSEvent* unknown_system = [NSEvent otherEventWithType:NSEventTypeSystemDefined location:NSZeroPoint modifierFlags:0 timestamp:8 windowNumber:0 context:nil subtype:NX_SUBTYPE_ACCESSIBILITY data1:17 data2:23];
	Require(unknown_system != nil && unknown_system.window == nil, "unhandled system-subtype fixture differs");
	[NSApp postEvent:unknown_system atStart:YES];
	RequireError([&] {
		platform.DispatchEvents(events, pacing);
	},
	    "eventType=14 timestamp=8.000000 eventWindow=none eventWindowNumber=0 subtype=17 data1=17 data2=23");
	NSEvent* retained_system = [NSApp nextEventMatchingMask:NSEventMaskSystemDefined untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
	Require(retained_system != nil && retained_system.type == NSEventTypeSystemDefined && retained_system.timestamp == 8 && retained_system.window == nil && retained_system.windowNumber == 0 && retained_system.subtype == NX_SUBTYPE_ACCESSIBILITY && retained_system.data1 == 17 && retained_system.data2 == 23 && responder->Delivered == 3, "unhandled system subtype must preserve its original event fields through rejection/repost");
	NSEvent* unowned_control = CreateUnownedControlEvent();
	const auto unowned_number = unowned_control.windowNumber;
	Require(unowned_control.window == nil && unowned_number != 0 && [NSApp windowWithWindowNumber:unowned_number] == nil, "unresolved nonzero-window control fixture differs");
	[NSApp postEvent:unowned_control atStart:YES];
	RequireError([&] {
		platform.DispatchEvents(events, pacing);
	},
	    "event's exact owned hidden window");
	retained_system = [NSApp nextEventMatchingMask:NSEventMaskSystemDefined untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
	Require(retained_system != nil && retained_system.type == NSEventTypeSystemDefined && retained_system.timestamp == 9 && retained_system.window == nil && retained_system.windowNumber == unowned_number && retained_system.subtype == NX_SUBTYPE_AUX_CONTROL_BUTTONS && retained_system.data1 == 68096 && retained_system.data2 == -1 && responder->Delivered == 3, "nonzero unresolved window identity must retain the existing rejection/repost domain");
	NSEvent* unsupported = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:17 data2:23];
	Require(unsupported != nil && unsupported.window == nil, "unsupported application event fixture differs");
	[NSApp postEvent:unsupported atStart:YES];
	RequireError([&] {
		platform.DispatchEvents(events, pacing);
	},
	    "eventType=15 timestamp=0.000000 eventWindow=none eventWindowNumber=0 subtype=0 data1=17 data2=23");
	NSEvent* retained = [NSApp nextEventMatchingMask:NSEventMaskApplicationDefined untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
	Require(retained != nil && retained.type == NSEventTypeApplicationDefined && retained.window == nil && retained.data1 == 17 && retained.data2 == 23 && responder->Delivered == 3, "unsupported event must be preserved rather than dispatched or silently discarded");
	NSEvent* appkit = [NSEvent otherEventWithType:NSEventTypeAppKitDefined location:NSZeroPoint modifierFlags:0 timestamp:3 windowNumber:0 context:nil subtype:NSEventSubtypeApplicationDeactivated data1:31 data2:47];
	Require(appkit != nil && appkit.window == nil, "actual windowless AppKit diagnostic event differs");
	[NSApp postEvent:appkit atStart:YES];
	RequireError([&] {
		platform.DispatchEvents(events, pacing);
	},
	    "eventType=13 timestamp=3.000000 eventWindow=none eventWindowNumber=0 subtype=2 data1=31 data2=47");
	retained = [NSApp nextEventMatchingMask:NSEventMaskAppKitDefined untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
	Require(retained != nil && retained.type == NSEventTypeAppKitDefined && retained.subtype == NSEventSubtypeApplicationDeactivated && retained.data1 == 31 && retained.data2 == 47 && responder->Delivered == 3, "typed AppKit failure must preserve the original event without invoking application dispatch");
	NSEvent* periodic = [NSEvent otherEventWithType:NSEventTypePeriodic location:NSZeroPoint modifierFlags:0 timestamp:5 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
	Require(periodic != nil, "actual periodic diagnostic event differs");
	[NSApp postEvent:periodic atStart:YES];
	RequireError([&] {
		platform.DispatchEvents(events, pacing);
	},
	    "eventType=16 timestamp=5.000000 eventWindow=not-applicable-periodic");
	retained = [NSApp nextEventMatchingMask:NSEventMaskPeriodic untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
	Require(retained != nil && retained.type == NSEventTypePeriodic && retained.timestamp == 5 && responder->Delivered == 3, "periodic diagnostic must preserve the real event without accessing undefined window fields");
	Require([objects.Window makeFirstResponder:objects.View], "bounded actual responder cleanup failed");
	responder.nextResponder = nil;
	Require(platform.HasBoundPresentationTarget(), "bounded OS dispatch must preserve its attached target");
	platform.Close();
	Require(objects.Window.acceptsMouseMovedEvents == previous_mouse_moved && objects.View.trackingAreas.count == previous_tracking_count, "sampler retirement must restore only its actual owned tracking/movement registration");
	Platform reacquired(events, presentation);
	Require(reacquired.HasBoundPresentationTarget(), "actual dispatcher retirement must release its exclusive borrow");
	reacquired.Close();
	pacing.Close();
	events.Close();
	const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(!observation.ApplicationActive && !observation.WindowVisible && !observation.WindowKey && !observation.WindowMain && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "bounded dispatch changed hidden/foreground state");
	std::cout << "bounded queue/repost and actual exclusive borrow reacquisition PASS\n";
}

static void TestInteractiveOffscreen(const char* sprite_shader, const char* transfer_shader) {
	const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
	sgre::render::SpritePassDescription description;
	description.Width = 16;
	description.Height = 9;
	description.MaxVertices = 4;
	description.MaxIndices = 4;
	description.MaxDraws = 1;
	description.MaxTextures = 1;
	description.MaxPipelines = 1;
	description.MaxTextureBytes = 1024;
	description.MaxTargetBytes = 16384;
	sgre::render::SpritePassRenderer renderer(description, sprite_shader);
	sgre::render::SpritePresentationDescription window;
	window.VisibleWindow = true;
	window.WindowWidthPoints = 320;
	window.WindowHeightPoints = 180;
	window.Fullscreen = true;
	sgre::render::SpritePassPresentation presentation(renderer, transfer_shader, window);
	const auto objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(presentation);
	Require(!objects.Window.visible && !NSApp.active && !presentation.Geometry().Fullscreen, "interactive construction must not show, activate or enter fullscreen before explicit ShowWindow");
	Require((objects.Window.styleMask & (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)) == (NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable), "interactive window lacks native title/close/minimize/resize controls");
	Require([objects.Window.title isEqualToString:@"STEINS;GATE RE:BOOT"], "interactive title changed");
	[objects.Window setContentSize:NSMakeSize(400, 400)];
	const auto resized = presentation.Geometry();
	Require(resized.WidthPoints == 400 && resized.HeightPoints == 400 && resized.SourceWidthPixels == 16 && resized.SourceHeightPixels == 9 && resized.Content.Left == 0 && resized.Content.Top > 0 && resized.Content.Width / resized.Content.Height == 16.0 / 9.0, "resize changed logical target or omitted centered aspect-fit bars");
	Require(presentation.DrawableCapacity() == 2 && objects.Layer.drawableSize.width == resized.WidthPixels && objects.Layer.drawableSize.height == resized.HeightPixels && objects.Layer.contentsScale == resized.BackingScale, "dynamic backing geometry did not reach the actual layer");

	sgre::render::SpritePresentationSubmission source;
	source.Owner = &renderer;
	source.Identity = renderer.Submit({}, {}, {1, 0, 0, 1});
	for (const auto extent : {std::array<std::uint32_t, 2>{8, 8}, std::array<std::uint32_t, 2>{32, 9}}) {
		const auto pixels = sgre::render::SpritePassPresentationFixtureBridge::CaptureInteractiveTransfer(presentation, source, extent[0], extent[1]);
		for (std::uint32_t y = 0; y < extent[1]; ++y) {
			for (std::uint32_t x = 0; x < extent[0]; ++x) {
				const bool content = extent[0] == 8 ? y >= 2 && y < 6 : x >= 8 && x < 24;
				const auto offset = (static_cast<std::size_t>(y) * extent[0] + x) * 4;
				Require(pixels[offset] == 0 && pixels[offset + 1] == 0 && pixels[offset + 2] == (content ? 255 : 0) && pixels[offset + 3] == 255, "actual scaled presentation did not preserve red content/opaque black bars");
			}
		}
	}

	const auto previous_frame = objects.View.frame;
	const auto previous_bounds = objects.View.bounds;
	// Changing bounds scales local coordinates; resize the actual view to exercise an unavailable drawable.
	[objects.View setFrameSize:NSZeroSize];
	Require(objects.View.frame.size.width == 0 && objects.View.frame.size.height == 0 && objects.View.bounds.size.width == 0 && objects.View.bounds.size.height == 0, "actual zero-frame fixture did not produce zero frame and default bounds");
	Require(!presentation.Geometry().DrawableAvailable && presentation.TryPresentLatest(source) == sgre::render::SpritePresentationStatus::DrawableUnavailable, "temporary zero-size view must defer the actual drawable without changing the source");
	[objects.View setFrame:previous_frame];
	Require(NSEqualRects(objects.View.frame, previous_frame) && NSEqualRects(objects.View.bounds, previous_bounds), "actual zero-frame fixture did not restore its previous frame and default bounds");
	Require(presentation.Geometry().DrawableAvailable && presentation.DrawableCapacity() == 2, "restored drawable extent did not recover after zero-size transition");
	Require(objects.View.acceptsFirstResponder && [objects.Window makeFirstResponder:objects.View], "interactive gameplay content must accept native first-responder ownership");

	Events events;
	Platform platform(events, presentation);
	Pacing pacing(events, platform.Frequency(), platform.ReadRawCounter());
	NSEvent* entered = [NSEvent enterExitEventWithType:NSEventTypeMouseEntered location:NSZeroPoint modifierFlags:0 timestamp:11 windowNumber:objects.Window.windowNumber context:nil eventNumber:1 trackingNumber:1 userData:nullptr];
	Require(entered != nil && entered.type == NSEventTypeMouseEntered && entered.window == objects.Window && entered.windowNumber == objects.Window.windowNumber, "interactive unavailable mouse fixture requires its actual owned enter event");
	[NSApp postEvent:entered atStart:YES];
	platform.DispatchEvents(events, pacing);
	Require([NSApp nextEventMatchingMask:NSEventMaskMouseEntered untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:NO] == nil, "interactive unavailable mouse fixture did not consume its exact owned enter event");
	const auto unavailable_mouse = platform.ReadMouseFrame();
	sgre::input::MouseInput unavailable_input;
	unavailable_input.Advance(unavailable_mouse);
	Require(!unavailable_mouse.EnterTracking && !unavailable_mouse.TargetWindowMatches && !unavailable_input.Inside() && unavailable_input.Position().X == 32768 && unavailable_input.Position().Y == 32768, "an entered but never-shown interactive window must project native outside mouse state");
	Require(!objects.Window.visible && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "owned mouse entry fixture showed or activated the interactive window");
	bool accept_close = false;
	std::uint32_t close_calls = 0;
	sgre::core::ApplicationEventCallbackOperations operations;
	operations.Suspend = [] {
	};
	operations.Resume = [] {
	};
	operations.Resize = [](auto) {
	};
	operations.Close = [&] {
		++close_calls;

		return accept_close;
	};
	sgre::core::ApplicationEventCallbacks callbacks(events, std::move(operations));
	static_cast<void>(events.ExchangeCallback(sgre::core::ApplicationEventSlot::Close, &callbacks));
	[objects.Window performClose:nil];
	Require(!events.State().ClosePending && objects.View.window == objects.Window, "window callback bypassed containing request phase or destroyed the live target");
	platform.DispatchEvents(events, pacing);
	Require(events.State().ClosePending, "native close did not queue actual engine close request");
	events.Flush();
	Require(close_calls == 1 && !events.State().CloseAccepted && objects.View.window == objects.Window && platform.HasBoundPresentationTarget(), "vetoed close detached the active engine/GPU window");

	// Exercise the actual delegate protocol directly; calling NSApplication.terminate could exit a broken fixture.
	id delegate = objects.Window.delegate;
	Require([delegate applicationShouldTerminate:NSApp] == NSTerminateCancel, "Cmd-Q must defer termination to the actual engine close callback");
	accept_close = true;
	platform.DispatchEvents(events, pacing);
	events.Flush();
	Require(close_calls == 2 && events.State().CloseAccepted && objects.View.window == objects.Window && platform.HasBoundPresentationTarget(), "accepted close bypassed the containing retirement boundary");
	const auto mouse = platform.ReadMouseFrame();
	Require(mouse.Client.Right == 16 && mouse.Client.Bottom == 9 && !mouse.TargetWindowMatches, "resized hidden fixture mouse must retain logical scene extent without pointer capture");
	Require(!objects.Window.visible && !objects.Window.keyWindow && !objects.Window.mainWindow && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "offscreen interactive lifecycle fixture altered foreground or visibility");
	callbacks.Close();
	platform.Close();
	pacing.Close();
	events.Close();
	std::cout << "interactive offscreen resize/deferred close/Cmd-Q PASS\n";
}

int main(int argc, char** argv) {
	@autoreleasepool {
		try {
			Require(argc == 3, "usage: application_pump_tests sprite_shader transfer_shader");
			// AppKit events and window-list snapshots can retain a retired window until their pool drains.
			@autoreleasepool {
				TestPlatform(argv[1], argv[2]);
			}
			@autoreleasepool {
				TestBoundedQueue(argv[1], argv[2]);
			}
			Require(!NSApp.active && NSApp.activationPolicy == NSApplicationActivationPolicyAccessory && NSApp.windows.count == 0, "completed hidden fixture must release all windows before constructing the next owner");
			@autoreleasepool {
				TestInteractiveOffscreen(argv[1], argv[2]);
			}

			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';

			return 1;
		}
	}
}
