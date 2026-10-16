#include "sgre/text/vector_font.hpp"

#include "sgre/text/vector_font_metric_services.hpp"
#include "sgre/text/vector_font_normalization_font.hpp"
#include "vector_font_fixture.hpp"

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

	throw std::runtime_error("default CFF metric admission fixture expected rejection");
}

static void TestDefaultOwnerAndLiveSize() {
	auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	const std::weak_ptr<const std::vector<std::uint8_t>> resource = bytes;
	{
		sgre::text::VectorFont font(bytes);
		Require(font.SizeState().Word == 0xffffffff && !font.SizeState().RequestedHeight26Dot6, "default sign admission must preserve the real raw size sentinel");
		RequireFailure([&]() {
			font.BindDefaultCffMetricServices();
		},
		    "positive supported");
		RequireFailure([&]() {
			static_cast<void>(font.NormalizationFont().MetricServices());
		},
		    "admission");
		font.SetSize(24.9f);
		const auto resource_pins = bytes.use_count();
		font.BindDefaultCffMetricServices();
		const auto& service = font.MetricServices();
		Require(&service == &font.NormalizationFont().MetricServices() && &service.Font() == &font.Font() && bytes.use_count() == resource_pins, "default signs must bind the same real metric owner without another resource root");
		const auto origin = service.VerticalOrigin(0);
		Require(service.HorizontalAdvance(0) == 1536 && service.VerticalAdvance(0) == -1536 && origin && origin->X26Dot6 == 768 && origin->Y26Dot6 == 1636, "default CFF nonnegative signs must preserve actual owned table and unhinted origin values");
		Require(!service.HasHorizontalOrigin() && service.HasVerticalOrigin() && font.PrepareGlyph(0).Metrics.VerticalBearingY26Dot6 == 64, "default admission must retain absent horizontal-origin capability and separate hinted flag8 preparation");
		font.SetSize(48);
		const auto resized_origin = service.VerticalOrigin(0);
		Require(&font.MetricServices() == &service && service.HorizontalAdvance(0) == 3072 && service.VerticalAdvance(0) == -3072 && resized_origin && resized_origin->X26Dot6 == 1536 && resized_origin->Y26Dot6 == 3172, "default HB signs must survive live FT size changes in the same service");
		bytes.reset();
		Require(!resource.expired() && service.VerticalOrigin(1)->Y26Dot6 == 100, "the actual font owner must retain bytes through empty glyph queries");
	}

	Require(resource.expired(), "default binding teardown must not leak a font or resource root");
}

static void TestAdmissionRejectsUnsupportedCurrentSizes() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont());
	for (const auto size : {0.0f, -2.0f, 2001.0f, std::numeric_limits<float>::quiet_NaN()}) {
		sgre::text::VectorFont font(bytes);
		font.SetSize(size);
		const auto raw_word = font.SizeState().Word;
		const auto raw_request = font.SizeState().RequestedHeight26Dot6;
		RequireFailure([&]() {
			font.BindDefaultCffMetricServices();
		},
		    "positive supported");
		Require(font.SizeState().Word == raw_word && font.SizeState().RequestedHeight26Dot6 == raw_request, "rejected default admission must not rewrite raw or failed-size state");
		RequireFailure([&]() {
			static_cast<void>(font.MetricServices());
		},
		    "admission");
		font.SetSize(1);
		font.BindDefaultCffMetricServices();
		Require(font.MetricServices().HorizontalAdvance(0) == 64 && font.MetricServices().VerticalAdvance(0) == -64, "a rejected admission must not consume the later valid default binding");
		font.SetSize(0);
		RequireFailure([&]() {
			static_cast<void>(font.MetricServices().HorizontalAdvance(0));
		},
		    "positive supported");
		Require(font.MetricServices().HorizontalAdvance(0xffffffff) == 0, "existing invalid glyph error gate must retain its ordering");
	}

	const auto no_vertical_bytes = std::make_shared<const std::vector<std::uint8_t>>(vector_font_test::MakeFont(1000, false));
	sgre::text::VectorFont no_vertical(no_vertical_bytes);
	no_vertical.SetSize(24);
	RequireFailure([&]() {
		no_vertical.BindDefaultCffMetricServices();
	},
	    "original vertical tables");
	RequireFailure([&]() {
		static_cast<void>(no_vertical.NormalizationFont().MetricServices());
	},
	    "admission");
}

static std::vector<std::uint8_t> HalfAdvanceFixture() {
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
	Require(horizontal != 0 && vertical != 0, "independent half-rounding tables must exist");
	vector_font_test::WriteWord(bytes, horizontal, 836);
	vector_font_test::WriteWord(bytes, vertical, 836);

	return bytes;
}

static void TestExplicitSignAndOneTimeBindingRemainDistinct() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(HalfAdvanceFixture());
	sgre::text::VectorFont ordinary(bytes);
	sgre::text::VectorFont prepared_negative(bytes);
	ordinary.SetSize(1);
	prepared_negative.SetSize(1);
	ordinary.BindDefaultCffMetricServices();
	prepared_negative.BindMetricServices(sgre::text::VectorFontMetricScaleSign::Negative, sgre::text::VectorFontMetricScaleSign::Negative);
	Require(ordinary.MetricServices().HorizontalAdvance(0) == 54 && ordinary.MetricServices().VerticalAdvance(0) == -53 && prepared_negative.MetricServices().HorizontalAdvance(0) == -53 && prepared_negative.MetricServices().VerticalAdvance(0) == 54, "default native signs and separately prepared signs must remain independent before half-unit rounding");
	RequireFailure([&]() {
		ordinary.BindDefaultCffMetricServices();
	},
	    "already been bound");
	RequireFailure([&]() {
		ordinary.BindMetricServices(sgre::text::VectorFontMetricScaleSign::Negative, sgre::text::VectorFontMetricScaleSign::Negative);
	},
	    "already been bound");
	RequireFailure([&]() {
		prepared_negative.BindDefaultCffMetricServices();
	},
	    "already been bound");
	Require(prepared_negative.MetricServices().HorizontalAdvance(0) == -53 && ordinary.MetricServices().HorizontalAdvance(0) == 54, "rejected default/explicit rebinding must preserve both installed identities and signs");
}

int main() {
	try {
		TestDefaultOwnerAndLiveSize();
		TestAdmissionRejectsUnsupportedCurrentSizes();
		TestExplicitSignAndOneTimeBindingRemainDistinct();
		std::cout << "three default CFF positive-size owner/sign/admission groups pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
