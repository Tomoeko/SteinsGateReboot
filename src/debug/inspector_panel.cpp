#include "sgre/debug/inspector_panel.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sgre::debug {

	static constexpr std::uint64_t kDrag = 1;
	static constexpr std::uint64_t kResize = 2;
	static constexpr std::uint64_t kClose = 3;
	static constexpr std::uint64_t kSearch = 4;
	static constexpr std::uint64_t kBrowse = 5;
	static constexpr std::uint64_t kJump = 6;
	static constexpr std::uint64_t kCopy = 7;
	static constexpr std::uint64_t kFirstResult = 100;
	static constexpr float kRowHeight = 42;

	bool PanelRectangle::Contains(float x, float y) const noexcept {
		return x >= X && y >= Y && x < X + Width && y < Y + Height;
	}

	static void Paint(InspectorPanelFrame& frame, PanelPaint::Kind type, PanelRectangle bounds, std::uint32_t color, std::string text = {}, float size = 13, bool wrap = false) {
		PanelPaint command;
		command.Type = type;
		command.Bounds = bounds;
		command.Color = color;
		command.Text = std::move(text);
		command.FontSize = size;
		command.Wrap = wrap;
		frame.Paint.push_back(std::move(command));
	}

	static std::string Preview(std::string_view text) {
		constexpr std::size_t kMaximumPreviewBytes = 4096;
		if (text.size() <= kMaximumPreviewBytes) {
			return std::string(text);
		}
		auto end = kMaximumPreviewBytes;
		while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) {
			--end;
		}

		return std::string(text.substr(0, end)) + "…";
	}

	static bool Button(InspectorPanelFrame& frame, InspectorPanelState& state, const InspectorPanelInput& input, std::uint64_t id, PanelRectangle bounds, std::string title, bool enabled = true) {
		const bool hovered = bounds.Contains(input.PointerX, input.PointerY);
		if (enabled && input.PointerPressed && hovered && state.ActiveWidget == 0) {
			state.ActiveWidget = id;
		}
		const bool clicked = enabled && hovered && input.PointerReleased && state.ActiveWidget == id;
		std::uint32_t color = 0x303640ff;
		if (!enabled) {
			color = 0x282b31ff;
		} else if (state.ActiveWidget == id && input.PointerHeld) {
			color = 0x345b83ff;
		} else if (hovered) {
			color = 0x3c4655ff;
		}

		Paint(frame, PanelPaint::Kind::Fill, bounds, color);
		Paint(frame, PanelPaint::Kind::Border, bounds, 0x586270ff);
		bounds.X += 8;
		bounds.Y += 5;
		bounds.Width -= 12;
		bounds.Height -= 6;
		Paint(frame, PanelPaint::Kind::Text, bounds, enabled ? 0xf0f2f5ffu : 0x858b94ffu, std::move(title));

		return clicked;
	}

	static void MovePanel(InspectorPanelState& state, const InspectorPanelInput& input) {
		auto& bounds = state.Bounds;
		if (!state.PreferredBounds) {
			state.PreferredBounds = bounds;
		}
		auto& desired = *state.PreferredBounds;
		const PanelRectangle title{bounds.X, bounds.Y, bounds.Width - 34, 30};
		const PanelRectangle resize{bounds.X + bounds.Width - 20, bounds.Y + bounds.Height - 20, 20, 20};
		if (input.PointerPressed && state.ActiveWidget == 0) {
			if (title.Contains(input.PointerX, input.PointerY)) {
				state.ActiveWidget = kDrag;
				state.DragX = input.PointerX - bounds.X;
				state.DragY = input.PointerY - bounds.Y;
			} else if (resize.Contains(input.PointerX, input.PointerY)) {
				state.ActiveWidget = kResize;
				state.DragX = bounds.X + bounds.Width - input.PointerX;
				state.DragY = bounds.Y + bounds.Height - input.PointerY;
			}
		}
		if (input.PointerHeld) {
			if (state.ActiveWidget == kDrag) {
				desired.X = input.PointerX - state.DragX;
				desired.Y = input.PointerY - state.DragY;
			} else if (state.ActiveWidget == kResize) {
				desired.Width = input.PointerX - bounds.X + state.DragX;
				desired.Height = input.PointerY - bounds.Y + state.DragY;
			}
		}
		desired.Width = std::clamp(desired.Width, 380.0f, 10000.0f);
		desired.Height = std::clamp(desired.Height, 320.0f, 10000.0f);
		bounds = desired;
		bounds.Width = std::clamp(bounds.Width, std::min(380.0f, input.ViewportWidth), input.ViewportWidth);
		bounds.Height = std::clamp(bounds.Height, std::min(320.0f, input.ViewportHeight), input.ViewportHeight);
		bounds.X = std::clamp(bounds.X, 0.0f, input.ViewportWidth - bounds.Width);
		bounds.Y = std::clamp(bounds.Y, 0.0f, input.ViewportHeight - bounds.Height);
	}

	InspectorPanelFrame BuildInspectorPanel(InspectorPanelState& state, const InspectorPanelContent& content, const InspectorPanelInput& input) {
		if (!std::isfinite(input.ViewportWidth) || !std::isfinite(input.ViewportHeight) || input.ViewportWidth < 128 || input.ViewportHeight < 128 || input.ViewportWidth > 10000 || input.ViewportHeight > 10000 || !std::isfinite(input.PointerX) || !std::isfinite(input.PointerY) || !std::isfinite(input.WheelRows) || std::abs(input.WheelRows) > 10000 || !std::isfinite(state.Bounds.X) || !std::isfinite(state.Bounds.Y) || !std::isfinite(state.Bounds.Width) || !std::isfinite(state.Bounds.Height) || state.Bounds.Width <= 0 || state.Bounds.Height <= 0 || content.Rows.size() > 10000 || state.Query.size() > 4096) {
			throw std::runtime_error("inspector panel input exceeds its layout/text bounds");
		}
		InspectorPanelFrame frame;
		if (!state.Visible) {
			state.ActiveWidget = 0;
			return frame;
		}
		if (input.Escape) {
			state.Visible = false;
			state.ActiveWidget = 0;
			return frame;
		}
		MovePanel(state, input);
		const auto& bounds = state.Bounds;
		Paint(frame, PanelPaint::Kind::Fill, bounds, 0x1c2026f8);
		Paint(frame, PanelPaint::Kind::Border, bounds, 0x788596ff);
		Paint(frame, PanelPaint::Kind::Fill, {bounds.X + 1, bounds.Y + 1, bounds.Width - 2, 29}, 0x303b4bff);
		Paint(frame, PanelPaint::Kind::Text, {bounds.X + 10, bounds.Y + 6, bounds.Width - 48, 20}, 0xf2f4f7ff, "SGRE Inspector   ⌘⇧I", 14);
		if (Button(frame, state, input, kClose, {bounds.X + bounds.Width - 29, bounds.Y + 4, 24, 22}, "×")) {
			state.Visible = false;
			state.ActiveWidget = 0;
			frame.Paint.clear();
			return frame;
		}
		if (bounds.Width < 380 || bounds.Height < 320) {
			Paint(frame, PanelPaint::Kind::Text, {bounds.X + 10, bounds.Y + 40, bounds.Width - 20, bounds.Height - 50}, 0xf0f2f5ff, "Enlarge the game window to use the inspector.", 13, true);
			if (input.PointerReleased) {
				state.ActiveWidget = 0;
			}
			return frame;
		}
		frame.QueryBounds = {bounds.X + 10, bounds.Y + 40, bounds.Width - 198, 28};
		frame.Search = Button(frame, state, input, kSearch, {bounds.X + bounds.Width - 180, bounds.Y + 40, 80, 28}, "Search", !content.Busy) || (!content.Busy && input.SubmitQuery);
		frame.Browse = Button(frame, state, input, kBrowse, {bounds.X + bounds.Width - 90, bounds.Y + 40, 80, 28}, "Browse", !content.Busy);
		Paint(frame, PanelPaint::Kind::Text, {bounds.X + 10, bounds.Y + 76, bounds.Width - 20, 20}, 0xa9b7caff, Preview(content.Status), 12);
		const PanelRectangle results{bounds.X + 10, bounds.Y + 100, bounds.Width - 20, std::max(0.0f, bounds.Height - 244)};
		Paint(frame, PanelPaint::Kind::Fill, results, 0x13171dff);
		const auto visible_rows = static_cast<std::size_t>(results.Height / kRowHeight);
		const auto maximum_first = content.Rows.size() > visible_rows ? content.Rows.size() - visible_rows : 0;
		state.FirstRow = std::min(state.FirstRow, maximum_first);
		if (results.Contains(input.PointerX, input.PointerY)) {
			state.ScrollRemainder = std::clamp(state.ScrollRemainder + input.WheelRows, -10000.0f, 10000.0f);
			const auto whole = static_cast<std::int32_t>(state.ScrollRemainder);
			state.ScrollRemainder -= static_cast<float>(whole);
			const auto scrolled = static_cast<std::int64_t>(state.FirstRow) + whole;
			state.FirstRow = static_cast<std::size_t>(std::clamp(scrolled, std::int64_t{0}, static_cast<std::int64_t>(maximum_first)));
		}
		state.FirstRow = std::min(state.FirstRow, maximum_first);
		if (state.Selected && *state.Selected >= content.Rows.size()) {
			state.Selected.reset();
		}
		for (std::size_t visible = 0; visible < visible_rows && state.FirstRow + visible < content.Rows.size(); ++visible) {
			const auto index = state.FirstRow + visible;
			const auto& row = content.Rows[index];
			const PanelRectangle area{results.X, results.Y + static_cast<float>(visible) * kRowHeight, results.Width, kRowHeight - 2};
			if (Button(frame, state, input, kFirstResult + index, area, row.Target + (row.Speaker.empty() ? "" : "    " + row.Speaker))) {
				state.Selected = index;
			}
			if (state.Selected == index) {
				Paint(frame, PanelPaint::Kind::Border, area, 0x6ea9e1ff);
			}
			Paint(frame, PanelPaint::Kind::Text, {area.X + 8, area.Y + 22, area.Width - 16, 17}, 0xbac2cdff, Preview(row.Text), 11);
		}
		if (state.Selected) {
			Paint(frame, PanelPaint::Kind::Text, {bounds.X + 10, bounds.Y + bounds.Height - 134, bounds.Width - 20, 82}, 0xe8edf4ff, Preview(content.Rows[*state.Selected].Text), 13, true);
		}
		frame.Jump = Button(frame, state, input, kJump, {bounds.X + 10, bounds.Y + bounds.Height - 42, 152, 28}, "Jump to selection", state.Selected.has_value() && !content.Busy);
		frame.Copy = Button(frame, state, input, kCopy, {bounds.X + 172, bounds.Y + bounds.Height - 42, 100, 28}, "Copy target", state.Selected.has_value());
		Paint(frame, PanelPaint::Kind::Text, {bounds.X + bounds.Width - 24, bounds.Y + bounds.Height - 24, 20, 20}, 0x97a8bcff, "◢", 17);
		if (input.PointerReleased) {
			state.ActiveWidget = 0;
		}

		return frame;
	}

} // namespace sgre::debug
