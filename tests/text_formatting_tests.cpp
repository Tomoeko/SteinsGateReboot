#include "sgre/text/formatting.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("text formatting fixture expected failure");
}

static sgre::text::TextFontResetCallbacks Callbacks(std::uint32_t& calls, float width = 0) {
	sgre::text::TextFontResetCallbacks callbacks;
	callbacks.SelectNormal = [&] {
		calls |= 1;
	};
	callbacks.MeasureNormal = [&, width](std::uint16_t unit) {
		Require(unit == 0x3000, "normal font reset must measure its actual ideographic-space unit");
		calls |= 2;

		return sgre::resource::GlyphSize{width, 7};
	};
	callbacks.SelectRuby = [&] {
		calls |= 4;
	};

	return callbacks;
}

static void TestLiveCallbackOrderAndLateDefaults() {
	sgre::text::TextFormattingState state;
	std::vector<std::uint32_t> calls;
	sgre::text::TextFontResetCallbacks callbacks;
	callbacks.SelectNormal = [&] {
		Require(state.Current.FontSize == 24 && state.Current.NormalFaceIndex == 0 && !state.Current.Bold, "normal current key must update before its real selector callback");
		state.Defaults.RubySize = 11;
		state.Defaults.ShadowColor = 0xff112233;
		calls.push_back(1);
	};
	callbacks.MeasureNormal = [&](std::uint16_t unit) {
		Require(unit == 0x3000, "wrong reset measurement unit");
		calls.push_back(2);

		return sgre::resource::GlyphSize{0, 31};
	};
	callbacks.SelectRuby = [&] {
		Require(state.Current.RubySize == 11 && state.Current.RubyFaceIndex == 1, "ruby selection must use defaults observed after normal callbacks");
		state.Defaults.RubyOffset = -3;
		state.Defaults.EdgeSize = 9;
		calls.push_back(3);
	};
	sgre::text::ResetTextFontDefaults(state, callbacks);
	Require(calls == std::vector<std::uint32_t>{1, 2, 3} && state.Current.FontBaseWidth == 24 && state.Current.RubyOffset == -3 && state.Current.ShadowColor == 0xff112233 && state.Current.EdgeSize == 9, "font reset must preserve normal/measure/ruby order, zero-width fallback and late appearance reads");
}

static void TestMatchingKeysAvoidCallbacksAndPreserveBaseWidth() {
	sgre::text::TextFormattingState state;
	state.Current.NormalFaceIndex = state.Defaults.NormalFaceIndex;
	state.Current.RubyFaceIndex = state.Defaults.RubyFaceIndex;
	state.Current.FontSize = state.Defaults.FontSize;
	state.Current.RubySize = state.Defaults.RubySize;
	state.Current.FontBaseWidth = 13;
	state.Defaults.Bold = state.Current.Bold = true;
	state.Defaults.Shadow = false;
	state.Defaults.Edge = true;
	state.Defaults.EdgeColor = 0x10203040;
	sgre::text::ResetTextFontDefaults(state, {});
	Require(state.Current.FontBaseWidth == 13 && !state.Current.Shadow && state.Current.Edge && state.Current.EdgeColor == 0x10203040, "matching live keys must preserve base width while restoring appearance without an invented selector");
}

static void TestVerticalScaleAndFallback() {
	sgre::text::TextFormattingState state;
	state.FontScale = -2;
	std::uint32_t calls = 0;
	sgre::text::ResetTextFontDefaults(state, Callbacks(calls, 9.75f));
	Require(state.Current.FontBaseWidth == 9.75f && calls == 7, "horizontal reset must use measured width despite negative font scale");
	state.Vertical = true;
	state.Current.FontSize = -1;
	calls = 0;
	sgre::text::ResetTextFontDefaults(state, Callbacks(calls, 9.75f));
	Require(state.Current.FontBaseWidth == -48 && calls == 3, "vertical reset must still measure but choose scaled current font size");
	state.FontScale = -0.0f;
	state.Current.FontSize = -1;
	sgre::text::ResetTextFontDefaults(state, Callbacks(calls));
	Require(state.Current.FontBaseWidth == 24, "signed-zero vertical width must fall back to unscaled current size");
}

static void TestOrderedSizeComparisonAndGeometryOperandOrder() {
	sgre::text::TextFormattingState state;
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc01234});
	state.Defaults.FontSize = state.Current.FontSize = nan;
	state.Current.RubySize = state.Defaults.RubySize;
	state.Current.RubyFaceIndex = state.Defaults.RubyFaceIndex;
	std::uint32_t calls = 0;
	sgre::text::ResetTextFontDefaults(state, Callbacks(calls, 15));
	Require(calls == 3 && state.Current.FontBaseWidth == 15, "unordered equal-payload font sizes must still invoke the actual normal callbacks");
	state.Current.FontSize = -0.0f;
	state.Defaults.LineSize = 0;
	state.Defaults.LineSpacing = 7;
	state.Defaults.Pitch = -4;
	state.Defaults.Alignment = -17;
	state.Defaults.VerticalAlignment = 23;
	state.Defaults.WordBreak = false;
	sgre::text::ResetTextGeometryDefaults(state);
	Require(std::signbit(state.Current.LineSize) && state.Current.LineSpacing == 7 && state.Current.Pitch == -4 && state.Current.Alignment == -17 && state.Current.VerticalAlignment == 23 && !state.Current.WordBreak, "geometry reset must preserve MAXSS second operand signed-zero and exact copied controls");
	state.Defaults.LineSize = nan;
	state.Current.FontSize = 12;
	sgre::text::ResetTextGeometryDefaults(state);
	Require(state.Current.LineSize == 12, "unordered first maximum operand must select current font size");
}

static void TestMissingRealSelectorsRejectBeforeKeyMutation() {
	sgre::text::TextFormattingState state;
	RequireFailure([&] {
		sgre::text::ResetTextFontDefaults(state, {});
	},
	    "actual normal selector");
	Require(state.Current.FontSize == -1, "unsupported normal selector must not silently install a pretend current font");
	state.Current.FontSize = state.Defaults.FontSize;
	state.Current.NormalFaceIndex = state.Defaults.NormalFaceIndex;
	RequireFailure([&] {
		sgre::text::ResetTextFontDefaults(state, {});
	},
	    "actual ruby selector");
	Require(state.Current.RubySize == -1, "unsupported ruby selector must not silently install a pretend current font");
}

int main() {
	try {
		TestLiveCallbackOrderAndLateDefaults();
		TestMatchingKeysAvoidCallbacksAndPreserveBaseWidth();
		TestVerticalScaleAndFallback();
		TestOrderedSizeComparisonAndGeometryOperandOrder();
		TestMissingRealSelectorsRejectBeforeKeyMutation();
		std::cout << "Five independent native text font/default geometry reset groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
