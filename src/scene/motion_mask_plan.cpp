#include "sgre/scene/motion_mask_plan.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaximumMaskNodes = 65536;
	static constexpr std::size_t kMaximumMaskWork = 1048576;

	struct MotionMaskPlanSize {
		std::size_t Operations = 2;
		std::uint32_t InitialReference = 0;
	};

	static void AddMaskWork(std::size_t& work, std::size_t count) {
		if (count > kMaximumMaskWork - work) {
			throw std::runtime_error("motion mask plan exceeds its work bound");
		}

		work += count;
	}

	static MotionMaskPlanSize MeasureMaskPlan(std::span<const MotionClipNode> nodes, std::span<const MotionMaskSettings> settings, std::uint32_t first) {
		MotionMaskPlanSize result;
		std::size_t work = 0;
		std::size_t visited = 0;
		std::optional<std::uint32_t> current = first;
		while (current) {
			if (*current >= nodes.size()) {
				throw std::runtime_error("motion mask plan parent index exceeds node storage");
			}

			if (++visited > nodes.size()) {
				throw std::runtime_error("motion mask plan contains a parent cycle");
			}

			AddMaskWork(work, 3);
			const auto& node = nodes[*current];
			const auto mode = node.ModeFlags & 3u;
			if (mode == 1 || mode == 2) {
				const auto& parameters = settings[*current];
				const auto maximum = mode == 2 ? (1.0f - parameters.ThresholdMinimum) - parameters.ThresholdExtent : parameters.ThresholdExtent;
				if (!std::isfinite(parameters.ThresholdMinimum) || !std::isfinite(parameters.ThresholdExtent) || !std::isfinite(maximum)) {
					throw std::runtime_error("motion mask plan requires finite threshold parameters");
				}

				result.Operations += 2;
				if (mode == 2) {
					result.InitialReference = 1;
				}

				if ((node.ModeFlags & 4u) != 0) {
					AddMaskWork(work, node.Members.size());
					for (const auto& member : node.Members) {
						if (!member.Hidden) {
							++result.Operations;
						}
					}
				} else if (!node.Command.Hidden) {
					++result.Operations;
				}
			}

			current = node.Parent;
		}

		if (result.Operations > kMaximumMaskWork) {
			throw std::runtime_error("motion mask plan exceeds its operation bound");
		}

		return result;
	}

	static void AppendMaskOperation(std::span<MotionMaskOperation> output, std::size_t& count, MotionMaskOperationKind kind, std::uint32_t reference = 0, const MotionClipCommand* command = nullptr) {
		MotionMaskOperation operation;
		operation.Kind = kind;
		operation.Reference = reference;
		operation.Command = command;
		output[count++] = operation;
	}

	std::size_t PrepareMotionMaskPlan(std::span<const MotionClipNode> nodes, std::span<const MotionMaskSettings> settings, std::optional<std::uint32_t> first, std::span<MotionMaskOperation> output) {
		if (!first) {
			return 0;
		}

		if (nodes.size() > kMaximumMaskNodes || settings.size() != nodes.size()) {
			throw std::runtime_error("motion mask plan requires bounded matching node/settings storage");
		}

		const auto measured = MeasureMaskPlan(nodes, settings, *first);
		if (output.size() < measured.Operations) {
			throw std::runtime_error("motion mask plan exceeds prepared output capacity");
		}

		std::size_t count = 0;
		auto reference = measured.InitialReference;
		AppendMaskOperation(output, count, MotionMaskOperationKind::Prepare, reference);
		for (const auto mode : std::array<std::uint8_t, 2>{1, 2}) {
			std::optional<std::uint32_t> current = first;
			while (current) {
				const auto& node = nodes[*current];
				if ((node.ModeFlags & 3u) == mode) {
					const auto& parameters = settings[*current];
					MotionMaskOperation configuration;
					configuration.Kind = MotionMaskOperationKind::Configure;
					configuration.Operation = parameters.Operation;
					configuration.ThresholdMinimum = parameters.ThresholdMinimum;
					configuration.ThresholdMaximum = mode == 2 ? (1.0f - parameters.ThresholdMinimum) - parameters.ThresholdExtent : parameters.ThresholdExtent;
					output[count++] = configuration;
					AppendMaskOperation(output, count, mode == 1 ? MotionMaskOperationKind::Begin : MotionMaskOperationKind::End, reference);
					if (mode == 1) {
						++reference;
					}

					if ((node.ModeFlags & 4u) != 0) {
						for (const auto& member : node.Members) {
							if (!member.Hidden) {
								AppendMaskOperation(output, count, MotionMaskOperationKind::VisitCommand, 0, &member);
							}
						}
					} else if (!node.Command.Hidden) {
						AppendMaskOperation(output, count, MotionMaskOperationKind::VisitCommand, 0, &node.Command);
					}
				}

				current = node.Parent;
			}
		}

		AppendMaskOperation(output, count, MotionMaskOperationKind::Resolve, reference);

		return count;
	}

} // namespace sgre::scene
