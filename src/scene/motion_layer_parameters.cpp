#include "sgre/scene/motion_layer_parameters.hpp"

#include "sgre/scene/motion_player_commands.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumParameterBoundLayers = 65536;
	static constexpr std::size_t kMaximumParameterLayerDepth = 128;

	MotionLayerParameterBindings::MotionLayerParameterBindings(const MotionTimelineParameters& parameters) : m_Parameters(parameters), m_Timeline(parameters.Timeline()), m_Records(parameters.Entries()) {
		if (!std::holds_alternative<std::monostate>(m_Timeline.Parameterization.Data) && (parameters.m_Values.CurrentTimelineParameters(m_Timeline) != &parameters || parameters.TimelineParameter() == nullptr)) {
			throw std::runtime_error("motion layer parameter positions require their exact live containing timeline controller");
		}

		AddHierarchy(m_Timeline.Layers, 0);
	}

	const MotionPlayerCommands& MotionLayerParameterBindings::Commands() const {
		RequireLive();

		return m_Parameters.m_Values.Commands();
	}

	std::uint64_t MotionLayerParameterBindings::ValueSerial() const {
		RequireLive();

		return m_Parameters.ValueSerial();
	}

	void MotionLayerParameterBindings::AddHierarchy(const std::vector<resource::MotionLayer>& layers, std::size_t depth) {
		if (depth > kMaximumParameterLayerDepth) {
			throw std::runtime_error("motion layer parameter hierarchy exceeds its bounded native owner traversal");
		}

		for (const auto& layer : layers) {
			if (m_Bindings.size() == kMaximumParameterBoundLayers) {
				throw std::runtime_error("motion layer parameter hierarchy exceeds its bounded selected layer count");
			}
			const resource::PsbValue* property = nullptr;
			for (const auto& [name, value] : layer.Properties) {
				if (name == "parameterize") {
					property = &value;
					break;
				}
			}
			if (property == nullptr) {
				throw std::runtime_error("motion raw layer is missing its required parameterize owner field");
			}

			Binding binding;
			binding.Layer = &layer;
			if (!std::holds_alternative<std::monostate>(property->Data)) {
				const auto index = ReadMotionParameterRecordIndex(*property);
				if (index < 0 || static_cast<std::size_t>(index) >= m_Records.size()) {
					throw std::runtime_error("motion layer parameter index exceeds this exact selected player's ordered records");
				}
				binding.Parameter = &m_Records[static_cast<std::size_t>(index)];
			}
			m_Bindings.push_back(binding);
			AddHierarchy(layer.Children, depth + 1);
		}
	}

	void MotionLayerParameterBindings::RequireLive() const {
		const auto records = m_Parameters.Entries();
		if (&m_Parameters.Timeline() != &m_Timeline || records.data() != m_Records.data() || records.size() != m_Records.size()) {
			throw std::runtime_error("motion layer parameter borrow differs from its exact live selected record owner");
		}
	}

	const MotionLayerParameterBindings::Binding& MotionLayerParameterBindings::RequireLayer(std::uint32_t layer) const {
		RequireLive();
		if (layer >= m_Bindings.size()) {
			throw std::runtime_error("motion layer parameter index exceeds its actual selected raw hierarchy");
		}

		return m_Bindings[layer];
	}

	std::size_t MotionLayerParameterBindings::Count() const {
		RequireLive();

		return m_Bindings.size();
	}

	const resource::MotionLayer& MotionLayerParameterBindings::Layer(std::uint32_t layer) const {
		return *RequireLayer(layer).Layer;
	}

	const MotionParameterCell* MotionLayerParameterBindings::Parameter(std::uint32_t layer) const {
		return RequireLayer(layer).Parameter;
	}

	float MotionLayerParameterBindings::Position(std::uint32_t layer) const {
		const auto* parameter = RequireLayer(layer).Parameter;

		return parameter == nullptr ? m_Parameters.m_Values.Commands().ClockState().TickCount : parameter->Position;
	}

	std::uint32_t MotionLayerParameterBindings::ControlFlags(std::uint32_t layer) const {
		const auto* parameter = RequireLayer(layer).Parameter;

		return parameter == nullptr ? 0 : parameter->ControlFlags;
	}

} // namespace sgre::scene
