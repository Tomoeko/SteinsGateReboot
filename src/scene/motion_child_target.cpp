#include "sgre/scene/motion_child_target.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumChildTargetNameBytes = 65536;
	static constexpr std::size_t kMaximumChildTargetLayers = 1000000;

	static std::uint32_t ChildTargetWord(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1u : 0u;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			if (*integer >= std::numeric_limits<std::int32_t>::min() && *integer <= std::numeric_limits<std::int32_t>::max()) {
				return static_cast<std::uint32_t>(*integer);
			}
		}

		if (const auto* real = std::get_if<double>(&value.Data)) {
			if (std::isfinite(*real) && *real >= std::numeric_limits<std::int32_t>::min() && *real <= std::numeric_limits<std::int32_t>::max()) {
				return static_cast<std::uint32_t>(static_cast<std::int32_t>(*real));
			}
		}

		throw std::runtime_error("motion child target index is outside its recovered native 32-bit conversion domain");
	}

	std::optional<std::uint32_t> ResolveMotionChildTargetIndex(const resource::MotionTimeline& timeline, std::string_view target, std::size_t selected_layer_count) {
		if (timeline.Kind != resource::MotionTimelineKind::Frames || selected_layer_count > kMaximumChildTargetLayers) {
			throw std::runtime_error("motion child target requires its actual ordinary selected layer span");
		}

		target = target.substr(0, target.find('\0'));
		if (target.size() > kMaximumChildTargetNameBytes) {
			throw std::runtime_error("motion child target name exceeds its native bounded string domain");
		}

		if (std::holds_alternative<std::monostate>(timeline.LayerIndexMap.Data)) {
			return std::nullopt;
		}

		if (!std::holds_alternative<resource::PsbObject>(timeline.LayerIndexMap.Data)) {
			throw std::runtime_error("motion child target requires the actual serialized layerIndexMap object");
		}

		const resource::PsbValue* value = nullptr;
		for (const auto& entry : std::get<resource::PsbObject>(timeline.LayerIndexMap.Data)) {
			if (entry.first == target) {
				if (value != nullptr) {
					throw std::runtime_error("motion child target map has ambiguous duplicate keys outside its recovered domain");
				}

				value = &entry.second;
			}
		}
		if (value == nullptr) {
			return std::nullopt;
		}

		// Native INC EAX wraps before zero-extension and indexing the root-containing node vector.
		const auto root_containing_index = ChildTargetWord(*value) + std::uint32_t{1};
		if (root_containing_index > selected_layer_count) {
			throw std::runtime_error("motion child target index exceeds its actual selected span including root");
		}

		return root_containing_index;
	}

} // namespace sgre::scene
