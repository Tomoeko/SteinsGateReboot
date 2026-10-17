#include "sgre/debug/inspector_panel.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

static void Require(bool condition, std::string_view reason) {
	if (!condition) {
		throw std::runtime_error(std::string(reason));
	}
}

static sgre::debug::PanelRectangle FindText(const sgre::debug::InspectorPanelFrame& frame, std::string_view text) {
	for (const auto& command : frame.Paint) {
		if (command.Type == sgre::debug::PanelPaint::Kind::Text && command.Text.starts_with(text)) {
			return command.Bounds;
		}
	}

	throw std::runtime_error("inspector fixture could not find the visible control");
}

static sgre::debug::InspectorPanelFrame Click(sgre::debug::InspectorPanelState& state, const sgre::debug::InspectorPanelContent& content, sgre::debug::PanelRectangle control) {
	sgre::debug::InspectorPanelInput input;
	input.PointerX = control.X + 2;
	input.PointerY = control.Y + 2;
	input.PointerPressed = true;
	input.PointerHeld = true;
	static_cast<void>(sgre::debug::BuildInspectorPanel(state, content, input));
	input.PointerPressed = false;
	input.PointerHeld = false;
	input.PointerReleased = true;

	return sgre::debug::BuildInspectorPanel(state, content, input);
}

static void TestSearchSelectionAndClose() {
	sgre::debug::InspectorPanelState state;
	const std::vector<sgre::debug::InspectorRow> rows{{"fixture.ks:42", "First", "First independent preview"}, {"other.ks:7", "Second", "Second independent preview"}};
	sgre::debug::InspectorPanelContent content{rows, "Two results", false};
	sgre::debug::InspectorPanelInput input;
	Require(sgre::debug::BuildInspectorPanel(state, content, input).Paint.empty(), "hidden inspector authored a visible frame");
	state.Visible = true;
	auto frame = sgre::debug::BuildInspectorPanel(state, content, input);
	Require(Click(state, content, FindText(frame, "Search")).Search, "search control failed its actual press/release sequence");
	frame = Click(state, content, FindText(frame, "other.ks:7"));
	Require(state.Selected == 1, "inspector selected the wrong real result row");
	Require(Click(state, content, FindText(frame, "Jump to selection")).Jump, "selected jump was not emitted");
	content.Busy = true;
	Require(!Click(state, content, FindText(frame, "Jump to selection")).Jump && !Click(state, content, FindText(frame, "Search")).Search, "busy inspector issued a second operation");
	content.Busy = false;
	const auto selected = state.Selected;
	input.ViewportWidth = 1920;
	input.ViewportHeight = 1080;
	frame = sgre::debug::BuildInspectorPanel(state, content, input);
	Require(state.Selected == selected, "window resize lost selected result identity");
	frame = Click(state, content, FindText(frame, "×"));
	Require(!state.Visible && frame.Paint.empty() && state.ActiveWidget == 0, "close control left an invisible input trap");
}

static void TestDragResizeScrollAndSmallViewport() {
	sgre::debug::InspectorPanelState state;
	state.Visible = true;
	std::vector<sgre::debug::InspectorRow> rows(100);
	for (std::size_t index = 0; index < rows.size(); ++index) {
		rows[index].Target = "fixture.ks:" + std::to_string(index);
	}
	const sgre::debug::InspectorPanelContent content{rows, "Results", false};
	sgre::debug::InspectorPanelInput input;
	input.PointerX = 35;
	input.PointerY = 25;
	input.PointerPressed = true;
	input.PointerHeld = true;
	static_cast<void>(sgre::debug::BuildInspectorPanel(state, content, input));
	input.PointerPressed = false;
	input.PointerX += 100;
	input.PointerY += 60;
	static_cast<void>(sgre::debug::BuildInspectorPanel(state, content, input));
	Require(state.Bounds.X == 116 && state.Bounds.Y == 76, "drag did not preserve pointer offset");
	input.PointerHeld = false;
	input.PointerReleased = true;
	static_cast<void>(sgre::debug::BuildInspectorPanel(state, content, input));
	input.PointerReleased = false;
	input.PointerPressed = true;
	input.PointerHeld = true;
	input.PointerX = state.Bounds.X + state.Bounds.Width - 3;
	input.PointerY = state.Bounds.Y + state.Bounds.Height - 3;
	static_cast<void>(sgre::debug::BuildInspectorPanel(state, content, input));
	input.PointerPressed = false;
	input.PointerX += 200;
	input.PointerY += 80;
	static_cast<void>(sgre::debug::BuildInspectorPanel(state, content, input));
	Require(state.Bounds.Width == 840 && state.Bounds.Height == 540, "panel resize failed to expand its content area");
	input.PointerReleased = true;
	input.PointerHeld = false;
	static_cast<void>(sgre::debug::BuildInspectorPanel(state, content, input));
	input.PointerReleased = false;
	input.PointerX = state.Bounds.X + 30;
	input.PointerY = state.Bounds.Y + 120;
	input.WheelRows = 1000;
	static_cast<void>(sgre::debug::BuildInspectorPanel(state, content, input));
	Require(state.FirstRow > 80 && state.FirstRow < rows.size(), "scroll did not clamp to the last actual page");
	input.ViewportWidth = 160;
	input.ViewportHeight = 180;
	const auto compact = sgre::debug::BuildInspectorPanel(state, content, input);
	Require(state.Bounds.X >= 0 && state.Bounds.X + state.Bounds.Width <= 160 && state.Bounds.Y >= 0 && state.Bounds.Y + state.Bounds.Height <= 180 && !compact.Paint.empty(), "small viewport left the panel inaccessible");
	for (const auto& command : compact.Paint) {
		Require(command.Bounds.Width >= 0 && command.Bounds.Height >= 0, "small viewport emitted negative drawing dimensions");
	}
	input.ViewportWidth = 1920;
	input.ViewportHeight = 1080;
	const auto restored = sgre::debug::BuildInspectorPanel(state, content, input);
	Require(state.Bounds.Width == 840 && state.Bounds.Height == 540 && state.FirstRow > 80 && !restored.Paint.empty(), "temporary window shrink permanently discarded the result viewport or scroll position");
	Require(FindText(restored, "fixture.ks:").Width > 0, "results did not reappear after enlarging the game window");
	input.Escape = true;
	Require(sgre::debug::BuildInspectorPanel(state, content, input).Paint.empty() && !state.Visible, "Escape failed to close the inspector");
}

int main() {
	try {
		TestSearchSelectionAndClose();
		TestDragResizeScrollAndSmallViewport();
		std::cout << "inspector search, selection, input ownership, drag/resize, scrolling and close passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
