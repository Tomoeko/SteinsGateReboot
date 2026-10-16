#include "sgre/scene/motion_clip_bounds.hpp"

#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaxBoundsWork = 1024 * 1024;
	static constexpr std::size_t kMaxClipNodes = 65536;

	static float NativeMinimum(float left, float right) noexcept {
		// MINSS/MAXSS select the second operand for equality and unordered comparisons.
		return left < right ? left : right;
	}

	static float NativeMaximum(float left, float right) noexcept {
		return left > right ? left : right;
	}

	static void UnionBounds(MotionBounds& result, const MotionBounds& bounds) noexcept {
		result.MinimumX = NativeMinimum(result.MinimumX, bounds.MinimumX);
		result.MinimumY = NativeMinimum(result.MinimumY, bounds.MinimumY);
		result.MaximumX = NativeMaximum(result.MaximumX, bounds.MaximumX);
		result.MaximumY = NativeMaximum(result.MaximumY, bounds.MaximumY);
	}

	static void IntersectBounds(MotionBounds& result, const MotionBounds& bounds) noexcept {
		result.MinimumX = NativeMaximum(result.MinimumX, bounds.MinimumX);
		result.MinimumY = NativeMaximum(result.MinimumY, bounds.MinimumY);
		result.MaximumX = NativeMinimum(result.MaximumX, bounds.MaximumX);
		result.MaximumY = NativeMinimum(result.MaximumY, bounds.MaximumY);
	}

	static MotionBounds GeometryBounds(MotionClipGeometry& geometry, std::size_t& remaining) {
		if (!geometry.Calculated) {
			if (geometry.Positions.size() > remaining) {
				throw std::runtime_error("motion clip bounds exceed the position work limit");
			}

			remaining -= geometry.Positions.size();
			geometry.Calculated = true;
			for (const auto& point : geometry.Positions) {
				geometry.CachedBounds.MinimumX = NativeMinimum(geometry.CachedBounds.MinimumX, point.X);
				geometry.CachedBounds.MaximumY = NativeMaximum(geometry.CachedBounds.MaximumY, point.Y);
				geometry.CachedBounds.MaximumX = NativeMaximum(geometry.CachedBounds.MaximumX, point.X);
				geometry.CachedBounds.MinimumY = NativeMinimum(geometry.CachedBounds.MinimumY, point.Y);
			}
		}

		return geometry.CachedBounds;
	}

	static MotionBounds CommandBounds(const MotionClipCommand& command, std::size_t& remaining) {
		if (!command.Geometry) {
			throw std::runtime_error("visible motion clip command requires a live geometry cache");
		}

		return GeometryBounds(*command.Geometry, remaining);
	}

	MotionBounds MeasureMotionGeometryBounds(MotionClipGeometry& geometry) {
		auto remaining = kMaxBoundsWork;

		return GeometryBounds(geometry, remaining);
	}

	MotionClipBoundsResult MeasureMotionClipRun(std::span<const MotionClipCommand> commands, std::size_t start, std::uint64_t clip_group) {
		if (commands.size() > kMaxBoundsWork || start > commands.size()) {
			throw std::runtime_error("motion clip run exceeds its command range");
		}

		MotionClipBoundsResult result;
		auto remaining = kMaxBoundsWork;
		for (std::size_t index = start; index < commands.size(); ++index) {
			const auto& command = commands[index];
			if (command.Hidden || command.GeometryCount == 0) {
				continue;
			}

			if (command.ClipGroup != clip_group) {
				break;
			}

			UnionBounds(result.Bounds, CommandBounds(command, remaining));
			result.HasBounds = true;
		}

		return result;
	}

	static void ValidateChain(std::span<const MotionClipNode> nodes, std::optional<std::uint32_t> first) {
		if (nodes.size() > kMaxClipNodes) {
			throw std::runtime_error("motion clip chain exceeds its node limit");
		}

		std::size_t visited = 0;
		for (auto index = first; index; index = nodes[*index].Parent) {
			if (*index >= nodes.size() || visited >= nodes.size()) {
				throw std::runtime_error("motion clip chain has an invalid parent or cycle");
			}

			if (nodes[*index].Members.size() > kMaxBoundsWork) {
				throw std::runtime_error("motion clip member count exceeds its work limit");
			}

			++visited;
		}
	}

	MotionClipBoundsResult MeasureMotionClipChain(std::span<const MotionClipNode> nodes, std::optional<std::uint32_t> first) {
		ValidateChain(nodes, first);
		MotionClipBoundsResult result;
		auto remaining = kMaxBoundsWork;
		std::size_t member_visits = 0;
		for (auto index = first; index; index = nodes[*index].Parent) {
			const auto& node = nodes[*index];
			if ((node.ModeFlags & 3) == 2) {
				continue;
			}

			MotionBounds bounds;
			if ((node.ModeFlags & 4) != 0) {
				if (node.Members.size() > kMaxBoundsWork - member_visits) {
					throw std::runtime_error("motion clip chain exceeds its member work limit");
				}

				member_visits += node.Members.size();
				for (const auto& member : node.Members) {
					if (!member.Hidden) {
						UnionBounds(bounds, CommandBounds(member, remaining));
					}
				}

				// Native COMISS/JB rejects unordered union bounds as well as reversed ranges.
				if (!(bounds.MaximumX >= bounds.MinimumX) || !(bounds.MaximumY >= bounds.MinimumY)) {
					continue;
				}
			} else {
				if (node.Command.Hidden) {
					continue;
				}

				bounds = CommandBounds(node.Command, remaining);
			}

			if (result.HasBounds) {
				IntersectBounds(result.Bounds, bounds);
			} else {
				result.Bounds = bounds;
				result.HasBounds = true;
			}
		}

		return result;
	}

} // namespace sgre::scene
