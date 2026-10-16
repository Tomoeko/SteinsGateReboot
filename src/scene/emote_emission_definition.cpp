#include "sgre/scene/emote_emission_definition.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <variant>

namespace sgre::scene {
	static constexpr std::size_t kMaximumEmissionMetadataItems = 65536;
	static constexpr std::size_t kMaximumEmissionMetadataNames = 1024 * 1024;

	static const resource::PsbValue* EmissionField(const resource::PsbValue* object, std::string_view name) {
		return object == nullptr ? nullptr : object->Find(name);
	}

	static float EmissionNumber(const resource::PsbValue* value) {
		if (value == nullptr || std::holds_alternative<std::monostate>(value->Data)) {
			return 0;
		}
		float result = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value->Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value->Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value->Data)) {
			result = *boolean ? 1.0f : 0.0f;
		} else {
			throw std::runtime_error("Emote emission metadata requires actual native numeric cells");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote emission metadata exceeds finite native float conversion");
		}
		return result;
	}

	static std::int32_t EmissionInteger(const resource::PsbValue* value) {
		if (value != nullptr) {
			if (const auto* integer = std::get_if<std::int64_t>(&value->Data)) {
				if (*integer < std::numeric_limits<std::int32_t>::min() || *integer > std::numeric_limits<std::int32_t>::max()) {
					throw std::runtime_error("Emote emission integer exceeds native int32 metadata");
				}
				return static_cast<std::int32_t>(*integer);
			}
		}
		const auto number = EmissionNumber(value);
		if (number < -2147483648.0f || number >= 2147483648.0f) {
			throw std::runtime_error("Emote emission integer exceeds finite native conversion");
		}
		return static_cast<std::int32_t>(number);
	}

	static std::uint32_t EmissionCount(const resource::PsbValue* value) {
		const auto number = EmissionInteger(value);
		if (number < 0 || static_cast<std::uint32_t>(number) > kMaximumEmissionMetadataItems) {
			throw std::runtime_error("Emote emission count exceeds bounded actual metadata storage");
		}
		return static_cast<std::uint32_t>(number);
	}

	static std::span<const resource::PsbValue> EmissionArray(const resource::PsbValue* value) {
		if (value == nullptr || std::holds_alternative<std::monostate>(value->Data)) {
			return {};
		}
		const auto* array = std::get_if<resource::PsbArray>(&value->Data);
		if (array == nullptr || array->size() > kMaximumEmissionMetadataItems) {
			throw std::runtime_error("Emote emission metadata requires a bounded actual array");
		}
		return *array;
	}

	static std::string EmissionName(const resource::PsbValue* value, std::size_t& name_bytes) {
		const auto* text = value == nullptr ? nullptr : std::get_if<std::string>(&value->Data);
		if (text == nullptr) {
			throw std::runtime_error("Emote emission metadata requires its actual string identity");
		}
		const auto name = std::string_view(*text).substr(0, text->find('\0'));
		if (name.size() > 4096 || name.size() > kMaximumEmissionMetadataNames - name_bytes) {
			throw std::runtime_error("Emote emission metadata exceeds bounded native string storage");
		}
		name_bytes += name.size();
		return std::string(name);
	}

	static EmoteEmissionRange EmissionRange(const resource::PsbValue* value) {
		EmoteEmissionRange range;
		range.Value = EmissionNumber(EmissionField(value, "value"));
		range.Variation = EmissionNumber(EmissionField(value, "drift"));
		range.Minimum = EmissionNumber(EmissionField(value, "minValue"));
		range.Maximum = EmissionNumber(EmissionField(value, "maxValue"));
		return range;
	}

	struct EmissionCelGroup {
		std::int32_t Id = 0;
		std::int32_t AnimationType = 0;
		float AnimationInterval = 0;
		std::uint32_t ParticleLoopCount = 0;
		std::int32_t OriginX = 0;
		std::int32_t OriginY = 0;
		std::uint32_t MaximumWidth = 0;
		std::uint32_t FinalHeightBound = 0;
		std::uint32_t TotalWeight = 0;
		std::uint32_t MirroredWeight = 0;
		std::vector<EmoteEmissionMotionVariant> Cels;
	};

	class EmoteEmissionCatalog::Implementation {
	public:
		explicit Implementation(const resource::PsbDocument& document) {
			const auto* metadata = document.Root.Find("metadata");
			const auto* common = EmissionField(metadata, "velvetyCommonParams");
			if (common == nullptr) {
				return;
			}
			Ppm = EmissionNumber(EmissionField(common, "ppm"));
			if (!(Ppm > 0)) {
				throw std::runtime_error("Emote emission source interpretation requires actual positive Common ppm");
			}
			const auto* cel_group = EmissionField(metadata, "celGroup");
			std::size_t total_cels = 0;
			for (const auto& value : EmissionArray(EmissionField(cel_group, "groups"))) {
				EmissionCelGroup group;
				group.Id = EmissionInteger(value.Find("id"));
				group.AnimationType = EmissionInteger(value.Find("animType"));
				group.AnimationInterval = EmissionNumber(value.Find("animInterval"));
				group.ParticleLoopCount = EmissionCount(value.Find("particleLoopCount"));
				group.OriginX = EmissionInteger(value.Find("originOffsetX"));
				group.OriginY = EmissionInteger(value.Find("originOffsetY"));
				for (const auto& cel : EmissionArray(value.Find("cels"))) {
					if (++total_cels > kMaximumEmissionMetadataItems) {
						throw std::runtime_error("Emote emission cel inventory exceeds bounded actual source records");
					}
					const auto width = EmissionCount(cel.Find("width"));
					const auto height = EmissionCount(cel.Find("height"));
					EmoteEmissionMotionVariant variant;
					variant.Motion = EmissionName(cel.Find("id"), NameBytes);
					variant.SourceSize = std::max(width, height);
					variant.Frames = 1;
					variant.Weight = EmissionCount(cel.Find("weight"));
					if (variant.Weight > kMaximumEmissionMetadataItems - group.TotalWeight) {
						throw std::runtime_error("Emote emission cel weights exceed bounded native selection storage");
					}
					group.TotalWeight += variant.Weight;
					group.MaximumWidth = std::max(group.MaximumWidth, width);
					// Native2e8980..8998 overwrites this with max(current width bound, this height).
					group.FinalHeightBound = std::max(group.MaximumWidth, height);
					group.Cels.push_back(std::move(variant));
				}
				if (group.Cels.size() >= 2) {
					group.MirroredWeight = (group.TotalWeight * 2 - group.Cels.front().Weight) - group.Cels.back().Weight;
				}
				Groups.insert_or_assign(group.Id, std::move(group));
			}
			Map = EmissionField(metadata, "emissionMap");
			if (Map != nullptr && !std::holds_alternative<std::monostate>(Map->Data) && !std::holds_alternative<resource::PsbObject>(Map->Data)) {
				throw std::runtime_error("Emote emissionMap requires its actual dictionary or null");
			}
			for (const auto& uid : EmissionArray(EmissionField(Map, "auto"))) {
				Automatic.push_back(EmissionName(&uid, NameBytes));
			}
		}

		EmoteEmissionParticleDefinition ReadParticle(const resource::PsbValue* value) const {
			EmoteEmissionParticleDefinition result;
			result.PixelsPerMeter = Ppm;
			result.Scale = EmissionRange(EmissionField(value, "scale"));
			result.TerminalScaleRatio = EmissionRange(EmissionField(value, "termScaleRatio"));
			result.Density = EmissionRange(EmissionField(value, "density"));
			result.Velocity = EmissionRange(EmissionField(value, "velocity"));
			result.TerminalVelocityRatio = EmissionRange(EmissionField(value, "termVelocityRatio"));
			result.Lifespan = EmissionRange(EmissionField(value, "lifespan"));
			result.FadeIn = EmissionRange(EmissionField(value, "fadeIn"));
			result.FadeOut = EmissionRange(EmissionField(value, "fadeOut"));
			result.StartWithEmissionAngle = EmissionNumber(EmissionField(value, "startWithEmissionAngle")) != 0;
			result.FaceTravelDirection = EmissionNumber(EmissionField(value, "faceTravelDirection")) != 0;
			result.AngularVelocity = EmissionRange(EmissionField(value, "angularVelocity"));
			result.AngularOffset = EmissionRange(EmissionField(value, "angularOffset"));
			result.WaveAmplitude = EmissionRange(EmissionField(value, "waveAmplitude"));
			result.TerminalWaveAmplitudeRatio = EmissionRange(EmissionField(value, "termWaveAmplitudeRatio"));
			result.WaveFrequency = EmissionRange(EmissionField(value, "waveFrequency"));
			result.WavePhase = EmissionRange(EmissionField(value, "wavePhase"));
			result.GravityStrength = EmissionNumber(EmissionField(value, "gravityStrength"));
			result.GravityDirection = EmissionNumber(EmissionField(value, "gravityDirection"));
			const auto default_group = Groups.find(1);
			if (default_group == Groups.end()) {
				throw std::runtime_error("Emote emission particle requires its genuine default cel group1 source-size owner");
			}
			result.SourceSize = static_cast<float>(std::max(default_group->second.MaximumWidth, default_group->second.FinalHeightBound));
			const auto found = Groups.find(EmissionInteger(EmissionField(value, "celGroup")));
			if (found == Groups.end()) {
				return result;
			}
			const auto& group = found->second;
			result.SourceOrigin = {static_cast<float>(group.OriginX) / Ppm, static_cast<float>(group.OriginY) / Ppm};
			result.MotionInterval = group.AnimationInterval;
			result.MotionCycles = group.ParticleLoopCount;
			if (group.Cels.size() < 2 || group.AnimationType == 0) {
				result.Motions = group.Cels;
			} else if (group.AnimationType >= 1 && group.AnimationType <= 3) {
				EmoteEmissionMotionVariant variant;
				variant.Motion = std::to_string(group.Id) + (group.AnimationType == 1 ? "s" : group.AnimationType == 2 ? "r"
				                                                                                                       : "o");
				variant.SourceSize = std::max(group.MaximumWidth, group.FinalHeightBound);
				variant.Frames = group.AnimationType == 2 ? group.MirroredWeight : group.TotalWeight;
				variant.Weight = 1;
				result.Motions.push_back(std::move(variant));
			}
			return result;
		}

		std::map<std::int32_t, EmissionCelGroup> Groups;
		const resource::PsbValue* Map = nullptr;
		std::vector<std::string> Automatic;
		float Ppm = 1;
		std::size_t NameBytes = 0;
	};

	EmoteEmissionCatalog::EmoteEmissionCatalog(const resource::PsbDocument& document)
	    : m_Implementation(std::make_unique<Implementation>(document)) {
	}
	EmoteEmissionCatalog::~EmoteEmissionCatalog() noexcept = default;

	std::optional<EmoteEmissionGroupDefinition> EmoteEmissionCatalog::Read(std::string_view uid) const {
		const auto& state = *m_Implementation;
		uid = uid.substr(0, uid.find('\0'));
		if (uid.size() > 4096) {
			throw std::runtime_error("Emote emission group lookup exceeds bounded native UID length");
		}
		const auto* value = EmissionField(state.Map, uid);
		if (value == nullptr) {
			return std::nullopt;
		}
		EmoteEmissionGroupDefinition group;
		group.Uid = uid;
		group.Pool.PixelsPerMeter = state.Ppm;
		group.Pool.MaximumParticles = EmissionCount(value->Find("maxParticles"));
		group.StartWithParticles = EmissionInteger(value->Find("startWithParticles")) != 0;
		group.GenerateParticlesAtBack = EmissionNumber(value->Find("generateParticlesAtBack")) != 0;
		group.CollisionType = static_cast<std::uint32_t>(EmissionInteger(value->Find("collisionType")));
		group.ReductionRatioAtReflection = EmissionNumber(value->Find("reductionRatioAtReflection"));
		std::size_t name_bytes = group.Uid.size();
		std::size_t motion_records = 0;
		for (const auto& name : EmissionArray(value->Find("speedVariables"))) {
			group.SpeedVariables.push_back(EmissionName(&name, name_bytes));
		}
		for (const auto& reference : EmissionArray(value->Find("collisionRefs"))) {
			group.CollisionReferences.push_back(EmissionName(reference.Find("uid"), name_bytes));
		}
		for (const auto& emitter : EmissionArray(value->Find("emitters"))) {
			if (group.Emitters.size() == 4096) {
				throw std::runtime_error("Emote emission group exceeds bounded actual emitter count");
			}
			EmoteEmissionEmitterMetadata metadata;
			const auto type = EmissionInteger(emitter.Find("type"));
			if (type != 0 && type != 1) {
				throw std::runtime_error("Emote emission Bezier/type requires its genuine resource geometry owner");
			}
			const auto* common = emitter.Find("emitter");
			const auto* particle = emitter.Find("particle");
			metadata.FollowMesh = EmissionNumber(EmissionField(particle, "followMesh")) != 0;
			metadata.IgnoreCollision = EmissionNumber(EmissionField(common, "ignoreCollision")) != 0;
			metadata.IgnoreSourceAngle = EmissionNumber(EmissionField(common, "ignoreSourceAngle")) != 0;
			metadata.Trigger = static_cast<std::uint32_t>(EmissionInteger(EmissionField(common, "trigger")));
			metadata.TriggerEmitter = EmissionInteger(EmissionField(common, "triggerEmitter"));
			for (const auto& name : EmissionArray(EmissionField(common, "triggerVariables"))) {
				metadata.TriggerVariables.push_back(EmissionName(&name, name_bytes));
			}
			auto& definition = metadata.Definition;
			definition.Emitter = static_cast<std::uint32_t>(group.Emitters.size());
			definition.Geometry = type == 0 ? EmoteEmissionGeometry::Point : EmoteEmissionGeometry::Rectangle;
			definition.IgnoreCollision = metadata.IgnoreCollision;
			definition.DepthOffset = EmissionNumber(EmissionField(common, "zofst"));
			definition.Angle = EmissionRange(EmissionField(common, "angle"));
			definition.Frequency = EmissionRange(EmissionField(common, "frequency"));
			definition.GenerationCount = EmissionRange(EmissionField(common, "generationCount"));
			definition.Particle = state.ReadParticle(particle);
			if (definition.Particle.Motions.size() > kMaximumEmissionMetadataItems - motion_records) {
				throw std::runtime_error("Emote emission group exceeds bounded cloned cel records");
			}
			motion_records += definition.Particle.Motions.size();
			for (const auto& motion : definition.Particle.Motions) {
				if (motion.Motion.size() > kMaximumEmissionMetadataNames - name_bytes) {
					throw std::runtime_error("Emote emission group exceeds bounded cloned Motion names");
				}
				name_bytes += motion.Motion.size();
			}
			if (type == 0) {
				definition.First = {EmissionNumber(emitter.Find("x")), EmissionNumber(emitter.Find("y"))};
			} else {
				definition.First = {EmissionNumber(emitter.Find("x1")), EmissionNumber(emitter.Find("y1"))};
				definition.Second = {EmissionNumber(emitter.Find("x2")), EmissionNumber(emitter.Find("y2"))};
			}
			group.Emitters.push_back(std::move(metadata));
		}
		group.Pool.EmitterCount = static_cast<std::uint32_t>(group.Emitters.size());
		return group;
	}

	std::span<const std::string> EmoteEmissionCatalog::AutomaticGroups() const noexcept {
		return m_Implementation->Automatic;
	}

	float EmoteEmissionCatalog::PixelsPerMeter() const noexcept {
		return m_Implementation->Ppm;
	}
} // namespace sgre::scene
