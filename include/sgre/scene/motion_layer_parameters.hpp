#pragma once

#include "sgre/resource/motion.hpp"
#include "sgre/scene/motion_parameter_values.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {

	class MotionLayerParameterBindings {
	public:
		// The actual selected parameter owner outlives these raw-layer and stable record borrows.
		explicit MotionLayerParameterBindings(const MotionTimelineParameters& parameters);
		[[nodiscard]] std::size_t Count() const;
		[[nodiscard]] const MotionPlayerCommands& Commands() const;
		[[nodiscard]] std::uint64_t ValueSerial() const;
		[[nodiscard]] const resource::MotionLayer& Layer(std::uint32_t layer) const;
		[[nodiscard]] const MotionParameterCell* Parameter(std::uint32_t layer) const;
		// Ordinary layers read the actual player's effective clock; bound layers read their exact current record.
		[[nodiscard]] float Position(std::uint32_t layer) const;
		[[nodiscard]] std::uint32_t ControlFlags(std::uint32_t layer) const;

	private:
		struct Binding {
			const resource::MotionLayer* Layer = nullptr;
			const MotionParameterCell* Parameter = nullptr;
		};
		void AddHierarchy(const std::vector<resource::MotionLayer>& layers, std::size_t depth);
		void RequireLive() const;
		[[nodiscard]] const Binding& RequireLayer(std::uint32_t layer) const;
		const MotionTimelineParameters& m_Parameters;
		const resource::MotionTimeline& m_Timeline;
		std::span<const MotionParameterCell> m_Records;
		std::vector<Binding> m_Bindings;
	};

} // namespace sgre::scene
