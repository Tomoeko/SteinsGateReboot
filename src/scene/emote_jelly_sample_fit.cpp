#include "sgre/scene/emote_jelly_sample_fit.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::uint32_t kMaximumGridDimension = 256;
	static constexpr std::size_t kMaximumSamples = 65536;

	static std::size_t RequireGridSize(std::uint32_t width, std::uint32_t height) {
		if (width < 2 || height < 2 || width > kMaximumGridDimension || height > kMaximumGridDimension) {
			throw std::runtime_error("Emote Jelly fitting requires bounded nondegenerate grid dimensions");
		}

		return static_cast<std::size_t>(width) * height;
	}

	static bool FinitePoint(const resource::MotionPoint& point) {
		return std::isfinite(point.X) && std::isfinite(point.Y);
	}

	struct SampleAxis {
		std::uint32_t Cell = 0;
		float Fraction = 0;
	};

	static SampleAxis LocateSample(float value, std::uint32_t count) {
		if (!std::isfinite(value) || value < 0) {
			throw std::runtime_error("Emote Jelly sample coordinate requires a finite nonnegative native registration");
		}

		if (value >= 1) {
			return {count - 2, 1};
		}

		const float position = static_cast<float>(count - 1) * value;
		const auto cell = static_cast<std::uint32_t>(position);

		return {cell, position - static_cast<float>(cell)};
	}

	EmoteJellySampleFit::EmoteJellySampleFit(std::uint32_t width, std::uint32_t height, std::span<const EmoteJellySampleBinding> samples)
	    : m_GridCount(RequireGridSize(width, height)) {
		if (samples.size() > kMaximumSamples) {
			throw std::runtime_error("Emote Jelly fitting exceeds its bounded actual sample count");
		}

		m_Bindings.reserve(samples.size());
		for (std::size_t index = 0; index < samples.size(); ++index) {
			const auto& sample = samples[index];
			if (!std::isfinite(sample.Order)) {
				throw std::runtime_error("Emote Jelly fitting requires its finite native float sort key");
			}

			const auto horizontal = LocateSample(sample.Coordinate.X, width);
			const auto vertical = LocateSample(sample.Coordinate.Y, height);
			const float left = 1.0f - horizontal.Fraction;
			const float top = 1.0f - vertical.Fraction;
			const auto first = static_cast<std::size_t>(vertical.Cell) * width + horizontal.Cell;
			Binding binding;
			binding.Source = index;
			binding.Priority = sample.Priority;
			binding.Order = sample.Order;
			binding.Coefficients[0] = {first, top * left};
			binding.Coefficients[1] = {first + 1, top * horizontal.Fraction};
			binding.Coefficients[2] = {first + width, left * vertical.Fraction};
			binding.Coefficients[3] = {first + width + 1, vertical.Fraction * horizontal.Fraction};
			m_Bindings.push_back(binding);
		}

		std::stable_sort(m_Bindings.begin(), m_Bindings.end(), [](const Binding& left, const Binding& right) {
			return left.Priority < right.Priority || (left.Priority == right.Priority && left.Order < right.Order);
		});
	}

	void EmoteJellySampleFit::Apply(std::span<resource::MotionPoint> grid, std::span<const resource::MotionPoint> targets) const {
		ApplyImpl(grid, targets, false);
	}

	void EmoteJellySampleFit::ApplyForUnorderedComparison(std::span<resource::MotionPoint> grid, std::span<const resource::MotionPoint> targets) const {
		ApplyImpl(grid, targets, true);
	}

	void EmoteJellySampleFit::ApplyImpl(std::span<resource::MotionPoint> grid, std::span<const resource::MotionPoint> targets, bool unordered_comparison) const {
		if (grid.size() != m_GridCount || targets.size() != m_Bindings.size() || (!unordered_comparison && (!std::all_of(grid.begin(), grid.end(), FinitePoint) || !std::all_of(targets.begin(), targets.end(), FinitePoint)))) {
			throw std::runtime_error("Emote Jelly fitting requires the registered finite grid and actual target cardinality");
		}

		for (const auto& binding : m_Bindings) {
			resource::MotionPoint sampled;
			for (const auto& coefficient : binding.Coefficients) {
				const auto& point = grid[coefficient.Index];
				sampled.X = sampled.X + coefficient.Weight * point.X;
				sampled.Y = sampled.Y + coefficient.Weight * point.Y;
			}

			const auto& target = targets[binding.Source];
			const float delta_x = target.X - sampled.X;
			const float delta_y = target.Y - sampled.Y;
			for (const auto& coefficient : binding.Coefficients) {
				auto& point = grid[coefficient.Index];
				point.X = coefficient.Weight * delta_x + point.X;
				point.Y = coefficient.Weight * delta_y + point.Y;
				if (!unordered_comparison && !FinitePoint(point)) {
					throw std::runtime_error("Emote Jelly residual scatter exceeded finite output");
				}
			}
		}
	}

	void InitializeEmoteJellySampleGrid(std::span<resource::MotionPoint> grid, std::uint32_t width, std::uint32_t height) {
		if (grid.size() != RequireGridSize(width, height)) {
			throw std::runtime_error("Emote Jelly identity grid requires exact registered dimensions");
		}

		for (std::uint32_t row = 0; row < height; ++row) {
			const float vertical = static_cast<float>(row) / static_cast<float>(height - 1);
			for (std::uint32_t column = 0; column < width; ++column) {
				auto& point = grid[static_cast<std::size_t>(row) * width + column];
				point.X = static_cast<float>(column) / static_cast<float>(width - 1);
				point.Y = vertical;
			}
		}
	}

} // namespace sgre::scene
