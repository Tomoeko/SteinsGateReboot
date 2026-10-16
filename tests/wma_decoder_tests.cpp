#include "sgre/audio/wma_decoder.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

class BitFixture {
public:
	void Append(std::uint32_t value, std::uint8_t width) {
		for (std::uint8_t index = width; index != 0; --index) {
			if (m_Bits % 8 == 0) {
				m_Bytes.push_back(0);
			}

			m_Bytes.back() |= static_cast<std::uint8_t>(((value >> (index - 1)) & 1) << (7 - m_Bits % 8));
			++m_Bits;
		}
	}

	std::vector<std::uint8_t> Packet() const {
		auto bytes = m_Bytes;
		bytes.resize(1487);

		return bytes;
	}

private:
	std::vector<std::uint8_t> m_Bytes;
	std::size_t m_Bits = 0;
};

static sgre::resource::WaveFormat Format() {
	sgre::resource::WaveFormat format;
	format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	format.Channels = 1;
	format.SamplesPerSecond = 44100;
	format.AverageBytesPerSecond = 4000;
	format.BlockAlignment = 1487;
	format.BitsPerSample = 16;

	return format;
}

static std::vector<std::uint8_t> SyntheticPacket(bool impulse) {
	BitFixture fixture;
	fixture.Append(0, 4);
	fixture.Append(6, 4);
	fixture.Append(0, 12);
	fixture.Append(0, 3);
	fixture.Append(0, 3);
	for (std::size_t frame = 0; frame < 6; ++frame) {
		fixture.Append(0, 3);
		fixture.Append(1, 1);
		fixture.Append(63, 7);
		for (std::size_t band = 0; band < 25; ++band) {
			fixture.Append(0, 1);
		}

		if (impulse && frame == 2) {
			fixture.Append(0x3e6, 11);
			fixture.Append(128, 9);
			fixture.Append(0, 11);
			fixture.Append(1, 1);
		}

		fixture.Append(0xf6, 9);
	}

	return fixture.Packet();
}

static void TestIndependentImpulse() {
	const std::array<std::uint32_t, 1> ledger = {16384};
	const auto decoded = sgre::audio::DecodeWmaStandardAudio(Format(), SyntheticPacket(true), ledger, 31);
	Require(decoded.Channels == 1 && decoded.SamplesPerSecond == 44100 && decoded.Samples.size() == 8192, "WMA synthetic native extent or format differs");
	constexpr double coefficient_count = 2048.0;
	const float native_gain_argument = 64.0f * 0.05f;
	const double amplitude = static_cast<float>(std::pow(10.0, static_cast<double>(native_gain_argument))) * 128.0;
	for (std::size_t index = 0; index < decoded.Samples.size(); ++index) {
		double expected = 0;
		if (index < 4096) {
			const double sample_position = static_cast<double>(index) + 0.5;
			expected = -amplitude / 32768.0 * (2.0 / coefficient_count) * std::cos(std::numbers::pi / coefficient_count * (sample_position + coefficient_count / 2.0) * 0.5) * std::sin(std::numbers::pi / (2.0 * coefficient_count) * sample_position);
		}

		Require(std::abs(static_cast<double>(decoded.Samples[index]) - expected) < 2e-9, "WMA synthetic impulse differs from independent cosine and sine definition");
	}

	const auto silent = sgre::audio::DecodeWmaStandardAudio(Format(), SyntheticPacket(false), ledger, 31);
	for (const auto value : silent.Samples) {
		Require(value == 0.0f, "WMA zero coefficients produced nonzero output");
	}
}

static void TestRejectedInput() {
	const auto packet = SyntheticPacket(true);
	const std::array<std::uint32_t, 1> ledger = {16384};
	for (std::size_t variant = 0; variant < 10; ++variant) {
		auto format = Format();
		auto bytes = packet;
		auto cumulative = ledger;
		std::uint16_t options = 31;
		sgre::audio::WmaDecodeLimits limits;
		switch (variant) {
		case 0:
			format.Encoding = sgre::resource::WaveEncoding::WmaProfessional;
			break;
		case 1:
			format.Extra.push_back(0);
			break;
		case 2:
			options = 7;
			break;
		case 3:
			limits.MaxDecodedBytes = 16;
			break;
		case 4:
			bytes.pop_back();
			break;
		case 5:
			bytes[0] |= 0x10;
			break;
		case 6:
			cumulative[0] = 16385;
			break;
		case 7:
			bytes[2] |= 0x0f;
			break;
		case 8:
			bytes[0] &= 0xf0;
			break;
		case 9:
			cumulative[0] = 65536;
			break;
		}

		bool rejected = false;
		try {
			static_cast<void>(sgre::audio::DecodeWmaStandardAudio(format, bytes, cumulative, options, limits));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "Malformed or unsupported WMA whole-stream input was accepted");
	}
}

int main() {
	try {
		TestIndependentImpulse();
		TestRejectedInput();
		std::cout << "WMA decoder tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
