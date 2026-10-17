#include "sgre/platform/debug_inspector.hpp"

#include "sgre/app/display_path.hpp"
#include "sgre/debug/inspector_panel.hpp"

#include "debug_inspector_bridge.hpp"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::platform {

	struct DebugInspectorSearch {
		std::string Query;
		std::string Language = "en";
		std::shared_ptr<std::atomic_bool> Cancelled = std::make_shared<std::atomic_bool>(false);
	};

	struct DebugInspectorResults {
		std::vector<debug::InspectorRow> Rows;
		std::string Status;
	};

	static NSString* DebugInspectorString(std::string_view text) {
		NSString* result = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
		if (result == nil) {
			throw std::runtime_error("debug inspector text is not UTF-8");
		}

		return result;
	}

	static std::string DebugInspectorUtf8(NSString* text) {
		NSData* bytes = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
		if (bytes == nil || bytes.length > 4096) {
			throw std::runtime_error("debug inspector query exceeds 4096 UTF-8 bytes");
		}
		const auto* first = static_cast<const char*>(bytes.bytes);

		return std::string(first, bytes.length);
	}

	class DebugInspectorSearchWorker {
	public:
		explicit DebugInspectorSearchWorker(scenario::ScenarioSearchLocation location) : m_Location(std::move(location)), m_Worker([this] {
			                                                                                 Run();
		                                                                                 }) {
		}

		~DebugInspectorSearchWorker() {
			{
				const std::lock_guard lock(m_Mutex);
				m_Stopping = true;
				if (m_CurrentCancellation) {
					m_CurrentCancellation->store(true);
				}
			}
			m_Changed.notify_one();
			m_Worker.join();
		}

		void Search(std::string query) {
			const std::lock_guard lock(m_Mutex);
			if (m_CurrentCancellation) {
				m_CurrentCancellation->store(true);
			}
			m_Pending.emplace();
			m_Pending->Query = std::move(query);
			m_CurrentCancellation = m_Pending->Cancelled;
			m_Completed.reset();
			m_Changed.notify_one();
		}

		std::optional<DebugInspectorResults> Poll() {
			const std::lock_guard lock(m_Mutex);
			auto result = std::move(m_Completed);
			m_Completed.reset();

			return result;
		}

	private:
		DebugInspectorResults Execute(const DebugInspectorSearch& search) {
			if (!m_Catalog) {
				m_Catalog = std::make_unique<scenario::ScenarioSearchCatalog>(m_Location);
			}
			DebugInspectorResults result;
			const auto separator = search.Query.find_last_of(':');
			if (separator != std::string::npos && std::string_view(search.Query).substr(0, separator).ends_with(".ks")) {
				std::int32_t line = 0;
				const auto* first = search.Query.data() + separator + 1;
				const auto* last = search.Query.data() + search.Query.size();
				const auto parsed = std::from_chars(first, last, line);
				if (parsed.ec != std::errc{} || parsed.ptr != last || line < 0) {
					throw std::runtime_error("direct jump requires scene.ks:nonnegative-line");
				}
				const auto target = m_Catalog->ValidateJump(std::string_view(search.Query).substr(0, separator), line);
				result.Rows.push_back({target.CanonicalStorage + ":" + std::to_string(line), target.SceneLabel, "Original snapshot line " + std::to_string(target.SnapshotLine) + ", point " + std::to_string(target.Point)});
				result.Status = "Validated original restore point";
				return result;
			}
			scenario::ScenarioSearchRequest request;
			request.Query = search.Query;
			request.Language = search.Language;
			request.Limit = 500;
			request.Cancelled = [cancelled = search.Cancelled] {
				return cancelled->load();
			};
			auto found = m_Catalog->Search(request);
			for (auto& match : found.Matches) {
				result.Rows.push_back({std::move(match.JumpTarget), match.SceneLabel + (match.Character.empty() ? "" : " / " + match.Character), match.Translation.empty() ? std::move(match.OriginalText) : std::move(match.Translation)});
			}
			result.Status = std::to_string(result.Rows.size()) + " results" + (found.Truncated ? " (refine the query for more)" : "");

			return result;
		}

		void Run() noexcept {
			for (;;) {
				DebugInspectorSearch request;
				{
					std::unique_lock lock(m_Mutex);
					m_Changed.wait(lock, [this] {
						return m_Stopping || m_Pending.has_value();
					});
					if (m_Stopping) {
						return;
					}
					request = std::move(*m_Pending);
					m_Pending.reset();
				}
				DebugInspectorResults result;
				try {
					result = Execute(request);
				} catch (const std::exception& error) {
					result.Status = std::string("Search failed: ") + error.what();
				} catch (...) {
					result.Status = "Search failed with an unknown exception";
				}
				{
					const std::lock_guard lock(m_Mutex);
					if (!m_Stopping && !request.Cancelled->load()) {
						m_Completed = std::move(result);
					}
				}
			}
		}

		scenario::ScenarioSearchLocation m_Location;
		std::mutex m_Mutex;
		std::condition_variable m_Changed;
		bool m_Stopping = false;
		std::optional<DebugInspectorSearch> m_Pending;
		std::optional<DebugInspectorResults> m_Completed;
		std::shared_ptr<std::atomic_bool> m_CurrentCancellation;
		std::unique_ptr<scenario::ScenarioSearchCatalog> m_Catalog;
		std::thread m_Worker;
	};

	struct DebugInspectorViewState {
		explicit DebugInspectorViewState(scenario::ScenarioSearchLocation location) : Worker(std::move(location)) {
		}
		DebugInspectorSearchWorker Worker;
		debug::InspectorPanelState Panel;
		debug::InspectorPanelInput Input;
		debug::InspectorPanelFrame Frame;
		std::vector<debug::InspectorRow> Rows;
		std::string Status = "Search text, a character, or a scene.ks:line target. Esc closes this panel.";
		std::optional<std::string> Jump;
		bool Busy = false;
		bool Dirty = true;
		bool SelectingQuery = false;
		NSRange SelectionAnchor = NSMakeRange(0, 0);
		NSSelectionGranularity SelectionGranularity = NSSelectByCharacter;
	};

} // namespace sgre::platform

@interface SGREImmediateInspectorView : NSView <NSTextFieldDelegate> {
@public
	sgre::platform::DebugInspectorViewState* State;
	NSTextField* Query;
}
- (void)refresh;
- (void)submitQuery:(id)sender;
@end

namespace sgre::platform {

	static void DebugInspectorCallback(SGREImmediateInspectorView* view, const auto& operation) noexcept {
		@try {
			try {
				if (view->State != nullptr) {
					operation(*view->State);
				}
			} catch (const std::exception& error) {
				if (view->State != nullptr) {
					try {
						view->State->Status = std::string("Inspector error: ") + error.what();
					} catch (...) {
						// Retain the previous status if allocation failed at this boundary.
					}
					view->State->Dirty = true;
				}
			} catch (...) {
				if (view->State != nullptr) {
					view->State->Dirty = true;
				}
			}
		} @catch (NSException*) {
			// No C++ or Objective-C exception may escape a platform callback.
		}
	}

	static NSColor* DebugInspectorColor(std::uint32_t color) {
		return [NSColor colorWithCalibratedRed:static_cast<double>((color >> 24) & 255) / 255 green:static_cast<double>((color >> 16) & 255) / 255 blue:static_cast<double>((color >> 8) & 255) / 255 alpha:static_cast<double>(color & 255) / 255];
	}

	static void RestoreGameFirstResponder(SGREImmediateInspectorView* view) {
		[view.window endEditingFor:nil];
		if (![view.window makeFirstResponder:view.superview]) {
			[view.window makeFirstResponder:nil];
		}
	}

	static bool HandleQuerySelection(SGREImmediateInspectorView* view, DebugInspectorViewState& state, NSEvent* event) {
		const auto type = event.type;
		if (type != NSEventTypeLeftMouseDown && type != NSEventTypeLeftMouseDragged && type != NSEventTypeLeftMouseUp) {
			return false;
		}
		if (type == NSEventTypeLeftMouseDown) {
			const auto bounds = [view->Query convertRect:view->Query.bounds toView:nil];
			if (!state.Panel.Visible || view->Query.hidden || !NSPointInRect(event.locationInWindow, bounds)) {
				return false;
			}
			state.SelectingQuery = true;
		} else if (!state.SelectingQuery) {
			return false;
		}
		if (view->Query.currentEditor == nil) {
			[view->Query selectText:nil];
		}
		NSText* current = view->Query.currentEditor;
		if (![current isKindOfClass:NSTextView.class]) {
			state.SelectingQuery = false;
			return true;
		}
		NSTextView* editor = static_cast<NSTextView*>(current);
		const auto point = [editor convertPoint:event.locationInWindow fromView:nil];
		const auto index = [editor characterIndexForInsertionAtPoint:point];
		const auto length = editor.string.length;
		if (index > length) {
			throw std::runtime_error("query selection exceeds its native text extent");
		}
		if (type == NSEventTypeLeftMouseDown) {
			state.SelectionGranularity = event.clickCount >= 3 ? NSSelectByParagraph : event.clickCount == 2 ? NSSelectByWord
			                                                                                                 : NSSelectByCharacter;
			state.SelectionAnchor = [editor selectionRangeForProposedRange:NSMakeRange(index, 0) granularity:state.SelectionGranularity];
			if ((event.modifierFlags & NSEventModifierFlagShift) != 0) {
				state.SelectionAnchor = editor.selectedRange;
			}
		}
		const auto cursor = [editor selectionRangeForProposedRange:NSMakeRange(index, 0) granularity:state.SelectionGranularity];
		const auto first = std::min(cursor.location, state.SelectionAnchor.location);
		const auto end = std::max(NSMaxRange(cursor), NSMaxRange(state.SelectionAnchor));
		if (first > end || end > length) {
			throw std::runtime_error("query selection range exceeds its native text extent");
		}
		// AppKit's normal mouseDown tracks until release. Handle each event once
		// so the ordinary game/audio pump continues while selecting text.
		[editor setSelectedRange:NSMakeRange(first, end - first) affinity:NSSelectionAffinityDownstream stillSelecting:type != NSEventTypeLeftMouseUp];
		[editor scrollRangeToVisible:editor.selectedRange];
		if (type == NSEventTypeLeftMouseUp) {
			state.SelectingQuery = false;
		}

		return true;
	}

	static void DebugInspectorPointer(SGREImmediateInspectorView* view, NSEvent* event) {
		const auto point = [view.superview convertPoint:event.locationInWindow fromView:nil];
		view->State->Input.PointerX = static_cast<float>(point.x);
		view->State->Input.PointerY = static_cast<float>(view.superview.bounds.size.height - point.y);
	}

} // namespace sgre::platform

@implementation SGREImmediateInspectorView
- (BOOL)isFlipped {
	return YES;
}
- (BOOL)acceptsFirstResponder {
	return YES;
}
- (void)refresh {
	sgre::platform::DebugInspectorCallback(self, [&](auto& state) {
		const auto viewport = self.superview.bounds.size;
		if (viewport.width < 128 || viewport.height < 128) {
			self.hidden = YES;
			return;
		}
		state.Input.ViewportWidth = static_cast<float>(viewport.width);
		state.Input.ViewportHeight = static_cast<float>(viewport.height);
		state.Frame = sgre::debug::BuildInspectorPanel(state.Panel, {state.Rows, state.Status, state.Busy}, state.Input);
		state.Input.PointerPressed = false;
		state.Input.PointerReleased = false;
		state.Input.WheelRows = 0;
		state.Input.SubmitQuery = false;
		state.Input.Escape = false;
		if (!state.Panel.Visible) {
			state.SelectingQuery = false;
		}
		if (!state.Panel.Visible && !self.hidden) {
			sgre::platform::RestoreGameFirstResponder(self);
		}
		self.hidden = !state.Panel.Visible;
		const auto& bounds = state.Panel.Bounds;
		self.frame = NSMakeRect(bounds.X, viewport.height - bounds.Y - bounds.Height, bounds.Width, bounds.Height);
		const auto& query = state.Frame.QueryBounds;
		Query.hidden = !state.Panel.Visible || query.Width <= 0;
		Query.frame = NSMakeRect(query.X - bounds.X, query.Y - bounds.Y, query.Width, query.Height);
		if (state.Frame.Search || state.Frame.Browse) {
			state.Panel.Query = state.Frame.Browse ? std::string{} : sgre::platform::DebugInspectorUtf8(Query.stringValue);
			Query.stringValue = sgre::platform::DebugInspectorString(state.Panel.Query);
			state.Worker.Search(state.Panel.Query);
			state.Busy = true;
			state.Status = "Searching original scenario records…";
		}
		if (state.Frame.Jump && state.Panel.Selected) {
			state.Jump = state.Rows[*state.Panel.Selected].Target;
			state.Status = "Restoring selected original state…";
		}
		if (state.Frame.Copy && state.Panel.Selected) {
			NSPasteboard* clipboard = NSPasteboard.generalPasteboard;
			[clipboard clearContents];
			[clipboard setString:sgre::platform::DebugInspectorString(state.Rows[*state.Panel.Selected].Target) forType:NSPasteboardTypeString];
		}
		state.Dirty = state.Frame.Search || state.Frame.Browse || state.Frame.Jump;
		[self setNeedsDisplay:YES];
	});
}
- (void)drawRect:(NSRect)dirty {
	static_cast<void>(dirty);
	sgre::platform::DebugInspectorCallback(self, [&](auto& state) {
		if (!state.Panel.Visible) {
			return;
		}
		for (const auto& command : state.Frame.Paint) {
			const auto& bounds = command.Bounds;
			const NSRect area = NSMakeRect(bounds.X - state.Panel.Bounds.X, bounds.Y - state.Panel.Bounds.Y, bounds.Width, bounds.Height);
			NSColor* color = sgre::platform::DebugInspectorColor(command.Color);
			if (command.Type == sgre::debug::PanelPaint::Kind::Fill) {
				[color setFill];
				NSRectFill(area);
			} else if (command.Type == sgre::debug::PanelPaint::Kind::Border) {
				[color setStroke];
				[NSBezierPath strokeRect:NSInsetRect(area, 0.5, 0.5)];
			} else {
				NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
				paragraph.lineBreakMode = command.Wrap ? NSLineBreakByWordWrapping : NSLineBreakByTruncatingTail;
				[sgre::platform::DebugInspectorString(command.Text) drawInRect:area withAttributes:@{NSFontAttributeName : [NSFont systemFontOfSize:command.FontSize], NSForegroundColorAttributeName : color, NSParagraphStyleAttributeName : paragraph}];
			}
		}
	});
}
- (void)mouseDown:(NSEvent*)event {
	sgre::platform::DebugInspectorCallback(self, [&](auto& state) {
		sgre::platform::DebugInspectorPointer(self, event);
		state.Input.PointerPressed = true;
		state.Input.PointerHeld = true;
		[self refresh];
	});
}
- (void)mouseDragged:(NSEvent*)event {
	sgre::platform::DebugInspectorCallback(self, [&](auto& state) {
		sgre::platform::DebugInspectorPointer(self, event);
		state.Input.PointerHeld = true;
		[self refresh];
	});
}
- (void)mouseUp:(NSEvent*)event {
	sgre::platform::DebugInspectorCallback(self, [&](auto& state) {
		sgre::platform::DebugInspectorPointer(self, event);
		state.Input.PointerHeld = false;
		state.Input.PointerReleased = true;
		[self refresh];
	});
}
- (void)scrollWheel:(NSEvent*)event {
	sgre::platform::DebugInspectorCallback(self, [&](auto& state) {
		sgre::platform::DebugInspectorPointer(self, event);
		state.Input.WheelRows += static_cast<float>(-event.scrollingDeltaY / (event.hasPreciseScrollingDeltas ? 20.0 : 1.0));
		[self refresh];
	});
}
- (void)controlTextDidChange:(NSNotification*)notification {
	static_cast<void>(notification);
	sgre::platform::DebugInspectorCallback(self, [&](auto& state) {
		state.Panel.Query = sgre::platform::DebugInspectorUtf8(Query.stringValue);
	});
}
- (void)submitQuery:(id)sender {
	static_cast<void>(sender);
	sgre::platform::DebugInspectorCallback(self, [&](auto& state) {
		state.Input.SubmitQuery = true;
		[self refresh];
	});
}
@end

namespace sgre::platform {

	class DebugInspector::Implementation {
	public:
		Implementation(render::SpritePassPresentation& presentation, scenario::ScenarioSearchLocation location) : State(std::move(location)) {
			if (![NSThread isMainThread]) {
				throw std::runtime_error("debug inspector requires the main thread");
			}
			NSView* parent = render::SpritePassDebugInspectorBridge::ParentView(presentation);
			try {
				@try {
					View = [[SGREImmediateInspectorView alloc] initWithFrame:NSZeroRect];
					if (View == nil) {
						throw std::runtime_error("debug inspector could not allocate its in-window view");
					}
					View->State = &State;
					View.wantsLayer = YES;
					View.hidden = YES;
					View->Query = [[NSTextField alloc] initWithFrame:NSZeroRect];
					View->Query.placeholderString = @"Text, character, scene or scene.ks:line";
					View->Query.font = [NSFont systemFontOfSize:13];
					View->Query.target = View;
					View->Query.action = @selector(submitQuery:);
					View->Query.delegate = View;
					[View addSubview:View->Query];
					[parent addSubview:View];
					__weak SGREImmediateInspectorView* weak_view = View;
					Monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp
					                                                handler:^NSEvent*(NSEvent* event) {
						                                              SGREImmediateInspectorView* view = weak_view;
						                                              if (view == nil) {
							                                              return event;
						                                              }
						                                              bool handled = false;
						                                              DebugInspectorCallback(view, [&](auto& state) {
							                                              if (event.window != view.window) {
								                                              return;
							                                              }
							                                              if (HandleQuerySelection(view, state, event)) {
								                                              handled = true;
								                                              return;
							                                              }
							                                              if (event.type != NSEventTypeKeyDown) {
								                                              return;
							                                              }
							                                              const auto modifiers = event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagShift | NSEventModifierFlagOption | NSEventModifierFlagControl);
							                                              const bool toggle = modifiers == (NSEventModifierFlagCommand | NSEventModifierFlagShift) && [event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"i"];
							                                              const bool escape = state.Panel.Visible && event.keyCode == 53;
							                                              if (!toggle && !escape) {
								                                              return;
							                                              }
							                                              handled = true;
							                                              if (!event.isARepeat) {
								                                              state.Panel.Visible = toggle && !state.Panel.Visible;
								                                              [view refresh];
								                                              if (state.Panel.Visible) {
									                                              [view.window makeFirstResponder:view->Query];
								                                              } else {
									                                              RestoreGameFirstResponder(view);
								                                              }
							                                              }
						                                              });

						                                              return handled ? nil : event;
					                                                }];
					if (Monitor == nil) {
						throw std::runtime_error("debug inspector could not register its local shortcut");
					}
				} @catch (NSException*) {
					throw std::runtime_error("debug inspector AppKit setup failed");
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
					View->Query.delegate = nil;
					View->Query.target = nil;
					[View removeFromSuperview];
				}
				Monitor = nil;
				View = nil;
			} @catch (NSException*) {
				// Detach is also used while unwinding a failed platform construction.
			}
		}

		DebugInspectorViewState State;
		SGREImmediateInspectorView* View = nil;
		id Monitor = nil;
	};

	DebugInspector::DebugInspector(render::SpritePassPresentation& presentation, scenario::ScenarioSearchLocation location) : m_Implementation(std::make_unique<Implementation>(presentation, std::move(location))) {
	}

	DebugInspector::~DebugInspector() = default;

	void DebugInspector::Toggle() {
		auto& state = *m_Implementation;
		state.State.Panel.Visible = !state.State.Panel.Visible;
		[state.View refresh];
		DebugInspectorCallback(state.View, [&](auto& panel) {
			if (panel.Panel.Visible) {
				[state.View.window makeFirstResponder:state.View->Query];
			} else {
				RestoreGameFirstResponder(state.View);
			}
		});
	}

	void DebugInspector::Hide() {
		m_Implementation->State.Panel.Visible = false;
		[m_Implementation->View refresh];
	}

	void DebugInspector::Update() {
		auto& state = *m_Implementation;
		if (auto result = state.State.Worker.Poll()) {
			state.State.Rows = std::move(result->Rows);
			state.State.Status = app::RedactUserHome(result->Status, std::filesystem::path(DebugInspectorUtf8(NSHomeDirectory())));
			state.State.Busy = false;
			state.State.Panel.Selected.reset();
			state.State.Panel.FirstRow = 0;
			state.State.Dirty = true;
		}
		const auto viewport = state.View.superview.bounds.size;
		if (state.State.Dirty || state.State.Input.ViewportWidth != viewport.width || state.State.Input.ViewportHeight != viewport.height) {
			[state.View refresh];
		}
	}

	void DebugInspector::SetJumpStatus(std::string message) {
		m_Implementation->State.Status = std::move(message);
		m_Implementation->State.Dirty = true;
	}

	bool DebugInspector::CapturesInput() const noexcept {
		return m_Implementation->State.Panel.Visible;
	}

	std::optional<std::string> DebugInspector::TakeJumpRequest() {
		auto result = std::move(m_Implementation->State.Jump);
		m_Implementation->State.Jump.reset();

		return result;
	}

} // namespace sgre::platform
