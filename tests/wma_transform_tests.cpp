#include "sgre/audio/wma_transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestIndependentCosineDefinition() {
	constexpr std::uint32_t kSize = 128;
	std::vector<float> coefficients(kSize);
	for (std::size_t index = 0; index < coefficients.size(); ++index) {
		coefficients[index] = static_cast<float>(static_cast<std::int32_t>(index * 13 % 41) - 20) / 32.0f;
	}

	sgre::audio::WmaInverseTransform transform(kSize);
	std::vector<float> output(kSize);
	transform.ComputeCompact(coefficients, output);
	for (std::size_t sample = 0; sample < output.size(); ++sample) {
		double expected = 0.0;
		for (std::size_t coefficient = 0; coefficient < coefficients.size(); ++coefficient) {
			const double phase = std::numbers::pi / kSize * (static_cast<double>(sample) + 0.5) * (static_cast<double>(coefficient) + 0.5);
			expected += static_cast<double>(coefficients[coefficient]) * std::cos(phase);
		}

		expected *= 2.0 / kSize;
		Require(std::abs(static_cast<double>(output[sample]) - expected) <= 0.00000003, "compact inverse transform differs from the independent cosine definition");
	}

	auto aliased = coefficients;
	transform.ComputeCompact(aliased, aliased);
	Require(aliased == output && transform.CoefficientCount() == kSize, "inverse transform changed when input/output storage was identical");
	std::ranges::fill(coefficients, 0.0f);
	transform.ComputeCompact(coefficients, output);
	Require(std::ranges::all_of(output, [](float value) {
		return value == 0.0f;
	}),
	    "reused inverse transform retained samples from the preceding block");
}

static void TestTimeDomainDefinition() {
	for (const auto size : std::array<std::uint32_t, 5>{128, 256, 512, 1024, 2048}) {
		std::vector<float> coefficients(size);
		coefficients[size / 3] = 0.25f;
		sgre::audio::WmaInverseTransform transform(size);
		std::vector<float> compact(size);
		std::vector<float> output(size * 2);
		transform.ComputeCompact(coefficients, compact);
		sgre::audio::ExpandWmaInverseTransform(compact, output);
		for (std::size_t sample = 0; sample < output.size(); ++sample) {
			const double phase = std::numbers::pi / size * (static_cast<double>(sample) + 0.5 + size / 2) * (static_cast<double>(size / 3) + 0.5);
			const double expected = 0.25 * 2.0 / size * std::cos(phase);
			Require(std::abs(static_cast<double>(output[sample]) - expected) <= 0.00000001, "expanded inverse transform differs from the independent 2N cosine definition");
		}
	}
}

static void TestBoundsAndTransactionalOutput() {
	for (const auto size : std::array<std::uint32_t, 5>{0, 64, 129, 768, 4096}) {
		bool rejected = false;
		try {
			const sgre::audio::WmaInverseTransform transform(size);
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "invalid inverse transform coefficient count was accepted");
	}

	sgre::audio::WmaInverseTransform transform(128);
	std::vector<float> coefficients(128);
	std::vector<float> output(128, 7.0f);
	for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
		coefficients[17] = invalid;
		bool rejected = false;
		try {
			transform.ComputeCompact(coefficients, output);
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected && std::ranges::all_of(output, [](float value) {
			return value == 7.0f;
		}),
		    "invalid inverse transform coefficient changed caller output");
	}

	std::ranges::fill(coefficients, std::numeric_limits<float>::max());
	bool rejected = false;
	try {
		transform.ComputeCompact(coefficients, output);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && std::ranges::all_of(output, [](float value) {
		return value == 7.0f;
	}),
	    "inverse transform overflow changed caller output");
	std::vector<float> short_output(127, 7.0f);
	rejected = false;
	try {
		transform.ComputeCompact(coefficients, short_output);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "short inverse transform output was accepted");
	std::vector<float> overlapping(256, 1.0f);
	rejected = false;
	try {
		sgre::audio::ExpandWmaInverseTransform(std::span<const float>(overlapping).first(128), overlapping);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && std::ranges::all_of(overlapping, [](float value) {
		return value == 1.0f;
	}),
	    "overlapping expanded transform storage was accepted or changed");
}

int main() {
	try {
		TestIndependentCosineDefinition();
		TestTimeDomainDefinition();
		TestBoundsAndTransactionalOutput();
		std::cout << "WMA transform tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
