#include "sgre/platform/volume_menu.hpp"

#include "debug_inspector_bridge.hpp"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::platform {

	struct VolumeMenuState {
		audio::UserAudioSettings Settings;
		audio::UserAudioSettings Committed;
		std::optional<audio::UserAudioSettings> Changed;
		std::function<void()> OnOpen;
		std::exception_ptr Failure;
		bool Visible = false;
		std::optional<std::size_t> Dragged;
	};

	static std::uint32_t& VolumeValue(audio::UserAudioSettings& settings, std::size_t row) {
		const std::array<std::uint32_t*, 4> values{&settings.Master, &settings.Music, &settings.Voice, &settings.Effects};
		return *values.at(row);
	}

	static NSRect VolumePanel(NSRect bounds) {
		const auto width = std::min<CGFloat>(420, bounds.size.width - 24);
		const auto height = std::min<CGFloat>(286, bounds.size.height - 24);
		return NSMakeRect((bounds.size.width - width) / 2, (bounds.size.height - height) / 2, std::max<CGFloat>(1, width), std::max<CGFloat>(1, height));
	}

	static NSRect VolumeTrack(NSRect panel, std::size_t row) {
		return NSMakeRect(panel.origin.x + 20, panel.origin.y + 88 + static_cast<CGFloat>(row) * 48, std::max<CGFloat>(1, panel.size.width - 40), 12);
	}

	static void CommitVolume(VolumeMenuState& state) {
		if (state.Settings != state.Committed) {
			state.Changed = state.Settings;
			state.Committed = state.Settings;
		}
	}

} // namespace sgre::platform

@interface SGREVolumeMenuView : NSView {
@public
	sgre::platform::VolumeMenuState* State;
}
@end

namespace sgre::platform {

	static void VolumeCallback(SGREVolumeMenuView* view, const auto& operation) noexcept {
		if (view->State == nullptr || view->State->Failure) {
			return;
		}
		try {
			@try {
				operation(*view->State);
			} @catch (NSException*) {
				throw std::runtime_error("volume menu AppKit callback failed");
			}
		} catch (...) {
			view->State->Failure = std::current_exception();
		}
	}

	static void SetVolumeAtPointer(SGREVolumeMenuView* view, VolumeMenuState& state, NSEvent* event) {
		if (!state.Dragged) {
			return;
		}
		const auto track = VolumeTrack(VolumePanel(view.bounds), *state.Dragged);
		const auto point = [view convertPoint:event.locationInWindow fromView:nil];
		VolumeValue(state.Settings, *state.Dragged) = static_cast<std::uint32_t>(std::round(std::clamp((point.x - track.origin.x) / track.size.width, 0.0, 1.0) * 100));
		view.needsDisplay = YES;
	}

} // namespace sgre::platform

@implementation SGREVolumeMenuView
- (BOOL)isFlipped {
	return YES;
}
- (BOOL)isOpaque {
	return NO;
}
- (void)drawRect:(NSRect)dirty_rect {
	(void)dirty_rect;
	sgre::platform::VolumeCallback(self, [&](auto& state) {
		[[NSColor colorWithWhite:0 alpha:0.28] setFill];
		NSRectFillUsingOperation(self.bounds, NSCompositingOperationSourceOver);
		const auto panel = sgre::platform::VolumePanel(self.bounds);
		[[NSColor colorWithWhite:0.07 alpha:0.88] setFill];
		[[NSBezierPath bezierPathWithRoundedRect:panel xRadius:12 yRadius:12] fill];
		const auto attributes = @{NSFontAttributeName : [NSFont systemFontOfSize:14], NSForegroundColorAttributeName : NSColor.whiteColor};
		[@"Audio" drawAtPoint:NSMakePoint(panel.origin.x + 20, panel.origin.y + 16) withAttributes:@{NSFontAttributeName : [NSFont boldSystemFontOfSize:20], NSForegroundColorAttributeName : NSColor.whiteColor}];
		[@"Esc to return" drawAtPoint:NSMakePoint(panel.origin.x + panel.size.width - 112, panel.origin.y + 22) withAttributes:attributes];
		const std::array<NSString*, 4> labels{@"Master volume", @"Music volume", @"Voice volume", @"Sound effects volume"};
		for (std::size_t row = 0; row < labels.size(); ++row) {
			const auto track = sgre::platform::VolumeTrack(panel, row);
			[labels[row] drawAtPoint:NSMakePoint(track.origin.x, track.origin.y - 22) withAttributes:attributes];
			const auto value = sgre::platform::VolumeValue(state.Settings, row);
			[[NSString stringWithFormat:@"%u%%", value] drawAtPoint:NSMakePoint(NSMaxX(track) - 42, track.origin.y - 22) withAttributes:attributes];
			[[NSColor colorWithWhite:1 alpha:0.20] setFill];
			[[NSBezierPath bezierPathWithRoundedRect:track xRadius:6 yRadius:6] fill];
			auto fill = track;
			fill.size.width *= static_cast<CGFloat>(value) / 100;
			[[NSColor colorWithRed:0.35 green:0.72 blue:0.95 alpha:0.95] setFill];
			[[NSBezierPath bezierPathWithRoundedRect:fill xRadius:6 yRadius:6] fill];
			[[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMaxX(fill) - 7, track.origin.y - 1, 14, 14)] fill];
		}
	});
}
- (void)mouseDown:(NSEvent*)event {
	sgre::platform::VolumeCallback(self, [&](auto& state) {
		const auto point = [self convertPoint:event.locationInWindow fromView:nil];
		for (std::size_t row = 0; row < 4; ++row) {
			const auto track = sgre::platform::VolumeTrack(sgre::platform::VolumePanel(self.bounds), row);
			if (NSPointInRect(point, NSInsetRect(track, -8, -12))) {
				state.Dragged = row;
				sgre::platform::SetVolumeAtPointer(self, state, event);
				break;
			}
		}
	});
}
- (void)mouseDragged:(NSEvent*)event {
	sgre::platform::VolumeCallback(self, [&](auto& state) {
		sgre::platform::SetVolumeAtPointer(self, state, event);
	});
}
- (void)mouseUp:(NSEvent*)event {
	sgre::platform::VolumeCallback(self, [&](auto& state) {
		sgre::platform::SetVolumeAtPointer(self, state, event);
		state.Dragged.reset();
		sgre::platform::CommitVolume(state);
	});
}
@end

namespace sgre::platform {

	class VolumeMenu::Implementation {
	public:
		Implementation(render::SpritePassPresentation& presentation, audio::UserAudioSettings settings, std::function<void()> on_open) {
			ValidateUserAudioSettings(settings);
			State.Settings = settings;
			State.Committed = settings;
			State.OnOpen = std::move(on_open);
			NSView* parent = render::SpritePassDebugInspectorBridge::ParentView(presentation);
			if (parent == nil || ![NSThread isMainThread]) {
				throw std::runtime_error("volume menu requires its interactive main-thread game view");
			}
			try {
				@try {
					View = [[SGREVolumeMenuView alloc] initWithFrame:parent.bounds];
					if (View == nil) {
						throw std::runtime_error("volume menu could not create its view");
					}
					View->State = &State;
					View.wantsLayer = YES;
					View.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
					View.hidden = YES;
					[parent addSubview:View];
					__weak SGREVolumeMenuView* weak_view = View;
					Monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
					                                                handler:^NSEvent*(NSEvent* event) {
						                                              SGREVolumeMenuView* view = weak_view;
						                                              if (view == nil || event.window != view.window || event.keyCode != 53) {
							                                              return event;
						                                              }
						                                              VolumeCallback(view, [&](auto& state) {
							                                              if (!event.isARepeat) {
								                                              state.Visible = !state.Visible;
								                                              state.Dragged.reset();
								                                              CommitVolume(state);
								                                              view.hidden = !state.Visible;
								                                              view.needsDisplay = YES;
								                                              if (state.Visible && state.OnOpen) {
									                                              state.OnOpen();
								                                              }
							                                              }
						                                              });
						                                              return nil;
					                                                }];
					if (Monitor == nil) {
						throw std::runtime_error("volume menu could not register its Esc shortcut");
					}
				} @catch (NSException*) {
					throw std::runtime_error("volume menu AppKit setup failed");
				}
			} catch (...) {
				Detach();
				throw;
			}
		}
		~Implementation() {
			Detach();
		}
		void Detach() noexcept {
			@try {
				if (Monitor != nil) {
					[NSEvent removeMonitor:Monitor];
				}
				if (View != nil) {
					View->State = nullptr;
					[View removeFromSuperview];
				}
				Monitor = nil;
				View = nil;
			} @catch (NSException*) {
			}
		}
		VolumeMenuState State;
		SGREVolumeMenuView* View = nil;
		id Monitor = nil;
	};

	VolumeMenu::VolumeMenu(render::SpritePassPresentation& presentation, audio::UserAudioSettings settings, std::function<void()> on_open) : m_Implementation(std::make_unique<Implementation>(presentation, settings, std::move(on_open))) {
	}
	VolumeMenu::~VolumeMenu() = default;

	void VolumeMenu::Update() {
		if (m_Implementation->State.Failure) {
			std::rethrow_exception(m_Implementation->State.Failure);
		}
	}

	bool VolumeMenu::CapturesInput() const noexcept {
		return m_Implementation->State.Visible;
	}

	audio::UserAudioSettings VolumeMenu::Settings() const noexcept {
		return m_Implementation->State.Settings;
	}

	std::optional<audio::UserAudioSettings> VolumeMenu::TakeChanges() {
		auto& changed = m_Implementation->State.Changed;
		return std::exchange(changed, {});
	}

} // namespace sgre::platform
