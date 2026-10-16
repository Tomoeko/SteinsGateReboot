#include "sgre/text/formatting.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestCurrentKeysAndStylePrecedence() {
	sgre::text::TextFormattingState state;
	state.FontScale = 1.25f;
	state.Current.FontSize = 23.5f;
	state.Current.NormalFaceIndex = 17;
	state.Current.Bold = true;
	state.Current.Italic = true;
	state.Defaults.FontSize = 99;
	state.Defaults.NormalFaceIndex = 88;
	const auto request = sgre::text::PrepareTextFontSelectionRequest(state, sgre::text::TextFontFamily::Normal, true);
	Require(request.Family == sgre::text::TextFontFamily::Normal && request.Key.RequestedSize == 29.375f && request.Key.FaceIndex == 17 && request.Key.Style == sgre::text::TextFontStyle::Bold && request.Filtering, "normal bridge must use scaled current keys and give bold precedence over italic");
	state.Current.Bold = false;
	Require(sgre::text::PrepareTextFontSelectionRequest(state, sgre::text::TextFontFamily::Normal, false).Key.Style == sgre::text::TextFontStyle::Italic, "italic must select style2 only when bold is absent");
	state.Current.Italic = false;
	Require(sgre::text::PrepareTextFontSelectionRequest(state, sgre::text::TextFontFamily::Normal, false).Key.Style == sgre::text::TextFontStyle::Regular, "no style flags must select regular");
}

static void TestRubyAndSignedScale() {
	sgre::text::TextFormattingState state;
	state.FontScale = -2;
	state.Current.RubySize = 10.25f;
	state.Current.RubyFaceIndex = 21;
	state.Current.Bold = state.Current.Italic = true;
	const auto request = sgre::text::PrepareTextFontSelectionRequest(state, sgre::text::TextFontFamily::Ruby, false);
	Require(request.Family == sgre::text::TextFontFamily::Ruby && request.Key.RequestedSize == -20.5f && request.Key.FaceIndex == 21 && request.Key.Style == sgre::text::TextFontStyle::Regular && !request.Filtering, "ruby bridge must use its independent current size/face, preserve scale sign and ignore normal style");
	state.FontScale = -0.0f;
	Require(std::bit_cast<std::uint32_t>(sgre::text::PrepareTextFontSelectionRequest(state, sgre::text::TextFontFamily::Ruby, true).Key.RequestedSize) == 0x80000000, "selection must preserve signed zero rather than force a positive size");
}

static void TestInvalidFamily() {
	try {
		(void)sgre::text::PrepareTextFontSelectionRequest({}, static_cast<sgre::text::TextFontFamily>(77), false);
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("invalid family") != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("invalid font request family was silently accepted");
}

int main() {
	try {
		TestCurrentKeysAndStylePrecedence();
		TestRubyAndSignedScale();
		TestInvalidFamily();
		std::cout << "Three independent native text font selector request groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
