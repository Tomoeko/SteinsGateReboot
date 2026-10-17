#include "sgre/platform/debug_inspector.hpp"

#include "sgre/core/application_event_control.hpp"
#include "sgre/platform/application_pump.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"

#include "debug_inspector_bridge.hpp"

#import <AppKit/AppKit.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

@interface NSView (SGREInspectorFixture)
- (void)submitQuery:(id)sender;
@end

static void Require(bool condition, const char* reason) {
	if (!condition) {
		throw std::runtime_error(reason);
	}
}

static void Pointer(NSView* view, NSEventType type, double x, double y) {
	NSView* parent = view.superview;
	const auto point = [parent convertPoint:NSMakePoint(x, parent.bounds.size.height - y) toView:nil];
	NSEvent* event = [NSEvent mouseEventWithType:type location:point modifierFlags:0 timestamp:0 windowNumber:view.window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];
	if (type == NSEventTypeLeftMouseDown) {
		[view mouseDown:event];
	} else if (type == NSEventTypeLeftMouseDragged) {
		[view mouseDragged:event];
	} else {
		[view mouseUp:event];
	}
}

static void Click(NSView* view, double x, double y) {
	Pointer(view, NSEventTypeLeftMouseDown, x, y);
	Pointer(view, NSEventTypeLeftMouseUp, x, y);
}

static void TestPanel(const char* sprite, const char* transfer, const std::optional<sgre::scenario::ScenarioSearchLocation>& location) {
	sgre::render::SpritePassDescription target;
	target.Width = 1280;
	target.Height = 720;
	sgre::render::SpriteGraphicsTargets graphics(target, sprite, 2);
	sgre::render::SpritePresentationDescription settings;
	settings.VisibleWindow = true;
	sgre::render::SpritePassPresentation surface(*graphics.PrimaryTarget(), transfer, settings);
	sgre::core::ApplicationEventControl events;
	sgre::platform::MacApplicationPump pump(events, surface);
	NSView* parent = sgre::render::SpritePassDebugInspectorBridge::ParentView(surface);
	const auto count = parent.subviews.count;
	{
		sgre::platform::DebugInspector inspector(surface, location.value_or(sgre::scenario::ScenarioSearchLocation{}));
		pump.ConfigureToolInputCapture([&] {
			return inspector.CapturesInput();
		});
		Require(parent.subviews.count == count + 1 && !inspector.CapturesInput(), "closed inspector changed ownership or captured gameplay input");
		NSView* panel = parent.subviews.lastObject;
		inspector.Toggle();
		inspector.Update();
		Require(!panel.hidden && inspector.CapturesInput() && pump.ToolInputCaptured(), "visible inspector failed its actual input capture join");
		Require(!pump.ReadFunctionKeySample().Available && pump.ReadMouseFrame().Buttons == 0, "inspector input leaked through the real macOS keyboard or mouse sampler");
		NSBitmapImageRep* pixels = [panel bitmapImageRepForCachingDisplayInRect:panel.bounds];
		Require(pixels != nil && pixels.pixelsWide >= 380 && pixels.pixelsHigh >= 280, "native inspector could not allocate its real AppKit paint surface");
		[panel cacheDisplayInRect:panel.bounds toBitmapImageRep:pixels];
		NSColor* background = [[pixels colorAtX:4 y:4] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
		Require(background != nil && background.alphaComponent > 0.9 && background.blueComponent > 0.01, "native inspector did not paint its immediate command buffer");
		NSTextField* native_query = static_cast<NSTextField*>(panel.subviews.firstObject);
		native_query.stringValue = @"alpha 日本語 beta";
		const auto query_rect = [native_query convertRect:native_query.bounds toView:nil];
		const auto held_point = NSMakePoint(query_rect.origin.x + 20, NSMidY(query_rect));
		const auto held_begin = std::chrono::steady_clock::now();
		NSEvent* held = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:held_point modifierFlags:0 timestamp:0 windowNumber:parent.window.windowNumber context:nil eventNumber:2 clickCount:1 pressure:1];
		[NSApp sendEvent:held];
		Require(std::chrono::steady_clock::now() - held_begin < std::chrono::milliseconds(250), "query mouseDown blocked waiting for a release instead of returning to the game/audio pump");
		NSEvent* dragged = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged location:NSMakePoint(held_point.x + 60, held_point.y) modifierFlags:0 timestamp:0 windowNumber:parent.window.windowNumber context:nil eventNumber:3 clickCount:1 pressure:1];
		[NSApp sendEvent:dragged];
		NSTextView* editor = static_cast<NSTextView*>(native_query.currentEditor);
		Require(editor != nil && editor.selectedRange.length > 0, "nonblocking native query drag failed to select text");
		NSEvent* released = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:dragged.locationInWindow modifierFlags:0 timestamp:0 windowNumber:parent.window.windowNumber context:nil eventNumber:4 clickCount:1 pressure:0];
		[NSApp sendEvent:released];
		const auto before = panel.frame;
		Pointer(panel, NSEventTypeLeftMouseDown, 35, 25);
		Pointer(panel, NSEventTypeLeftMouseDragged, 75, 55);
		Pointer(panel, NSEventTypeLeftMouseUp, 75, 55);
		Require(panel.frame.origin.x == before.origin.x + 40 && panel.frame.origin.y == before.origin.y - 30, "native panel drag lost its portable top-left placement");
		const double right = NSMaxX(panel.frame);
		const double bottom = parent.bounds.size.height - panel.frame.origin.y;
		Pointer(panel, NSEventTypeLeftMouseDown, right - 3, bottom - 3);
		Pointer(panel, NSEventTypeLeftMouseDragged, right + 47, bottom + 27);
		Pointer(panel, NSEventTypeLeftMouseUp, right + 47, bottom + 27);
		Require(panel.frame.size.width == before.size.width + 50 && panel.frame.size.height == before.size.height + 30, "native panel resize lost the portable content extent");
		if (location) {
			NSTextField* query = static_cast<NSTextField*>(panel.subviews.firstObject);
			query.stringValue = @"resg01_10.ks:795";
			[panel submitQuery:query];
			const auto started = std::chrono::steady_clock::now();
			std::optional<std::string> jump;
			while (!jump && std::chrono::steady_clock::now() - started < std::chrono::seconds(15)) {
				inspector.Update();
				const double x = panel.frame.origin.x + 15;
				const double y = parent.bounds.size.height - NSMaxY(panel.frame);
				Click(panel, x, y + 110);
				Click(panel, x, y + panel.frame.size.height - 30);
				jump = inspector.TakeJumpRequest();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			Require(jump == std::optional<std::string>{"resg01_10.ks:795"}, "native search, row selection and jump did not return the validated original target");
			const auto old_window = parent.window.contentView.bounds.size;
			const auto old_panel = panel.frame.size;
			[parent.window setContentSize:NSMakeSize(400, 240)];
			inspector.Update();
			[parent.window setContentSize:old_window];
			inspector.Update();
			Require(panel.frame.size.width == old_panel.width && panel.frame.size.height == old_panel.height, "temporary game-window resize lost the inspector's retained result viewport");
			const double x = panel.frame.origin.x + 15;
			const double y = parent.bounds.size.height - NSMaxY(panel.frame);
			Click(panel, x, y + panel.frame.size.height - 30);
			Require(inspector.TakeJumpRequest() == jump, "native window resize discarded the selected original search result");
		}
		const double close_x = NSMaxX(panel.frame) - 20;
		const double close_y = parent.bounds.size.height - NSMaxY(panel.frame) + 13;
		Click(panel, close_x, close_y);
		Require(panel.hidden && !inspector.CapturesInput(), "native close button left an invisible input trap");
		Require(parent.window.firstResponder != panel.subviews.firstObject && ![parent.window.firstResponder isKindOfClass:NSTextView.class], "hidden query retained the native field editor after close");
		inspector.Toggle();
		Require(inspector.CapturesInput(), "closed inspector failed to reopen");
		inspector.Hide();
		Require(!inspector.CapturesInput(), "accepted jump did not release the inspector");
		pump.ConfigureToolInputCapture({});
	}
	Require(parent.subviews.count == count && !parent.window.visible && !parent.window.keyWindow && !NSApp.active, "native inspector retained a view, showed a window or activated the app");
	pump.Close();
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3 || argc == 4, "inspector fixture requires sprite/transfer shaders and an optional workspace");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				std::optional<sgre::scenario::ScenarioSearchLocation> location;
				if (argc == 4) {
					location = sgre::scenario::ResolveScenarioSearchLocation(argv[3]);
				}
				TestPanel(argv[1], argv[2], location);
				Require(!NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "inspector fixture changed the foreground application");
				std::cout << "native in-window inspector search, drag/resize, capture, close and ownership passed; hidden, inactive and silent\n";
			}

			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* error) {
		std::cerr << error.reason.UTF8String << '\n';
		return 1;
	}
}
