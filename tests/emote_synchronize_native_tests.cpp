#include "sgre/scene/emote_synchronize_control.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"

#include "emote_synchronize_native_vectors.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

class NativeSynchronizeReader {
public:
	explicit NativeSynchronizeReader(std::span<const std::uint32_t> words)
	    : m_Words(words) {
	}
	std::uint32_t Word() {
		Require(m_Position < m_Words.size(), "original Synchronize vector ended before its declared input/output");
		return m_Words[m_Position++];
	}
	float Float() {
		const auto result = std::bit_cast<float>(Word());
		Require(std::isfinite(result), "original Synchronize vector contains a nonfinite scalar");
		return result;
	}
	sgre::resource::MotionPoint Point() {
		const auto x = Float();
		const auto y = Float();
		return {x, y};
	}
	bool Complete() const noexcept {
		return m_Position == m_Words.size();
	}

private:
	std::span<const std::uint32_t> m_Words;
	std::size_t m_Position = 0;
};

static void Compare(float actual, float expected, float& maximum, const std::string& context) {
	const auto error = std::abs(actual - expected) / std::max(1.0f, std::abs(expected));
	if (!std::isfinite(actual) || !std::isfinite(error) || error > 0.000008f) {
		throw std::runtime_error(context + " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual) + " scaled-error=" + std::to_string(error));
	}
	maximum = std::max(maximum, error);
}

static void Replay() {
	NativeSynchronizeReader reader(kEmoteSynchronizeNativeWords);
	Require(reader.Word() == 0x314e5953U, "original Synchronize vector magic changed");
	const auto count = reader.Word();
	Require(count == 96, "original Synchronize constructor corpus count changed");
	float maximum = 0;
	std::size_t compared = 0;
	for (std::uint32_t ordinal = 0; ordinal != count; ++ordinal) {
		const auto degree = reader.Word();
		const auto index = reader.Word();
		Require(degree <= 255, "original Synchronize degree exceeds one encoded byte");
		const auto context = "Synchronize degree=" + std::to_string(degree) + " case=" + std::to_string(index);
		sgre::scene::EmoteSynchronizeDefinition definition;
		definition.PixelsPerMeter = reader.Float();
		definition.Width = reader.Float();
		definition.Height = reader.Float();
		definition.OriginX = reader.Float();
		definition.OriginY = reader.Float();
		const auto source_origin = reader.Point();
		definition.Weight = reader.Float();
		definition.HorizontalWeight = reader.Float();
		definition.VerticalWeight = reader.Float();
		const auto flags = reader.Word();
		Require(flags <= 15, "original Synchronize fixture flags exceed actual four booleans");
		definition.HorizontalFlip = (flags & 1) != 0;
		definition.VerticalFlip = (flags & 2) != 0;
		definition.CorrectScale = (flags & 4) != 0;
		definition.CorrectFix = (flags & 8) != 0;
		const auto fix_count = reader.Word();
		Require(fix_count <= 3, "original Synchronize fixture fix count changed");
		for (std::uint32_t fix = 0; fix != fix_count; ++fix) {
			definition.Fixes.push_back(reader.Point());
		}
		std::array<sgre::resource::MotionPoint, 3> targets{};
		for (std::uint32_t fix = 0; fix != fix_count; ++fix) {
			targets[fix] = reader.Point();
		}
		sgre::scene::EmoteVelvetyPosture posture;
		for (auto& value : posture.Physical.Coordinate) {
			value = reader.Float();
		}
		posture.Physical.Rotation = reader.Float();
		posture.Physical.Cosine = reader.Float();
		posture.Physical.Sine = reader.Float();
		posture.CorrectionAngle = reader.Float();
		posture.CorrectionScale = reader.Float();
		const auto control_count = reader.Word();
		Require(control_count == sgre::scene::MotionBezierLatticeControlCount(static_cast<std::uint8_t>(degree)), "original Synchronize source extent differs from the actual encoded degree");
		sgre::resource::MotionBezierLattice source;
		source.EncodedDegree = static_cast<std::uint8_t>(degree);
		for (std::uint32_t control = 0; control != control_count; ++control) {
			source.Controls.push_back(reader.Point());
		}
		sgre::scene::EmoteSynchronizeMeshOutput output(definition);
		output.SetSourceOrigin(source_origin);
		const auto& actual = output.Prepare(source, posture, std::span(targets).first(fix_count));
		Require(actual.EncodedDegree == source.EncodedDegree && actual.Controls.size() == control_count, "portable Synchronize lost the original source operator/extent");
		for (std::size_t control = 0; control != actual.Controls.size(); ++control) {
			Compare(actual.Controls[control].X, reader.Float(), maximum, context + " control=" + std::to_string(control) + " X");
			Compare(actual.Controls[control].Y, reader.Float(), maximum, context + " control=" + std::to_string(control) + " Y");
			compared += 2;
		}
		const auto pixel = output.PixelOrigin();
		const auto physical = output.PhysicalOrigin();
		Compare(pixel.X, reader.Float(), maximum, context + " pixel-origin X");
		Compare(pixel.Y, reader.Float(), maximum, context + " pixel-origin Y");
		Compare(physical.X, reader.Float(), maximum, context + " physical-origin X");
		Compare(physical.Y, reader.Float(), maximum, context + " physical-origin Y");
		compared += 4;
	}
	Require(reader.Complete(), "original Synchronize vector has unconsumed input/output words");
	std::cout << "Original Synchronize constructor/origin/full3182b0: " << count << " cases/" << compared << " output words PASS maximum=" << maximum << '\n';
}

int main() {
	try {
		Replay();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
