#include "sgre/scene/emote_string_physics.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::scene {
	static const resource::PsbValue& StringField(const resource::PsbValue& value, std::string_view name) {
		if (const auto* field = value.Find(name)) {
			return *field;
		}
		throw std::runtime_error("Emote String metadata is missing " + std::string(name));
	}

	static float StringNumber(const resource::PsbValue& value) {
		float number = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			number = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			number = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			number = *boolean ? 1 : 0;
		} else {
			throw std::runtime_error("Emote String scalar requires actual numeric metadata");
		}
		if (!std::isfinite(number)) {
			throw std::runtime_error("Emote String scalar exceeds its finite metadata domain");
		}
		return number;
	}

	static float StringNumber(const resource::PsbValue& value, std::string_view name) {
		return StringNumber(StringField(value, name));
	}

	static std::int32_t StringInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote String integer exceeds its native finite conversion domain");
	}

	static bool StringBoolean(const resource::PsbValue& value, std::string_view name, bool fallback) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			return fallback;
		}
		if (const auto* boolean = std::get_if<bool>(&field->Data)) {
			return *boolean;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&field->Data)) {
			return static_cast<std::uint8_t>(*integer) != 0;
		}
		if (const auto* real = std::get_if<double>(&field->Data)) {
			return *real != 0;
		}
		throw std::runtime_error("Emote String flag requires actual numeric metadata");
	}

	static const resource::PsbArray& StringArray(const resource::PsbValue& value) {
		if (const auto* array = std::get_if<resource::PsbArray>(&value.Data)) {
			return *array;
		}
		throw std::runtime_error("Emote String list requires actual array metadata");
	}

	static float NormalizeStringRotation(float rotation) {
		static constexpr float kPi = 3.1415927410125732421875f;
		static constexpr float kTwoPi = 6.283185482025146484375f;
		if (std::abs(rotation) > 4096 * kPi) {
			throw std::runtime_error("Emote String reference rotation exceeds bounded native normalization work");
		}
		rotation += kPi;
		while (rotation < 0) {
			rotation += kTwoPi;
		}
		while (rotation >= kTwoPi) {
			rotation -= kTwoPi;
		}
		return rotation - kPi;
	}

	std::vector<EmoteStringDefinition> ReadEmoteStringPhysicsDefinitions(const resource::PsbDocument& document) {
		const auto& metadata = StringField(document.Root, "metadata");
		if (metadata.Find("velvetyCommonParams") == nullptr) {
			return {};
		}
		const auto& entries = StringArray(StringField(metadata, "velvetyStringControl"));
		if (entries.size() > 4096) {
			throw std::runtime_error("Emote String controls exceed bounded physical-owner initialization");
		}
		std::vector<EmoteStringDefinition> definitions;
		definitions.reserve(entries.size());
		std::size_t tress_count = 0;
		std::size_t uid_bytes = 0;
		for (const auto& entry : entries) {
			EmoteStringDefinition definition;
			const auto* uid = std::get_if<std::string>(&StringField(entry, "uid").Data);
			if (uid == nullptr || uid->size() > 1024 * 1024 - uid_bytes) {
				throw std::runtime_error("Emote String UID requires bounded actual string metadata");
			}
			definition.Uid = uid->substr(0, uid->find('\0'));
			uid_bytes += uid->size();
			definition.HorizontalBones = StringInteger(StringField(entry, "boneDirection")) == 1;
			const auto* degree_field = entry.Find("degreeV");
			const auto degree = degree_field == nullptr ? 4 : StringInteger(*degree_field);
			if (degree < 1 || degree > 15) {
				throw std::runtime_error("Emote String longitudinal degree exceeds encoded native lattice range");
			}
			definition.LongitudinalDegree = static_cast<std::uint8_t>(degree);
			const auto* substeps_field = entry.Find("subStep");
			const auto substeps = substeps_field == nullptr ? 1 : StringInteger(*substeps_field);
			if (substeps < 1 || substeps > 4096) {
				throw std::runtime_error("Emote String configured substeps exceed bounded native physical work");
			}
			definition.Substeps = static_cast<std::uint32_t>(substeps);
			definition.Speed = entry.Find("speed") == nullptr ? 1 : StringNumber(*entry.Find("speed"));
			definition.Gravity = StringNumber(entry, "gravity");
			definition.DefaultRotation = NormalizeStringRotation(StringBoolean(entry, "defaultRotAsGravity", false) ? StringNumber(entry, "defaultRot") : 0);
			definition.Straightness = StringNumber(entry, "straightness");
			definition.Roughness = StringNumber(entry, "roughness");
			if (StringBoolean(entry, "autoMass", false)) {
				const auto pixels_per_meter = StringNumber(entry, "ppm");
				if (!(pixels_per_meter > 0)) {
					throw std::runtime_error("Emote String automatic mass requires positive actual pixels per meter");
				}
				definition.Mass = (StringNumber(entry, "imageArea") * StringNumber(entry, "massPerSquareMeter")) / (pixels_per_meter * pixels_per_meter);
			} else {
				definition.Mass = StringNumber(entry, "mass");
			}
			definition.SizeHorizontal = StringNumber(entry, "sizeH");
			definition.SizeVertical = StringNumber(entry, "sizeV");
			definition.OriginX = StringNumber(entry, "originX");
			definition.OriginY = StringNumber(entry, "originY");
			definition.SplitLeft = StringNumber(entry, "splitLeft");
			definition.SplitRight = StringNumber(entry, "splitRight");
			definition.ParticlesPerMeter = StringNumber(entry, "numPerMeter");
			definition.ControlRadius = StringNumber(entry, "controlPointSize");
			const auto rigid = StringInteger(StringField(entry, "rigidMode"));
			if (rigid < 0 || rigid > 2) {
				throw std::runtime_error("Emote String rigid-side policy is outside the recovered physical domain");
			}
			definition.RigidSide = static_cast<EmoteTressRigidSide>(rigid);
			definition.FrictionTop = StringNumber(entry, "frictionTop");
			definition.FrictionBottom = StringNumber(entry, "frictionBottom");
			definition.Elastic = StringNumber(entry, "elastic");
			definition.CoordinateScale = {StringNumber(entry, "coordScaleH"), StringNumber(entry, "coordScaleV"), StringNumber(entry, "coordScaleD")};
			definition.CompressWiden = StringNumber(entry, "compressWiden");
			definition.AdhocSimulation = StringBoolean(entry, "adhocSim", true);
			definition.MassTiltedDistribution = StringBoolean(entry, "massTiltedDist", false);
			if (const auto* hardness = entry.Find("collisionHardness")) {
				const auto value = StringInteger(*hardness);
				if (value < 0 || value > 1) {
					throw std::runtime_error("Emote String collision hardness is outside recovered response modes0/1");
				}
				definition.CollisionHardness = static_cast<std::uint32_t>(value);
			}
			if (const auto* repulsion = entry.Find("collisionRepulsion")) {
				definition.CollisionRepulsion = StringNumber(*repulsion);
			}
			const auto& tresses = StringArray(StringField(entry, "tresses"));
			if (tresses.size() > 65536 - tress_count) {
				throw std::runtime_error("Emote String authored tresses exceed bounded initialization storage");
			}
			tress_count += tresses.size();
			definition.Tresses.reserve(tresses.size());
			for (const auto& tress : tresses) {
				EmoteStringLine line;
				line.TopX = StringNumber(tress, "topXRate");
				line.TopY = StringNumber(tress, "topYRate");
				line.BottomX = StringNumber(tress, "bottomXRate");
				line.BottomY = StringNumber(tress, "bottomYRate");
				definition.Tresses.push_back(line);
			}
			definitions.push_back(std::move(definition));
		}
		return definitions;
	}
} // namespace sgre::scene
