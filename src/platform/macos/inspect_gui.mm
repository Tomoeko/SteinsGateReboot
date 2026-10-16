#include "sgre/platform/inspect_gui.hpp"

#include "sgre/scenario/scenario_search.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

namespace sgre::platform {

	struct InspectWorker {
		std::unique_ptr<scenario::ScenarioSearchCatalog> Catalog;
		std::filesystem::path Installation;
	};

	struct InspectJob {
		std::shared_ptr<std::atomic_bool> Cancelled = std::make_shared<std::atomic_bool>(false);
		std::optional<std::filesystem::path> RequestedInstallation;
		scenario::ScenarioSearchRequest Request;
		scenario::ScenarioSearchResults Results;
		std::vector<scenario::ScenarioArchiveEntry> Entries;
		std::filesystem::path Installation;
		std::exception_ptr Failure;
	};

	struct InspectGuiState {
		InspectGuiOptions Options;
		std::filesystem::path Workspace;
		std::shared_ptr<InspectWorker> Worker = std::make_shared<InspectWorker>();
		std::shared_ptr<InspectJob> ActiveJob;
		scenario::ScenarioSearchResults Results;
		std::vector<scenario::ScenarioArchiveEntry> Entries;
		std::filesystem::path Installation;
		std::exception_ptr Failure;
		bool Busy = false;
		bool Closing = false;
		bool Closed = false;
	};

	static NSString* InspectString(std::string_view value) {
		NSString* result = [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
		if (result == nil) {
			throw std::runtime_error("inspector text is not valid UTF-8");
		}

		return result;
	}

	static std::string InspectUtf8(NSString* value) {
		if (value == nil) {
			throw std::runtime_error("inspector received a missing native string");
		}

		NSData* data = [value dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
		if (data == nil) {
			throw std::runtime_error("inspector native string cannot be represented as UTF-8");
		}

		if (data.length == 0) {
			return {};
		}

		return std::string(static_cast<const char*>(data.bytes), data.length);
	}

	static NSString* InspectPath(const std::filesystem::path& path) {
		return InspectString(path.string());
	}

	static std::exception_ptr InspectNativeFailure(NSException* error) noexcept {
		try {
			return std::make_exception_ptr(std::runtime_error("AppKit inspector failure: " + InspectUtf8(error.reason != nil ? error.reason : error.name)));
		} catch (...) {
			return std::current_exception();
		}
	}

	static std::string InspectFailureText(const std::exception_ptr& failure) {
		try {
			std::rethrow_exception(failure);
		} catch (const std::exception& error) {
			return error.what();
		} catch (...) {
			return "unknown inspector failure";
		}
	}

	static std::string InspectShellQuote(std::string_view value) {
		std::string result = "'";
		for (const char character : value) {
			if (character == '\'') {
				result += "'\\''";
			} else {
				result += character;
			}
		}
		result += '\'';

		return result;
	}

	static std::string InspectJumpCommand(const scenario::ScenarioSearchMatch& match, const std::filesystem::path& installation) {
		return "sgre-game " + InspectShellQuote(installation.string()) + " --jump " + InspectShellQuote(match.JumpTarget);
	}

	static void InspectStopEventLoop() {
		[NSApp stop:nil];
		// stop: is observed after an event dispatch, including when shutdown finished on the main queue.
		NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
		[NSApp postEvent:event atStart:NO];
	}

} // namespace sgre::platform

@interface SGREInspectController : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSTabViewDelegate> {
@public
	std::unique_ptr<sgre::platform::InspectGuiState> State;
@private
	dispatch_queue_t m_WorkQueue;
	NSWindow* m_Window;
	NSSearchField* m_Query;
	NSPopUpButton* m_Language;
	NSPopUpButton* m_Limit;
	NSButton* m_Search;
	NSButton* m_Cancel;
	NSButton* m_ChooseInstallation;
	NSTextField* m_Installation;
	NSTextField* m_Status;
	NSTableView* m_Matches;
	NSTableView* m_Entries;
	NSTabView* m_Tabs;
	NSTextView* m_Detail;
	NSButton* m_Copy;
	NSButton* m_Launch;
}
- (void)buildWindow;
- (void)startSearch;
- (void)beginClose;
- (void)finishClose;
- (void)callbackFailure:(std::exception_ptr)failure;
- (void)completeJob:(std::shared_ptr<sgre::platform::InspectJob>)job;
- (void)updateSelection;
- (void)updateEntrySelection;
- (void)updateControls;
- (void)drainWorker;
- (void)search:(id)sender;
- (void)cancel:(id)sender;
- (void)chooseInstallation:(id)sender;
- (void)copyJump:(id)sender;
- (void)launchGame:(id)sender;
- (void)quit:(id)sender;
@end

namespace sgre::platform {

	template <typename Action>
	static void InspectCallback(SGREInspectController* controller, const Action& action) noexcept {
		@try {
			try {
				action();
			} catch (...) {
				[controller callbackFailure:std::current_exception()];
			}
		} @catch (NSException* error) {
			[controller callbackFailure:InspectNativeFailure(error)];
		}
	}

	static NSTextField* InspectLabel(NSString* text) {
		NSTextField* label = [NSTextField labelWithString:text];
		label.lineBreakMode = NSLineBreakByTruncatingMiddle;

		return label;
	}

	static NSButton* InspectButton(NSString* title, id target, SEL action) {
		return [NSButton buttonWithTitle:title target:target action:action];
	}

	static NSTableView* InspectTable(NSArray<NSString*>* identifiers, NSArray<NSString*>* titles, NSArray<NSNumber*>* widths) {
		NSTableView* table = [[NSTableView alloc] initWithFrame:NSZeroRect];
		table.usesAlternatingRowBackgroundColors = YES;
		table.allowsMultipleSelection = NO;
		table.rowHeight = 25;
		table.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
		for (NSUInteger index = 0; index < identifiers.count; ++index) {
			NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:identifiers[index]];
			column.title = titles[index];
			column.width = widths[index].doubleValue;
			column.minWidth = 60;
			[table addTableColumn:column];
		}

		return table;
	}

	static NSScrollView* InspectScroll(NSView* view) {
		NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
		scroll.borderType = NSBezelBorder;
		scroll.hasVerticalScroller = YES;
		scroll.hasHorizontalScroller = YES;
		scroll.autohidesScrollers = YES;
		scroll.documentView = view;

		return scroll;
	}

	static NSStackView* InspectRow(NSArray<NSView*>* views) {
		NSStackView* row = [NSStackView stackViewWithViews:views];
		row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
		row.alignment = NSLayoutAttributeCenterY;
		row.spacing = 8;

		return row;
	}

	static void InspectInstallMenu(SGREInspectController* controller) {
		NSMenu* main = [[NSMenu alloc] initWithTitle:@"Inspector"];
		NSMenuItem* app_item = [[NSMenuItem alloc] initWithTitle:@"Inspector" action:nil keyEquivalent:@""];
		NSMenu* app = [[NSMenu alloc] initWithTitle:@"Inspector"];
		NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit sgre-inspect" action:@selector(quit:) keyEquivalent:@"q"];
		quit.target = controller;
		[app addItem:quit];
		app_item.submenu = app;
		[main addItem:app_item];

		NSMenuItem* edit_item = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
		NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
		[edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
		[edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
		[edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
		[edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
		edit_item.submenu = edit;
		[main addItem:edit_item];

		NSMenuItem* window_item = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
		NSMenu* window = [[NSMenu alloc] initWithTitle:@"Window"];
		[window addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
		[window addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
		window_item.submenu = window;
		[main addItem:window_item];
		NSApp.mainMenu = main;
		NSApp.windowsMenu = window;
	}

	static void InspectPerformSearch(const std::shared_ptr<InspectWorker>& worker, const std::shared_ptr<InspectJob>& job, const std::filesystem::path& workspace) noexcept {
		@autoreleasepool {
			@try {
				try {
					if (job->Cancelled->load(std::memory_order_relaxed)) {
						job->Results.Cancelled = true;
						return;
					}

					if (worker->Catalog == nullptr || (job->RequestedInstallation && worker->Installation != *job->RequestedInstallation)) {
						auto location = scenario::ResolveScenarioSearchLocation(workspace, job->RequestedInstallation);
						auto catalog = std::make_unique<scenario::ScenarioSearchCatalog>(location);
						worker->Installation = location.Installation;
						worker->Catalog = std::move(catalog);
					}

					const auto entries = worker->Catalog->Entries();
					job->Entries.assign(entries.begin(), entries.end());
					job->Installation = worker->Installation;
					job->Request.Cancelled = [cancelled = job->Cancelled] {
						return cancelled->load(std::memory_order_relaxed);
					};
					job->Results = worker->Catalog->Search(job->Request);
				} catch (...) {
					job->Failure = std::current_exception();
				}
			} @catch (NSException* error) {
				job->Failure = InspectNativeFailure(error);
			}
		}
	}

} // namespace sgre::platform

@implementation SGREInspectController

- (void)buildWindow {
	m_WorkQueue = dispatch_queue_create("org.sgre.inspect.scenario", DISPATCH_QUEUE_SERIAL);
	m_Window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1180, 780) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
	m_Window.title = @"STEINS;GATE RE:BOOT — Scenario Inspector";
	m_Window.releasedWhenClosed = NO;
	m_Window.contentMinSize = NSMakeSize(880, 600);
	m_Window.delegate = self;

	m_ChooseInstallation = sgre::platform::InspectButton(@"Choose installation…", self, @selector(chooseInstallation:));
	m_Installation = sgre::platform::InspectLabel(@"Reading the remembered installation, or choose a folder…");
	m_Installation.selectable = YES;
	[m_Installation setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
	NSStackView* location = sgre::platform::InspectRow(@[ m_ChooseInstallation, m_Installation ]);

	m_Query = [[NSSearchField alloc] initWithFrame:NSZeroRect];
	m_Query.placeholderString = @"Scene, character, original text or translation (case-sensitive)";
	m_Query.target = self;
	m_Query.action = @selector(search:);
	m_Query.sendsSearchStringImmediately = NO;
	m_Query.sendsWholeSearchString = YES;
	[m_Query setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
	m_Language = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
	[m_Language addItemsWithTitles:@[ @"English (en)", @"Original (ja)", @"繁體中文 (tc)", @"简体中文 (sc)" ]];
	m_Limit = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
	[m_Limit addItemsWithTitles:@[ @"100 results", @"500 results", @"2,000 results" ]];
	m_Search = sgre::platform::InspectButton(@"Search / Browse", self, @selector(search:));
	m_Cancel = sgre::platform::InspectButton(@"Cancel", self, @selector(cancel:));
	NSStackView* search = sgre::platform::InspectRow(@[ m_Query, m_Language, m_Limit, m_Search, m_Cancel ]);
	[NSLayoutConstraint activateConstraints:@[ [m_Query.widthAnchor constraintGreaterThanOrEqualToConstant:220] ]];

	m_Matches = sgre::platform::InspectTable(@[ @"scene", @"line", @"character", @"original", @"translation" ], @[ @"Scene / storage", @"Source line", @"Character", @"Original", @"Translation" ], @[ @210, @90, @100, @310, @360 ]);
	m_Matches.dataSource = self;
	m_Matches.delegate = self;
	NSScrollView* match_scroll = sgre::platform::InspectScroll(m_Matches);
	m_Entries = sgre::platform::InspectTable(@[ @"name", @"offset", @"bytes" ], @[ @"Scenario archive entry", @"Body offset", @"Packed bytes" ], @[ @650, @180, @180 ]);
	m_Entries.dataSource = self;
	m_Entries.delegate = self;
	NSScrollView* entry_scroll = sgre::platform::InspectScroll(m_Entries);

	m_Tabs = [[NSTabView alloc] initWithFrame:NSZeroRect];
	NSTabView* tabs = m_Tabs;
	tabs.delegate = self;
	NSTabViewItem* results = [[NSTabViewItem alloc] initWithIdentifier:@"results"];
	results.label = @"Scene text";
	results.view = match_scroll;
	[tabs addTabViewItem:results];
	NSTabViewItem* files = [[NSTabViewItem alloc] initWithIdentifier:@"files"];
	files.label = @"Scenario files";
	files.view = entry_scroll;
	[tabs addTabViewItem:files];

	m_Detail = [[NSTextView alloc] initWithFrame:NSZeroRect];
	m_Detail.editable = NO;
	m_Detail.selectable = YES;
	m_Detail.richText = NO;
	m_Detail.font = [NSFont systemFontOfSize:13];
	m_Detail.textContainerInset = NSMakeSize(10, 8);
	m_Detail.verticallyResizable = YES;
	m_Detail.horizontallyResizable = NO;
	m_Detail.autoresizingMask = NSViewWidthSizable;
	m_Detail.textContainer.widthTracksTextView = YES;
	NSScrollView* detail_scroll = sgre::platform::InspectScroll(m_Detail);
	detail_scroll.hasHorizontalScroller = NO;
	[NSLayoutConstraint activateConstraints:@[ [detail_scroll.heightAnchor constraintEqualToConstant:190] ]];
	m_Copy = sgre::platform::InspectButton(@"Copy jump command", self, @selector(copyJump:));
	m_Launch = sgre::platform::InspectButton(@"Launch game at selection…", self, @selector(launchGame:));
	NSTextField* scope = sgre::platform::InspectLabel(@"Read-only scenario metadata. Jump restores the selected original snapshot; it may include nearby dialogue.");
	[scope setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
	NSStackView* actions = sgre::platform::InspectRow(@[ m_Copy, m_Launch, scope ]);
	m_Status = sgre::platform::InspectLabel(@"Empty query browses real records. Search does not execute scripts or render the game.");
	m_Status.selectable = YES;

	NSStackView* content = [NSStackView stackViewWithViews:@[ location, search, tabs, detail_scroll, actions, m_Status ]];
	content.orientation = NSUserInterfaceLayoutOrientationVertical;
	content.alignment = NSLayoutAttributeLeading;
	content.spacing = 10;
	content.translatesAutoresizingMaskIntoConstraints = NO;
	[m_Window.contentView addSubview:content];
	[NSLayoutConstraint activateConstraints:@[ [content.leadingAnchor constraintEqualToAnchor:m_Window.contentView.leadingAnchor constant:16], [content.trailingAnchor constraintEqualToAnchor:m_Window.contentView.trailingAnchor constant:-16], [content.topAnchor constraintEqualToAnchor:m_Window.contentView.topAnchor constant:16], [content.bottomAnchor constraintEqualToAnchor:m_Window.contentView.bottomAnchor constant:-16], [tabs.heightAnchor constraintGreaterThanOrEqualToConstant:170] ]];
	for (NSView* view in @[ location, search, tabs, detail_scroll, actions, m_Status ]) {
		[view.widthAnchor constraintEqualToAnchor:content.widthAnchor].active = YES;
	}

	[self updateControls];
	[m_Window center];
	[m_Window makeKeyAndOrderFront:nil];
	[m_Window makeFirstResponder:m_Query];
}

- (void)startSearch {
	if (State->Busy || State->Closing) {
		return;
	}

	auto job = std::make_shared<sgre::platform::InspectJob>();
	job->RequestedInstallation = State->Options.Installation;
	job->Request.Query = sgre::platform::InspectUtf8(m_Query.stringValue);
	static constexpr std::string_view kLanguages[] = {"en", "ja", "tc", "sc"};
	static constexpr std::size_t kLimits[] = {100, 500, 2000};
	const NSInteger language_index = m_Language.indexOfSelectedItem;
	const NSInteger limit_index = m_Limit.indexOfSelectedItem;
	if (language_index < 0 || language_index >= 4 || limit_index < 0 || limit_index >= 3) {
		throw std::runtime_error("inspector selection is outside its language/result limits");
	}

	job->Request.Language = kLanguages[static_cast<std::size_t>(language_index)];
	job->Request.Limit = kLimits[static_cast<std::size_t>(limit_index)];
	State->ActiveJob = job;
	State->Busy = true;
	m_Status.stringValue = @"Searching one archive entry at a time…";
	[self updateControls];
	const auto worker = State->Worker;
	const auto workspace = State->Workspace;
	__weak SGREInspectController* weak_self = self;
	dispatch_async(m_WorkQueue, ^{
	  sgre::platform::InspectPerformSearch(worker, job, workspace);
	  dispatch_async(dispatch_get_main_queue(), ^{
		SGREInspectController* controller = weak_self;
		if (controller != nil) {
			sgre::platform::InspectCallback(controller, [&] {
				[controller completeJob:job];
			});
		}
	  });
	});
}

- (void)completeJob:(std::shared_ptr<sgre::platform::InspectJob>)job {
	if (State->Closing) {
		return;
	}

	if (State->ActiveJob != job) {
		throw std::runtime_error("inspector completed a search outside its serialized owner");
	}

	State->ActiveJob.reset();
	State->Busy = false;
	if (job->Failure) {
		m_Status.stringValue = sgre::platform::InspectString("Search failed: " + sgre::platform::InspectFailureText(job->Failure));
		[self updateControls];
		return;
	}

	State->Results = std::move(job->Results);
	State->Entries = std::move(job->Entries);
	State->Installation = std::move(job->Installation);
	if (!State->Installation.empty()) {
		State->Options.Installation = State->Installation;
		m_Installation.stringValue = sgre::platform::InspectPath(State->Installation);
	}

	[m_Matches reloadData];
	[m_Entries reloadData];
	const auto& results = State->Results;
	std::string status = std::to_string(results.Matches.size()) + " matches; " + std::to_string(results.EntriesRead) + " entries / " + std::to_string(results.TextRowsRead) + " text records read";
	if (results.Cancelled) {
		status += "; cancelled (partial results)";
	} else if (results.Truncated) {
		status += "; result limit reached — refine query or increase limit";
	}
	m_Status.stringValue = sgre::platform::InspectString(status);
	if (!results.Matches.empty()) {
		[m_Matches selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
	}
	[self updateSelection];
	[self updateControls];
}

- (void)updateControls {
	const bool available = !State->Busy && !State->Closing;
	m_Query.enabled = available;
	m_Language.enabled = available;
	m_Limit.enabled = available;
	m_Search.enabled = available;
	m_ChooseInstallation.enabled = available;
	m_Cancel.enabled = State->Busy && !State->Closing;
	const NSInteger selected = m_Matches.selectedRow;
	const bool has_jump = [m_Tabs.selectedTabViewItem.identifier isEqual:@"results"] && !State->Installation.empty() && selected >= 0 && static_cast<std::size_t>(selected) < State->Results.Matches.size() && !State->Results.Matches[static_cast<std::size_t>(selected)].JumpTarget.empty();
	m_Copy.enabled = available && has_jump;
	m_Launch.enabled = available && has_jump && !State->Installation.empty();
}

- (void)updateSelection {
	const NSInteger selected = m_Matches.selectedRow;
	if (selected < 0 || static_cast<std::size_t>(selected) >= State->Results.Matches.size()) {
		m_Detail.string = @"Select a text result to inspect the original and translated record, or a file to inspect its archive location.";
		[self updateControls];
		return;
	}

	const auto& match = State->Results.Matches[static_cast<std::size_t>(selected)];
	std::string detail = "Scene: " + match.Storage + "  /  " + match.SceneLabel + "\nSource line: " + std::to_string(match.SourceLine) + "    Text ID: " + std::to_string(match.TextId) + "    Character: " + match.Character;
	if (match.TranslatedSourceLine) {
		detail += "    Translation line: " + std::to_string(*match.TranslatedSourceLine);
	}
	detail += "\nOriginal: " + match.OriginalText + "\nTranslation (" + match.Language + "): " + match.Translation;
	if (!match.JumpTarget.empty()) {
		detail += "\nRestore snapshot line: " + std::to_string(match.SnapshotLine) + "    Jump line: " + std::to_string(match.JumpLine) + "\n" + sgre::platform::InspectJumpCommand(match, State->Installation);
	} else {
		detail += "\nNo verified restore target is available for this record.";
	}
	m_Detail.string = sgre::platform::InspectString(detail);
	[self updateControls];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)table {
	NSInteger rows = 0;
	sgre::platform::InspectCallback(self, [&] {
		rows = static_cast<NSInteger>(table == m_Matches ? State->Results.Matches.size() : State->Entries.size());
	});

	return rows;
}

- (NSView*)tableView:(NSTableView*)table viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
	NSView* result = nil;
	sgre::platform::InspectCallback(self, [&] {
		if (row < 0) {
			return;
		}

		std::string text;
		if (table == m_Matches && static_cast<std::size_t>(row) < State->Results.Matches.size()) {
			const auto& match = State->Results.Matches[static_cast<std::size_t>(row)];
			if ([column.identifier isEqualToString:@"scene"]) {
				text = match.Storage + " / " + match.SceneLabel;
			} else if ([column.identifier isEqualToString:@"line"]) {
				text = std::to_string(match.SourceLine);
			} else if ([column.identifier isEqualToString:@"character"]) {
				text = match.Character;
			} else if ([column.identifier isEqualToString:@"original"]) {
				text = match.OriginalText;
			} else {
				text = match.Translation;
			}
		} else if (table == m_Entries && static_cast<std::size_t>(row) < State->Entries.size()) {
			const auto& entry = State->Entries[static_cast<std::size_t>(row)];
			if ([column.identifier isEqualToString:@"name"]) {
				text = entry.Name;
			} else if ([column.identifier isEqualToString:@"offset"]) {
				text = std::to_string(entry.Offset);
			} else {
				text = std::to_string(entry.PackedBytes);
			}
		}

		NSTextField* label = [table makeViewWithIdentifier:column.identifier owner:self];
		if (label == nil) {
			label = sgre::platform::InspectLabel(@"");
			label.identifier = column.identifier;
		}
		label.stringValue = sgre::platform::InspectString(text);
		label.toolTip = label.stringValue;
		result = label;
	});

	return result;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
	sgre::platform::InspectCallback(self, [&] {
		if (notification.object == m_Entries) {
			[self updateEntrySelection];
		} else {
			[self updateSelection];
		}
	});
}

- (void)updateEntrySelection {
	const NSInteger row = m_Entries.selectedRow;
	if (row >= 0 && static_cast<std::size_t>(row) < State->Entries.size()) {
		const auto& entry = State->Entries[static_cast<std::size_t>(row)];
		m_Detail.string = sgre::platform::InspectString("Scenario archive entry: " + entry.Name + "\nBody byte offset: " + std::to_string(entry.Offset) + "\nPacked byte count: " + std::to_string(entry.PackedBytes) + "\nThe Scene text tab searches decoded scenario records. Other resource inspection remains available through the existing CLI.");
	} else {
		m_Detail.string = @"Select an archive entry to inspect its name, offset and packed size. Select Scene text to copy or launch a text result.";
	}
	[self updateControls];
}

- (void)tabView:(NSTabView*)tab_view didSelectTabViewItem:(NSTabViewItem*)item {
	sgre::platform::InspectCallback(self, [&] {
		if (tab_view != m_Tabs) {
			return;
		}
		if ([item.identifier isEqual:@"results"]) {
			[self updateSelection];
		} else {
			[self updateEntrySelection];
		}
	});
}

- (void)search:(id)sender {
	(void)sender;
	sgre::platform::InspectCallback(self, [&] {
		[self startSearch];
	});
}

- (void)cancel:(id)sender {
	(void)sender;
	sgre::platform::InspectCallback(self, [&] {
		if (State->ActiveJob) {
			State->ActiveJob->Cancelled->store(true, std::memory_order_relaxed);
			m_Status.stringValue = @"Cancelling after the current bounded archive operation…";
		}
	});
}

- (void)chooseInstallation:(id)sender {
	(void)sender;
	sgre::platform::InspectCallback(self, [&] {
		if (State->Busy || State->Closing) {
			return;
		}

		NSOpenPanel* panel = [NSOpenPanel openPanel];
		panel.title = @"Choose the supplied game installation";
		panel.canChooseDirectories = YES;
		panel.canChooseFiles = NO;
		panel.allowsMultipleSelection = NO;
		if ([panel runModal] == NSModalResponseOK) {
			State->Options.Installation = std::filesystem::path(sgre::platform::InspectUtf8(panel.URL.path));
			m_Installation.stringValue = sgre::platform::InspectPath(*State->Options.Installation);
			State->Results = {};
			State->Entries.clear();
			State->Installation.clear();
			[m_Matches reloadData];
			[m_Entries reloadData];
			[self updateSelection];
			[self startSearch];
		}
	});
}

- (void)copyJump:(id)sender {
	(void)sender;
	sgre::platform::InspectCallback(self, [&] {
		const NSInteger row = m_Matches.selectedRow;
		if (State->Busy || State->Closing || ![m_Tabs.selectedTabViewItem.identifier isEqual:@"results"] || State->Installation.empty() || row < 0 || static_cast<std::size_t>(row) >= State->Results.Matches.size()) {
			return;
		}

		const auto& match = State->Results.Matches[static_cast<std::size_t>(row)];
		if (match.JumpTarget.empty()) {
			return;
		}

		NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
		[pasteboard clearContents];
		if (![pasteboard setString:sgre::platform::InspectString(sgre::platform::InspectJumpCommand(match, State->Installation)) forType:NSPasteboardTypeString]) {
			throw std::runtime_error("could not copy the selected jump command");
		}
		m_Status.stringValue = @"Jump command copied. Launch remains an explicit action.";
	});
}

- (void)launchGame:(id)sender {
	(void)sender;
	sgre::platform::InspectCallback(self, [&] {
		const NSInteger row = m_Matches.selectedRow;
		if (State->Busy || State->Closing || ![m_Tabs.selectedTabViewItem.identifier isEqual:@"results"] || row < 0 || static_cast<std::size_t>(row) >= State->Results.Matches.size() || State->Installation.empty()) {
			return;
		}

		const auto match = State->Results.Matches[static_cast<std::size_t>(row)];
		if (match.JumpTarget.empty()) {
			return;
		}

		std::filesystem::path executable;
		if (State->Options.GameExecutable) {
			executable = *State->Options.GameExecutable;
		} else {
			NSURL* inspector_url = NSBundle.mainBundle.executableURL;
			if (inspector_url == nil) {
				throw std::runtime_error("could not locate the inspector executable for sibling game selection");
			}
			executable = std::filesystem::path(sgre::platform::InspectUtf8(inspector_url.path)).parent_path() / "sgre-game";
		}

		if (![[NSFileManager defaultManager] isExecutableFileAtPath:sgre::platform::InspectPath(executable)]) {
			NSOpenPanel* panel = [NSOpenPanel openPanel];
			panel.title = @"Choose the sgre-game executable";
			panel.canChooseDirectories = NO;
			panel.canChooseFiles = YES;
			panel.allowsMultipleSelection = NO;
			if ([panel runModal] != NSModalResponseOK) {
				return;
			}
			executable = std::filesystem::path(sgre::platform::InspectUtf8(panel.URL.path));
		}

		if (![[NSFileManager defaultManager] isExecutableFileAtPath:sgre::platform::InspectPath(executable)]) {
			m_Status.stringValue = @"The selected game executable cannot be launched.";
			return;
		}

		State->Options.GameExecutable = executable;
		NSTask* task = [[NSTask alloc] init];
		task.executableURL = [NSURL fileURLWithPath:sgre::platform::InspectPath(executable)];
		task.arguments = @[ sgre::platform::InspectPath(State->Installation), @"--jump", sgre::platform::InspectString(match.JumpTarget) ];
		task.currentDirectoryURL = [NSURL fileURLWithPath:sgre::platform::InspectPath(State->Workspace) isDirectory:YES];
		NSError* error = nil;
		if (![task launchAndReturnError:&error]) {
			m_Status.stringValue = [@"Game launch failed: " stringByAppendingString:error.localizedDescription != nil ? error.localizedDescription : @"unknown process error"];
			return;
		}
		m_Status.stringValue = @"Game launched at the selected original restore point. Closing the inspector leaves the game running.";
	});
}

- (void)beginClose {
	if (State->Closing) {
		return;
	}

	State->Closing = true;
	if (State->ActiveJob) {
		State->ActiveJob->Cancelled->store(true, std::memory_order_relaxed);
	}
	[self updateControls];
	[m_Window orderOut:nil];
	if (m_WorkQueue == nil) {
		[self finishClose];
		return;
	}

	const auto worker = State->Worker;
	__weak SGREInspectController* weak_self = self;
	dispatch_async(m_WorkQueue, ^{
	  worker->Catalog.reset();
	  dispatch_async(dispatch_get_main_queue(), ^{
		SGREInspectController* controller = weak_self;
		if (controller != nil) {
			sgre::platform::InspectCallback(controller, [&] {
				[controller finishClose];
			});
		}
	  });
	});
}

- (void)finishClose {
	State->ActiveJob.reset();
	State->Closed = true;
	m_Window.delegate = nil;
	[m_Window close];
	sgre::platform::InspectStopEventLoop();
}

- (void)callbackFailure:(std::exception_ptr)failure {
	// This boundary retains the first failure; no C++ exception escapes an AppKit callback.
	if (!State->Failure) {
		State->Failure = failure;
	}
	@try {
		try {
			if (State->Closing) {
				sgre::platform::InspectStopEventLoop();
			} else {
				[self beginClose];
			}
		} catch (...) {
			sgre::platform::InspectStopEventLoop();
		}
	} @catch (NSException* error) {
		(void)error;
		sgre::platform::InspectStopEventLoop();
	}
}

- (void)quit:(id)sender {
	(void)sender;
	sgre::platform::InspectCallback(self, [&] {
		[self beginClose];
	});
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
	(void)sender;
	sgre::platform::InspectCallback(self, [&] {
		[self beginClose];
	});

	return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
	(void)sender;
	sgre::platform::InspectCallback(self, [&] {
		[self beginClose];
	});

	return NSTerminateCancel;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
	(void)notification;
	sgre::platform::InspectCallback(self, [&] {
		[self buildWindow];
		[NSApp activateIgnoringOtherApps:NO];
		[self startSearch];
	});
}

- (void)drainWorker {
	if (State->ActiveJob) {
		State->ActiveJob->Cancelled->store(true, std::memory_order_relaxed);
	}
	if (m_WorkQueue != nil) {
		dispatch_sync(m_WorkQueue, ^{
		              });
	}
	State->Worker->Catalog.reset();
	m_Window.delegate = nil;
	[m_Window orderOut:nil];
	[m_Window close];
}

@end

namespace sgre::platform {

	int RunInspectGui(const InspectGuiOptions& options) {
		if (![NSThread isMainThread]) {
			throw std::runtime_error("the macOS inspector GUI must run on the main thread");
		}

		@autoreleasepool {
			NSApplication* application = [NSApplication sharedApplication];
			if (application.isRunning || application.windows.count != 0 || application.delegate != nil) {
				throw std::runtime_error("the inspector GUI requires its own application event loop");
			}

			SGREInspectController* controller = [[SGREInspectController alloc] init];
			controller->State = std::make_unique<InspectGuiState>();
			controller->State->Options = options;
			controller->State->Workspace = std::filesystem::current_path();
			NSMenu* previous_menu = application.mainMenu;
			NSMenu* previous_windows_menu = application.windowsMenu;
			application.delegate = controller;
			@try {
				try {
					[application setActivationPolicy:NSApplicationActivationPolicyRegular];
					InspectInstallMenu(controller);
					[application run];
				} catch (...) {
					if (!controller->State->Failure) {
						controller->State->Failure = std::current_exception();
					}
				}
			} @catch (NSException* error) {
				if (!controller->State->Failure) {
					controller->State->Failure = InspectNativeFailure(error);
				}
			}

			[controller drainWorker];
			application.delegate = nil;
			application.mainMenu = previous_menu;
			application.windowsMenu = previous_windows_menu;
			if (controller->State->Failure) {
				std::rethrow_exception(controller->State->Failure);
			}
			if (!controller->State->Closed) {
				throw std::runtime_error("inspector event loop stopped outside its owned close sequence");
			}

			return 0;
		}
	}

} // namespace sgre::platform
