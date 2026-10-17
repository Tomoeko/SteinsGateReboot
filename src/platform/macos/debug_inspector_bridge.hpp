#pragma once

#include "sgre/render/sprite_presentation.hpp"

#import <AppKit/NSView.h>

namespace sgre::render {

	class SpritePassDebugInspectorBridge {
	public:
		[[nodiscard]] static NSView* ParentView(SpritePassPresentation& presentation);
	};

} // namespace sgre::render
