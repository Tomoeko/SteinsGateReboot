#include "sgre/scene/motion_particle_definition.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumParticleTargets = 65536;
	static constexpr std::size_t kMaximumParticleTargetNameBytes = 4096;
	static constexpr std::size_t kMaximumParticleTargetBytes = 16 * 1024 * 1024;

	static const resource::PsbValue* FindParticleProperty(const resource::PsbObject& properties, std::string_view name) {
		for (const auto& [key, value] : properties) {
			if (key == name) {
				return &value;
			}
		}

		return nullptr;
	}

	static const resource::PsbValue& RequireParticleProperty(const resource::PsbObject& properties, std::string_view name) {
		const auto* value = FindParticleProperty(properties, name);
		if (value == nullptr) {
			throw std::runtime_error("motion particle definition requires field " + std::string(name));
		}

		return *value;
	}

	static const resource::PsbValue& RequireParticleField(const resource::PsbValue& object, std::string_view name) {
		const auto* value = object.Find(name);
		if (value == nullptr) {
			throw std::runtime_error("motion particle control requires field " + std::string(name));
		}

		return *value;
	}

	static std::int32_t ReadParticleInteger(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}

		if (const auto* number = std::get_if<double>(&value.Data); number != nullptr && std::isfinite(*number) && *number >= std::numeric_limits<std::int32_t>::min() && *number <= std::numeric_limits<std::int32_t>::max()) {
			return static_cast<std::int32_t>(*number);
		}

		throw std::runtime_error("motion particle integer exceeds its recovered native conversion domain");
	}

	static float ReadParticleNumber(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1.0f : 0.0f;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return static_cast<float>(*integer);
		}

		if (const auto* number = std::get_if<double>(&value.Data); number != nullptr && std::isfinite(*number) && std::abs(*number) <= std::numeric_limits<float>::max()) {
			return static_cast<float>(*number);
		}

		throw std::runtime_error("motion particle number exceeds its recovered finite domain");
	}

	static bool ReadParticleBoolean(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			// Native 4ca782/4ca76f tests the complete integer before SETNE.
			return *integer != 0;
		}

		if (const auto* number = std::get_if<double>(&value.Data); number != nullptr && std::isfinite(*number)) {
			return *number != 0;
		}

		throw std::runtime_error("motion particle flag requires actual finite numeric or boolean metadata");
	}

	static std::string ReadParticleTargetName(const resource::PsbValue& value, std::size_t& remaining_bytes) {
		const auto* text = std::get_if<std::string>(&value.Data);
		if (text == nullptr || text->size() > kMaximumParticleTargetNameBytes || text->size() > remaining_bytes) {
			throw std::runtime_error("motion particle target name exceeds its bounded native string domain");
		}

		remaining_bytes -= text->size();

		return text->substr(0, text->find('\0'));
	}

	static std::vector<MotionParticleTarget> ReadParticleTargets(const resource::PsbValue& value) {
		const auto* records = std::get_if<resource::PsbArray>(&value.Data);
		if (records == nullptr || records->size() > kMaximumParticleTargets) {
			throw std::runtime_error("motion particle motion list requires a bounded native array");
		}

		std::vector<MotionParticleTarget> result;
		result.reserve(records->size());
		std::size_t remaining_bytes = kMaximumParticleTargetBytes;
		for (const auto& record : *records) {
			const auto* pair = std::get_if<resource::PsbArray>(&record.Data);
			if (pair == nullptr || pair->size() < 2) {
				throw std::runtime_error("motion particle target requires its actual character and timeline array entries");
			}

			MotionParticleTarget target;
			target.Character = ReadParticleTargetName((*pair)[0], remaining_bytes);
			target.Timeline = ReadParticleTargetName((*pair)[1], remaining_bytes);
			result.push_back(std::move(target));
		}

		return result;
	}

	static void ReadParticleScreen(MotionParticleDefinition& definition, const resource::PsbValue& document_root) {
		const auto& screen = RequireParticleField(document_root, "screenSize");
		const auto width = ReadParticleNumber(RequireParticleField(screen, "width"));
		const auto height = ReadParticleNumber(RequireParticleField(screen, "height"));
		const auto origin_x = ReadParticleNumber(RequireParticleField(screen, "originX"));
		const auto origin_y = ReadParticleNumber(RequireParticleField(screen, "originY"));
		auto horizontal_origin = -origin_x;
		horizontal_origin -= width * 0.5f;
		auto vertical_origin = -origin_y;
		vertical_origin -= height * 0.5f;
		MotionParticleScreenRectangle rectangle;
		rectangle.Left = horizontal_origin + 0.0f;
		rectangle.Top = vertical_origin + 0.0f;
		rectangle.Right = width + horizontal_origin;
		rectangle.Bottom = height + vertical_origin;
		auto horizontal_margin = -(rectangle.Right - rectangle.Left);
		horizontal_margin *= 0.05f;
		auto vertical_margin = -(rectangle.Bottom - rectangle.Top);
		vertical_margin *= 0.05f;
		MotionParticleScreenRectangle expanded;
		expanded.Left = rectangle.Left + horizontal_margin;
		expanded.Top = rectangle.Top + vertical_margin;
		expanded.Right = rectangle.Right - horizontal_margin;
		expanded.Bottom = rectangle.Bottom - vertical_margin;
		for (const auto number : {rectangle.Left, rectangle.Top, rectangle.Right, rectangle.Bottom, expanded.Left, expanded.Top, expanded.Right, expanded.Bottom}) {
			if (!std::isfinite(number)) {
				throw std::runtime_error("motion particle screen bounds exceed finite native arithmetic");
			}
		}

		definition.Screen = rectangle;
		definition.ExpandedScreen = expanded;
	}

	MotionParticleDefinition ReadMotionParticleDefinition(const resource::MotionLayer& layer, const resource::PsbValue& document_root) {
		if (layer.Type != 4) {
			throw std::runtime_error("motion particle definition requires its actual kind4 layer");
		}

		const auto& properties = layer.Properties;
		MotionParticleDefinition result;
		result.Particle = ReadParticleInteger(RequireParticleProperty(properties, "particle"));
		result.MaximumParticles = ReadParticleInteger(RequireParticleProperty(properties, "particleMaxNum"));
		result.AccelerationRatio = ReadParticleNumber(RequireParticleProperty(properties, "particleAccelRatio"));
		result.InheritAngle = ReadParticleBoolean(RequireParticleProperty(properties, "particleInheritAngle"));
		if (const auto* value = FindParticleProperty(properties, "particleInheritOpacity")) {
			result.InheritOpacity = ReadParticleInteger(*value);
		}

		result.InheritVelocity = ReadParticleInteger(RequireParticleProperty(properties, "particleInheritVelocity"));
		result.FlyDirection = ReadParticleInteger(RequireParticleProperty(properties, "particleFlyDirection"));
		result.ApplyZoomToVelocity = ReadParticleInteger(RequireParticleProperty(properties, "particleApplyZoomToVelocity"));
		result.DeleteOutsideScreen = ReadParticleBoolean(RequireParticleProperty(properties, "particleDeleteOutsideScreen"));
		result.MotionList = ReadParticleTargets(RequireParticleProperty(properties, "particleMotionList"));
		result.TriangularVolume = ReadParticleBoolean(RequireParticleProperty(properties, "particleTriVolume"));
		if (result.DeleteOutsideScreen) {
			ReadParticleScreen(result, document_root);
		}

		return result;
	}

	std::optional<MotionParticleFrameControl> PrepareMotionParticleFrameControl(const resource::MotionLayer& layer, const resource::MotionFrame& frame) {
		if (layer.Type != 4 || frame.Type > 3) {
			throw std::runtime_error("motion particle control requires its actual kind4 layer and bounded frame type");
		}

		if (frame.Type == 0 || (static_cast<std::uint32_t>(ReadParticleInteger(RequireParticleField(frame.Content, "mask"))) & 0x100000u) == 0) {
			return std::nullopt;
		}

		const auto& particle = RequireParticleField(frame.Content, "prt");
		const auto mask = static_cast<std::uint8_t>(ReadParticleInteger(RequireParticleField(particle, "mask")));
		MotionParticleFrameControl control;
		control.FrequencyMin = 10;
		control.FrequencyMax = 10;
		control.ZoomMin = 1;
		control.ZoomMax = 1;
		if ((mask & 1) != 0) {
			control.Trigger = ReadParticleInteger(RequireParticleField(particle, "trigger"));
		}

		if ((mask & 2) != 0) {
			control.FrequencyMin = ReadParticleNumber(RequireParticleField(particle, "fmin"));
			control.FrequencyMax = ReadParticleNumber(RequireParticleField(particle, "fmax"));
		}

		if ((mask & 4) != 0) {
			control.VelocityMin = ReadParticleNumber(RequireParticleField(particle, "vmin"));
			control.VelocityMax = ReadParticleNumber(RequireParticleField(particle, "vmax"));
		}

		if ((mask & 8) != 0) {
			control.AngleMin = ReadParticleNumber(RequireParticleField(particle, "amin"));
			control.AngleMax = ReadParticleNumber(RequireParticleField(particle, "amax"));
		}

		if ((mask & 16) != 0) {
			control.ZoomMin = ReadParticleNumber(RequireParticleField(particle, "zmin"));
			control.ZoomMax = ReadParticleNumber(RequireParticleField(particle, "zmax"));
		}

		if ((mask & 32) != 0) {
			control.Range = ReadParticleNumber(RequireParticleField(particle, "range"));
		}

		return control;
	}

	void ApplyMotionParticleFrameControl(const std::optional<MotionParticleFrameControl>& update, MotionParticleFrameControl& slot) {
		if (update) {
			slot = *update;
		}
	}

} // namespace sgre::scene
