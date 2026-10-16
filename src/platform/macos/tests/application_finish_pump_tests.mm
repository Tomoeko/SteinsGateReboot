#include "application_finish_event_fixture.hpp"

#include "sgre/core/window_frame_pacing.hpp"
#include "sgre/platform/application_pump.hpp"
#include "sgre/render/sprite_presentation.hpp"
#include "sprite_presentation_fixture.hpp"

#include <exception>
#include <iostream>

static void TestQuietOriginalClose(const char* sprite_shader, const char* transfer_shader, const char* override_bytecode) {
	RequireFinish(NSThread.isMainThread, "finish pump fixture requires the actual main thread");
	const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
	sgre::core::ApplicationEventControl events;
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
	sgre::render::SpritePresentationDescription window;
	window.VisibleWindow = true;
	window.WindowWidthPoints = 320;
	window.WindowHeightPoints = 180;
	sgre::render::SpritePassPresentation presentation(renderer, transfer_shader, window);
	const auto objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(presentation);
	sgre::platform::MacApplicationPump platform(events, presentation);
	sgre::core::WindowFramePacing pacing(events, platform.Frequency(), platform.ReadRawCounter());
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptApplicationFinishBindings finish(vm);
	const auto previous = InstallOriginalFinish(vm, override_bytecode);
	sgre::core::ApplicationEventCallbacks receiver(events, FinishOperations(events, finish));
	static_cast<void>(events.ExchangeCallback(sgre::core::ApplicationEventSlot::Close, &receiver));
	[objects.Window performClose:nil];
	RequireFinish(!events.State().ClosePending && objects.View.window == objects.Window, "AppKit delegate must defer close without destroying the owned window");
	platform.DispatchEvents(events, pacing);
	events.Flush();
	RequireFinish(!events.State().CloseAccepted && !events.State().Quit && vm.GetSlot(vm.RootTable(), std::string("sync")) != previous && objects.View.window == objects.Window, "actual original callback must veto the offscreen close and install its unexecuted confirmation continuation");
	const auto approved = vm.CreateNativeClosure("independent approved finish", [](auto&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(true);
	});
	RegisterFinish(vm, approved);
	[objects.Window performClose:nil];
	platform.DispatchEvents(events, pacing);
	events.Flush();
	RequireFinish(events.State().CloseAccepted && !events.State().Quit && objects.View.window == objects.Window, "approved decision must still await the containing engine retirement, not AppKit window destruction");
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	RequireFinish(!observed.ApplicationActive && !observed.WindowVisible && !observed.WindowKey && !observed.WindowMain && observed.DrawableRequests == 0 && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "finish transport showed/activated/drew the offscreen window or disturbed foreground state");
	receiver.Close();
	vm.Close();
	platform.Close();
	pacing.Close();
	events.Close();
	std::cout << "quiet actual AppKit close→original compiled finish veto→independent approval PASS; no confirmation execution, audio, activation or full shutdown claim\n";
}

int main(int argc, char** argv) {
	@autoreleasepool {
		try {
			RequireFinish(argc == 4, "usage: application_finish_pump_tests sprite_shader transfer_shader original_override.sqbc");
			TestQuietOriginalClose(argv[1], argv[2], argv[3]);

			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';

			return 1;
		}
	}
}
