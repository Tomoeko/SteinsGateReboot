#include "sgre/resource/motion.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "motion failure omitted required context");

		return;
	}

	throw std::runtime_error("invalid motion was accepted: " + std::string(context));
}

template <typename Value>
static sgre::resource::PsbValue MakeValue(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue& Field(sgre::resource::PsbValue& value, std::string_view name) {
	for (auto& member : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (member.first == name) {
			return member.second;
		}
	}

	throw std::runtime_error("missing motion test field");
}

static sgre::resource::EasingCurve SyntheticCurve() {
	sgre::resource::EasingCurve curve;
	curve.Segments = {{{0, 0, 0}, {0.2f, 0.15f, 3}, {0.5f, 0.45f, -2}, {0.8f, 0.9f, -4}, {1, 1, 0}}};

	return curve;
}

static void TestNativeSplineVectors() {
	// Expected float bits come from executing original 0x14051c910 instructions on synthetic knots.
	const std::array<std::uint32_t, 9> expected = {0x00000000, 0x3db06666, 0x3e460b62, 0x3ea460b6, 0x3ee66666, 0x3f2b71c7, 0x3f585b05, 0x3f72999a, 0x3f800000};
	const auto curve = SyntheticCurve();
	for (std::size_t index = 0; index < expected.size(); ++index) {
		const auto position = static_cast<float>(index) / 8.0f;
		const auto value = sgre::resource::EvaluateEasing(curve, position);
		Require(std::bit_cast<std::uint32_t>(value) == expected[index], "native spline golden bits changed");
	}

	RequireFailure([&]() {
		static_cast<void>(sgre::resource::EvaluateEasing(curve, -0.01f));
	},
	    "outside");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::EvaluateEasing(curve, std::numeric_limits<float>::quiet_NaN()));
	},
	    "nonfinite");
}

static void TestRecoveredInterpolation() {
	Require(sgre::resource::InterpolateScalar(10, 20, 0.25f) == 12.5f, "scalar blend changed");
	Require(sgre::resource::InterpolateScalar(10, 10, 5) == 10, "equal scalar endpoint behavior changed");
	const auto curve = SyntheticCurve();
	Require(sgre::resource::InterpolateScalar(0, 1, 0.5f, &curve) == 0.45f, "scalar easing remap changed");
	Require(sgre::resource::InterpolateAngle(350, 10, 0.5f, sgre::resource::RotationTransition::Shortest) == 0, "shortest positive-wrap angle changed");
	Require(sgre::resource::InterpolateAngle(10, 350, 0.5f, sgre::resource::RotationTransition::Shortest) == 0, "shortest negative-wrap angle changed");
	Require(sgre::resource::InterpolateAngle(350, 10, 0.5f, sgre::resource::RotationTransition::Directed) == 180, "directed angle changed");
	Require(sgre::resource::InterpolateAngle(720, 720, 0.5f, sgre::resource::RotationTransition::Directed) == 0, "directed equal endpoint normalization changed");
	Require(sgre::resource::InterpolateAngle(720, 720, 0.5f, sgre::resource::RotationTransition::Shortest) == 720, "native equal shortest endpoint shortcut changed");
	Require(sgre::resource::InterpolatePackedColor(0x000000ff, 0xffffffff, 0.5f) == 0x7f7f7fff, "packed color integer truncation changed");
	Require(sgre::resource::InterpolatePackedColor(0x10203040, 0xaabbccdd, 1) == 0xaabbccdd, "packed color final endpoint changed");
	RequireFailure([]() {
		static_cast<void>(sgre::resource::InterpolateScalar(0, 1, std::numeric_limits<float>::infinity()));
	},
	    "ratio is nonfinite");
	RequireFailure([]() {
		static_cast<void>(sgre::resource::InterpolateAngle(0, 1, 0.5f, static_cast<sgre::resource::RotationTransition>(9)));
	},
	    "unsupported rotation");
	RequireFailure([]() {
		static_cast<void>(sgre::resource::InterpolatePackedColor(0, 1, std::numeric_limits<float>::max()));
	},
	    "integer limit");
}

static sgre::resource::PsbValue SplineValue() {
	return MakeValue(sgre::resource::PsbObject{{"x", MakeValue(sgre::resource::PsbArray{MakeValue(0.0), MakeValue(1.0)})}, {"y", MakeValue(sgre::resource::PsbArray{MakeValue(0.0), MakeValue(1.0)})}, {"p", MakeValue(sgre::resource::PsbArray{MakeValue(0.0), MakeValue(0.0)})}});
}

static void TestSplineReaderSafety() {
	const auto serialized = MakeValue(sgre::resource::PsbArray{SplineValue()});
	const auto curve = sgre::resource::ReadEasingCurve(serialized);
	Require(sgre::resource::EvaluateEasing(curve, 0.25f) == 0.25f, "linear spline parsing changed");
	auto invalid = serialized;
	auto& segment = std::get<sgre::resource::PsbArray>(invalid.Data)[0];
	Field(segment, "p").Data = sgre::resource::PsbArray{MakeValue(0.0)};
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadEasingCurve(invalid));
	},
	    "inconsistent lengths");
	Field(segment, "p").Data = sgre::resource::PsbArray{MakeValue(0.0), MakeValue(0.0)};
	Field(segment, "x").Data = sgre::resource::PsbArray{MakeValue(1.0), MakeValue(1.0)};
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadEasingCurve(invalid));
	},
	    "increase strictly");
}

static void TestBezierPath() {
	sgre::resource::PsbValue value = MakeValue(sgre::resource::PsbObject{{"x", MakeValue(sgre::resource::PsbArray{MakeValue(0.0), MakeValue(0.0), MakeValue(1.0), MakeValue(1.0)})}, {"y", MakeValue(sgre::resource::PsbArray{MakeValue(0.0), MakeValue(1.0), MakeValue(1.0), MakeValue(0.0)})}, {"t", MakeValue(sgre::resource::PsbArray{MakeValue(0.0), MakeValue(1.0)})}, {"s", MakeValue(sgre::resource::PsbArray{SplineValue()})}});
	const auto path = sgre::resource::ReadMotionPath(value);
	const auto point = sgre::resource::EvaluateMotionPath(path, 0.5f);
	Require(point.X == 0.5f && point.Y == 0.75f, "native cubic Bezier basis changed");
	Require(sgre::resource::EvaluateMotionPath(path, 0).X == 0 && sgre::resource::EvaluateMotionPath(path, 1).X == 1, "Bezier endpoint placement changed");
	Field(value, "t").Data = sgre::resource::PsbArray{MakeValue(0.0), MakeValue(0.0)};
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionPath(value));
	},
	    "increase strictly");
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::EvaluateMotionPath(path, 2));
	},
	    "outside");
}

static sgre::resource::PsbValue MakeLayer() {
	sgre::resource::PsbValue content = MakeValue(sgre::resource::PsbObject{{"src", MakeValue(std::string("Texture"))}, {"icon", MakeValue(std::string("Icon"))}, {"mask", MakeValue(std::int64_t{0})}});
	sgre::resource::PsbValue frame = MakeValue(sgre::resource::PsbObject{{"time", MakeValue(0.0)}, {"type", MakeValue(std::int64_t{2})}, {"content", std::move(content)}});
	sgre::resource::PsbValue terminal = MakeValue(sgre::resource::PsbObject{{"time", MakeValue(1.0)}, {"type", MakeValue(std::int64_t{0})}});

	return MakeValue(sgre::resource::PsbObject{{"label", MakeValue(std::string("Layer"))}, {"type", MakeValue(std::int64_t{0})}, {"coordinate", MakeValue(std::int64_t{0})}, {"inheritMask", MakeValue(std::int64_t{33556476})}, {"transformOrder", MakeValue(sgre::resource::PsbArray{MakeValue(std::int64_t{0}), MakeValue(std::int64_t{3}), MakeValue(std::int64_t{2}), MakeValue(std::int64_t{1})})}, {"children", MakeValue(sgre::resource::PsbArray{})}, {"frameList", MakeValue(sgre::resource::PsbArray{std::move(frame), std::move(terminal)})}, {"meshTransform", MakeValue(std::int64_t{0})}});
}

static sgre::resource::PsbDocument MakeDocument() {
	sgre::resource::PsbChunkReference reference;
	sgre::resource::PsbValue texture = MakeValue(sgre::resource::PsbObject{{"type", MakeValue(std::string("BC7"))}, {"width", MakeValue(std::int64_t{4})}, {"height", MakeValue(std::int64_t{4})}, {"truncated_width", MakeValue(std::int64_t{3})}, {"truncated_height", MakeValue(std::int64_t{3})}, {"pixel", MakeValue(reference)}});
	sgre::resource::PsbValue icon = MakeValue(sgre::resource::PsbObject{{"left", MakeValue(1.0)}, {"top", MakeValue(1.0)}, {"width", MakeValue(std::int64_t{2})}, {"height", MakeValue(std::int64_t{2})}, {"originX", MakeValue(0.5)}, {"originY", MakeValue(0.5)}, {"attr", MakeValue(std::int64_t{2})}, {"resolution", MakeValue(0.5)}});
	sgre::resource::PsbValue source = MakeValue(sgre::resource::PsbObject{{"texture", std::move(texture)}, {"icon", MakeValue(sgre::resource::PsbObject{{"Icon", std::move(icon)}})}});
	sgre::resource::PsbValue timeline = MakeValue(sgre::resource::PsbObject{{"type", MakeValue(std::int64_t{0})}, {"bounds", MakeValue(sgre::resource::PsbObject{{"left", MakeValue(0.0)}, {"top", MakeValue(0.0)}, {"right", MakeValue(0.0)}, {"bottom", MakeValue(0.0)}})}, {"tag", MakeValue(sgre::resource::PsbArray{})}, {"lastTime", MakeValue(1.0)}, {"loopTime", MakeValue(-1.0)}, {"layer", MakeValue(sgre::resource::PsbArray{MakeLayer()})}, {"priority", MakeValue(sgre::resource::PsbArray{})}, {"parameter", MakeValue(sgre::resource::PsbArray{})}, {"variable", MakeValue(sgre::resource::PsbArray{})}, {"parameterize", MakeValue(std::monostate{})}});
	sgre::resource::PsbValue object = MakeValue(sgre::resource::PsbObject{{"type", MakeValue(std::int64_t{0})}, {"motion", MakeValue(sgre::resource::PsbObject{{"Normal", std::move(timeline)}})}});
	sgre::resource::PsbDocument document;
	document.Header.Version = 4;
	sgre::resource::PsbChunkRange range;
	range.Offset = 10;
	range.Size = 16;
	document.Chunks.push_back(range);
	document.Root = MakeValue(sgre::resource::PsbObject{{"id", MakeValue(std::string("motion"))}, {"spec", MakeValue(std::string("win"))}, {"version", MakeValue(3.03)}, {"label", MakeValue(std::string("Synthetic"))}, {"easing", MakeValue(sgre::resource::PsbArray{})}, {"source", MakeValue(sgre::resource::PsbObject{{"Texture", std::move(source)}})}, {"object", MakeValue(sgre::resource::PsbObject{{"Object", std::move(object)}})}});

	return document;
}

static sgre::resource::PsbValue& Timeline(sgre::resource::PsbDocument& document) {
	return Field(Field(Field(Field(document.Root, "object"), "Object"), "motion"), "Normal");
}

static void TestResourceSchema() {
	const auto resource = sgre::resource::ReadMotionResource(MakeDocument());
	Require(resource.Label == "Synthetic" && resource.Sources.size() == 1 && resource.Objects.size() == 1, "motion resource identity changed");
	Require(resource.Chunks[0].Offset == 10 && resource.Sources[0].Texture.TruncatedWidth == 3, "motion texture padding or chunk identity changed");
	Require(resource.Sources[0].Icons[0].AttributeFlags == 2 && resource.Sources[0].Icons[0].Resolution == 0.5f, "motion icon attributes or resolution changed");
	auto scaled_document = MakeDocument();
	auto& scaled_icon = Field(Field(Field(Field(scaled_document.Root, "source"), "Texture"), "icon"), "Icon");
	Field(scaled_icon, "width").Data = 6.9;
	Field(scaled_icon, "left").Data = 1.9;
	const auto scaled_resource = sgre::resource::ReadMotionResource(scaled_document);
	Require(scaled_resource.Sources[0].Icons[0].Width > 6, "motion geometric extent was confused with the scaled atlas extent");
	Field(scaled_icon, "width").Data = 8.0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionResource(scaled_document));
	},
	    "exceeds its atlas");
	Field(scaled_icon, "width").Data = 2147483648.0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionResource(scaled_document));
	},
	    "exceeds its limit");

	const auto& timeline = resource.Objects[0].Timelines[0];
	Require(timeline.Name == "Normal" && timeline.LoopTime == -1 && timeline.LastTime == 1, "motion timeline identity or times changed");
	const auto& layer = timeline.Layers[0];
	Require(layer.Frames.size() == 2 && layer.Frames[0].Type == 2 && layer.Frames[1].Type == 0, "motion keyframe types changed");
	Require(layer.TransformOrder == std::vector<std::uint32_t>({0, 3, 2, 1}), "motion transform order changed");
	Require(layer.Frames[0].Content.Find("src") != nullptr && !layer.Properties.empty(), "unrecovered motion attributes were discarded");
}

static void TestResourceSafety() {
	auto document = MakeDocument();
	Field(document.Root, "version").Data = 3.04;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionResource(document));
	},
	    "schema version");
	document = MakeDocument();
	auto& icon = Field(Field(Field(Field(document.Root, "source"), "Texture"), "icon"), "Icon");
	Field(icon, "resolution").Data = 0.0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionResource(document));
	},
	    "resolution must be positive");
	document = MakeDocument();
	Field(Timeline(document), "loopTime").Data = 2.0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionResource(document));
	},
	    "timeline loop");
	document = MakeDocument();
	auto& layer = std::get<sgre::resource::PsbArray>(Field(Timeline(document), "layer").Data)[0];
	auto& frames = std::get<sgre::resource::PsbArray>(Field(layer, "frameList").Data);
	Field(frames[1], "time").Data = -1.0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionResource(document));
	},
	    "frame time");
	document = MakeDocument();
	for (std::size_t depth = 0; depth < 130; ++depth) {
		auto& layers = Field(Timeline(document), "layer");
		auto parent = MakeLayer();
		Field(parent, "children") = layers;
		layers.Data = sgre::resource::PsbArray{std::move(parent)};
	}

	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionResource(document));
	},
	    "layer depth");
}

int main() {
	try {
		TestNativeSplineVectors();
		TestRecoveredInterpolation();
		TestSplineReaderSafety();
		TestBezierPath();
		TestResourceSchema();
		TestResourceSafety();
		std::cout << "Motion schema, native spline/angle/color math, Bezier paths, and safety checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
