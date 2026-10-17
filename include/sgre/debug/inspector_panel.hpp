#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::debug {

	struct PanelRectangle {
		float X = 0;
		float Y = 0;
		float Width = 0;
		float Height = 0;
		[[nodiscard]] bool Contains(float x, float y) const noexcept;
	};

	struct InspectorRow {
		std::string Target;
		std::string Speaker;
		std::string Text;
	};

	struct InspectorPanelInput {
		float ViewportWidth = 1280;
		float ViewportHeight = 720;
		float PointerX = -1;
		float PointerY = -1;
		float WheelRows = 0;
		bool PointerPressed = false;
		bool PointerHeld = false;
		bool PointerReleased = false;
		bool Escape = false;
		bool SubmitQuery = false;
	};

	struct InspectorPanelState {
		PanelRectangle Bounds{16, 16, 640, 460};
		// Window clipping is temporary; retain the user's placement for the next larger viewport.
		std::optional<PanelRectangle> PreferredBounds;
		std::string Query;
		std::optional<std::size_t> Selected;
		std::size_t FirstRow = 0;
		bool Visible = false;
		std::uint64_t ActiveWidget = 0;
		float DragX = 0;
		float DragY = 0;
		float ScrollRemainder = 0;
	};

	struct InspectorPanelContent {
		std::span<const InspectorRow> Rows;
		std::string_view Status;
		bool Busy = false;
	};

	struct PanelPaint {
		enum class Kind {
			Fill,
			Border,
			Text
		};
		Kind Type = Kind::Fill;
		PanelRectangle Bounds;
		std::uint32_t Color = 0xffffffff;
		float FontSize = 13;
		bool Wrap = false;
		std::string Text;
	};

	struct InspectorPanelFrame {
		std::vector<PanelPaint> Paint;
		PanelRectangle QueryBounds;
		bool Search = false;
		bool Browse = false;
		bool Jump = false;
		bool Copy = false;
	};

	// Authored immediate-mode tooling: each call lays out, hit-tests and paints
	// the current values. No game scripts, assets or original menu state are changed.
	[[nodiscard]] InspectorPanelFrame BuildInspectorPanel(InspectorPanelState& state, const InspectorPanelContent& content, const InspectorPanelInput& input);

} // namespace sgre::debug
