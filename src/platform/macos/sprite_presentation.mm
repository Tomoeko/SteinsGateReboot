#include "sgre/render/sprite_presentation.hpp"

#include "sgre/render/presentation_synchronization.hpp"

#include "debug_inspector_bridge.hpp"
#include "metal_support.hpp"
#include "presentation_system_boundary.hpp"
#include "sprite_application_pump_bridge.hpp"
#include "sprite_presentation_bridge.hpp"

#if defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
#include "sprite_presentation_fixture.hpp"
#endif

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

@interface SGREPresentationContentView : NSView
@end

@implementation SGREPresentationContentView
- (BOOL)acceptsFirstResponder {
	return YES;
}
- (void)keyDown:(NSEvent*)event {
	// NSApplication has already offered key equivalents to its menus. The engine polls focused state.
	static_cast<void>(event);
}
- (void)keyUp:(NSEvent*)event {
	static_cast<void>(event);
}
- (void)flagsChanged:(NSEvent*)event {
	static_cast<void>(event);
}
@end

@interface SGREPresentationWindowDelegate : NSObject <NSWindowDelegate, NSApplicationDelegate>
@property(nonatomic) BOOL CloseRequested;
@property(nonatomic) BOOL FullscreenTransition;
@property(nonatomic) BOOL TrackWindowedResizes;
@property(nonatomic) BOOL WindowedSizeChanged;
@property(nonatomic) double WindowedWidth;
@property(nonatomic) double WindowedHeight;
@property(nonatomic, strong) NSException* Failure;
@end

@implementation SGREPresentationWindowDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
	static_cast<void>(sender);
	_CloseRequested = YES;

	return NSTerminateCancel;
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
	static_cast<void>(sender);
	_CloseRequested = YES;

	// The containing loop must run its recovered close callback before any GPU/window owner retires.
	return NO;
}
- (void)windowWillEnterFullScreen:(NSNotification*)notification {
	static_cast<void>(notification);
	_FullscreenTransition = YES;
}
- (void)windowWillExitFullScreen:(NSNotification*)notification {
	static_cast<void>(notification);
	_FullscreenTransition = YES;
}
- (void)windowDidEnterFullScreen:(NSNotification*)notification {
	static_cast<void>(notification);
	_FullscreenTransition = NO;
}
- (void)windowDidExitFullScreen:(NSNotification*)notification {
	_FullscreenTransition = NO;
	[self windowDidResize:notification];
}
- (void)windowDidResize:(NSNotification*)notification {
	@try {
		NSWindow* window = notification.object;
		if (!_FullscreenTransition && (window.styleMask & NSWindowStyleMaskFullScreen) == 0 && !window.miniaturized) {
			const auto bounds = window.contentView.bounds;
			if (bounds.size.width > 0 && bounds.size.height > 0) {
				_WindowedWidth = bounds.size.width;
				_WindowedHeight = bounds.size.height;
				_WindowedSizeChanged = _WindowedSizeChanged || _TrackWindowedResizes;
			}
		}
	} @catch (NSException* exception) {
		if (_Failure == nil) {
			_Failure = exception;
		}
	}
}
@end

namespace sgre::render {

	static constexpr std::size_t kPresentationSlots = 3;
	static constexpr double kMaximumWindowPoints = 10000;

	static void RequirePresentationMainThread() {
		@try {
			if (![NSThread isMainThread]) {
				throw std::runtime_error("hidden sprite presentation requires the main thread");
			}
		} @catch (NSException* exception) {
			throw PresentationObjectiveCFailure(PresentationSystemOperation::Observation, exception);
		}
	}

	static NSApplication* AcquireHiddenApplication() {
		NSApplication* application = NSApp;
		if (application == nil) {
			application = [NSApplication sharedApplication];
			if (application == nil || ![application setActivationPolicy:NSApplicationActivationPolicyAccessory]) {
				throw std::runtime_error("hidden sprite presentation could not initialize its accessory application");
			}
		}
		if (application.active || application.activationPolicy != NSApplicationActivationPolicyAccessory || application.windows.count != 0) {
			throw std::runtime_error("hidden sprite presentation requires an inactive accessory application without existing windows");
		}

		// run/finishLaunching activate the application; this diagnostic owner calls neither.
		return application;
	}

	struct HiddenPresentationWindow {
		HiddenPresentationWindow() = default;
		HiddenPresentationWindow(const HiddenPresentationWindow&) = delete;
		HiddenPresentationWindow& operator=(const HiddenPresentationWindow&) = delete;
		HiddenPresentationWindow(HiddenPresentationWindow&&) = delete;
		HiddenPresentationWindow& operator=(HiddenPresentationWindow&&) = delete;

		NSApplication* Application = nil;
		NSWindow* Window = nil;
		NSView* View = nil;
		CAMetalLayer* Layer = nil;
		SGREPresentationWindowDelegate* Delegate = nil;
		__weak id<NSApplicationDelegate> PreviousApplicationDelegate = nil;
		NSMenu* PreviousMainMenu = nil;
		NSMenu* PreviousWindowsMenu = nil;
		NSMenu* WindowsMenu = nil;
		NSMenu* MainMenu = nil;
		NSView* LoadingOverlay = nil;
		NSTextField* LoadingText = nil;
		NSProgressIndicator* LoadingSpinner = nil;
		bool Interactive = false;
		std::uint32_t MaximumDrawableDimension = 0;
		mutable SpritePresentationGeometry LastGeometry;

		~HiddenPresentationWindow() {
			if (Window == nil) {
				return;
			}
			@try {
				if (![NSThread isMainThread]) {
					std::terminate();
				}
				@autoreleasepool {
					if (Application.delegate == Delegate && Delegate != nil) {
						Application.delegate = PreviousApplicationDelegate;
					}
					if (Application.windowsMenu == WindowsMenu && WindowsMenu != nil) {
						Application.windowsMenu = PreviousWindowsMenu;
					}
					if (Application.mainMenu == MainMenu && MainMenu != nil) {
						Application.mainMenu = PreviousMainMenu;
					}
					Window.delegate = nil;
					[Window orderOut:nil];
					View.layer = nil;
					Window.contentView = nil;
					[Window close];
				}
			} @catch (NSException*) {
				std::terminate();
			}
		}
	};

	struct SpritePresentationSlot {
		id<MTLCommandBuffer> Producer = nil;
		id<MTLCommandBuffer> Commands = nil;
		id<CAMetalDrawable> Drawable = nil;
	};

	static bool PresentationSlotAvailable(const SpritePresentationSlot& slot) {
		return slot.Commands == nil || slot.Commands.status == MTLCommandBufferStatusCompleted || slot.Commands.status == MTLCommandBufferStatusError;
	}

	static std::uint32_t PresentationPixelExtent(double value) {
		if (!std::isfinite(value) || value <= 0 || value > std::numeric_limits<std::uint32_t>::max() || value != std::floor(value)) {
			throw std::runtime_error("hidden sprite presentation backing extent is not a positive bounded integer pixel count");
		}

		return static_cast<std::uint32_t>(value);
	}

	static void RequirePresentationDrawable(id<CAMetalDrawable> drawable, const MetalSpritePresentationSource& source, std::uint32_t width, std::uint32_t height) {
		id<MTLTexture> destination = drawable.texture;
		if (destination == nil || destination.device != source.Device || destination.pixelFormat != MTLPixelFormatBGRA8Unorm || destination.width != width || destination.height != height || destination.sampleCount != 1) {
			throw std::runtime_error("hidden sprite presentation drawable device, format or physical dimensions differ");
		}
	}

	static SpritePresentationGeometry ReadPresentationGeometry(const HiddenPresentationWindow& window, const MetalSpritePresentationSource& source) {
		if (window.Window == nil || window.View == nil || window.Layer == nil || window.View.window != window.Window || (!window.Interactive && (window.Window.visible || window.Window.keyWindow || window.Window.mainWindow || window.Application.active))) {
			throw std::runtime_error("hidden sprite presentation window/view/application lifetime or hidden state changed");
		}

		if (window.Delegate.Failure != nil) {
			throw PresentationObjectiveCFailure(PresentationSystemOperation::Observation, window.Delegate.Failure);
		}
		const auto bounds = window.View.bounds;
		if (window.Interactive && std::isfinite(bounds.size.width) && std::isfinite(bounds.size.height) && (bounds.size.width == 0 || bounds.size.height == 0) && window.LastGeometry.WidthPixels != 0) {
			// An AppKit transition can temporarily remove the drawable extent. Keep input outside and defer transfer.
			auto previous = window.LastGeometry;
			previous.DrawableAvailable = false;
			previous.Miniaturized = window.Window.miniaturized;

			return previous;
		}
		const auto backing = [window.View convertRectToBacking:bounds];
		const auto scale = static_cast<double>(window.Window.backingScaleFactor);
		if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(bounds.size.width) || !std::isfinite(bounds.size.height) || bounds.size.width <= 0 || bounds.size.height <= 0 || bounds.origin.x != 0 || bounds.origin.y != 0 || backing.origin.x != 0 || backing.origin.y != 0) {
			throw std::runtime_error("hidden sprite presentation requires its actual finite positive backing conversion with zero origin");
		}

		SpritePresentationGeometry geometry;
		// Fractional AppKit backing edges need enough integer drawable pixels to cover the view.
		geometry.WidthPixels = PresentationPixelExtent(window.Interactive ? std::ceil(static_cast<double>(backing.size.width)) : static_cast<double>(backing.size.width));
		geometry.HeightPixels = PresentationPixelExtent(window.Interactive ? std::ceil(static_cast<double>(backing.size.height)) : static_cast<double>(backing.size.height));
		geometry.BackingScale = scale;
		geometry.WidthPoints = static_cast<double>(bounds.size.width);
		geometry.HeightPoints = static_cast<double>(bounds.size.height);
		if (!window.Interactive && (geometry.WidthPixels != source.Width || geometry.HeightPixels != source.Height || geometry.WidthPoints * scale != source.Width || geometry.HeightPoints * scale != source.Height)) {
			throw std::runtime_error("hidden sprite presentation actual backing dimensions differ from the submitted source target");
		}

		geometry.SourceWidthPixels = source.Width;
		geometry.SourceHeightPixels = source.Height;
		geometry.Content = FitPresentationRectangle(source.Width, source.Height, geometry.WidthPixels, geometry.HeightPixels);
		geometry.Fullscreen = (window.Window.styleMask & NSWindowStyleMaskFullScreen) != 0;
		geometry.Miniaturized = window.Window.miniaturized;
		geometry.WindowedWidthPoints = window.Interactive ? window.Delegate.WindowedWidth : geometry.WidthPoints;
		geometry.WindowedHeightPoints = window.Interactive ? window.Delegate.WindowedHeight : geometry.HeightPoints;
		geometry.WindowedSizeChanged = window.Delegate.WindowedSizeChanged;
		if (window.Interactive && (geometry.WidthPixels > window.MaximumDrawableDimension || geometry.HeightPixels > window.MaximumDrawableDimension)) {
			throw std::runtime_error("interactive backing extent exceeds the selected Metal GPU family's maximum 2D texture dimension; reduce window dimensions");
		}
		window.LastGeometry = geometry;

		return geometry;
	}

	class SpritePassPresentation::Implementation {
	public:
		struct EventLifetime {
			Implementation* Receiver = nullptr;
			const void* Borrower = nullptr;
		};

		Implementation(SpritePassRenderer& renderer, const std::filesystem::path& shader_source, SpritePresentationDescription description) : Owner(&renderer), Description(std::move(description)) {
			Boundary.Invoke(PresentationSystemOperation::Creation, [&] {
				RequirePresentationMainThread();
				if (Description.BufferCount != 2 && Description.BufferCount != 3) {
					throw std::runtime_error("hidden sprite presentation drawable capacity must be two or three");
				}

				@autoreleasepool {
					Source = SpritePassPresentationBridge::CreateSource(renderer);
					if (Source.Device == nil || Source.Queue == nil || Source.Queue.device != Source.Device || Source.Width == 0 || Source.Height == 0 || !Source.RequireLatest || !Source.CurrentSubmission || !Source.FaultCommit || !Source.RequireAlive) {
						throw std::runtime_error("hidden sprite presentation requires its actual renderer device, queue and source lifetime");
					}

					Source.RequireAlive(Source);
					PrepareTransfer(shader_source);
					CreateHiddenWindow(Description.BufferCount);
				}
				EventOwner = std::make_shared<EventLifetime>();
				EventOwner->Receiver = this;
			});
		}

		~Implementation() {
			if (SynchronizationOwner != nullptr || Presenting) {
				std::terminate();
			}
			if (EventOwner) {
				EventOwner->Receiver = nullptr;
			}
			@try {
				if (![NSThread isMainThread]) {
					std::terminate();
				}
				for (const auto& slot : Slots) {
					// A failed commit can leave an actual uncommitted command; waiting for it never completes.
					if (slot.Commands != nil && slot.Commands.status >= MTLCommandBufferStatusCommitted) {
						[slot.Commands waitUntilCompleted];
					}
				}
			} @catch (NSException*) {
				std::terminate();
			}
			// The window member detaches its layer only after every retained transfer has completed.
		}

		const SpritePassRenderer* Owner = nullptr;
		SpritePresentationDescription Description;
		bool Shown = false;
		MetalSpritePresentationSource Source;
		HiddenPresentationWindow Window;
		std::array<SpritePresentationSlot, kPresentationSlots> Slots;
		id<MTLRenderPipelineState> Pipeline = nil;
		std::shared_ptr<EventLifetime> EventOwner;
		mutable PresentationSystemBoundary Boundary;
		std::uint32_t ConfiguredDrawableCapacity = 0;
		bool DisplaySynchronized = true;
		bool Presenting = false;
		const PresentationSynchronization* SynchronizationOwner = nullptr;
#if defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
		std::uint64_t DrawableRequestCount = 0;
		std::function<void(PresentationSystemOperation, PresentationFixtureMoment)> SystemHook;
#endif

		template <typename Work>
		void CallSystem(PresentationSystemOperation operation, Work&& work) {
#if defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
			if (SystemHook) {
				SystemHook(operation, PresentationFixtureMoment::Before);
			}
#endif
			std::forward<Work>(work)();
#if defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
			if (SystemHook) {
				SystemHook(operation, PresentationFixtureMoment::After);
			}
#else
			static_cast<void>(operation);
#endif
		}

		void EncodeTransfer(id<MTLRenderCommandEncoder> encoder, id<MTLTexture> source, PresentationRectangle content) const {
			[encoder setRenderPipelineState:Pipeline];
			[encoder setCullMode:MTLCullModeNone];
			[encoder setViewport:MTLViewport{content.Left, content.Top, content.Width, content.Height, 0, 1}];
			if (Window.Interactive) {
				const std::array<float, 4> projection{static_cast<float>(content.Left), static_cast<float>(content.Top), static_cast<float>(1 / content.Width), static_cast<float>(1 / content.Height)};
				[encoder setFragmentBytes:projection.data() length:sizeof(projection) atIndex:0];
			}
			[encoder setFragmentTexture:source atIndex:0];
			[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		}

		void SetPolicy(bool synchronized) {
			CallSystem(PresentationSystemOperation::Policy, [&] {
				Window.Layer.displaySyncEnabled = synchronized ? YES : NO;
				DisplaySynchronized = synchronized;
			});
		}

		void PrepareTransfer(const std::filesystem::path& shader_source) {
			id<MTLLibrary> library = CompileMetalLibrary(Source.Device, shader_source);
			id<MTLFunction> vertex = [library newFunctionWithName:@"SpritePresentationVertexMain"];
			id<MTLFunction> fragment = [library newFunctionWithName:Description.VisibleWindow ? @"SpritePresentationScaledFragmentMain" : @"SpritePresentationFragmentMain"];
			if (vertex == nil || fragment == nil) {
				throw std::runtime_error("hidden sprite presentation transfer shader functions are missing");
			}

			MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
			descriptor.vertexFunction = vertex;
			descriptor.fragmentFunction = fragment;
			descriptor.rasterSampleCount = 1;
			descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
			descriptor.colorAttachments[0].blendingEnabled = NO;
			descriptor.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
			NSError* error = nil;
			Pipeline = [Source.Device newRenderPipelineStateWithDescriptor:descriptor error:&error];
			if (Pipeline == nil) {
				throw MetalOperationError("prepare hidden sprite presentation transfer", error);
			}
		}

		void CreateHiddenWindow(std::uint32_t buffer_count) {
			Window.Application = AcquireHiddenApplication();
			NSScreen* screen = [NSScreen mainScreen];
			const auto scale = static_cast<double>(screen.backingScaleFactor);
			if (screen == nil || !std::isfinite(scale) || scale <= 0) {
				throw std::runtime_error("hidden sprite presentation has no actual screen backing scale");
			}

			Window.Interactive = Description.VisibleWindow;
			auto width_points = Description.VisibleWindow ? static_cast<double>(Description.WindowWidthPoints) : static_cast<double>(Source.Width) / scale;
			auto height_points = Description.VisibleWindow ? static_cast<double>(Description.WindowHeightPoints) : static_cast<double>(Source.Height) / scale;
			if (width_points <= 0 || height_points <= 0 || width_points > (Description.VisibleWindow ? 16384 : kMaximumWindowPoints) || height_points > (Description.VisibleWindow ? 16384 : kMaximumWindowPoints)) {
				throw std::runtime_error("hidden sprite presentation target exceeds the bounded window point extent");
			}

			const auto style = Description.VisibleWindow ? NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable : NSWindowStyleMaskBorderless;
			if (Description.VisibleWindow) {
				// Apple Metal feature tables: Mac2/Apple3+ expose at least 16384px; Apple2 exposes 8192px.
				if ([Source.Device supportsFamily:MTLGPUFamilyMac2] || [Source.Device supportsFamily:MTLGPUFamilyApple3]) {
					Window.MaximumDrawableDimension = 16384;
				} else if ([Source.Device supportsFamily:MTLGPUFamilyApple2]) {
					Window.MaximumDrawableDimension = 8192;
				} else {
					throw std::runtime_error("interactive presentation requires a measured Metal GPU family texture limit");
				}
				const auto visible_content = [NSWindow contentRectForFrameRect:screen.visibleFrame styleMask:style];
				const auto maximum_points = static_cast<double>(Window.MaximumDrawableDimension) / scale;
				const auto fit = std::min({1.0, visible_content.size.width / width_points, visible_content.size.height / height_points, maximum_points / width_points, maximum_points / height_points});
				if (!std::isfinite(fit) || fit <= 0) {
					throw std::runtime_error("interactive presentation has no finite visible screen content extent");
				}
				width_points = std::max(1.0, std::floor(width_points * fit));
				height_points = std::max(1.0, std::floor(height_points * fit));
			}

			Window.Window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width_points, height_points) styleMask:style backing:NSBackingStoreBuffered defer:NO screen:screen];
			if (Window.Window == nil) {
				throw std::runtime_error("hidden sprite presentation window allocation failed");
			}
			Window.Window.releasedWhenClosed = NO;
			Window.Window.restorable = NO;
			if (Description.VisibleWindow) {
				if (Description.Title.size() > 4096) {
					throw std::runtime_error("interactive presentation title exceeds its startup bound");
				}
				NSString* title = [[NSString alloc] initWithBytes:Description.Title.data() length:Description.Title.size() encoding:NSUTF8StringEncoding];
				if (title == nil) {
					throw std::runtime_error("interactive presentation title must be valid UTF-8");
				}
				Window.Window.title = title;
				Window.Window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
				Window.Window.contentMinSize = NSMakeSize(160, 90);
				Window.Window.contentMaxSize = NSMakeSize(static_cast<double>(Window.MaximumDrawableDimension) / scale, static_cast<double>(Window.MaximumDrawableDimension) / scale);
				Window.Delegate = [[SGREPresentationWindowDelegate alloc] init];
				Window.Delegate.WindowedWidth = width_points;
				Window.Delegate.WindowedHeight = height_points;
				Window.Window.delegate = Window.Delegate;
				if (Description.WindowCentered) {
					const auto visible = screen.visibleFrame;
					const auto frame = Window.Window.frame;
					[Window.Window setFrameOrigin:NSMakePoint(NSMidX(visible) - frame.size.width * 0.5, NSMidY(visible) - frame.size.height * 0.5)];
				}
			}
			[Window.Window orderOut:nil];
			Window.View = Description.VisibleWindow ? [[SGREPresentationContentView alloc] initWithFrame:NSMakeRect(0, 0, width_points, height_points)] : [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width_points, height_points)];
			Window.Layer = [CAMetalLayer layer];
			if (Window.View == nil || Window.Layer == nil) {
				throw std::runtime_error("hidden sprite presentation view/layer allocation failed");
			}

			Window.Layer.device = Source.Device;
			Window.Layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
			Window.Layer.framebufferOnly = YES;
			Window.Layer.colorspace = nil;
			Window.Layer.presentsWithTransaction = NO;
			Window.Layer.displaySyncEnabled = YES;
			Window.Layer.allowsNextDrawableTimeout = YES;
			Window.Layer.maximumDrawableCount = buffer_count;
			const auto actual_capacity = Window.Layer.maximumDrawableCount;
			if (actual_capacity != buffer_count) {
				throw std::runtime_error("hidden sprite presentation layer rejected its configured drawable capacity");
			}
			ConfiguredDrawableCapacity = static_cast<std::uint32_t>(actual_capacity);
			Window.Layer.frame = Window.View.bounds;
			// NSView's hosting contract requires installing the managed layer before enabling wantsLayer.
			Window.View.layer = Window.Layer;
			Window.View.wantsLayer = YES;
			Window.Window.contentView = Window.View;
			const auto geometry = ReadPresentationGeometry(Window, Source);
			Window.Layer.contentsScale = geometry.BackingScale;
			Window.Layer.drawableSize = CGSizeMake(geometry.WidthPixels, geometry.HeightPixels);
		}

		std::uint32_t RequireLayerGeometry() const {
			Source.RequireAlive(Source);
			const auto actual_capacity = Window.Layer.maximumDrawableCount;
			if (actual_capacity != ConfiguredDrawableCapacity) {
				throw std::runtime_error("hidden sprite presentation drawable capacity changed");
			}
			if (!Window.View.wantsLayer || Window.View.layer != Window.Layer || Window.Layer.device != Source.Device || Window.Layer.pixelFormat != MTLPixelFormatBGRA8Unorm || !Window.Layer.framebufferOnly || Window.Layer.presentsWithTransaction || static_cast<bool>(Window.Layer.displaySyncEnabled) != DisplaySynchronized || Window.Layer.colorspace != nil || !Window.Layer.allowsNextDrawableTimeout) {
				throw std::runtime_error("hidden sprite presentation layer device, format or physical geometry changed");
			}
			const auto geometry = ReadPresentationGeometry(Window, Source);
			if (!geometry.DrawableAvailable) {
				return static_cast<std::uint32_t>(actual_capacity);
			}
			if (Window.Interactive) {
				// AppKit can resize or move to a screen with another backing scale between iterations.
				Window.Layer.frame = Window.View.bounds;
				Window.Layer.contentsScale = geometry.BackingScale;
				Window.Layer.drawableSize = CGSizeMake(geometry.WidthPixels, geometry.HeightPixels);
			}
			if (Window.Layer.contentsScale != geometry.BackingScale || Window.Layer.drawableSize.width != geometry.WidthPixels || Window.Layer.drawableSize.height != geometry.HeightPixels || Window.Layer.bounds.origin.x != 0 || Window.Layer.bounds.origin.y != 0 || Window.Layer.bounds.size.width != geometry.WidthPoints || Window.Layer.bounds.size.height != geometry.HeightPoints) {
				throw std::runtime_error("hidden sprite presentation layer device, format or physical geometry changed");
			}

			return static_cast<std::uint32_t>(actual_capacity);
		}

		void InspectTransfers() const {
			for (const auto& slot : Slots) {
				if (slot.Producer.status == MTLCommandBufferStatusError) {
					throw MetalOperationError("hidden sprite presentation source submission", slot.Producer.error);
				}
				if (slot.Commands.status == MTLCommandBufferStatusError) {
					throw MetalOperationError("hidden sprite presentation transfer", slot.Commands.error);
				}
			}
		}

		void RetireCompletedTransfers() {
			for (auto& slot : Slots) {
				if (slot.Commands != nil && slot.Commands.status == MTLCommandBufferStatusCompleted) {
					if (slot.Producer.status == MTLCommandBufferStatusError) {
						throw MetalOperationError("completed hidden sprite presentation source submission", slot.Producer.error);
					}
					// Return our drawable reference before requesting another from the bounded layer pool.
					slot = SpritePresentationSlot{};
				}
			}
		}
	};

	static SpritePresentationDescription DiagnosticPresentationDescription() {
		SpritePresentationDescription description;
		description.BufferCount = static_cast<std::uint32_t>(kPresentationSlots);

		return description;
	}

	SpritePassPresentation::SpritePassPresentation(SpritePassRenderer& renderer, const std::filesystem::path& transfer_shader_source) : m_Implementation(std::make_unique<Implementation>(renderer, transfer_shader_source, DiagnosticPresentationDescription())) {
	}

	SpritePassPresentation::SpritePassPresentation(SpritePassRenderer& renderer, const std::filesystem::path& transfer_shader_source, SpritePresentationDescription description) : m_Implementation(std::make_unique<Implementation>(renderer, transfer_shader_source, std::move(description))) {
	}

	SpritePassPresentation::~SpritePassPresentation() = default;

	NSView* SpritePassDebugInspectorBridge::ParentView(SpritePassPresentation& presentation) {
		RequirePresentationMainThread();
		if (!presentation.m_Implementation->Description.VisibleWindow) {
			throw std::runtime_error("debug inspector requires the actual interactive presentation");
		}

		return presentation.m_Implementation->Window.View;
	}

	void SpritePassPresentation::SetLoadingMessage(std::string_view message) {
		RequirePresentationMainThread();
		auto& state = *m_Implementation;
		if (message.size() > 4096 || message.find('\0') != std::string_view::npos) {
			throw std::runtime_error("presentation loading message exceeds its UTF-8 text boundary");
		}
		if (!state.Description.VisibleWindow) {
			return;
		}
		state.Boundary.Invoke(PresentationSystemOperation::Policy, [&] {
			auto& window = state.Window;
			if (message.empty()) {
				[window.LoadingSpinner stopAnimation:nil];
				[window.LoadingOverlay removeFromSuperview];
				window.LoadingOverlay = nil;
				window.LoadingText = nil;
				window.LoadingSpinner = nil;
				return;
			}
			NSString* text = [[NSString alloc] initWithBytes:message.data() length:message.size() encoding:NSUTF8StringEncoding];
			if (text == nil) {
				throw std::runtime_error("presentation loading message is not UTF-8");
			}
			if (window.LoadingOverlay == nil) {
				window.LoadingOverlay = [[NSView alloc] initWithFrame:window.View.bounds];
				window.LoadingOverlay.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
				window.LoadingOverlay.wantsLayer = YES;
				window.LoadingOverlay.layer.backgroundColor = [NSColor colorWithCalibratedWhite:0.06 alpha:1.0].CGColor;
				window.LoadingText = [NSTextField labelWithString:text];
				window.LoadingText.textColor = NSColor.whiteColor;
				window.LoadingText.font = [NSFont systemFontOfSize:16];
				window.LoadingText.alignment = NSTextAlignmentCenter;
				window.LoadingText.maximumNumberOfLines = 3;
				window.LoadingText.translatesAutoresizingMaskIntoConstraints = NO;
				window.LoadingSpinner = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
				window.LoadingSpinner.style = NSProgressIndicatorStyleSpinning;
				window.LoadingSpinner.indeterminate = YES;
				window.LoadingSpinner.translatesAutoresizingMaskIntoConstraints = NO;
				[window.LoadingOverlay addSubview:window.LoadingText];
				[window.LoadingOverlay addSubview:window.LoadingSpinner];
				[NSLayoutConstraint activateConstraints:@[ [window.LoadingText.centerXAnchor constraintEqualToAnchor:window.LoadingOverlay.centerXAnchor], [window.LoadingText.centerYAnchor constraintEqualToAnchor:window.LoadingOverlay.centerYAnchor], [window.LoadingText.widthAnchor constraintLessThanOrEqualToAnchor:window.LoadingOverlay.widthAnchor constant:-48], [window.LoadingSpinner.centerXAnchor constraintEqualToAnchor:window.LoadingOverlay.centerXAnchor], [window.LoadingSpinner.bottomAnchor constraintEqualToAnchor:window.LoadingText.topAnchor constant:-16], [window.LoadingSpinner.widthAnchor constraintEqualToConstant:24], [window.LoadingSpinner.heightAnchor constraintEqualToConstant:24] ]];
				[window.View addSubview:window.LoadingOverlay];
				[window.LoadingSpinner startAnimation:nil];
			}
			window.LoadingText.stringValue = text;
		});
	}

	void SpritePassPresentation::ShowWindow() {
		RequirePresentationMainThread();
		auto& state = *m_Implementation;
		if (!state.Description.VisibleWindow || state.EventOwner->Borrower == nullptr || state.Presenting) {
			throw std::runtime_error("showing a presentation requires explicit interactive policy and its actual idle event-pump owner");
		}
		if (state.Shown) {
			return;
		}
		state.Boundary.Invoke(PresentationSystemOperation::Policy, [&] {
			state.RequireLayerGeometry();
			if (![state.Window.Application setActivationPolicy:NSApplicationActivationPolicyRegular]) {
				throw std::runtime_error("interactive presentation could not select regular application policy");
			}
			state.Window.PreviousApplicationDelegate = state.Window.Application.delegate;
			state.Window.Application.delegate = state.Window.Delegate;
			state.Window.PreviousMainMenu = state.Window.Application.mainMenu;
			state.Window.MainMenu = [[NSMenu alloc] initWithTitle:@""];
			NSMenuItem* application_item = [[NSMenuItem alloc] initWithTitle:@"" action:nullptr keyEquivalent:@""];
			NSMenu* application_menu = [[NSMenu alloc] initWithTitle:@"Application"];
			NSMenuItem* quit_item = [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
			quit_item.target = state.Window.Application;
			[application_menu addItem:quit_item];
			application_item.submenu = application_menu;
			[state.Window.MainMenu addItem:application_item];
			state.Window.PreviousWindowsMenu = state.Window.Application.windowsMenu;
			state.Window.WindowsMenu = [[NSMenu alloc] initWithTitle:@"Window"];
			NSMenuItem* window_item = [[NSMenuItem alloc] initWithTitle:@"Window" action:nullptr keyEquivalent:@""];
			NSMenuItem* close_item = [[NSMenuItem alloc] initWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
			close_item.target = state.Window.Window;
			NSMenuItem* minimize_item = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
			minimize_item.target = state.Window.Window;
			NSMenuItem* fullscreen_item = [[NSMenuItem alloc] initWithTitle:@"Toggle Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
			fullscreen_item.target = state.Window.Window;
			fullscreen_item.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
			[state.Window.WindowsMenu addItem:close_item];
			[state.Window.WindowsMenu addItem:minimize_item];
			[state.Window.WindowsMenu addItem:fullscreen_item];
			window_item.submenu = state.Window.WindowsMenu;
			[state.Window.MainMenu addItem:window_item];
			state.Window.Application.windowsMenu = state.Window.WindowsMenu;
			state.Window.Application.mainMenu = state.Window.MainMenu;
			[state.Window.Application finishLaunching];
			if (![state.Window.Window makeFirstResponder:state.Window.View]) {
				throw std::runtime_error("interactive presentation could not install its owned gameplay responder");
			}
			state.Window.Delegate.TrackWindowedResizes = YES;
			[state.Window.Window makeKeyAndOrderFront:nil];
			if (@available(macOS 14.0, *)) {
				[state.Window.Application activate];
			} else {
				[state.Window.Application activateIgnoringOtherApps:NO];
			}
			state.Shown = true;
			if (state.Description.Fullscreen) {
				[state.Window.Window toggleFullScreen:nil];
			}
		});
	}

	SpriteApplicationWindowSource SpritePassApplicationPumpBridge::CreateSource(const SpritePassPresentation& presentation, const void* borrower) {
		RequirePresentationMainThread();
		const auto& state = *presentation.m_Implementation;
		state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			state.RequireLayerGeometry();
		});
		if (borrower == nullptr || state.EventOwner->Borrower != nullptr) {
			throw std::runtime_error("macOS application window requires its exclusive actual dispatcher borrow");
		}
		const std::weak_ptr<SpritePassPresentation::Implementation::EventLifetime> lifetime = state.EventOwner;
		SpriteApplicationWindowSource source;
		source.Application = state.Window.Application;
		source.Window = state.Window.Window;
		source.RequireLive = [lifetime, borrower] {
			RequirePresentationMainThread();
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr || live->Borrower != borrower) {
				throw std::runtime_error("macOS application window presentation owner has retired");
			}
			const auto& receiver = *live->Receiver;
			receiver.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
				@autoreleasepool {
					if (receiver.Window.Application != NSApp || (!receiver.Window.Interactive && (receiver.Window.Application.windows.count != 1 || receiver.Window.Application.windows.firstObject != receiver.Window.Window))) {
						throw std::runtime_error("macOS application dispatch requires its exact exclusive hidden application/window");
					}
					if (!receiver.Window.View.wantsLayer || receiver.Window.View.layer != receiver.Window.Layer) {
						throw std::runtime_error("macOS application attached target identity changed");
					}
					receiver.RequireLayerGeometry();
				}
			});
		};
		source.HasBoundTarget = [lifetime, borrower] {
			RequirePresentationMainThread();
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr || live->Borrower != borrower) {
				throw std::runtime_error("macOS application target owner has retired");
			}
			const auto& receiver = *live->Receiver;
			return receiver.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
				@autoreleasepool {
					receiver.RequireLayerGeometry();

					return receiver.Window.View.layer == receiver.Window.Layer && receiver.Window.Layer.device == receiver.Source.Device;
				}
			});
		};
		source.Interactive = state.Window.Interactive;
		source.Geometry = [lifetime, borrower] {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr || live->Borrower != borrower) {
				throw std::runtime_error("macOS presentation geometry owner has retired");
			}
			return ReadPresentationGeometry(live->Receiver->Window, live->Receiver->Source);
		};
		source.TakeCloseRequest = [lifetime, borrower] {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr || live->Borrower != borrower) {
				throw std::runtime_error("macOS presentation close-request owner has retired");
			}
			auto& window = live->Receiver->Window;
			const bool requested = window.Delegate.CloseRequested;
			window.Delegate.CloseRequested = NO;

			return requested;
		};
		source.ReleaseBorrow = [lifetime, borrower] {
			RequirePresentationMainThread();
			const auto live = lifetime.lock();
			if (live && live->Borrower == borrower) {
				live->Borrower = nullptr;
			}
		};
		state.EventOwner->Borrower = borrower;

		return source;
	}

	void SpritePassPresentation::RequireRenderer(const SpritePassRenderer& actual_renderer) const {
		RequirePresentationMainThread();
		const auto& state = *m_Implementation;
		state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			if (state.Owner != &actual_renderer) {
				throw std::runtime_error("hidden presentation belongs to another actual renderer namespace");
			}
			static_cast<void>(state.RequireLayerGeometry());
		});
	}

	void SpritePassPresentation::RegisterSynchronization(const PresentationSynchronization& synchronization) const {
		RequirePresentationMainThread();
		auto& state = *m_Implementation;
		state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			state.RequireLayerGeometry();
		});
		if (state.Presenting || state.SynchronizationOwner != nullptr) {
			throw std::runtime_error("hidden presentation requires one exclusive actual synchronization owner");
		}
		state.SynchronizationOwner = &synchronization;
	}

	void SpritePassPresentation::RequireSynchronization(const PresentationSynchronization& synchronization) const {
		RequirePresentationMainThread();
		const auto& state = *m_Implementation;
		if (state.SynchronizationOwner != &synchronization) {
			throw std::runtime_error("hidden synchronization registration belongs to another actual owner");
		}
		state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			state.RequireLayerGeometry();
		});
	}

	void SpritePassPresentation::ReleaseSynchronization(const PresentationSynchronization& synchronization) const {
		RequirePresentationMainThread();
		auto& state = *m_Implementation;
		if (state.Presenting || state.SynchronizationOwner != &synchronization) {
			throw std::runtime_error("hidden synchronization retirement requires its exact quiescent owner");
		}
		state.SynchronizationOwner = nullptr;
	}

	SpritePresentationSubmission SpritePassPresentation::CurrentSubmission() const {
		RequirePresentationMainThread();
		const auto& state = *m_Implementation;
		return state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			@autoreleasepool {
				state.RequireLayerGeometry();
				SpritePresentationSubmission submission;
				submission.Owner = state.Owner;
				submission.Identity = state.Source.CurrentSubmission();
				static_cast<void>(state.Source.RequireLatest(submission.Identity));

				return submission;
			}
		});
	}

	class PresentationPhase {
	public:
		explicit PresentationPhase(bool& presenting) : m_Presenting(presenting) {
			m_Presenting = true;
		}
		~PresentationPhase() noexcept {
			m_Presenting = false;
		}
		PresentationPhase(const PresentationPhase&) = delete;
		PresentationPhase& operator=(const PresentationPhase&) = delete;

	private:
		bool& m_Presenting;
	};

	SpritePresentationStatus SpritePassPresentation::TryPresentLatest(SpritePresentationSubmission submission) {
		RequirePresentationMainThread();
		auto& state = *m_Implementation;
		state.Boundary.RequireOperational();
		if (state.SynchronizationOwner != nullptr) {
			throw std::runtime_error("actual synchronization owner requires its checked current presentation path");
		}
		if (state.Presenting) {
			throw std::runtime_error("hidden presentation rejects a recursive actual transfer");
		}
		state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			if (submission.Owner == nullptr || submission.Owner != state.Owner) {
				throw std::runtime_error("hidden sprite presentation submission belongs to another renderer");
			}
			static_cast<void>(state.Source.RequireLatest(submission.Identity));
			state.RequireLayerGeometry();
		});
		const PresentationPhase phase(state.Presenting);

		return state.Boundary.Invoke(PresentationSystemOperation::Transfer, [&] {
			return TryPresentFrame(submission);
		});
	}

	SpritePresentationStatus SpritePassPresentation::TryPresentCurrent(PresentationSynchronization& synchronization, bool force_unsynchronized) {
		RequirePresentationMainThread();
		auto& state = *m_Implementation;
		state.Boundary.RequireOperational();
		if (state.Presenting) {
			throw std::runtime_error("hidden current presentation rejects a recursive actual transfer");
		}
		synchronization.RequirePresentation(*this);
		const auto interval = synchronization.Interval(force_unsynchronized);
		if (interval > 1) {
			throw std::runtime_error("hidden Metal synchronization admits only original fixed base interval0/1");
		}
		const auto submission = CurrentSubmission();
		const bool previous = state.DisplaySynchronized;
		const bool synchronized = interval != 0;
		const bool policy_attempted = previous != synchronized;
		const PresentationPhase phase(state.Presenting);
		try {
			if (policy_attempted) {
				state.Boundary.Invoke(PresentationSystemOperation::Policy, [&] {
					state.SetPolicy(synchronized);
				});
			}
			const auto status = state.Boundary.Invoke(PresentationSystemOperation::Transfer, [&] {
				return TryPresentFrame(submission);
			});
			if (status == SpritePresentationStatus::QueuedHidden) {
				state.Boundary.Invoke(PresentationSystemOperation::Observation, [&] {
					synchronization.Presented(*this);
				});
			} else if (policy_attempted) {
				state.Boundary.Invoke(PresentationSystemOperation::Policy, [&] {
					state.SetPolicy(previous);
				});
			}

			return status;
		} catch (...) {
			// The setter may have changed the real layer before throwing, even with an unchanged cache.
			if (policy_attempted) {
				static_cast<void>(state.Boundary.Cleanup(PresentationSystemOperation::Policy, [&] {
					state.SetPolicy(previous);
				}));
			}
			throw;
		}
	}

	SpritePresentationStatus SpritePassPresentation::TryPresentFrame(SpritePresentationSubmission submission) {
		RequirePresentationMainThread();
		auto& state = *m_Implementation;
		if (submission.Owner == nullptr || submission.Owner != state.Owner) {
			throw std::runtime_error("hidden sprite presentation submission belongs to another renderer");
		}

		@autoreleasepool {
			const auto source = state.Source.RequireLatest(submission.Identity);
			if (source.Target == nil || source.Producer == nil || source.Producer.commandQueue != state.Source.Queue || source.Target.device != state.Source.Device || source.Target.pixelFormat != MTLPixelFormatRGBA8Unorm || source.Target.width != state.Source.Width || source.Target.height != state.Source.Height || source.Target.sampleCount != 1 || (source.Target.usage & MTLTextureUsageShaderRead) == 0 || source.Target.hazardTrackingMode != MTLHazardTrackingModeTracked) {
				throw std::runtime_error("hidden sprite presentation source target device, format, dimensions, shader-read capability or hazard tracking differs");
			}
			state.RequireLayerGeometry();
			state.InspectTransfers();
			if (state.Window.Interactive && (state.Window.Window.miniaturized || !ReadPresentationGeometry(state.Window, state.Source).DrawableAvailable)) {
				state.RetireCompletedTransfers();

				return SpritePresentationStatus::DrawableUnavailable;
			}
			state.RetireCompletedTransfers();
			const auto found = std::find_if(state.Slots.begin(), state.Slots.end(), PresentationSlotAvailable);
			if (found == state.Slots.end()) {
				return SpritePresentationStatus::Busy;
			}

#if defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
			++state.DrawableRequestCount;
#endif
			const auto geometry = ReadPresentationGeometry(state.Window, state.Source);
			id<CAMetalDrawable> drawable = [state.Window.Layer nextDrawable];
			if (drawable == nil) {
				return SpritePresentationStatus::DrawableUnavailable;
			}
			if (state.Window.Interactive) {
				id<MTLTexture> actual = drawable.texture;
				if (actual.width == 0 || actual.height == 0 || actual.width > state.Window.MaximumDrawableDimension || actual.height > state.Window.MaximumDrawableDimension) {
					throw std::runtime_error("interactive drawable exceeds the selected Metal GPU family extent");
				}
				RequirePresentationDrawable(drawable, state.Source, static_cast<std::uint32_t>(actual.width), static_cast<std::uint32_t>(actual.height));
				if (actual.width != geometry.WidthPixels || actual.height != geometry.HeightPixels) {
					// A resize can leave a pooled drawable at its preceding size. Refresh on the next iteration.
					return SpritePresentationStatus::DrawableUnavailable;
				}
			} else {
				RequirePresentationDrawable(drawable, state.Source, geometry.WidthPixels, geometry.HeightPixels);
			}
			id<MTLTexture> destination = drawable.texture;

			MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
			pass.colorAttachments[0].texture = destination;
			pass.colorAttachments[0].loadAction = state.Window.Interactive ? MTLLoadActionClear : MTLLoadActionDontCare;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			id<MTLCommandBuffer> commands = nil;
			id<MTLRenderCommandEncoder> encoder = nil;
			bool commit_attempted = false;
			try {
				state.Boundary.Invoke(PresentationSystemOperation::Transfer, [&] {
					state.CallSystem(PresentationSystemOperation::Transfer, [&] {
						commands = [state.Source.Queue commandBuffer];
						encoder = [commands renderCommandEncoderWithDescriptor:pass];
						if (commands == nil || encoder == nil) {
							throw std::runtime_error("hidden sprite presentation transfer command allocation failed");
						}
						state.EncodeTransfer(encoder, source.Target, geometry.Content);
					});
					[encoder endEncoding];
					encoder = nil;
					[commands presentDrawable:drawable];
				});
				found->Producer = source.Producer;
				found->Commands = commands;
				found->Drawable = drawable;
				state.Boundary.Invoke(PresentationSystemOperation::Commit, [&] {
					state.CallSystem(PresentationSystemOperation::Commit, [&] {
						commit_attempted = true;
						[commands commit];
					});
				});
			} catch (...) {
				if (commit_attempted) {
					// A foreign commit exception does not prove whether the ordered queue accepted the work.
					state.Source.FaultCommit();
				}
				if (encoder != nil) {
					static_cast<void>(state.Boundary.Cleanup(PresentationSystemOperation::Transfer, [&] {
						[encoder endEncoding];
					}));
				}
				throw;
			}
		}

		return SpritePresentationStatus::QueuedHidden;
	}

	bool SpritePassPresentation::PollTransfers() {
		RequirePresentationMainThread();
		auto& state = *m_Implementation;
		if (state.Presenting) {
			throw std::runtime_error("hidden presentation transfers cannot retire inside an actual transfer");
		}

		return state.Boundary.Invoke(PresentationSystemOperation::Retirement, [&] {
			@autoreleasepool {
				state.InspectTransfers();
				state.RetireCompletedTransfers();

				return std::all_of(state.Slots.begin(), state.Slots.end(), [](const auto& slot) {
					return slot.Commands == nil;
				});
			}
		});
	}

	SpritePresentationGeometry SpritePassPresentation::Geometry() const {
		RequirePresentationMainThread();
		const auto& state = *m_Implementation;

		return state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			@autoreleasepool {
				return ReadPresentationGeometry(state.Window, state.Source);
			}
		});
	}

	std::uint32_t SpritePassPresentation::DrawableCapacity() const {
		RequirePresentationMainThread();
		const auto& state = *m_Implementation;

		return state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			@autoreleasepool {
				return state.RequireLayerGeometry();
			}
		});
	}

#if defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
	SpritePresentationFixtureObservation SpritePassPresentationFixtureBridge::Observe(const SpritePassPresentation& presentation) {
		RequirePresentationMainThread();
		const auto& state = *presentation.m_Implementation;
		SpritePresentationFixtureObservation result;
		result.Presenting = state.Presenting;
		result.DisplaySynchronized = state.DisplaySynchronized;
		result.CleanupFailed = state.Boundary.CleanupFailed();
		result.FailedOperation = state.Boundary.Operation();
		result.FailureKind = state.Boundary.Kind();
		result.ApplicationActive = state.Window.Application.active;
		result.WindowVisible = state.Window.Window.visible;
		result.WindowKey = state.Window.Window.keyWindow;
		result.WindowMain = state.Window.Window.mainWindow;
		result.ViewAttached = state.Window.View.window == state.Window.Window;
		result.LayerAttached = state.Window.View.layer == state.Window.Layer;
		result.DrawableRequests = state.DrawableRequestCount;
		for (const auto& slot : state.Slots) {
			if (slot.Commands != nil) {
				++result.RetainedTransfers;
			}
			if (slot.Drawable != nil) {
				++result.RetainedDrawables;
			}
		}

		return result;
	}

	SpritePresentationFixtureObjects SpritePassPresentationFixtureBridge::BorrowObjects(const SpritePassPresentation& presentation) {
		RequirePresentationMainThread();
		const auto& window = presentation.m_Implementation->Window;
		SpritePresentationFixtureObjects result;
		result.Window = window.Window;
		result.View = window.View;
		result.Layer = window.Layer;

		return result;
	}

	std::array<SpritePresentationFixtureCommands, 3> SpritePassPresentationFixtureBridge::RetainCommands(const SpritePassPresentation& presentation) {
		RequirePresentationMainThread();
		std::array<SpritePresentationFixtureCommands, 3> result;
		const auto& slots = presentation.m_Implementation->Slots;
		for (std::size_t index = 0; index < slots.size(); ++index) {
			result[index].Producer = slots[index].Producer;
			result[index].Transfer = slots[index].Commands;
		}

		return result;
	}

	void SpritePassPresentationFixtureBridge::SetSystemHook(SpritePassPresentation& presentation, std::function<void(PresentationSystemOperation, PresentationFixtureMoment)> hook) {
		RequirePresentationMainThread();
		auto& state = *presentation.m_Implementation;
		if (state.Presenting) {
			throw std::runtime_error("presentation fixture hook requires its quiescent actual owner");
		}
		state.SystemHook = std::move(hook);
	}

	std::vector<std::uint8_t> SpritePassPresentationFixtureBridge::CaptureInteractiveTransfer(const SpritePassPresentation& presentation, SpritePresentationSubmission submission, std::uint32_t width, std::uint32_t height) {
		RequirePresentationMainThread();
		const auto& state = *presentation.m_Implementation;
		if (!state.Window.Interactive || state.Presenting || submission.Owner != state.Owner || width == 0 || height == 0 || width > 64 || height > 64) {
			throw std::runtime_error("interactive transfer fixture requires an idle exact submission and bounded 64x64 destination");
		}
		return state.Boundary.Observe(PresentationSystemOperation::Observation, [&] {
			state.RequireLayerGeometry();
			const auto source = state.Source.RequireLatest(submission.Identity);
			MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
			descriptor.usage = MTLTextureUsageRenderTarget;
			descriptor.storageMode = MTLStorageModeShared;
			id<MTLTexture> destination = [state.Source.Device newTextureWithDescriptor:descriptor];
			MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
			pass.colorAttachments[0].texture = destination;
			pass.colorAttachments[0].loadAction = MTLLoadActionClear;
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
			id<MTLCommandBuffer> commands = [state.Source.Queue commandBuffer];
			id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
			if (destination == nil || commands == nil || encoder == nil) {
				throw std::runtime_error("interactive transfer fixture could not allocate bounded actual Metal owners");
			}
			state.EncodeTransfer(encoder, source.Target, FitPresentationRectangle(state.Source.Width, state.Source.Height, width, height));
			[encoder endEncoding];
			[commands commit];
			[commands waitUntilCompleted];
			if (commands.status != MTLCommandBufferStatusCompleted) {
				throw MetalOperationError("interactive transfer fixture", commands.error);
			}
			std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
			[destination getBytes:bytes.data() bytesPerRow:static_cast<std::size_t>(width) * 4 fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];

			return bytes;
		});
	}

	void SpritePassPresentationFixtureBridge::RequireDestination(const SpritePassPresentation& presentation, id<CAMetalDrawable> actual_drawable) {
		RequirePresentationMainThread();
		const auto& state = *presentation.m_Implementation;
		const auto geometry = ReadPresentationGeometry(state.Window, state.Source);
		RequirePresentationDrawable(actual_drawable, state.Source, geometry.WidthPixels, geometry.HeightPixels);
	}
#endif

} // namespace sgre::render
