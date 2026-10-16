#include "sgre/script/text_render_bindings.hpp"

#include "glyph_atlas_owner_fixture.hpp"
#include "text_glyph_drawing_fixture.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
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
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("character query expected guarded failure");
}

static void TestOriginalRecordFieldsAndRanges() {
	text_atlas_test::Backend backend;
	std::uint32_t texture = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		return sgre::render::SpriteTextureHandle{++texture, 1, &texture};
	},
	    [](auto) {
	    });
	text_drawing_test::Fixture fixture(cache, backend, [](auto&, auto, const auto&) {
	});
	auto& vm = fixture.Vm;
	Require(vm.Length(fixture.Call("getCharacters")) == 0, "fresh text query must return an empty array");
	fixture.Call("setDefault", {fixture.Properties({{"face", std::string("main-face")}, {"rubyface", std::string("annotation-face")}})});
	fixture.Call("setDefault", {fixture.Properties({{"shadow", true}, {"shadowcolor", std::bit_cast<std::int32_t>(0x80123456U)}, {"shadowdiff", std::int32_t{-3}}, {"edge", true}, {"edgecolor", std::bit_cast<std::int32_t>(0xffabcdefU)}, {"edgesize", std::int32_t{2}}, {"bold", true}})});
	fixture.Call("resetFont");
	fixture.Call("resetStyle");
	fixture.Render("[B,1]BB\nB");
	const auto records = fixture.Keep(fixture.Call("getCharacters"));
	Require(vm.Length(records) == 3, "character list must omit newline and annotation entries");
	const auto first = vm.GetSlot(records, std::int32_t{0});
	Require(vm.Length(first) == 19 && sgre::script::StringText(vm.GetSlot(first, "text")) == "B", "native base record has18 fields plus optional ruby");
	Require(std::get<float>(vm.GetSlot(first, "x")) == 0 && std::get<float>(vm.GetSlot(first, "y")) == 0 && std::get<float>(vm.GetSlot(first, "cw")) == 8 && std::get<float>(vm.GetSlot(first, "size")) == 8, "character queries must copy finalized local geometry");
	Require(!std::get<bool>(vm.GetSlot(first, "graph")) && !std::get<bool>(vm.GetSlot(first, "vertical")), "ordinary horizontal elements must retain original type flags");
	Require(sgre::script::StringText(vm.GetSlot(first, "face")) == "main-face" && sgre::script::StringText(vm.GetSlot(first, "rubyface")) == "annotation-face", "character records must preserve selected face identities");
	Require(std::get<std::int32_t>(vm.GetSlot(first, "color")) == std::bit_cast<std::int32_t>(0xff204080U) && std::get<std::int32_t>(vm.GetSlot(first, "shadowColor")) == std::bit_cast<std::int32_t>(0x80123456U) && std::get<std::int32_t>(vm.GetSlot(first, "edgeColor")) == std::bit_cast<std::int32_t>(0xffabcdefU), "query colors must preserve signed native words without draw byte swap");
	Require(std::get<bool>(vm.GetSlot(first, "bold")) && !std::get<bool>(vm.GetSlot(first, "italic")) && std::get<bool>(vm.GetSlot(first, "shadow")) && std::get<bool>(vm.GetSlot(first, "edge")) && std::get<std::int32_t>(vm.GetSlot(first, "shadowDiff")) == -3 && std::get<std::int32_t>(vm.GetSlot(first, "edgeSize")) == 2, "query must retain captured style flags and offsets");
	const auto annotations = vm.GetSlot(first, "ruby");
	Require(vm.Length(annotations) == 1, "native annotation list belongs to its base element");
	const auto ruby = vm.GetSlot(annotations, std::int32_t{0});
	Require(vm.Length(ruby) == 4 && sgre::script::StringText(vm.GetSlot(ruby, "text")) == "B" && std::get<float>(vm.GetSlot(ruby, "x")) == 2 && std::get<float>(vm.GetSlot(ruby, "y")) == -2 && std::get<float>(vm.GetSlot(ruby, "size")) == 4, "annotation query keeps relative x/y and its independently selected size");
	const auto last = vm.GetSlot(records, std::int32_t{2});
	Require(vm.Length(last) == 18 && !vm.HasSlot(last, "ruby") && std::get<float>(vm.GetSlot(last, "y")) == 8, "following line must preserve y and omit absent ruby key");
	Require(vm.Length(fixture.Call("getCharacters", {std::int32_t{1}})) == 2 && vm.Length(fixture.Call("getCharacters", {std::int32_t{1}, std::int32_t{1}, true})) == 1, "native start/count and ignored trailing arguments");
	Require(vm.Length(fixture.Call("getCharacters", {std::int32_t{20}, std::int32_t{1}})) == 0 && vm.Length(fixture.Call("getCharacters", {std::int32_t{1}, std::int32_t{-1}})) == 0, "signed empty range is not a clamped inclusive span");
	Require(vm.Length(fixture.Call("getCharacters", {std::int32_t{1}, std::numeric_limits<std::int32_t>::max()})) == 0, "native range endpoint wraps at32 bits");
	RequireFailure([&] {
		static_cast<void>(fixture.Call("getCharacters", {std::int32_t{-1}, std::int32_t{2}}));
	},
	    "negative index");
	fixture.Call("setShowCount", {std::int32_t{0}});
	Require(vm.Length(fixture.Call("getCharacters", {std::string("numeric fallback"), false})) == 3, "query covers finalized elements independently of reveal visibility");
	vm.SetSlot(first, "size", 123.0f);
	Require(std::get<float>(vm.GetSlot(vm.GetSlot(fixture.Call("getCharacters"), std::int32_t{0}), "size")) == 8, "returned records must not alias actual content or a prior result");
	fixture.Call("clear");
	Require(vm.Length(fixture.Call("getCharacters")) == 0 && vm.Length(records) == 3, "clear must preserve owned prior query values while retiring real content");
	fixture.Layers.ReleaseInstance(fixture.Layer);
	Require(vm.Type(fixture.Call("getCharacters")) == sgre::script::SquirrelValueType::Array && vm.Length(fixture.Call("getCharacters")) == 0, "detached wrapper returns an actual fresh empty array");
	fixture.Text.ReleaseInstance(fixture.Instance);
}

int main() {
	try {
		TestOriginalRecordFieldsAndRanges();
		std::cout << "TextRender character fields, ruby, signed ranges, independent ownership and detachment passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
