#include "sgre/scene/emote_jelly_definition.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumJellyDefinitions = 4096;
	static constexpr std::size_t kMaximumJellyOwnedNames = 8 * 1024 * 1024;
	static constexpr std::size_t kMaximumJellyNestedEntries = 65536;

	static const resource::PsbValue& JellyField(const resource::PsbValue& object, std::string_view name) {
		if (const auto* value = object.Find(name)) {
			return *value;
		}
		throw std::runtime_error("Emote Jelly metadata requires actual field " + std::string(name));
	}

	static const resource::PsbArray& JellyArray(const resource::PsbValue& value) {
		if (const auto* array = std::get_if<resource::PsbArray>(&value.Data)) {
			return *array;
		}
		throw std::runtime_error("Emote Jelly metadata requires an actual array");
	}

	static float JellyNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1.0f : 0.0f;
		} else {
			throw std::runtime_error("Emote Jelly scalar requires native numeric metadata");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote Jelly metadata scalar exceeds its finite supported domain");
		}
		return result;
	}

	static float JellyNumber(const resource::PsbValue& object, std::string_view name, float fallback) {
		const auto* value = object.Find(name);
		return value == nullptr ? fallback : JellyNumber(*value);
	}

	static std::int32_t JellyInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote Jelly integer exceeds its native finite conversion domain");
	}

	static std::int32_t JellyInteger(const resource::PsbValue& object, std::string_view name, std::int32_t fallback) {
		const auto* value = object.Find(name);
		return value == nullptr ? fallback : JellyInteger(*value);
	}

	static bool JellyFlag(const resource::PsbValue& object, std::string_view name, bool fallback) {
		const auto* value = object.Find(name);
		if (value == nullptr) {
			return fallback;
		}
		if (const auto* real = std::get_if<double>(&value->Data)) {
			if (!std::isfinite(*real)) {
				throw std::runtime_error("Emote Jelly flag requires finite native metadata");
			}
			return *real != 0;
		}
		return static_cast<std::uint8_t>(JellyInteger(*value)) != 0;
	}

	struct JellyMetadataBudget {
		std::size_t Entries = 0;
		std::size_t Names = 0;

		const resource::PsbArray& Array(const resource::PsbValue& value) {
			const auto& array = JellyArray(value);
			if (array.size() > kMaximumJellyNestedEntries - Entries) {
				throw std::runtime_error("Emote Jelly definitions exceed bounded nested owner storage");
			}
			Entries += array.size();
			return array;
		}

		std::string Name(const resource::PsbValue& value) {
			const auto* text = std::get_if<std::string>(&value.Data);
			if (text == nullptr) {
				throw std::runtime_error("Emote Jelly UID/name requires actual string metadata");
			}
			const auto name = std::string_view(*text).substr(0, text->find('\0'));
			if (name.size() >= kMaximumJellyOwnedNames - Names) {
				throw std::runtime_error("Emote Jelly definitions exceed bounded owned name storage");
			}
			Names += name.size() + 1;
			return std::string(name);
		}
	};

	static std::array<float, 3> JellyVector3(const resource::PsbValue& value) {
		return {JellyNumber(JellyField(value, "x")), JellyNumber(JellyField(value, "y")), JellyNumber(JellyField(value, "z"))};
	}

	static EmoteJellyControlDefinition ReadJellyControl(const resource::PsbValue& control, JellyMetadataBudget& budget) {
		EmoteJellyControlDefinition result;
		auto& physics = result.Physics;
		physics.Uid = budget.Name(JellyField(control, "uid"));
		result.PixelsPerMeter = JellyNumber(JellyField(control, "ppm"));
		if (!(result.PixelsPerMeter > 0)) {
			throw std::runtime_error("Emote Jelly UID conversion requires actual positive pixels per meter");
		}
		const auto substeps = JellyInteger(control, "subStep", 1);
		const auto particles = JellyInteger(JellyField(control, "num"));
		const auto prediction = JellyInteger(control, "coordPred", 0);
		if (substeps < 1 || substeps > 4096 || particles < 2 || particles > 256 || prediction < 0 || prediction > 3) {
			throw std::runtime_error("Emote Jelly counts/prediction exceed the recovered physical-owner domain");
		}
		physics.Substeps = static_cast<std::uint32_t>(substeps);
		physics.ParticleCount = static_cast<std::uint32_t>(particles);
		result.PredictionDegree = static_cast<std::uint32_t>(prediction);
		physics.Gravity = JellyNumber(control, "gravity", 9.8f);
		physics.Speed = JellyNumber(control, "speed", 1.0f);
		physics.AutoMass = JellyFlag(control, "autoMass", true);
		// Native reads mass whenever autoMass exists, even when its actual value is true.
		physics.Mass = control.Find("autoMass") == nullptr ? 0.0f : JellyNumber(JellyField(control, "mass"));
		physics.Density = JellyNumber(JellyField(control, "density"));
		physics.SizeHorizontal = JellyNumber(JellyField(control, "sizeH"));
		physics.SizeVertical = JellyNumber(JellyField(control, "sizeV"));
		physics.DefaultRotation = JellyFlag(control, "defaultRotAsGravity", false) ? JellyNumber(JellyField(control, "defaultRot")) : 0.0f;
		physics.TiltFactor = JellyNumber(control, "tiltFactor", 0.0f);
		physics.StretchFactor = JellyNumber(control, "stretchFactor", 0.0f);
		physics.HeightRate = JellyNumber(JellyField(control, "heightRate"));
		physics.Friction = JellyNumber(JellyField(control, "friction"));
		physics.ElasticHorizontal = JellyNumber(JellyField(control, "elasticH"));
		// The original reader requests elasticH twice. elasticV is not an alternate vertical producer.
		physics.ElasticVertical = physics.ElasticHorizontal;
		physics.CoordinateScale = {JellyNumber(JellyField(control, "coordScaleH")), JellyNumber(JellyField(control, "coordScaleV")), JellyNumber(JellyField(control, "coordScaleD"))};
		physics.Sensitivity = JellyNumber(control, "sensitivity", 0.0f);
		physics.CollisionRepulsion = JellyNumber(control, "collisionRepulsion", 100.0f);
		physics.AdhocSimulation = JellyFlag(control, "adhocSim", true);
		physics.SampleDensity = JellyNumber(control, "sampleDensity", 2.0f);
		physics.LimitLengthRatio = JellyNumber(control, "limitLengthRatio", 2.0f);
		const auto sample_order = JellyInteger(control, "sampleOrder", 1);
		physics.ReverseSampleOrder = sample_order != 0;
		result.InheritShape = JellyFlag(control, "inheritShape", true);
		physics.CorrectFix = !result.InheritShape && JellyFlag(control, "correctFix", false);
		physics.CorrectScale = !result.InheritShape && JellyFlag(control, "correctScale", false);
		result.ReferenceParentMeshPosture = JellyFlag(control, "refParentMeshPosture", false);
		physics.TipTilt = JellyVector3(JellyField(control, "tipTilt"));
		const auto& rigid = JellyField(control, "rigidRange");
		physics.RigidRange = {JellyNumber(JellyField(rigid, "l")), JellyNumber(JellyField(rigid, "t")), JellyNumber(JellyField(rigid, "r")), JellyNumber(JellyField(rigid, "b"))};
		const auto& fixes = budget.Array(JellyField(control, "fixes"));
		if (fixes.size() > 3) {
			throw std::runtime_error("Emote Jelly fixes exceed the recovered three-fix output owner");
		}
		for (const auto& fix : fixes) {
			physics.Fixes.push_back({JellyNumber(fix, "x", 0.0f), JellyNumber(fix, "y", 0.0f)});
		}
		const auto& tresses = budget.Array(JellyField(control, "tresses"));
		if (tresses.size() > 4096 || physics.ParticleCount > 65536 / (tresses.empty() ? 1 : tresses.size())) {
			throw std::runtime_error("Emote Jelly tresses exceed bounded real physical initialization");
		}
		for (const auto& value : tresses) {
			EmoteJellyTressDefinition tress;
			tress.Coordinate = {JellyNumber(JellyField(value, "x")), JellyNumber(JellyField(value, "y"))};
			tress.Radius = JellyNumber(JellyField(value, "r"));
			tress.Tilt = JellyVector3(JellyField(value, "jellyTilt"));
			physics.Tresses.push_back(tress);
		}
		for (const auto& speed : budget.Array(JellyField(control, "speedVariables"))) {
			result.SpeedVariables.push_back(budget.Name(speed));
		}
		for (const auto& collision : budget.Array(JellyField(control, "collisionRefs"))) {
			EmoteJellyCollisionReference reference;
			reference.Uid = budget.Name(JellyField(collision, "uid"));
			reference.IgnoreMask = static_cast<std::uint32_t>(JellyInteger(JellyField(collision, "ignoreMask")));
			result.CollisionReferences.push_back(std::move(reference));
		}
		result.RequiresEmissions = !budget.Array(JellyField(control, "emissionRefs")).empty();
		return result;
	}

	std::vector<EmoteJellyControlDefinition> ReadEmoteJellyControlDefinitions(const resource::PsbDocument& document) {
		const auto& metadata = JellyField(document.Root, "metadata");
		if (metadata.Find("velvetyCommonParams") == nullptr) {
			return {};
		}
		JellyMetadataBudget budget;
		const auto& controls = budget.Array(JellyField(metadata, "velvetyJellyControl"));
		if (controls.size() > kMaximumJellyDefinitions) {
			throw std::runtime_error("Emote Jelly controls exceed bounded actual owner initialization");
		}
		std::vector<EmoteJellyControlDefinition> definitions;
		definitions.reserve(controls.size());
		for (const auto& control : controls) {
			definitions.push_back(ReadJellyControl(control, budget));
		}
		return definitions;
	}

} // namespace sgre::scene
