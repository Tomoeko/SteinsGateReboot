#include "sgre/scene/motion_masked_blend.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::array<std::uint32_t, 5> kMaskedBlendSteps{1, 2, 3, 6, 9};
	static constexpr std::uint64_t kMaximumPreparedPasses = 65536;

	static void RequireFiniteInput(const MotionMaskedBlendInput& input) {
		if (!std::isfinite(input.TargetBounds.MinimumX) || !std::isfinite(input.TargetBounds.MinimumY) || !std::isfinite(input.TargetBounds.MaximumX) || !std::isfinite(input.TargetBounds.MaximumY) || !std::isfinite(input.AxisScale.X) || !std::isfinite(input.AxisScale.Y) || !std::isfinite(input.Amount)) {
			throw std::runtime_error("motion masked blend requires finite prepared input");
		}
	}

	static std::int32_t TruncateRadius(float value) {
		// Original CVTTSS2SI produces INT_MIN for nonfinite or out-of-range intermediate results.
		if (!std::isfinite(value) || value >= 2147483648.0f || value < -2147483648.0f) {
			return std::numeric_limits<std::int32_t>::min();
		}

		return static_cast<std::int32_t>(value);
	}

	static std::uint32_t PositiveRadius(std::int32_t radius) {
		return radius > 0 ? static_cast<std::uint32_t>(radius) : 0;
	}

	static std::uint64_t CountResidualPasses(std::uint32_t radius, bool paired) {
		std::uint64_t count = 0;
		while (radius > 0) {
			for (std::size_t index = kMaskedBlendSteps.size(); index > 0; --index) {
				const auto step = kMaskedBlendSteps[index - 1];
				if (radius < step) {
					continue;
				}

				radius -= step;
				count += paired ? 2 : 1;
				if (paired) {
					break;
				}
			}
		}

		return count;
	}

	static std::uint64_t CountPasses(std::uint32_t radius, bool paired) {
		if (paired) {
			return std::uint64_t{radius / 9} * 2 + CountResidualPasses(radius % 9, true);
		}

		// A complete unequal-axis scan subtracts 9 + 6 + 3 + 2 + 1 before restarting.
		return std::uint64_t{radius / 21} * 5 + CountResidualPasses(radius % 21, false);
	}

	static void WritePass(std::span<MotionMaskedBlendPass> output, std::size_t& count, MotionMaskedBlendAxis axis, std::size_t index) {
		MotionMaskedBlendPass pass;
		pass.Axis = axis;
		pass.Step = kMaskedBlendSteps[index];
		pass.BlendParameter = static_cast<float>(index + (axis == MotionMaskedBlendAxis::Vertical ? 5 : 0));
		output[count] = pass;
		++count;
	}

	static void WriteAxisPasses(std::uint32_t radius, MotionMaskedBlendAxis axis, std::span<MotionMaskedBlendPass> output, std::size_t& count) {
		while (radius > 0) {
			for (std::size_t index = kMaskedBlendSteps.size(); index > 0; --index) {
				const auto step = kMaskedBlendSteps[index - 1];
				if (radius >= step) {
					radius -= step;
					WritePass(output, count, axis, index - 1);
				}
			}
		}
	}

	static void WritePairedPasses(std::uint32_t radius, std::span<MotionMaskedBlendPass> output, std::size_t& count) {
		while (radius > 0) {
			for (std::size_t index = kMaskedBlendSteps.size(); index > 0; --index) {
				const auto step = kMaskedBlendSteps[index - 1];
				if (radius < step) {
					continue;
				}

				radius -= step;
				WritePass(output, count, MotionMaskedBlendAxis::Horizontal, index - 1);
				WritePass(output, count, MotionMaskedBlendAxis::Vertical, index - 1);
				break;
			}
		}
	}

	std::size_t PrepareMotionMaskedBlendPasses(const MotionMaskedBlendInput& input, std::span<MotionMaskedBlendPass> output) {
		RequireFiniteInput(input);
		const float width = input.TargetBounds.MaximumX - input.TargetBounds.MinimumX;
		const float height = input.TargetBounds.MaximumY - input.TargetBounds.MinimumY;
		const bool paired = input.AxisScale.X == input.AxisScale.Y;
		std::uint32_t horizontal_radius = 0;
		std::uint32_t vertical_radius = 0;
		if (paired) {
			const float horizontal_extent = width * input.AxisScale.X;
			const float vertical_extent = height * input.AxisScale.Y;
			const float maximum_extent = vertical_extent > horizontal_extent ? vertical_extent : horizontal_extent;
			horizontal_radius = PositiveRadius(TruncateRadius(maximum_extent * input.Amount + 0.5f));
		} else {
			horizontal_radius = PositiveRadius(TruncateRadius((width * input.Amount) * input.AxisScale.X + 0.5f));
			vertical_radius = PositiveRadius(TruncateRadius((height * input.Amount) * input.AxisScale.Y + 0.5f));
		}

		const auto required = CountPasses(horizontal_radius, paired) + CountPasses(vertical_radius, false);
		if (required > kMaximumPreparedPasses) {
			throw std::runtime_error("motion masked blend pass count exceeds the prepared work bound");
		}

		if (required > output.size()) {
			throw std::runtime_error("motion masked blend output capacity is insufficient");
		}

		std::size_t count = 0;
		if (paired) {
			WritePairedPasses(horizontal_radius, output, count);
		} else {
			WriteAxisPasses(horizontal_radius, MotionMaskedBlendAxis::Horizontal, output, count);
			WriteAxisPasses(vertical_radius, MotionMaskedBlendAxis::Vertical, output, count);
		}

		return count;
	}

} // namespace sgre::scene
