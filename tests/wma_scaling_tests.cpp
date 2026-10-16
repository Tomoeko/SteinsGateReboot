#include "sgre/audio/wma_scaling.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestNativeGainVectors() {
	Require(std::bit_cast<std::uint32_t>(sgre::audio::WmaGainScale(0)) == 0x3f800000, "native zero-gain scale differs");
	Require(std::bit_cast<std::uint32_t>(sgre::audio::WmaGainScale(1)) == 0x3f8f9e4d, "native single-gain scale differs");
	Require(sgre::audio::WmaGainScale(100) == 100000.0f, "native gain power differs");
}

static void TestSignedScaleVectors() {
	sgre::audio::WmaCoefficient coefficient;
	coefficient.Magnitude = 1;
	coefficient.IsNegative = true;
	Require(sgre::audio::ScaleWmaCoefficient(coefficient, 0.25f, 0) == -0.25f, "negative native coefficient sign differs");
	Require(std::bit_cast<std::uint32_t>(sgre::audio::ScaleWmaCoefficient(coefficient, 0.25f, 1)) == 0xbe5dafd6, "native fractional exponent scale differs");
	coefficient.Magnitude = 511;
	coefficient.IsNegative = false;
	Require(std::bit_cast<std::uint32_t>(sgre::audio::ScaleWmaCoefficient(coefficient, 2.5f, 71)) == 0x3d3f151e, "native maximum tested exponent difference differs");
	Require(sgre::audio::ScaleWmaCoefficient(coefficient, 2.5f, 72) == sgre::audio::ScaleWmaCoefficient(coefficient, 2.5f, 71) && sgre::audio::ScaleWmaCoefficient(coefficient, 2.5f, 200) == sgre::audio::ScaleWmaCoefficient(coefficient, 2.5f, 71), "native exponent difference clamp differs");
	coefficient.Magnitude = 0;
	coefficient.IsNegative = true;
	Require(std::bit_cast<std::uint32_t>(sgre::audio::ScaleWmaCoefficient(coefficient, 1.0f, 0)) == 0x80000000, "native negative-zero coefficient sign was lost");
}

static void TestBounds() {
	for (const auto gain : {-33, 513}) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::WmaGainScale(gain));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "unvalidated WMA gain domain was accepted");
	}

	sgre::audio::WmaCoefficient coefficient;
	coefficient.Magnitude = 1;
	for (const auto scale : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::ScaleWmaCoefficient(coefficient, scale, 0));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "invalid WMA gain scale was accepted");
	}

	coefficient.Kind = sgre::audio::WmaCoefficientKind::EndOfBlock;
	bool rejected = false;
	try {
		static_cast<void>(sgre::audio::ScaleWmaCoefficient(coefficient, 1.0f, 0));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "end-of-block marker was treated as a scaled coefficient");
	coefficient.Kind = sgre::audio::WmaCoefficientKind::Value;
	rejected = false;
	try {
		static_cast<void>(sgre::audio::ScaleWmaCoefficient(coefficient, 1.0f, -1));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "negative exponent difference was accepted");
	coefficient.Magnitude = 1 << 24;
	rejected = false;
	try {
		static_cast<void>(sgre::audio::ScaleWmaCoefficient(coefficient, 1.0f, 0));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "coefficient magnitude beyond the native field was accepted");
}

int main() {
	try {
		TestNativeGainVectors();
		TestSignedScaleVectors();
		TestBounds();
		std::cout << "WMA scaling tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
