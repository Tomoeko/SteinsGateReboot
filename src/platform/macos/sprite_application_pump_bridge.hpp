#pragma once

#include "sgre/render/sprite_presentation.hpp"

#import <AppKit/AppKit.h>

#include <functional>

namespace sgre::render {

	struct SpriteApplicationWindowSource {
		// Weak OS identities and checked metadata do not extend presentation, window or renderer ownership.
		__weak NSApplication* Application = nil;
		__weak NSWindow* Window = nil;
		std::function<void()> RequireLive;
		std::function<bool()> HasBoundTarget;
		std::function<void()> ReleaseBorrow;
		std::function<SpritePresentationGeometry()> Geometry;
		std::function<bool()> TakeCloseRequest;
		bool Interactive = false;
	};

	class SpritePassApplicationPumpBridge {
	public:
		[[nodiscard]] static SpriteApplicationWindowSource CreateSource(const SpritePassPresentation& presentation, const void* borrower);
	};

} // namespace sgre::render
