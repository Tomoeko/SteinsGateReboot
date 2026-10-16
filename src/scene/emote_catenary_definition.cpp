#include "sgre/scene/emote_catenary_definition.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumCatenaryDefinitions = 4096;
	static constexpr std::size_t kMaximumCatenaryOwnedNames = 8 * 1024 * 1024;
	static constexpr std::size_t kMaximumCatenaryNestedEntries = 65536;

	static const resource::PsbValue& CatenaryField(const resource::PsbValue& object, std::string_view name) {
		if (const auto* value = object.Find(name)) {
			return *value;
		}
		throw std::runtime_error("Emote Catenary metadata requires actual field " + std::string(name));
	}

	static const resource::PsbArray& CatenaryArray(const resource::PsbValue& value) {
		if (const auto* array = std::get_if<resource::PsbArray>(&value.Data)) {
			return *array;
		}
		throw std::runtime_error("Emote Catenary metadata requires an actual array");
	}

	static float CatenaryNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1.0f : 0.0f;
		} else {
			throw std::runtime_error("Emote Catenary scalar requires native numeric metadata");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote Catenary metadata scalar exceeds its finite supported domain");
		}
		return result;
	}

	static float CatenaryNumber(const resource::PsbValue& object, std::string_view name, float fallback) {
		const auto* value = object.Find(name);
		return value == nullptr ? fallback : CatenaryNumber(*value);
	}

	static std::int32_t CatenaryInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote Catenary integer exceeds its native finite conversion domain");
	}

	static std::int32_t CatenaryInteger(const resource::PsbValue& object, std::string_view name, std::int32_t fallback) {
		const auto* value = object.Find(name);
		return value == nullptr ? fallback : CatenaryInteger(*value);
	}

	static bool CatenaryFlag(const resource::PsbValue& object, std::string_view name, bool fallback) {
		const auto* value = object.Find(name);
		if (value == nullptr) {
			return fallback;
		}
		if (const auto* real = std::get_if<double>(&value->Data)) {
			if (!std::isfinite(*real)) {
				throw std::runtime_error("Emote Catenary flag requires finite native metadata");
			}
			return *real != 0;
		}
		return static_cast<std::uint8_t>(CatenaryInteger(*value)) != 0;
	}

	struct CatenaryMetadataBudget {
		std::size_t Entries = 0;
		std::size_t Names = 0;

		const resource::PsbArray& Array(const resource::PsbValue& value) {
			const auto& array = CatenaryArray(value);
			if (array.size() > kMaximumCatenaryNestedEntries - Entries) {
				throw std::runtime_error("Emote Catenary definitions exceed bounded nested owner storage");
			}
			Entries += array.size();
			return array;
		}

		std::string Name(const resource::PsbValue& value) {
			const auto* text = std::get_if<std::string>(&value.Data);
			if (text == nullptr) {
				throw std::runtime_error("Emote Catenary UID/name requires actual string metadata");
			}
			const auto name = std::string_view(*text).substr(0, text->find('\0'));
			if (name.size() >= kMaximumCatenaryOwnedNames - Names) {
				throw std::runtime_error("Emote Catenary definitions exceed bounded owned name storage");
			}
			Names += name.size() + 1;
			return std::string(name);
		}
	};

	static EmoteCatenaryControlDefinition ReadCatenaryControl(const resource::PsbValue& control, CatenaryMetadataBudget& budget) {
		EmoteCatenaryControlDefinition result;
		auto& physics = result.Physics;
		result.Uid = budget.Name(CatenaryField(control, "uid"));
		physics.Uid = result.Uid;
		result.PixelsPerMeter = CatenaryNumber(CatenaryField(control, "ppm"));
		if (!(result.PixelsPerMeter > 0)) {
			throw std::runtime_error("Emote Catenary UID conversion requires actual positive pixels per meter");
		}
		const auto substeps = CatenaryInteger(control, "subStep", 1);
		const auto prediction = CatenaryInteger(control, "coordPred", 0);
		const auto iterations = CatenaryInteger(control, "deformIter", 0);
		if (substeps < 1 || substeps > 4096 || prediction < 0 || prediction > 3 || iterations < 0 || iterations > 4096) {
			throw std::runtime_error("Emote Catenary counts/prediction exceed the recovered physical-owner domain");
		}
		physics.Substeps = static_cast<std::uint32_t>(substeps);
		physics.DeformIterations = static_cast<std::uint32_t>(iterations);
		result.PredictionDegree = static_cast<std::uint32_t>(prediction);
		physics.Speed = CatenaryNumber(control, "speed", 1.0f);
		physics.Gravity = CatenaryNumber(control, "gravity", 0.0f);
		physics.DefaultRotation = CatenaryNumber(control, "defaultRot", 0.0f);
		physics.GravityRotation = CatenaryNumber(control, "gravityDirection", 0.0f) * 0.0174532924f + physics.DefaultRotation;
		physics.ControlRadius = CatenaryNumber(control, "controlPointSize", 0.0f);
		physics.SizeHorizontal = CatenaryNumber(control, "sizeH", 0.0f);
		physics.SizeVertical = CatenaryNumber(control, "sizeV", 0.0f);
		physics.Origin = {CatenaryNumber(control, "originX", 0.0f), CatenaryNumber(control, "originY", 0.0f)};
		if (CatenaryFlag(control, "autoMass", false)) {
			const float mass_per_area = CatenaryNumber(control, "massPerSquareMeter", 0.0f);
			const float area = CatenaryNumber(control, "imageArea", 0.0f);
			physics.Mass = (area * mass_per_area) / (result.PixelsPerMeter * result.PixelsPerMeter);
		} else {
			physics.Mass = CatenaryNumber(control, "mass", 0.0f);
		}
		const auto& fixes = budget.Array(CatenaryField(control, "fixes"));
		if (fixes.size() < 2) {
			throw std::runtime_error("Emote Catenary requires its actual first two serialized fixes");
		}
		for (std::size_t index = 0; index != 2; ++index) {
			physics.Fixes[index] = {CatenaryNumber(fixes[index], "x", 0.0f), CatenaryNumber(fixes[index], "y", 0.0f)};
		}
		const float x = (physics.Fixes[1].X - physics.Fixes[0].X) * physics.SizeHorizontal;
		const float y = (physics.Fixes[1].Y - physics.Fixes[0].Y) * physics.SizeVertical;
		const float distance = std::sqrt(y * y + x * x);
		physics.Length = CatenaryNumber(control, "lengthRatio", 0.0f) * distance;
		const float particle_count = std::max(physics.Length * CatenaryNumber(control, "numPerMeter", 0.0f) + 1.5f, 3.0f);
		if (!std::isfinite(particle_count) || particle_count >= 257) {
			throw std::runtime_error("Emote Catenary particle density exceeds bounded actual construction");
		}
		physics.ParticleCount = static_cast<std::uint32_t>(particle_count);
		const auto& points = budget.Array(CatenaryField(control, "points"));
		if (points.size() == static_cast<std::size_t>(physics.ParticleCount) * 2) {
			physics.RestPoints.reserve(physics.ParticleCount);
			for (std::size_t index = 0; index < points.size(); index += 2) {
				physics.RestPoints.push_back({CatenaryNumber(points[index]), CatenaryNumber(points[index + 1])});
			}
		}
		physics.FrictionTop = CatenaryNumber(control, "frictionTop", 0.0f);
		physics.FrictionBottom = CatenaryNumber(control, "frictionBottom", 0.0f);
		physics.BottomRatio = CatenaryNumber(control, "bottomRatio", 0.0f);
		physics.MassTiltedDistribution = CatenaryFlag(control, "massTiltedDist", false);
		physics.Elastic = CatenaryNumber(control, "elastic", 0.0f);
		physics.Straightness = CatenaryNumber(control, "straightness", 0.0f);
		physics.CoordinateScale = {CatenaryNumber(control, "coordScaleH", 0.0f), CatenaryNumber(control, "coordScaleV", 0.0f), CatenaryNumber(control, "coordScaleD", 0.0f)};
		result.InheritShape = CatenaryFlag(control, "inheritShape", true);
		physics.CorrectFix = !result.InheritShape && CatenaryFlag(control, "correctFix", false);
		physics.CorrectScale = !result.InheritShape && CatenaryFlag(control, "correctScale", false);
		result.ReferenceParentMeshPosture = CatenaryFlag(control, "refParentMeshPosture", false);
		physics.CollisionRepulsion = CatenaryNumber(control, "collisionRepulsion", 100.0f);
		physics.CollisionHardness = static_cast<std::uint32_t>(CatenaryInteger(control, "collisionHardness", 0));
		physics.AdhocSimulation = CatenaryFlag(control, "adhocSim", true);
		for (const auto& speed : budget.Array(CatenaryField(control, "speedVariables"))) {
			result.SpeedVariables.push_back(budget.Name(speed));
		}
		for (const auto& collision : budget.Array(CatenaryField(control, "collisionRefs"))) {
			EmoteCatenaryCollisionReference reference;
			reference.Uid = budget.Name(CatenaryField(collision, "uid"));
			reference.IgnoreMask = static_cast<std::uint32_t>(CatenaryInteger(CatenaryField(collision, "ignoreMask")));
			result.CollisionReferences.push_back(std::move(reference));
		}
		result.RequiresEmissions = !budget.Array(CatenaryField(control, "emissionRefs")).empty();
		if (!std::isfinite(physics.Mass) || !std::isfinite(physics.Length) || !std::isfinite(physics.GravityRotation)) {
			throw std::runtime_error("Emote Catenary derived mass/length/gravity exceeds its finite supported domain");
		}
		return result;
	}

	std::vector<EmoteCatenaryControlDefinition> ReadEmoteCatenaryControlDefinitions(const resource::PsbDocument& document) {
		const auto& metadata = CatenaryField(document.Root, "metadata");
		if (metadata.Find("velvetyCommonParams") == nullptr) {
			return {};
		}
		CatenaryMetadataBudget budget;
		const auto& controls = budget.Array(CatenaryField(metadata, "velvetyCatenaryControl"));
		if (controls.size() > kMaximumCatenaryDefinitions) {
			throw std::runtime_error("Emote Catenary controls exceed bounded actual owner initialization");
		}
		std::vector<EmoteCatenaryControlDefinition> definitions;
		definitions.reserve(controls.size());
		for (const auto& control : controls) {
			definitions.push_back(ReadCatenaryControl(control, budget));
		}
		return definitions;
	}

} // namespace sgre::scene
