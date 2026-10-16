#include "sgre/scene/emote_bloom_definition.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumBloomDefinitions = 4096;
	static constexpr std::size_t kMaximumBloomOwnedNames = 8 * 1024 * 1024;
	static constexpr std::size_t kMaximumBloomNestedEntries = 65536;

	static const resource::PsbValue& BloomField(const resource::PsbValue& object, std::string_view name) {
		if (const auto* value = object.Find(name)) {
			return *value;
		}
		throw std::runtime_error("Emote Bloom metadata requires actual field " + std::string(name));
	}

	static const resource::PsbArray& BloomArray(const resource::PsbValue& value) {
		if (const auto* array = std::get_if<resource::PsbArray>(&value.Data)) {
			return *array;
		}
		throw std::runtime_error("Emote Bloom metadata requires an actual array");
	}

	static float BloomNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1.0f : 0.0f;
		} else {
			throw std::runtime_error("Emote Bloom scalar requires native numeric metadata");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote Bloom metadata scalar exceeds its finite supported domain");
		}
		return result;
	}

	static float BloomNumber(const resource::PsbValue& object, std::string_view name, float fallback) {
		const auto* value = object.Find(name);
		return value == nullptr ? fallback : BloomNumber(*value);
	}

	static std::int32_t BloomInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote Bloom integer exceeds its native finite conversion domain");
	}

	static std::int32_t BloomInteger(const resource::PsbValue& object, std::string_view name, std::int32_t fallback) {
		const auto* value = object.Find(name);
		return value == nullptr ? fallback : BloomInteger(*value);
	}

	static bool BloomFlag(const resource::PsbValue& object, std::string_view name, bool fallback) {
		const auto* value = object.Find(name);
		if (value == nullptr) {
			return fallback;
		}
		if (const auto* real = std::get_if<double>(&value->Data)) {
			if (!std::isfinite(*real)) {
				throw std::runtime_error("Emote Bloom flag requires finite native metadata");
			}
			return *real != 0;
		}
		return static_cast<std::uint8_t>(BloomInteger(*value)) != 0;
	}

	struct BloomMetadataBudget {
		std::size_t Entries = 0;
		std::size_t Names = 0;

		const resource::PsbArray& Array(const resource::PsbValue& value) {
			const auto& array = BloomArray(value);
			if (array.size() > kMaximumBloomNestedEntries - Entries) {
				throw std::runtime_error("Emote Bloom definitions exceed bounded nested owner storage");
			}
			Entries += array.size();
			return array;
		}

		std::string Name(const resource::PsbValue& value) {
			const auto* text = std::get_if<std::string>(&value.Data);
			if (text == nullptr) {
				throw std::runtime_error("Emote Bloom UID/name requires actual string metadata");
			}
			const auto name = std::string_view(*text).substr(0, text->find('\0'));
			if (name.size() >= kMaximumBloomOwnedNames - Names) {
				throw std::runtime_error("Emote Bloom definitions exceed bounded owned name storage");
			}
			Names += name.size() + 1;
			return std::string(name);
		}
	};

	static EmoteBloomControlDefinition ReadBloomControl(const resource::PsbValue& control, BloomMetadataBudget& budget) {
		EmoteBloomControlDefinition result;
		auto& physics = result.Physics;
		result.Uid = budget.Name(BloomField(control, "uid"));
		result.PixelsPerMeter = BloomNumber(BloomField(control, "ppm"));
		if (!(result.PixelsPerMeter > 0)) {
			throw std::runtime_error("Emote Bloom UID conversion requires actual positive pixels per meter");
		}
		const auto substeps = BloomInteger(control, "subStep", 1);
		const auto prediction = BloomInteger(control, "coordPred", 0);
		if (substeps < 1 || substeps > 4096 || prediction < 0 || prediction > 3) {
			throw std::runtime_error("Emote Bloom counts/prediction exceed the recovered physical-owner domain");
		}
		physics.Substeps = static_cast<std::uint32_t>(substeps);
		result.PredictionDegree = static_cast<std::uint32_t>(prediction);
		physics.DefaultRotation = BloomNumber(control, "defaultRot", 0.0f);
		physics.Speed = BloomNumber(control, "speed", 1.0f);
		if (BloomFlag(control, "autoMass", false)) {
			const float mass_per_area = BloomNumber(control, "massPerSquareMeter", 0.0f);
			const float area = BloomNumber(control, "imageArea", 0.0f);
			physics.Mass = (area * mass_per_area) / (result.PixelsPerMeter * result.PixelsPerMeter);
		} else {
			physics.Mass = BloomNumber(control, "mass", 0.0f);
		}
		physics.LimitLengthRatio = BloomNumber(control, "limitLengthRatio", 0.0f);
		physics.SizeHorizontal = BloomNumber(control, "sizeH", 0.0f);
		physics.SizeVertical = BloomNumber(control, "sizeV", 0.0f);
		physics.Origin = {BloomNumber(control, "originX", 0.0f), BloomNumber(control, "originY", 0.0f)};
		physics.Center = {BloomNumber(control, "centerX", 0.0f), BloomNumber(control, "centerY", 0.0f)};
		physics.Elastic = BloomNumber(control, "elastic", 0.0f);
		physics.Friction = BloomNumber(control, "friction", 0.0f);
		physics.RotateWeight = BloomNumber(control, "rotateWeight", 0.0f);
		physics.TwistWeight = BloomNumber(control, "twistWeight", 0.0f);
		physics.TrapeziumWeight = BloomNumber(control, "trapeziumWeight", 0.0f);
		physics.PitchWeight = BloomNumber(control, "pitchWeight", 0.0f);
		physics.ControlPointSize = BloomNumber(control, "controlPointSize", 0.0f);
		physics.CoordinateScale = {BloomNumber(control, "coordScaleH", 0.0f), BloomNumber(control, "coordScaleV", 0.0f), BloomNumber(control, "coordScaleD", 0.0f)};
		physics.CollisionRepulsion = BloomNumber(control, "collisionRepulsion", 0.0f);
		physics.CollisionHardness = static_cast<std::uint32_t>(BloomInteger(control, "collisionHardness", 0));
		physics.Straightness = BloomNumber(control, "straightness", 0.0f);
		result.InheritShape = BloomFlag(control, "inheritShape", true);
		physics.CorrectScale = !result.InheritShape && BloomFlag(control, "correctScale", false);
		physics.CorrectFix = !result.InheritShape && BloomFlag(control, "correctFix", false);
		result.ReferenceParentMeshPosture = BloomFlag(control, "refParentMeshPosture", false);
		for (const auto& fix : budget.Array(BloomField(control, "fixes"))) {
			if (physics.Fixes.size() == 3) {
				throw std::runtime_error("Emote Bloom fixes exceed the recovered three-fix output owner");
			}
			physics.Fixes.push_back({BloomNumber(fix, "x", 0.0f), BloomNumber(fix, "y", 0.0f)});
		}
		for (const auto& tress : budget.Array(BloomField(control, "tresses"))) {
			if (physics.Tresses.size() == 32) {
				throw std::runtime_error("Emote Bloom tresses exceed bounded native influence-owner construction");
			}
			physics.Tresses.push_back({BloomNumber(tress, "x", 0.0f), BloomNumber(tress, "y", 0.0f)});
		}
		for (const auto& speed : budget.Array(BloomField(control, "speedVariables"))) {
			result.SpeedVariables.push_back(budget.Name(speed));
		}
		for (const auto& collision : budget.Array(BloomField(control, "collisionRefs"))) {
			EmoteBloomCollisionReference reference;
			reference.Uid = budget.Name(BloomField(collision, "uid"));
			reference.IgnoreMask = static_cast<std::uint32_t>(BloomInteger(BloomField(collision, "ignoreMask")));
			result.CollisionReferences.push_back(std::move(reference));
		}
		result.RequiresEmissions = !budget.Array(BloomField(control, "emissionRefs")).empty();
		if (!std::isfinite(physics.Mass)) {
			throw std::runtime_error("Emote Bloom automatic mass exceeds the finite supported owner domain");
		}
		return result;
	}

	std::vector<EmoteBloomControlDefinition> ReadEmoteBloomControlDefinitions(const resource::PsbDocument& document) {
		const auto& metadata = BloomField(document.Root, "metadata");
		if (metadata.Find("velvetyCommonParams") == nullptr) {
			return {};
		}
		BloomMetadataBudget budget;
		const auto& controls = budget.Array(BloomField(metadata, "velvetyBloomControl"));
		if (controls.size() > kMaximumBloomDefinitions) {
			throw std::runtime_error("Emote Bloom controls exceed bounded actual owner initialization");
		}
		std::vector<EmoteBloomControlDefinition> definitions;
		definitions.reserve(controls.size());
		for (const auto& control : controls) {
			definitions.push_back(ReadBloomControl(control, budget));
		}
		return definitions;
	}

} // namespace sgre::scene
