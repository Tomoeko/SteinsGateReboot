#include "sgre/scene/motion_evaluator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Value>
static sgre::resource::PsbValue MakeValue(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::MotionLayer MakeLayer(std::uint32_t layer_type, std::uint32_t frame_type, bool has_blend, std::uint32_t blend, std::uint32_t color_kind, const std::array<std::uint32_t, 4>& colors) {
	sgre::resource::MotionLayer layer;
	layer.Type = layer_type;
	layer.TransformOrder = {0, 3, 2, 1};
	sgre::resource::MotionFrame frame;
	frame.Type = frame_type;
	sgre::resource::PsbObject content;
	content.emplace_back("mask", MakeValue(std::int64_t{(has_blend ? 0x20000 : 0) | (color_kind != 0 ? 0x200 : 0)}));
	content.emplace_back("src", MakeValue(std::string("c")));
	content.emplace_back("icon", MakeValue(std::string("i")));
	if (has_blend) {
		content.emplace_back("bm", MakeValue(static_cast<std::int64_t>(blend)));
	}
	if (color_kind == 1) {
		content.emplace_back("color", MakeValue(static_cast<std::int64_t>(colors[0])));
	} else if (color_kind == 2) {
		sgre::resource::PsbArray corners;
		for (const auto color : colors) {
			corners.push_back(MakeValue(static_cast<std::int64_t>(color)));
		}

		content.emplace_back("color", MakeValue(std::move(corners)));
	}
	frame.Content = MakeValue(std::move(content));
	layer.Frames.push_back(std::move(frame));
	sgre::resource::MotionFrame terminal;
	terminal.Type = 0;
	terminal.Time = 10;
	layer.Frames.push_back(std::move(terminal));

	return layer;
}

static void TestPreparationBeforeInterpolation() {
	const sgre::resource::MotionResource resource;
	auto layer = MakeLayer(0, 3, true, 0x80000002, 1, {0x010101ff, 0, 0, 0});
	const auto next = MakeLayer(0, 2, true, 0x80000002, 1, {0x03030380, 0, 0, 0});
	layer.Frames.insert(layer.Frames.begin() + 1, next.Frames.front());
	layer.Frames[1].Time = 5;
	const auto ordinary = sgre::scene::PrepareMotionLayer(layer, resource);
	const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource, sgre::scene::MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble);
	Require(ordinary.Frames[0].Attributes.Colors[0] == 0x010101ff && ordinary.Frames[0].Attributes.BlendMode == 0x80000002, "ordinary preparation policy changed");
	Require(prepared.Frames[0].Attributes.Colors[0] == 0x000000ff && prepared.Frames[1].Attributes.Colors[0] == 0x01010180 && prepared.Frames[0].Attributes.BlendMode == 0x80000012, "explicit player policy did not prepare both endpoint owners");
	const auto middle = sgre::scene::EvaluateMotionLayer(prepared, resource, 2.5f);
	Require(middle.Colors[0] == 0x000000bf, "player policy must precede color interpolation and preserve interpolated alpha");
	Require(prepared.Frames.back().Hidden && prepared.Frames.back().Attributes.Colors[0] == 0x808080ff, "hidden frame must retain unprepared attributes");
	Require(std::get<std::int64_t>(layer.Frames.front().Content.Find("bm")->Data) == 0x80000002, "player policy mutated original raw metadata");

	for (const auto blend : {std::uint32_t{272}, std::uint32_t{274}, std::uint32_t{0xf0}}) {
		const auto existing = MakeLayer(0, 2, true, blend, 1, {0xff01027f, 0, 0, 0});
		const auto preserved = sgre::scene::PrepareMotionLayer(existing, resource, sgre::scene::MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble);
		Require(preserved.Frames.front().Attributes.BlendMode == blend && preserved.Frames.front().Attributes.Colors[0] == 0xff01027f, "nonzero blend high nibble changed under policy");
	}
	const auto clear_default = sgre::scene::PrepareMotionLayer(MakeLayer(0, 2, true, 0, 0, {}), resource, sgre::scene::MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble);
	Require(clear_default.Frames.front().Attributes.Colors[0] == 0x7f7f7fff, "decoded white default must become7f RGB with original alpha");
	const auto absent_blend = sgre::scene::PrepareMotionLayer(MakeLayer(0, 2, false, 0, 0, {}), resource, sgre::scene::MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble);
	Require(absent_blend.Frames.front().Attributes.Colors[0] == 0x808080ff, "native default0x10 blend must preserve neutral color");
}

static void TestRecursivePreparationAndPolicyValidation() {
	const sgre::resource::MotionResource resource;
	auto parent = MakeLayer(2, 2, true, 0, 1, {0xffffffff, 0, 0, 0});
	parent.Children.push_back(MakeLayer(3, 2, true, 0, 1, {0x03050709, 0, 0, 0}));
	sgre::resource::MotionTimeline timeline;
	timeline.LastTime = 10;
	timeline.Layers.push_back(std::move(parent));
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = MakeValue(sgre::resource::PsbArray{MakeValue(std::int64_t{0}), MakeValue(std::int64_t{1})});
	timeline.Priority.push_back(priority);
	priority.Time = 10;
	priority.Content = {};
	timeline.Priority.push_back(std::move(priority));
	const auto ordinary = sgre::scene::PrepareMotionTimeline(timeline, resource);
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource, sgre::scene::MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble);
	Require(prepared.Layers.size() == 2 && prepared.Parents[1] == 0, "policy changed actual hierarchy preparation");
	Require(prepared.Layers[0].Frames.front().Attributes.Colors[0] == 0x7f7f7fff && prepared.Layers[1].Frames.front().Attributes.Colors[0] == 0x01020309, "actual nested layers must receive the same ultimate player policy exactly once");
	Require(ordinary.Layers[0].Frames.front().Attributes.Colors[0] == 0xffffffff && ordinary.Layers[1].Frames.front().Attributes.Colors[0] == 0x03050709, "ordinary recursive preparation changed");
	bool rejected = false;
	try {
		static_cast<void>(sgre::scene::PrepareMotionTimeline(timeline, resource, static_cast<sgre::scene::MotionAttributeBlendPolicy>(2)));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("blend policy") != std::string_view::npos;
	}

	Require(rejected, "unsupported policy omitted precise rejection");
}

static void ReplayNativeFixture(const std::string& path) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	Require(static_cast<bool>(input), "cannot open native attribute policy fixture");
	const auto size = input.tellg();
	Require(size >= 8 && size <= 65536, "native attribute policy fixture length is outside its bound");
	std::vector<char> bytes(static_cast<std::size_t>(size));
	input.seekg(0);
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(input), "native attribute policy fixture read failed");
	Require(std::string_view(bytes.data(), 4) == "MAB1", "native attribute policy fixture magic is invalid");
	std::size_t offset = 4;
	const auto read_byte = [&]() {
		Require(offset < bytes.size(), "native attribute policy fixture is truncated");

		return static_cast<std::uint8_t>(bytes[offset++]);
	};
	const auto read_word = [&]() {
		std::uint32_t word = 0;
		for (std::size_t shift = 0; shift < 32; shift += 8) {
			word |= static_cast<std::uint32_t>(read_byte()) << shift;
		}

		return word;
	};
	const auto count = read_word();
	Require(count == 512, "native attribute policy fixture count changed");
	const sgre::resource::MotionResource resource;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto layer_type = read_byte();
		const auto frame_type = read_byte();
		const auto policy = read_byte();
		const auto has_blend = read_byte();
		const auto color_kind = read_byte();
		const auto blend = read_word();
		std::array<std::uint32_t, 4> colors{};
		for (auto& color : colors) {
			color = read_word();
		}

		const auto layer = MakeLayer(layer_type, frame_type, has_blend != 0, blend, color_kind, colors);
		const auto prepared = sgre::scene::PrepareMotionLayer(layer, resource, policy != 0 ? sgre::scene::MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble : sgre::scene::MotionAttributeBlendPolicy::Unchanged);
		const auto& attributes = prepared.Frames.front().Attributes;
		Require(attributes.BlendMode == read_word(), "native attribute policy blend mode differs");
		for (const auto color : attributes.Colors) {
			Require(color == read_word(), "native attribute policy corner color differs");
		}
	}

	Require(offset == bytes.size(), "native attribute policy fixture has trailing bytes");
}

int main(int argc, char** argv) {
	try {
		TestPreparationBeforeInterpolation();
		TestRecursivePreparationAndPolicyValidation();
		if (argc == 2) {
			ReplayNativeFixture(argv[1]);
		}

		std::cout << "Motion explicit player attribute policy and native preparation comparisons passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
