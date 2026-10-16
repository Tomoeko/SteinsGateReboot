#include "sgre/text/vector_font.hpp"

#include "sgre/text/vector_font_metric_services.hpp"
#include "sgre/text/vector_font_normalization_font.hpp"
#include "vector_font_fixture.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using ScaleSign = sgre::text::VectorFontMetricScaleSign;

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

	throw std::runtime_error("live metric owner fixture expected supported-domain rejection");
}

static std::vector<std::uint8_t> MetricFixture(std::uint16_t advance) {
	auto bytes = vector_font_test::MakeFont();
	std::size_t horizontal = 0;
	std::size_t vertical = 0;
	{
		const sgre::resource::SfntFont reader(bytes);
		for (const auto& table : reader.Tables()) {
			if (table.Tag == "hmtx") {
				horizontal = table.Offset;
			} else if (table.Tag == "vmtx") {
				vertical = table.Offset;
			}
		}
	}
	Require(horizontal != 0 && vertical != 0, "independent real metric tables are absent");
	vector_font_test::WriteWord(bytes, horizontal, advance);
	vector_font_test::WriteWord(bytes, vertical, advance);

	return bytes;
}

static void TestSameOwnerAndLiveSize() {
	auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	const std::weak_ptr<const std::vector<std::uint8_t>> resource = bytes;
	{
		sgre::text::VectorFont font(bytes);
		RequireFailure([&]() {
			static_cast<void>(font.MetricServices());
		},
		    "admission");
		RequireFailure([&]() {
			static_cast<void>(font.NormalizationFont().MetricServices());
		},
		    "admission");
		font.SetSize(-1);
		RequireFailure([&]() {
			font.BindMetricServices(ScaleSign::Nonnegative, ScaleSign::Nonnegative);
		},
		    "positive supported");
		Require(!font.SizeState().RequestedHeight26Dot6 && font.SizeState().Word == 0xffffffff, "rejected admission must preserve unchanged initial size sentinel");
		font.SetSize(24.9f);
		const auto pins = bytes.use_count();
		font.BindMetricServices(ScaleSign::Nonnegative, ScaleSign::Nonnegative);
		const auto& services = font.MetricServices();
		Require(&services == &font.NormalizationFont().MetricServices() && &services.Font() == &font.Font() && bytes.use_count() == pins, "normalization callbacks must borrow the same real owned service/reader without another resource root");
		Require(!services.HasHorizontalOrigin() && services.HasVerticalOrigin(), "missing horizontal-origin callback is capability absence, separate from a zero coordinate");
		const auto origin = services.VerticalOrigin(0);
		Require(services.HorizontalAdvance(0) == 1536 && services.VerticalAdvance(0) == -1536 && origin && origin->X26Dot6 == 768 && origin->Y26Dot6 == 1636, "applied flags-2 callback metrics must use actual table/program/size services");
		Require(font.PrepareGlyph(0).Metrics.VerticalBearingY26Dot6 == 64, "new shaping service must preserve existing hinted flag8 raster path");
		const auto* identity = &services;
		font.SetSize(48);
		const auto resized = services.VerticalOrigin(0);
		Require(&font.MetricServices() == identity && services.HorizontalAdvance(0) == 3072 && services.VerticalAdvance(0) == -3072 && resized && resized->X26Dot6 == 1536 && resized->Y26Dot6 == 3172, "live FT size changes must reach the same callback owner with unchanged HB signs");
		RequireFailure([&]() {
			font.BindMetricServices(ScaleSign::Negative, ScaleSign::Negative);
		},
		    "already been bound");
		Require(services.HorizontalAdvance(0) == 3072 && services.VerticalAdvance(0) == -3072, "rejected rebind must not replace immutable callback scale signs");
		bytes.reset();
		Require(!resource.expired() && services.VerticalOrigin(1)->Y26Dot6 == 100, "the original font pin must keep actual empty-glyph callback data alive");
	}
	Require(resource.expired(), "font/normalization/metric teardown must not leave an extra raw resource root");
}

static void TestImmutableSignBeforeRounding() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(MetricFixture(836));
	sgre::text::VectorFont positive(bytes);
	sgre::text::VectorFont negative(bytes);
	sgre::text::VectorFont mixed(bytes);
	sgre::text::VectorFont zero(bytes);
	for (auto* font : {&positive, &negative, &mixed, &zero}) {
		font->SetSize(1);
	}
	positive.BindMetricServices(ScaleSign::Nonnegative, ScaleSign::Nonnegative);
	negative.BindMetricServices(ScaleSign::Negative, ScaleSign::Negative);
	mixed.BindMetricServices(ScaleSign::Negative, ScaleSign::Nonnegative);
	zero.BindMetricServices(ScaleSign::Nonnegative, ScaleSign::Nonnegative);
	// Independent table836/size1/UPEM1000 gives advance16=54784, exactly half of one 26.6 unit.
	Require(positive.MetricServices().HorizontalAdvance(0) == 54 && positive.MetricServices().VerticalAdvance(0) == -53 && negative.MetricServices().HorizontalAdvance(0) == -53 && negative.MetricServices().VerticalAdvance(0) == 54, "HB sign must apply before signed half-unit rounding rather than negate a rounded metric");
	Require(zero.MetricServices().HorizontalAdvance(0) == 54 && zero.MetricServices().VerticalAdvance(0) == -53, "immutable nonnegative HB sign must not replace the actual live FT size scale");
	const auto origin = mixed.MetricServices().VerticalOrigin(0);
	Require(origin && origin->X26Dot6 == -27 && origin->Y26Dot6 == 164 && mixed.MetricServices().HorizontalAdvance(0) == -53 && mixed.MetricServices().VerticalAdvance(0) == -53, "origin flips axes independently and retains the actual unscaled vertical bearing");
	negative.SetSize(24);
	Require(negative.MetricServices().HorizontalAdvance(0) == -1284 && negative.MetricServices().VerticalAdvance(0) == 1284 && positive.MetricServices().HorizontalAdvance(0) == 54, "size updates must preserve signs and independent mutable size owners despite shared bytes");
}

static void TestErrorAndAppliedDomainGates() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	sgre::text::VectorFont font(bytes);
	font.SetSize(24);
	font.BindMetricServices(ScaleSign::Nonnegative, ScaleSign::Nonnegative);
	const auto& services = font.MetricServices();
	Require(services.HorizontalAdvance(3) == 0 && services.VerticalAdvance(0xffffffff) == 0 && !services.VerticalOrigin(3), "installed glyph-range failure must be zero advance or absent origin, not an invented glyph0 substitute");
	for (const auto size : {0.0f, -2.0f, 2001.0f, std::numeric_limits<float>::quiet_NaN()}) {
		font.SetSize(size);
		RequireFailure([&]() {
			static_cast<void>(services.HorizontalAdvance(0));
		},
		    "positive supported");
		RequireFailure([&]() {
			static_cast<void>(services.VerticalOrigin(0));
		},
		    "positive supported");
		Require(services.HorizontalAdvance(0xffffffff) == 0 && !services.VerticalOrigin(0xffffffff), "native invalid-glyph gate precedes the unverified current size domain");
	}
	font.SetSize(-2);
	Require(font.SizeState().Word == 0xfffffffe && font.SizeState().RequestedHeight26Dot6 == std::numeric_limits<std::int32_t>::min() && font.PrepareGlyph(0).PixelSize == 1, "metric rejection must preserve existing raw overflow and hinted preparation contracts");
	font.SetSize(24.9f);
	Require(services.HorizontalAdvance(0) == 1536 && services.VerticalAdvance(0) == -1536, "return to applied supported size must reuse the genuine binding without rescaling HB signs");
	const auto no_vertical_bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont(1000, false));
	sgre::text::VectorFont no_vertical(no_vertical_bytes);
	no_vertical.SetSize(24);
	RequireFailure([&]() {
		no_vertical.BindMetricServices(ScaleSign::Nonnegative, ScaleSign::Nonnegative);
	},
	    "original vertical tables");
	RequireFailure([&]() {
		static_cast<void>(no_vertical.NormalizationFont().MetricServices());
	},
	    "admission");
}

int main() {
	try {
		TestSameOwnerAndLiveSize();
		TestImmutableSignBeforeRounding();
		TestErrorAndAppliedDomainGates();
		std::cout << "three same-owner live flags-2 metric/sign/error/admission groups pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
