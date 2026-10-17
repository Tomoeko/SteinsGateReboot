#include "debug_inspector_bridge.hpp"
#include "sgre/core/application_event_control.hpp"
#include "sgre/platform/application_pump.hpp"
#include "sgre/platform/volume_menu.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"

#import <AppKit/AppKit.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

static void Require(bool value, const char* message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

static void Escape(NSView* view, bool repeat = false) {
	NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:view.window.windowNumber context:nil characters:@"\x1b" charactersIgnoringModifiers:@"\x1b" isARepeat:repeat keyCode:53];
	[NSApp sendEvent:event];
}

static void Pointer(NSView* view, NSEventType type, double x, double y) {
	const auto point = [view convertPoint:NSMakePoint(x, y) toView:nil];
	NSEvent* event = [NSEvent mouseEventWithType:type location:point modifierFlags:0 timestamp:0 windowNumber:view.window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];
	if (type == NSEventTypeLeftMouseDown) {
		[view mouseDown:event];
	} else if (type == NSEventTypeLeftMouseDragged) {
		[view mouseDragged:event];
	} else {
		[view mouseUp:event];
	}
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "volume menu fixture requires two shaders");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				sgre::render::SpritePassDescription target;
				target.Width = 1280;
				target.Height = 720;
				sgre::render::SpriteGraphicsTargets graphics(target, argv[1], 2);
				sgre::render::SpritePresentationDescription settings;
				settings.VisibleWindow = true;
				sgre::render::SpritePassPresentation surface(*graphics.PrimaryTarget(), argv[2], settings);
				sgre::core::ApplicationEventControl events;
				sgre::platform::MacApplicationPump pump(events, surface);
				NSView* parent = sgre::render::SpritePassDebugInspectorBridge::ParentView(surface);
				const auto count = parent.subviews.count;
				{
					std::size_t opens = 0;
					sgre::platform::VolumeMenu menu(surface, {}, [&] {
						++opens;
					});
					pump.ConfigureToolInputCapture([&] {
						return menu.CapturesInput();
					});
					NSView* view = parent.subviews.lastObject;
					Require(view.hidden && !menu.TakeChanges(), "closed volume menu committed or captured input");
					Escape(view);
					menu.Update();
					Require(!view.hidden && opens == 1 && menu.CapturesInput() && pump.ToolInputCaptured(), "Esc failed to open and capture the volume panel");
					Escape(view, true);
					Require(menu.CapturesInput() && !menu.TakeChanges(), "Esc repeat toggled or opening saved unchanged settings");
					NSBitmapImageRep* pixels = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
					[view cacheDisplayInRect:view.bounds toBitmapImageRep:pixels];
					const auto background = [[pixels colorAtX:4 y:4] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
					Require(background.alphaComponent > 0.2 && background.alphaComponent < 0.4, "volume overlay lost translucent game coverage");
					const double left = (view.bounds.size.width - 420) / 2 + 20;
					const double top = (view.bounds.size.height - 286) / 2 + 88;
					const auto begin = std::chrono::steady_clock::now();
					Pointer(view, NSEventTypeLeftMouseDown, left + 190, top + 48 + 6);
					Require(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(250), "volume drag blocked the audio pump waiting for release");
					Require(menu.Settings().Music == 50 && !menu.TakeChanges(), "volume did not apply live or wrote during every drag frame");
					Pointer(view, NSEventTypeLeftMouseDragged, left + 95, top + 48 + 6);
					Pointer(view, NSEventTypeLeftMouseUp, left + 95, top + 48 + 6);
					const auto changed = menu.TakeChanges();
					Require(changed && changed->Music == 25 && changed->Master == 100 && changed->Voice == 100 && changed->Effects == 80 && !menu.TakeChanges(), "volume edit changed another bus or committed more than once");
					Escape(view);
					Require(view.hidden && !menu.CapturesInput() && !menu.TakeChanges(), "Esc failed to close or saved an unchanged panel");
					Escape(view);
					Require(menu.Settings().Music == 25, "reopening discarded listening settings");
					Escape(view);
					pump.ConfigureToolInputCapture({});
				}
				Require(parent.subviews.count == count && !parent.window.visible && !parent.window.keyWindow && !NSApp.active, "volume fixture retained a view or exposed the game window");
				Require(foreground == NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier, "volume fixture changed foreground application");
				pump.Close();
			}
			std::cout << "PASS volume Esc/translucency/live separate controls/nonblocking drag/one-shot change/no-op close/ownership; hidden and silent\n";
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException*) {
		return 1;
	}
}
