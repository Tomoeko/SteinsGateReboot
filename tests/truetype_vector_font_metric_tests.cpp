#include "sgre/text/vector_font.hpp"

#include "sgre/text/vector_font_normalization_font.hpp"
#include "truetype_vector_font_fixture.hpp"
#include "vector_font_fixture.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("TrueType metric fixture expected its precise unsupported boundary");
}

static std::shared_ptr<const std::vector<std::uint8_t>> MetricBytes(std::uint16_t horizontal = 200, std::uint16_t vertical = 300, bool has_vertical = true) {
	auto rectangle = truetype_vector_test::Rectangle();
	rectangle.Horizontal.Advance = horizontal;
	rectangle.Vertical.Advance = vertical;
	const std::array<truetype_vector_test::GlyphDefinition, 2> definitions{rectangle, truetype_vector_test::GlyphDefinition{}};

	return std::make_shared<const std::vector<std::uint8_t>>(truetype_vector_test::MakeFont(definitions, has_vertical).Bytes);
}

static void TestDefaultSameOwnerLiveSizeAndRetirement() {
	auto bytes = MetricBytes();
	const std::weak_ptr<const std::vector<std::uint8_t>> weak = bytes;
	{
		sgre::text::VectorFont font(bytes);
		Require(font.Font().OutlineKind() == sgre::resource::SfntOutlineKind::TrueType && font.SizeState().Word == 0xffffffff && !font.SizeState().RequestedHeight26Dot6, "TrueType construction must retain its real outline identity and raw size sentinel");
		RequireFailure([&] {
			font.BindDefaultMetricServices();
		},
		    "positive supported");
		font.SetSize(24.9f);
		const auto resource_pins = bytes.use_count();
		font.BindDefaultMetricServices();
		const auto& metrics = font.MetricServices();
		Require(&metrics == &font.NormalizationFont().MetricServices() && &metrics.Font() == &font.Font() && bytes.use_count() == resource_pins, "TrueType callback owner must borrow the same reader without an added resource or self root");
		Require(!metrics.HasHorizontalOrigin() && metrics.HasVerticalOrigin(), "TrueType admission must preserve the native origin capability slots");
		const auto first = metrics.VerticalOrigin(0);
		Require(metrics.HorizontalAdvance(0) == 307 && metrics.VerticalAdvance(0) == -461 && first && first->X26Dot6 == 153 && first->Y26Dot6 == 108, "TrueType default callbacks must use actual tables and completed unhinted phantom metrics");
		font.SetSize(48);
		const auto resized = metrics.VerticalOrigin(0);
		Require(&font.MetricServices() == &metrics && metrics.HorizontalAdvance(0) == 614 && metrics.VerticalAdvance(0) == -922 && resized && resized->X26Dot6 == 307 && resized->Y26Dot6 == 215, "live size must reach the same TrueType service while table advance stays distinct from phantom-point advance");
		bytes.reset();
		const auto empty = metrics.VerticalOrigin(1);
		Require(!weak.expired() && empty && empty->X26Dot6 == 307 && empty->Y26Dot6 == 61, "the real resource owner must preserve empty-glyph vertical metrics after external source retirement");
		const auto prepared = font.PrepareGlyph(0);
		Require(prepared.GlyphIndex == 0 && prepared.PixelSize == 48 && prepared.PreparedPath && !prepared.PreparedPath->Commands.empty() && prepared.Outline.Points.empty() && prepared.Outline.ContourEnds.empty(), "TrueType glyph preparation must own its actual quadratic path without fabricated CFF point tags");
		Require(prepared.Metrics.AdvanceX26Dot6 == prepared.Metrics.HorizontalAdvance26Dot6 && prepared.Metrics.AdvanceY26Dot6 == 0 && !weak.expired(), "TrueType flags-8 horizontal glyph metrics must retain their same live font owner");
	}

	Require(weak.expired(), "TrueType services must retire with the enclosing font and release all resource pins");
}

static void TestExplicitSignsAndBindingFailures() {
	const auto bytes = MetricBytes(836, 836);
	sgre::text::VectorFont positive(bytes);
	sgre::text::VectorFont negative(bytes);
	sgre::text::VectorFont mixed(bytes);
	for (auto* font : {&positive, &negative, &mixed}) {
		font->SetSize(1);
	}
	positive.BindDefaultMetricServices();
	negative.BindMetricServices(sgre::text::VectorFontMetricScaleSign::Negative, sgre::text::VectorFontMetricScaleSign::Negative);
	mixed.BindMetricServices(sgre::text::VectorFontMetricScaleSign::Negative, sgre::text::VectorFontMetricScaleSign::Nonnegative);
	Require(positive.MetricServices().HorizontalAdvance(0) == 54 && positive.MetricServices().VerticalAdvance(0) == -53 && negative.MetricServices().HorizontalAdvance(0) == -53 && negative.MetricServices().VerticalAdvance(0) == 54, "native axis sign must apply before signed half-unit rounding");
	const auto mixed_origin = mixed.MetricServices().VerticalOrigin(0);
	Require(mixed_origin && mixed_origin->X26Dot6 == -27 && mixed_origin->Y26Dot6 == 4, "TrueType completed origin must apply each installed HB sign independently");
	RequireFailure([&] {
		positive.BindDefaultMetricServices();
	},
	    "already been bound");
	RequireFailure([&] {
		negative.BindDefaultMetricServices();
	},
	    "already been bound");
	RequireFailure([&] {
		mixed.BindDefaultCffMetricServices();
	},
	    "actual normalized CFF owner");
	Require(negative.MetricServices().HorizontalAdvance(0) == -53 && positive.MetricServices().HorizontalAdvance(0) == 54, "rejected bindings must preserve independent callback signs and identities");
}

static void TestAppliedSizeVerticalAndRasterGates() {
	const auto bytes = MetricBytes();
	sgre::text::VectorFont font(bytes);
	font.SetSize(24);
	font.BindDefaultMetricServices();
	const auto& metrics = font.MetricServices();
	for (const auto size : {0.0f, -2.0f, 2001.0f, std::numeric_limits<float>::quiet_NaN()}) {
		font.SetSize(size);
		RequireFailure([&] {
			static_cast<void>(metrics.HorizontalAdvance(0));
		},
		    "positive supported");
		RequireFailure([&] {
			static_cast<void>(metrics.VerticalOrigin(0));
		},
		    "positive supported");
		Require(metrics.HorizontalAdvance(2) == 0 && metrics.VerticalAdvance(0xffffffff) == 0 && !metrics.VerticalOrigin(2), "invalid-glyph callbacks must preserve zero/absent ordering before current-size admission");
	}
	font.SetSize(24);
	Require(metrics.HorizontalAdvance(0) == 307, "returning to a valid size must reuse the installed actual service");
	const auto horizontal_only = MetricBytes(200, 300, false);
	sgre::text::VectorFont missing_vertical(horizontal_only);
	missing_vertical.SetSize(24);
	RequireFailure([&] {
		missing_vertical.BindDefaultMetricServices();
	},
	    "original vertical tables");
	RequireFailure([&] {
		static_cast<void>(missing_vertical.NormalizationFont().MetricServices());
	},
	    "admission");
}

static void TestRealMeasureRunAndUnchangedCffPath() {
	const auto bytes = MetricBytes();
	sgre::text::VectorFont unit(bytes);
	unit.SetSize(24);
	const auto size = unit.MeasureCodeUnit('A');
	Require(size.Width == 307.0f / 64.0f && size.Height == 0, "the real single-unit path must install TrueType metric services without a CFF alias");
	sgre::text::VectorFont run(bytes);
	run.SetSize(24);
	const auto actual = run.PrepareRun(u"AB");
	Require(actual.Glyphs.size() == 2 && actual.Advance.X == 614.0f / 64.0f && actual.Advance.Y == 0, "the real clean whole-run path must use fresh zero HB layout scales and live TrueType table callbacks");
	Require(&run.MetricServices() == &run.NormalizationFont().MetricServices(), "whole-run callbacks must retain the same enclosing font owner");
	const auto cff_bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::text::VectorFont generic_cff(cff_bytes);
	sgre::text::VectorFont explicit_cff(cff_bytes);
	generic_cff.SetSize(24);
	explicit_cff.SetSize(24);
	generic_cff.BindDefaultMetricServices();
	explicit_cff.BindDefaultCffMetricServices();
	const auto generic_origin = generic_cff.MetricServices().VerticalOrigin(0);
	const auto explicit_origin = explicit_cff.MetricServices().VerticalOrigin(0);
	Require(generic_cff.MetricServices().HorizontalAdvance(0) == explicit_cff.MetricServices().HorizontalAdvance(0) && generic_cff.MetricServices().VerticalAdvance(0) == explicit_cff.MetricServices().VerticalAdvance(0) && generic_origin && explicit_origin && generic_origin->X26Dot6 == explicit_origin->X26Dot6 && generic_origin->Y26Dot6 == explicit_origin->Y26Dot6, "generic admission must preserve the original CFF callback equations and default signs");
	Require(generic_cff.PrepareGlyph(0).Metrics.VerticalBearingY26Dot6 == 64, "generic admission must preserve CFF's separate hinted glyph preparation");
}

int main() {
	try {
		TestDefaultSameOwnerLiveSizeAndRetirement();
		TestExplicitSignsAndBindingFailures();
		TestAppliedSizeVerticalAndRasterGates();
		TestRealMeasureRunAndUnchangedCffPath();
		std::cout << "four TrueType metric owner integration groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
