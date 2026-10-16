#pragma once

#include "sgre/render/sprite_presentation.hpp"

#include "presentation_system_boundary.hpp"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#if !defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
#error "This private observation boundary requires the explicit fixture build flag."
#endif

namespace sgre::render {

	enum class PresentationFixtureMoment {
		Before,
		After
	};

	struct SpritePresentationFixtureObservation {
		bool Presenting = false;
		bool DisplaySynchronized = true;
		bool CleanupFailed = false;
		PresentationSystemOperation FailedOperation = PresentationSystemOperation::None;
		PresentationSystemFailureKind FailureKind = PresentationSystemFailureKind::None;
		bool ApplicationActive = false;
		bool WindowVisible = false;
		bool WindowKey = false;
		bool WindowMain = false;
		bool ViewAttached = false;
		bool LayerAttached = false;
		std::uint64_t DrawableRequests = 0;
		std::size_t RetainedTransfers = 0;
		std::size_t RetainedDrawables = 0;
	};

	struct SpritePresentationFixtureObjects {
		// Weak observations cannot keep the actual hidden objects alive during teardown.
		__weak NSWindow* Window = nil;
		__weak NSView* View = nil;
		__weak CAMetalLayer* Layer = nil;
	};

	struct SpritePresentationFixtureCommands {
		// Only actual queued commands are retained, never window/layer/drawable objects.
		id<MTLCommandBuffer> Producer = nil;
		id<MTLCommandBuffer> Transfer = nil;
	};

	class SpritePassPresentationFixtureBridge {
	public:
		// Main-thread-only, scoped actual pimpl access; the presentation still owns every object.
		[[nodiscard]] static SpritePresentationFixtureObservation Observe(const SpritePassPresentation& presentation);
		[[nodiscard]] static SpritePresentationFixtureObjects BorrowObjects(const SpritePassPresentation& presentation);
		[[nodiscard]] static std::array<SpritePresentationFixtureCommands, 3> RetainCommands(const SpritePassPresentation& presentation);
		static void SetSystemHook(SpritePassPresentation& presentation, std::function<void(PresentationSystemOperation, PresentationFixtureMoment)> hook);
		// Isolated readback fixture only: the same actual transfer encoder, without requesting/showing a drawable.
		[[nodiscard]] static std::vector<std::uint8_t> CaptureInteractiveTransfer(const SpritePassPresentation& presentation, SpritePresentationSubmission submission, std::uint32_t width, std::uint32_t height);
		static void RequireDestination(const SpritePassPresentation& presentation, id<CAMetalDrawable> actual_drawable);
	};

} // namespace sgre::render
