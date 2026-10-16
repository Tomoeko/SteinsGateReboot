#include "sgre/platform/application_pump.hpp"

#include "hid_controller_presence.hpp"
#include "keyboard_sample.hpp"
#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/graphics_display_frame.hpp"
#include "sprite_application_pump_bridge.hpp"

#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/hidsystem/IOLLEvent.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <exception>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace sgre::platform {

	static void RequirePumpMainThread() {
		if (![NSThread isMainThread]) {
			throw std::runtime_error("macOS application pump requires its serialized main thread");
		}
	}

	static bool IsInactiveWindowlessSystemEvent(NSEvent* event, NSApplication* application, NSWindow* owned_window) {
		return event.type == NSEventTypeSystemDefined && event.window == nil && event.windowNumber == 0 && !application.active && !owned_window.visible && !owned_window.keyWindow && !owned_window.mainWindow;
	}

	static std::string RejectedEventDescription(NSEvent* event, NSApplication* application, NSWindow* owned_window) {
		const auto type = event.type;
		std::string result = "eventType=" + std::to_string(static_cast<std::uint64_t>(type));
		result += " timestamp=" + std::to_string(event.timestamp);
		if (type == NSEventTypePeriodic) {
			// Apple explicitly leaves NSEvent.window undefined for periodic events.
			result += " eventWindow=not-applicable-periodic";
		} else {
			NSWindow* actual_window = event.window;
			result += " eventWindow=" + std::string(actual_window == nil ? "none" : (actual_window == owned_window ? "owned" : "foreign"));
			result += " eventWindowNumber=" + std::to_string(static_cast<std::int64_t>(event.windowNumber));
		}
		if (type == NSEventTypeAppKitDefined || type == NSEventTypeSystemDefined || type == NSEventTypeApplicationDefined) {
			// Generic event fields are read only in their documented event domains, never key characters.
			result += " subtype=" + std::to_string(static_cast<std::int32_t>(event.subtype));
			result += " data1=" + std::to_string(static_cast<std::int64_t>(event.data1));
			result += " data2=" + std::to_string(static_cast<std::int64_t>(event.data2));
		}
		result += " ownedWindowNumber=" + std::to_string(static_cast<std::int64_t>(owned_window.windowNumber));
		result += " applicationActive=" + std::to_string(static_cast<bool>(application.active));
		result += " ownedVisible=" + std::to_string(static_cast<bool>(owned_window.visible));
		result += " ownedKey=" + std::to_string(static_cast<bool>(owned_window.keyWindow));
		result += " ownedMain=" + std::to_string(static_cast<bool>(owned_window.mainWindow));

		return result;
	}

	struct MacPhysicalKey {
		CGKeyCode Code;
		std::uint8_t NativeScan;
		std::uint16_t NativeVirtual;
	};

	// SDK ANSI key codes identify physical positions, independently of typed characters/layout.
	// Virtual identifiers here are only layout-independent controls; unknown focused VK conversions reject.
	static constexpr MacPhysicalKey kInputPhysicalKeys[] = {
	    {kVK_Escape, 0x1, 0x1b},
	    {kVK_ANSI_1, 0x2, 0x0},
	    {kVK_ANSI_2, 0x3, 0x0},
	    {kVK_ANSI_3, 0x4, 0x0},
	    {kVK_ANSI_4, 0x5, 0x0},
	    {kVK_ANSI_5, 0x6, 0x0},
	    {kVK_ANSI_6, 0x7, 0x0},
	    {kVK_ANSI_7, 0x8, 0x0},
	    {kVK_ANSI_8, 0x9, 0x0},
	    {kVK_ANSI_9, 0xa, 0x0},
	    {kVK_ANSI_0, 0xb, 0x0},
	    {kVK_ANSI_Minus, 0xc, 0x0},
	    {kVK_ANSI_Equal, 0xd, 0x0},
	    {kVK_Delete, 0xe, 0x8},
	    {kVK_Tab, 0xf, 0x9},
	    {kVK_ANSI_Q, 0x10, 0x0},
	    {kVK_ANSI_W, 0x11, 0x0},
	    {kVK_ANSI_E, 0x12, 0x0},
	    {kVK_ANSI_R, 0x13, 0x0},
	    {kVK_ANSI_T, 0x14, 0x0},
	    {kVK_ANSI_Y, 0x15, 0x0},
	    {kVK_ANSI_U, 0x16, 0x0},
	    {kVK_ANSI_I, 0x17, 0x0},
	    {kVK_ANSI_O, 0x18, 0x0},
	    {kVK_ANSI_P, 0x19, 0x0},
	    {kVK_ANSI_A, 0x1e, 0x0},
	    {kVK_ANSI_S, 0x1f, 0x0},
	    {kVK_ANSI_D, 0x20, 0x0},
	    {kVK_ANSI_F, 0x21, 0x0},
	    {kVK_ANSI_G, 0x22, 0x0},
	    {kVK_ANSI_H, 0x23, 0x0},
	    {kVK_ANSI_J, 0x24, 0x0},
	    {kVK_ANSI_K, 0x25, 0x0},
	    {kVK_ANSI_L, 0x26, 0x0},
	    {kVK_ANSI_Z, 0x2c, 0x0},
	    {kVK_ANSI_X, 0x2d, 0x0},
	    {kVK_ANSI_C, 0x2e, 0x0},
	    {kVK_ANSI_V, 0x2f, 0x0},
	    {kVK_ANSI_B, 0x30, 0x0},
	    {kVK_ANSI_N, 0x31, 0x0},
	    {kVK_ANSI_M, 0x32, 0x0},
	    {kVK_ANSI_LeftBracket, 0x1a, 0x0},
	    {kVK_ANSI_RightBracket, 0x1b, 0x0},
	    {kVK_Return, 0x1c, 0xd},
	    {kVK_Control, 0x1d, 0xa2},
	    {kVK_ANSI_Semicolon, 0x27, 0x0},
	    {kVK_ANSI_Quote, 0x28, 0x0},
	    {kVK_ANSI_Grave, 0x29, 0x0},
	    {kVK_Shift, 0x2a, 0xa0},
	    {kVK_ANSI_Backslash, 0x2b, 0x0},
	    {kVK_ANSI_Comma, 0x33, 0x0},
	    {kVK_ANSI_Period, 0x34, 0x0},
	    {kVK_ANSI_Slash, 0x35, 0x0},
	    {kVK_RightShift, 0x36, 0xa1},
	    {kVK_ANSI_KeypadMultiply, 0x37, 0x0},
	    {kVK_Option, 0x38, 0xa4},
	    {kVK_Space, 0x39, 0x20},
	    {kVK_F1, 0x3b, 0x70},
	    {kVK_F2, 0x3c, 0x71},
	    {kVK_F3, 0x3d, 0x72},
	    {kVK_F4, 0x3e, 0x73},
	    {kVK_F5, 0x3f, 0x74},
	    {kVK_F6, 0x40, 0x75},
	    {kVK_F7, 0x41, 0x76},
	    {kVK_F8, 0x42, 0x77},
	    {kVK_F9, 0x43, 0x78},
	    {kVK_F10, 0x44, 0x79},
	    {kVK_ANSI_KeypadClear, 0x45, 0xc},
	    {kVK_ANSI_Keypad7, 0x47, 0x67},
	    {kVK_ANSI_Keypad8, 0x48, 0x68},
	    {kVK_ANSI_Keypad9, 0x49, 0x69},
	    {kVK_ANSI_KeypadMinus, 0x4a, 0x6d},
	    {kVK_ANSI_Keypad4, 0x4b, 0x64},
	    {kVK_ANSI_Keypad5, 0x4c, 0x65},
	    {kVK_ANSI_Keypad6, 0x4d, 0x66},
	    {kVK_ANSI_KeypadPlus, 0x4e, 0x6b},
	    {kVK_ANSI_Keypad1, 0x4f, 0x61},
	    {kVK_ANSI_Keypad2, 0x50, 0x62},
	    {kVK_ANSI_Keypad3, 0x51, 0x63},
	    {kVK_ANSI_Keypad0, 0x52, 0x60},
	    {kVK_ANSI_KeypadDecimal, 0x53, 0x6e},
	    {kVK_F11, 0x57, 0x7a},
	    {kVK_F12, 0x58, 0x7b},
	    {kVK_F13, 0x64, 0x7c},
	    {kVK_F14, 0x65, 0x7d},
	    {kVK_F15, 0x66, 0x7e},
	    {kVK_JIS_Yen, 0x7d, 0x0},
	    {kVK_ANSI_KeypadEquals, 0x8d, 0x0},
	    {kVK_ANSI_KeypadEnter, 0x9c, 0xd},
	    {kVK_RightControl, 0x9d, 0xa3},
	    {kVK_JIS_KeypadComma, 0xb3, 0x6c},
	    {kVK_ANSI_KeypadDivide, 0xb5, 0x6f},
	    {kVK_RightOption, 0xb8, 0xa5},
	    {kVK_Home, 0xc7, 0x24},
	    {kVK_UpArrow, 0xc8, 0x26},
	    {kVK_PageUp, 0xc9, 0x21},
	    {kVK_LeftArrow, 0xcb, 0x25},
	    {kVK_RightArrow, 0xcd, 0x27},
	    {kVK_End, 0xcf, 0x23},
	    {kVK_DownArrow, 0xd0, 0x28},
	    {kVK_PageDown, 0xd1, 0x22},
	    // Apple USB KeyboardInsert 0x49 maps to this Help key-code position.
	    {kVK_Help, 0xd2, 0x2d},
	    {kVK_ForwardDelete, 0xd3, 0x2e},
	    {kVK_Command, 0xdb, 0x5b},
	    {kVK_RightCommand, 0xdc, 0x5c},
	    {kVK_ContextualMenu, 0xdd, 0x5d},
	};

	static bool KnownInputVirtual(std::uint8_t key) {
		if ((key >= 0x70 && key <= 0x87) || key == 1 || key == 2 || key == 4 || key == 5 || key == 6 || key == 0x10 || key == 0x11 || key == 0x12) {
			return true;
		}
		return std::any_of(std::begin(kInputPhysicalKeys), std::end(kInputPhysicalKeys), [key](const auto& physical) {
			return physical.NativeVirtual != 0 && physical.NativeVirtual == key;
		});
	}

	static bool KnownInputScan(std::uint8_t key) {
		return std::any_of(std::begin(kInputPhysicalKeys), std::end(kInputPhysicalKeys), [key](const auto& physical) {
			return physical.NativeScan == key;
		});
	}

	void SampleMacFunctionKeyboard(input::KeyboardSample& sample, const std::array<std::uint16_t, 24>& codes, const MacPhysicalKeyReader& reader) {
		if (!sample.Available) {
			return;
		}
		if (reader.Read == nullptr) {
			throw std::runtime_error("macOS focused function keyboard requires its physical key-state reader");
		}

		const bool function_modifier = reader.Read(reader.Context, kVK_Function);
		auto sampled = sample;
		for (std::size_t index = 0; index < codes.size(); ++index) {
			const auto code = codes[index];
			sampled.VirtualKeys[0x70 + index] = (index >= 12 || function_modifier) && code != 0xffff && reader.Read(reader.Context, code) ? 0x80 : 0;
		}
		sample = sampled;
	}

	void SampleMacPhysicalKeyboard(input::KeyboardSample& sample, const input::KeyMappings& actual_mappings, bool break_key_available, const MacPhysicalKeyReader& reader) {
		if (!sample.Available) {
			return;
		}
		if (reader.Read == nullptr) {
			throw std::runtime_error("macOS focused keyboard requires its physical key-state reader");
		}
		for (const auto& conversion : actual_mappings.ScanConversions()) {
			if (!KnownInputScan(conversion.Source)) {
				throw std::runtime_error("macOS focused InputHub requires recovered physical-key compatibility for scan code " + std::to_string(conversion.Source));
			}
		}
		for (const auto& conversion : actual_mappings.VirtualConversions()) {
			if (!KnownInputVirtual(conversion.Source) && !(conversion.Source == 3 && break_key_available)) {
				throw std::runtime_error("macOS focused InputHub requires recovered layout-specific virtual-key conversion for code " + std::to_string(conversion.Source));
			}
		}

		// The caller supplies its actual focus-gated function/mouse snapshot. Publish only after all reads succeed.
		const bool function_modifier = reader.Read(reader.Context, kVK_Function);
		auto sampled = sample;
		if (!function_modifier) {
			std::fill(sampled.VirtualKeys.begin() + 0x70, sampled.VirtualKeys.begin() + 0x7c, 0);
		}
		for (const auto& physical : kInputPhysicalKeys) {
			if (!function_modifier && physical.NativeVirtual >= 0x70 && physical.NativeVirtual <= 0x7b) {
				sampled.ScanKeys[physical.NativeScan] = 0;

				continue;
			}

			if (reader.Read(reader.Context, physical.Code)) {
				sampled.ScanKeys[physical.NativeScan] = 0x80;
				if (physical.NativeVirtual != 0) {
					sampled.VirtualKeys[physical.NativeVirtual] |= 0x80;
				}
			}
		}
		sampled.VirtualKeys[0x10] = sampled.VirtualKeys[0xa0] | sampled.VirtualKeys[0xa1];
		sampled.VirtualKeys[0x11] = sampled.VirtualKeys[0xa2] | sampled.VirtualKeys[0xa3];
		sampled.VirtualKeys[0x12] = sampled.VirtualKeys[0xa4] | sampled.VirtualKeys[0xa5];
		sample = sampled;
	}

	static bool ReadSessionPhysicalKey(void*, std::uint16_t code) {
		return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, code);
	}

	static std::int32_t NativeInputPixel(CGFloat value) {
		if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) || value >= 2147483648.0) {
			throw std::runtime_error("macOS actual input client coordinate exceeds native integer range");
		}
		return static_cast<std::int32_t>(value);
	}

	class MacApplicationPump::Implementation {
	public:
		Implementation(core::ApplicationEventControl& events, render::SpritePassPresentation& presentation, std::size_t maximum_events) : Events(events), Presentation(&presentation), MaximumEvents(maximum_events) {
			RequirePumpMainThread();
			events.RequireCanFlush();
			if (maximum_events == 0 || maximum_events > 65536) {
				throw std::runtime_error("macOS application pump requires a bounded positive OS event budget");
			}
			Window = render::SpritePassApplicationPumpBridge::CreateSource(presentation, this);
			try {
				RequireLive();
				TrackingView = Window.Window.contentView;
				if (TrackingView == nil) {
					throw std::runtime_error("actual input tracking requires the owned content view");
				}
				TrackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:TrackingView userInfo:nil];
				if (TrackingArea == nil) {
					throw std::runtime_error("actual input tracking could not register its native window area");
				}
				[TrackingView addTrackingArea:TrackingArea];
				PreviousMouseMoved = Window.Window.acceptsMouseMovedEvents;
				Window.Window.acceptsMouseMovedEvents = YES;
			} catch (...) {
				if (TrackingArea != nil) {
					[TrackingView removeTrackingArea:TrackingArea];
					TrackingArea = nil;
				}
				Window.ReleaseBorrow();
				throw;
			}
		}

		core::ApplicationEventControl& Events;
		render::SpritePassPresentation* Presentation;
		render::GraphicsDisplayFrame* Frames = nullptr;
		core::ApplicationClockPump* Clock = nullptr;
		HostPerformanceCounter Counter;
		HidControllerPresence Controllers;
		std::int32_t PendingWheelTicks = 0;
		std::int32_t SharedMouseWheelTicks = 0;
		std::uint32_t SharedMouseButtons = 0;
		bool MouseEntered = false;
		__weak NSView* TrackingView = nil;
		__strong NSTrackingArea* TrackingArea = nil;
		bool PreviousMouseMoved = false;
		render::SpriteApplicationWindowSource Window;
		core::WindowFramePacing* Pacing = nullptr;
		const std::size_t MaximumEvents;
		// Apple SDK physical compatibility codes; F21..F24/Break learn their real code from delivered events.
		std::array<std::uint16_t, 24> FunctionKeyCodes{kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20, 0xffff, 0xffff, 0xffff, 0xffff};
		std::uint16_t BreakCode = 0xffff;
		bool Dispatching = false;
		bool Closed = false;

		void RequireLive() const {
			RequirePumpMainThread();
			if (Closed || !Window.RequireLive || Window.Application == nil || Window.Window == nil) {
				throw std::runtime_error("macOS application pump requires its live actual hidden owner");
			}
			Events.RequireCanFlush();
			Window.RequireLive();
		}

		void Wait(std::int32_t milliseconds) {
			RequireLive();
			if (Dispatching) {
				throw std::runtime_error("macOS application wait cannot reenter its OS dispatch phase");
			}
			timespec request{};
			request.tv_nsec = milliseconds * 1000000;
			for (std::size_t interrupted = 0; interrupted < 1024; ++interrupted) {
				timespec remainder{};
				if (nanosleep(&request, &remainder) == 0) {
					return;
				}
				if (errno != EINTR) {
					throw std::runtime_error("macOS application nanosleep failed with errno " + std::to_string(errno));
				}
				if (remainder.tv_sec < 0 || remainder.tv_nsec < 0 || remainder.tv_nsec >= 1000000000) {
					throw std::runtime_error("macOS application interrupted wait returned an invalid duration");
				}
				request = remainder;
			}
			throw std::runtime_error("macOS application wait exceeded its bounded signal interruptions");
		}
	};

	MacApplicationPump::MacApplicationPump(core::ApplicationEventControl& events, render::SpritePassPresentation& presentation, std::size_t maximum_events) : m_Implementation(std::make_unique<Implementation>(events, presentation, maximum_events)) {
	}

	MacApplicationPump::~MacApplicationPump() {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void MacApplicationPump::RequireLive() const {
		m_Implementation->RequireLive();
	}

	std::int64_t MacApplicationPump::Frequency() const {
		RequireLive();

		return m_Implementation->Counter.Frequency();
	}

	std::int64_t MacApplicationPump::ReadRawCounter() {
		RequireLive();

		return static_cast<std::int64_t>(m_Implementation->Counter.ReadRawCounter());
	}

	void MacApplicationPump::DispatchEvents(core::ApplicationEventControl& events, core::WindowFramePacing& pacing) {
		auto& state = *m_Implementation;
		state.RequireLive();
		if (state.Dispatching || &events != &state.Events || !pacing.UsesControl(events) || (state.Pacing != nullptr && state.Pacing != &pacing)) {
			throw std::runtime_error("macOS application dispatch requires its exact nonrecursive event/pacing owners");
		}
		state.Pacing = &pacing;
		state.Dispatching = true;
		try {
			@autoreleasepool {
				@try {
					if (state.Window.TakeCloseRequest()) {
						events.RequestClose(1);
					}
					for (std::size_t work = 0; work < state.MaximumEvents; ++work) {
						NSEvent* event = [state.Window.Application nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
						if (event == nil) {
							state.Dispatching = false;
							return;
						}
						if (!state.Window.Interactive && IsInactiveWindowlessSystemEvent(event, state.Window.Application, state.Window.Window)) {
							// Windowless OS notifications are not input for an explicitly inactive hidden game.
							// Consume it within the same queue budget without invoking an AppKit or window handler.
							continue;
						}
						if (state.Window.Interactive && (event.type == NSEventTypePeriodic || event.window != state.Window.Window)) {
							// AppKit owns activation, application/menu, fullscreen and other process-local UI events.
							// They never enter the engine's owned-window key, wheel or pointer producers.
							[state.Window.Application sendEvent:event];
							state.Window.RequireLive();
							if (state.Window.TakeCloseRequest()) {
								events.RequestClose(1);
							}
							continue;
						}
						if (event.type == NSEventTypePeriodic || event.window != state.Window.Window) {
							// Preserve the actual queue event even if a later diagnostic property raises an OS exception.
							[state.Window.Application postEvent:event atStart:YES];
							throw std::runtime_error("macOS application dispatch requires the event's exact owned hidden window; application/foreign-window events remain unsupported; " + RejectedEventDescription(event, state.Window.Application, state.Window.Window));
						}

						if (event.type == NSEventTypeScrollWheel) {
							CGEventRef actual_event = event.CGEvent;
							if (actual_event == nullptr) {
								throw std::runtime_error("actual scroll event lacks its native CoreGraphics wheel record");
							}
							const auto delta = CGEventGetIntegerValueField(actual_event, kCGScrollWheelEventDeltaAxis1);
							if (delta < std::numeric_limits<std::int32_t>::min() / 120 || delta > std::numeric_limits<std::int32_t>::max() / 120) {
								throw std::runtime_error("actual mouse wheel delta exceeds native integer tick range");
							}
							const auto ticks = delta * 120;
							const auto total = static_cast<std::int64_t>(state.PendingWheelTicks) + ticks;
							if (total < std::numeric_limits<std::int32_t>::min() || total > std::numeric_limits<std::int32_t>::max()) {
								throw std::runtime_error("actual mouse wheel accumulation exceeds native integer tick range");
							}
							state.PendingWheelTicks = static_cast<std::int32_t>(total);
						}
						if (event.type == NSEventTypeMouseMoved || event.type == NSEventTypeLeftMouseDragged || event.type == NSEventTypeRightMouseDragged || event.type == NSEventTypeOtherMouseDragged || event.type == NSEventTypeMouseEntered) {
							state.MouseEntered = true;
						} else if (event.type == NSEventTypeMouseExited) {
							state.MouseEntered = false;
						}
						if (event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp) {
							NSString* characters = event.charactersIgnoringModifiers;
							if (characters.length == 1 && event.keyCode < 256) {
								const auto character = [characters characterAtIndex:0];
								if (character >= NSF1FunctionKey && character <= NSF24FunctionKey) {
									state.FunctionKeyCodes[character - NSF1FunctionKey] = event.keyCode;
								} else if (character == NSBreakFunctionKey) {
									state.BreakCode = event.keyCode;
								}
							}
						}
						if (state.Window.Interactive) {
							[state.Window.Application sendEvent:event];
						} else {
							[state.Window.Window sendEvent:event];
						}
						state.Window.RequireLive();
						if (state.Window.TakeCloseRequest()) {
							events.RequestClose(1);
						}
					}
					if (!state.Window.Interactive && [state.Window.Application nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:NO] != nil) {
						throw std::runtime_error("macOS application dispatch exceeded its bounded actual OS queue work");
					}
				} @catch (NSException* exception) {
					const char* reason = exception.reason.UTF8String;
					throw std::runtime_error("macOS application AppKit dispatch failed: " + std::string(reason == nullptr ? "unknown Objective-C exception" : reason));
				}
			}
			state.Dispatching = false;
		} catch (...) {
			state.Dispatching = false;
			throw;
		}
	}

	input::KeyboardSample MacApplicationPump::ReadFunctionKeySample() const {
		RequireLive();
		const auto& state = *m_Implementation;
		if (state.Dispatching) {
			throw std::runtime_error("function-key snapshot requires completed actual OS dispatch");
		}
		input::KeyboardSample sample;
		// Native048fa3 only samples while GetFocus() is the actual engine window.
		sample.Available = state.Window.Application.active && state.Window.Application.keyWindow == state.Window.Window && state.Window.Window.keyWindow;
		if (!sample.Available) {
			return sample;
		}
		MacPhysicalKeyReader reader;
		reader.Read = ReadSessionPhysicalKey;
		SampleMacFunctionKeyboard(sample, state.FunctionKeyCodes, reader);
		sample.VirtualKeys[1] = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft) ? 0x80 : 0;
		sample.VirtualKeys[2] = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonRight) ? 0x80 : 0;
		sample.VirtualKeys[4] = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonCenter) ? 0x80 : 0;
		if (state.BreakCode != 0xffff && CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, state.BreakCode)) {
			sample.VirtualKeys[3] = 0x80;
		}

		sample.VirtualKeys[0xa4] = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Option) ? 0x80 : 0;
		sample.VirtualKeys[0xa5] = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_RightOption) ? 0x80 : 0;
		sample.VirtualKeys[0x12] = sample.VirtualKeys[0xa4] | sample.VirtualKeys[0xa5];
		sample.VirtualKeys[0x0d] = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Return) || CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_ANSI_KeypadEnter) ? 0x80 : 0;

		return sample;
	}

	input::HubFrameSample MacApplicationPump::ReadHubFrame(const input::KeyMappings& actual_mappings) {
		RequireLive();
		auto& state = *m_Implementation;
		if (state.Dispatching) {
			throw std::runtime_error("InputHub sample requires completed actual OS dispatch");
		}
		state.Controllers.RequireKeyboardOnly();
		auto keyboard = ReadFunctionKeySample();
		state.SharedMouseButtons = 0;
		state.SharedMouseWheelTicks = 0;
		if (keyboard.Available) {
			MacPhysicalKeyReader reader;
			reader.Read = ReadSessionPhysicalKey;
			SampleMacPhysicalKeyboard(keyboard, actual_mappings, state.BreakCode != 0xffff, reader);
			for (std::uint32_t button = 0; button != 8; ++button) {
				if (CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, static_cast<CGMouseButton>(button))) {
					state.SharedMouseButtons |= std::uint32_t{1} << button;
				}
			}
			keyboard.VirtualKeys[5] = (state.SharedMouseButtons & 8) != 0 ? 0x80 : 0;
			keyboard.VirtualKeys[6] = (state.SharedMouseButtons & 16) != 0 ? 0x80 : 0;
			state.SharedMouseWheelTicks = state.PendingWheelTicks;
		}
		state.PendingWheelTicks = 0;
		auto frame = input::ResolveKeyboardOnlyFrame(keyboard, actual_mappings.Convert(keyboard));
		NSView* view = state.Window.Window.contentView;
		if (view == nil) {
			throw std::runtime_error("InputHub cursor extent lost its actual owned content view");
		}
		const auto geometry = state.Window.Geometry();
		frame.CursorExtent = input::InputExtent{static_cast<float>(geometry.SourceWidthPixels), static_cast<float>(geometry.SourceHeightPixels)};
		return frame;
	}

	input::MouseSample MacApplicationPump::ReadMouseFrame() const {
		RequireLive();
		const auto& state = *m_Implementation;
		if (state.Dispatching) {
			throw std::runtime_error("Input mouse sample requires completed actual OS dispatch");
		}
		input::MouseSample sample;
		sample.Buttons = state.SharedMouseButtons;
		sample.WheelTicks = state.SharedMouseWheelTicks;
		sample.EnterTracking = state.MouseEntered;
		NSWindow* window = state.Window.Window;
		NSView* view = window.contentView;
		if (view == nil) {
			throw std::runtime_error("Input mouse sample lost its actual owned content view");
		}
		const auto bounds = [view convertRectToBacking:view.bounds];
		const auto geometry = state.Window.Geometry();
		sample.Client = {0, 0, NativeInputPixel(geometry.SourceWidthPixels), NativeInputPixel(geometry.SourceHeightPixels)};
		// A hidden window cannot own WindowFromPoint. Do not activate it or capture the pointer.
		if (!window.visible || window.miniaturized || !geometry.DrawableAvailable) {
			if (state.Window.Interactive) {
				// Native mouse Inside is independent of pointer-hit status; unavailable UI is outside.
				sample.EnterTracking = false;
			}
			return sample;
		}
		const auto screen_point = NSEvent.mouseLocation;
		const auto window_point = [window convertPointFromScreen:screen_point];
		const auto point = [view convertPoint:window_point fromView:nil];
		const auto backing = [view convertPointToBacking:point];
		const auto x = static_cast<double>(backing.x - bounds.origin.x) * geometry.WidthPixels / bounds.size.width;
		const auto y = static_cast<double>(view.flipped ? backing.y - bounds.origin.y : NSMaxY(bounds) - backing.y) * geometry.HeightPixels / bounds.size.height;
		const auto mapped = render::MapPresentationPoint(geometry.Content, geometry.SourceWidthPixels, geometry.SourceHeightPixels, x, y);
		sample.ClientX = NativeInputPixel(mapped.X);
		sample.ClientY = NativeInputPixel(mapped.Y);
		sample.EnterTracking = state.MouseEntered && mapped.Inside;
		sample.TargetWindowMatches = mapped.Inside && [NSWindow windowNumberAtPoint:screen_point belowWindowWithWindowNumber:0] == window.windowNumber;
		return sample;
	}

	std::size_t MacApplicationPump::ObservedControllerCount() const {
		RequireLive();
		return m_Implementation->Controllers.Count();
	}

	void MacApplicationPump::PrecisionWaitOneMillisecond() {
		m_Implementation->Wait(1);
	}

	void MacApplicationPump::SuspendedWaitSixteenMilliseconds() {
		m_Implementation->Wait(16);
	}

	bool MacApplicationPump::HasBoundPresentationTarget() const {
		RequireLive();

		return m_Implementation->Window.HasBoundTarget();
	}

	void MacApplicationPump::Present(bool force_unsynchronized) {
		RequireLive();
		auto& state = *m_Implementation;
		if (state.Dispatching || state.Frames == nullptr || state.Clock == nullptr) {
			throw std::runtime_error("macOS application presentation requires the actual current-frame producer and original SyncInterval target join");
		}
		state.Frames->RequirePumpPhase(*state.Clock, *this);
		static_cast<void>(state.Frames->PresentCurrent(force_unsynchronized));
	}

	void MacApplicationPump::BindPresentation(render::GraphicsDisplayFrame& actual_frames, core::ApplicationClockPump& actual_clock) {
		RequireLive();
		auto& state = *m_Implementation;
		if (state.Dispatching || state.Frames != nullptr) {
			throw std::runtime_error("macOS presentation binding requires its actual inactive unbound dispatcher");
		}
		actual_clock.RequirePresentationBorrowAdmission(*this);
		actual_frames.RequirePumpBorrowAdmission(this, *state.Presentation, actual_clock, *this);
		// Both exact owners are checked before either reciprocal pointer is published.
		actual_frames.RegisterPumpBorrow(this, *state.Presentation, actual_clock, *this);
		actual_clock.RegisterPresentationBorrow(*this);
		state.Frames = &actual_frames;
		state.Clock = &actual_clock;
	}

	void MacApplicationPump::UnbindPresentation() {
		RequirePumpMainThread();
		auto& state = *m_Implementation;
		if (state.Dispatching) {
			throw std::runtime_error("macOS presentation cannot detach during actual OS dispatch");
		}
		if (state.Frames != nullptr) {
			state.Clock->RequirePresentationBorrowRetirement(*this);
			state.Frames->RequirePumpBorrowRetirement(this);
			state.Frames->ReleasePumpBorrow(this);
			state.Clock->ReleasePresentationBorrow(*this);
			state.Frames = nullptr;
			state.Clock = nullptr;
		}
	}

	void MacApplicationPump::Close() {
		RequirePumpMainThread();
		auto& state = *m_Implementation;
		if (state.Dispatching) {
			throw std::runtime_error("macOS application pump cannot retire during actual OS dispatch");
		}
		UnbindPresentation();
		if (state.TrackingArea != nil) {
			[state.TrackingView removeTrackingArea:state.TrackingArea];
			state.TrackingArea = nil;
			state.TrackingView = nil;
			if (state.Window.Window != nil && state.Window.Window.acceptsMouseMovedEvents) {
				state.Window.Window.acceptsMouseMovedEvents = state.PreviousMouseMoved;
			}
		}
		if (state.Window.ReleaseBorrow) {
			state.Window.ReleaseBorrow();
		}
		state.Window = {};
		state.Controllers.Close();
		state.Pacing = nullptr;
		state.Closed = true;
	}

} // namespace sgre::platform
