#include "sgre/scene/motion_child_configuration.hpp"

#include <stdexcept>
#include <variant>

namespace sgre::scene {

	static bool ChildIndependentInheritance(const resource::MotionLayer& layer) {
		if (layer.Properties.size() > 65536) {
			throw std::runtime_error("motion child properties exceed their bounded selected metadata");
		}

		const resource::PsbValue* found = nullptr;
		for (const auto& [name, value] : layer.Properties) {
			if (name == "motionIndependentLayerInherit") {
				if (found != nullptr) {
					throw std::runtime_error("motion child independent inheritance has ambiguous duplicate metadata");
				}

				found = &value;
			}
		}

		if (found == nullptr) {
			throw std::runtime_error("motion child constructor requires actual motionIndependentLayerInherit metadata");
		}

		if (const auto* boolean = std::get_if<bool>(&found->Data)) {
			return *boolean;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&found->Data)) {
			return *integer != 0;
		}

		if (const auto* real = std::get_if<double>(&found->Data)) {
			// Actual 4ca6d0 explicitly returns true for unordered float comparisons, including NaN.
			return *real != 0;
		}

		throw std::runtime_error("motion child independent inheritance requires actual boolean or numeric metadata");
	}

	MotionChildRootConfiguration PrepareMotionChildRootConfiguration(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float parent_priority) {
		// Native kind3 stencil5 aggregates real child rows; its root pose and frame controls retain ordinary kind3 semantics.
		if (layer.Type != 3 || prepared.Type != layer.Type || prepared.CoordinatePlane != layer.Coordinate || prepared.CoordinatePlane > 1 || prepared.MeshTransform != 0 || (prepared.StencilType != 0 && prepared.StencilType != 5)) {
			throw std::runtime_error("motion child constructor requires matching kind3 raw/prepared coordinates without mesh and with supported aggregate stencil");
		}

		for (const auto operation : prepared.TransformOrder) {
			if (static_cast<std::uint32_t>(operation) > 3) {
				throw std::runtime_error("motion child root transform order exceeds its recovered operation domain");
			}
		}

		MotionChildRootConfiguration configuration;
		configuration.CoordinatePlane = prepared.CoordinatePlane;
		configuration.TransformOrder = prepared.TransformOrder;
		configuration.IndependentLayerInherit = ChildIndependentInheritance(layer);
		configuration.Priority = parent_priority;

		return configuration;
	}

} // namespace sgre::scene
