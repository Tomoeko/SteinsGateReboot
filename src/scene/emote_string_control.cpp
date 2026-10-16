#include "sgre/scene/emote_string_control.hpp"

#include "sgre/scene/emote_collision_owner.hpp"
#include "sgre/scene/emote_point_prediction.hpp"
#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {
	static const resource::PsbValue& ControlField(const resource::PsbValue& value, std::string_view name) {
		if (const auto* field = value.Find(name)) {
			return *field;
		}
		throw std::runtime_error("Emote String driver metadata is missing " + std::string(name));
	}

	static const resource::PsbArray& ControlArray(const resource::PsbValue& value) {
		if (const auto* array = std::get_if<resource::PsbArray>(&value.Data)) {
			return *array;
		}
		throw std::runtime_error("Emote String driver requires actual array metadata");
	}

	static std::int32_t ControlInteger(const resource::PsbValue& value) {
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote String driver integer exceeds native finite conversion");
	}

	static bool ControlFlag(const resource::PsbValue& value, std::string_view name, bool fallback) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			return fallback;
		}
		if (const auto* real = std::get_if<double>(&field->Data)) {
			if (!std::isfinite(*real)) {
				throw std::runtime_error("Emote String driver flag requires finite metadata");
			}
			return *real != 0;
		}
		return static_cast<std::uint8_t>(ControlInteger(*field)) != 0;
	}

	static float ControlNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1 : 0;
		} else {
			throw std::runtime_error("Emote String driver scalar requires actual numeric metadata");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote String driver scalar requires finite metadata");
		}
		return result;
	}

	std::vector<EmoteStringControlDefinition> ReadEmoteStringControlDefinitions(const resource::PsbDocument& document) {
		auto physics = ReadEmoteStringPhysicsDefinitions(document);
		auto outputs = ReadEmoteStringMeshOutputDefinitions(document);
		const auto& metadata = ControlField(document.Root, "metadata");
		if (metadata.Find("velvetyCommonParams") == nullptr) {
			return {};
		}
		const auto& controls = ControlArray(ControlField(metadata, "velvetyStringControl"));
		if (controls.size() != physics.size() || controls.size() != outputs.size()) {
			throw std::runtime_error("Emote String driver definition orders differ from actual serialized controls");
		}
		std::vector<EmoteStringControlDefinition> result;
		result.reserve(controls.size());
		std::size_t name_bytes = 0;
		for (std::size_t index = 0; index != controls.size(); ++index) {
			const auto& control = controls[index];
			EmoteStringControlDefinition definition;
			definition.Physics = std::move(physics[index]);
			definition.Output = std::move(outputs[index]);
			definition.PixelsPerMeter = ControlNumber(ControlField(control, "ppm"));
			const auto prediction = control.Find("coordPred") == nullptr ? 0 : ControlInteger(*control.Find("coordPred"));
			if (prediction < 0 || prediction > 3) {
				throw std::runtime_error("Emote String driver prediction requires recovered degree0..3");
			}
			definition.PredictionDegree = static_cast<std::uint32_t>(prediction);
			definition.InheritShape = ControlFlag(control, "inheritShape", true);
			definition.ReferenceParentMeshPosture = ControlFlag(control, "refParentMeshPosture", false);
			definition.RequiresColliders = !ControlArray(ControlField(control, "collisionRefs")).empty();
			definition.RequiresEmissions = !ControlArray(ControlField(control, "emissionRefs")).empty();
			if (definition.RequiresColliders) {
				static_cast<void>(ControlField(control, "collisionHardness"));
				static_cast<void>(ControlField(control, "collisionRepulsion"));
			}
			const auto& collisions = ControlArray(ControlField(control, "collisionRefs"));
			if (collisions.size() > 4096) {
				throw std::runtime_error("Emote String collider references exceed bounded native instance storage");
			}
			definition.CollisionReferences.reserve(collisions.size());
			for (const auto& collision : collisions) {
				const auto* uid = std::get_if<std::string>(&ControlField(collision, "uid").Data);
				if (uid == nullptr || uid->size() > 1024 * 1024 - name_bytes) {
					throw std::runtime_error("Emote String collider UID requires bounded actual strings");
				}
				name_bytes += uid->size();
				EmoteStringCollisionReference reference;
				reference.Uid = uid->substr(0, uid->find('\0'));
				reference.IgnoreMask = static_cast<std::uint32_t>(ControlInteger(ControlField(collision, "ignoreMask")));
				definition.CollisionReferences.push_back(std::move(reference));
			}
			const auto& speeds = ControlArray(ControlField(control, "speedVariables"));
			if (speeds.size() > 4096) {
				throw std::runtime_error("Emote String driver speed list exceeds bounded native registration work");
			}
			for (const auto& speed : speeds) {
				const auto* name = std::get_if<std::string>(&speed.Data);
				if (name == nullptr || name->size() > 1024 * 1024 - name_bytes) {
					throw std::runtime_error("Emote String driver speed name requires bounded actual strings");
				}
				name_bytes += name->size();
				definition.SpeedVariables.push_back(name->substr(0, name->find('\0')));
			}
			result.push_back(std::move(definition));
		}
		return result;
	}

	static resource::MotionMeshMatrix PredictionNormalization(const EmoteVelvetyPosture& posture) {
		// Native2f2ffd..30c7 rotates by negative correction angle around the actual physical origin; scale is absent.
		const auto sine = std::sin(-posture.CorrectionAngle);
		const auto cosine = std::cos(-posture.CorrectionAngle);
		const auto negative_x = -posture.Physical.Coordinate[0];
		const auto negative_y = -posture.Physical.Coordinate[1];
		resource::MotionMeshMatrix result;
		result.XX = (-sine * 0.0f) + cosine;
		result.XY = (sine * 0.0f) - sine;
		result.TranslationX = ((-sine * negative_y) + negative_x * cosine) + 0.0f;
		result.YX = (sine * 0.0f) + sine;
		result.YY = (cosine * 0.0f) + cosine;
		result.TranslationY = ((negative_y * cosine) + negative_x * sine) + 0.0f;
		return result;
	}

	struct StringControlInstance {
		StringControlInstance(const MotionLayerUidReference& layer, const EmoteStringControlDefinition& definition)
		    : Layer(layer), Physics(definition.Physics), Output(Physics, definition.Output), Prediction(definition.PredictionDegree, definition.Output.Fixes.size()) {
		}
		MotionLayerUidReference Layer;
		// Reverse destruction retires physical borrowed collider pointers before their actual shared group leases.
		std::vector<std::shared_ptr<EmoteCollisionGroup>> Collisions;
		EmoteStringPhysics Physics;
		EmoteStringMeshOutput Output;
		EmotePointPrediction Prediction;
		bool Reset = true;
		bool PendingColliders = true;
	};

	class EmoteStringControl::Implementation final : public MotionLayerUidObserver {
	public:
		Implementation(const MotionPlayerCommands& primary, EmoteStringControlDefinition definition, EmoteVariableControls* variables, EmoteCollisionOwner* collisions)
		    : Commands(primary), Uids(primary.LayerUids()), Definition(std::move(definition)), Variables(variables), Collisions(collisions), Posture(Definition.Physics.SizeHorizontal, Definition.Physics.SizeVertical, Definition.Physics.OriginX, Definition.Physics.OriginY, Definition.PixelsPerMeter, Definition.Output.Fixes) {
			if (&Uids.UltimatePlayer() != &primary || Definition.Physics.Uid.size() > kMaximumMotionCommandNameBytes || !(Definition.PixelsPerMeter > 0) || !std::isfinite(Definition.PixelsPerMeter) || Definition.PredictionDegree > 3 || Definition.Output.Fixes.size() > 3) {
				throw std::runtime_error("Emote String control requires its actual primary UID owner and bounded metadata");
			}
			if (!Definition.InheritShape && (Definition.Output.CorrectFix || Definition.Output.CorrectScale)) {
				throw std::runtime_error("Emote String correction flags require actual inherited-shape metadata");
			}
			if (Definition.RequiresEmissions) {
				throw std::runtime_error("Emote String control resolved emission owner join is not recovered");
			}
			if ((Definition.RequiresColliders || !Definition.CollisionReferences.empty()) && (Collisions == nullptr || !Collisions->UsesCommands(primary) || Definition.CollisionReferences.empty())) {
				throw std::runtime_error("Emote String collider references require their exact actual primary collision owner");
			}
			if (Variables != nullptr && !Variables->UsesCommands(primary)) {
				throw std::runtime_error("Emote String speed publisher must use this exact primary plain command owner");
			}
			if (!Definition.SpeedVariables.empty() && Variables == nullptr) {
				throw std::runtime_error("Emote String control speed variables require the actual transition publisher");
			}
			BoundSpeedVariables.reserve(Definition.SpeedVariables.size());
			for (const auto& name : Definition.SpeedVariables) {
				if (Variables->BindSpeedTransition(name)) {
					BoundSpeedVariables.push_back(name);
				}
			}
			PixelOrigin.X = ((Definition.Physics.OriginX - 0.5f) * Definition.Physics.SizeHorizontal) * Definition.PixelsPerMeter;
			PixelOrigin.Y = ((Definition.Physics.OriginY - 0.5f) * Definition.Physics.SizeVertical) * Definition.PixelsPerMeter;
			PhysicalOrigin.X = (Definition.Physics.OriginX - 0.5f) * Definition.Physics.SizeHorizontal;
			PhysicalOrigin.Y = (Definition.Physics.OriginY - 0.5f) * Definition.Physics.SizeVertical;
			Uids.Subscribe(Definition.Physics.Uid, *this);
			Subscribed = true;
			try {
				FinishLayerChanges();
				RequireReady();
			} catch (...) {
				Close();
				throw;
			}
		}

		~Implementation() noexcept override {
			try {
				Close();
			} catch (...) {
				std::terminate();
			}
		}

		void LayerAdded(std::string_view uid, const MotionLayerUidMatch& match) noexcept override {
			try {
				if (Operating || Closing || uid != Definition.Physics.Uid || &match.Layer.Player().LayerUids().UltimatePlayer() != &Commands || Instances.contains(match.Occurrence) || Instances.size() >= 65536) {
					throw std::runtime_error("Emote String Added requires its exact primary UID occurrence and idle owner");
				}
				match.Layer.InitializeMeshOrigin(PixelOrigin, PhysicalOrigin);
				auto instance = std::make_unique<StringControlInstance>(match.Layer, Definition);
				instance->PendingColliders = !Definition.CollisionReferences.empty();
				Instances.emplace(match.Occurrence, std::move(instance));
			} catch (...) {
				if (!Failure) {
					Failure = std::current_exception();
				}
			}
		}

		void LayerRemoved(std::string_view, const MotionLayerUidMatch& match) noexcept override {
			const auto found = Instances.find(match.Occurrence);
			if (found != Instances.end()) {
				if (!(found->second->Layer == match.Layer) || Operating) {
					std::terminate();
				}
				Instances.erase(found);
			}
		}

		void RequireReady() const {
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Closing || Operating || !Valid) {
				throw std::runtime_error("Emote String control requires its live idle actual UID owner");
			}
			for (const auto& [occurrence, instance] : Instances) {
				static_cast<void>(occurrence);
				if (instance->PendingColliders) {
					throw std::runtime_error("Emote String requires FinishLayerChanges before the actual collider-pose phase");
				}
			}
		}

		void FinishLayerChanges() {
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Closing || Operating || !Valid) {
				throw std::runtime_error("Emote String pending collider joins require the live idle actual control");
			}
			try {
				for (auto& [occurrence, instance] : Instances) {
					static_cast<void>(occurrence);
					if (!instance->PendingColliders) {
						continue;
					}
					instance->Collisions.reserve(Definition.CollisionReferences.size());
					for (const auto& reference : Definition.CollisionReferences) {
						auto group = Collisions->Acquire(reference.Uid);
						if (group) {
							group->Attach(instance->Physics, ~reference.IgnoreMask);
							instance->Collisions.push_back(std::move(group));
						}
					}
					instance->PendingColliders = false;
				}
			} catch (...) {
				Valid = false;
				throw;
			}
		}

		void Close() {
			if (!Subscribed) {
				return;
			}
			if (Operating || Closing) {
				throw std::runtime_error("Emote String control cannot close during actual update or retirement");
			}
			Closing = true;
			try {
				Uids.Unsubscribe(Definition.Physics.Uid, *this);
				Subscribed = false;
				Instances.clear();
				Closing = false;
			} catch (...) {
				Closing = false;
				throw;
			}
		}

		void Update(float elapsed_ticks, const EmoteVelvetyInputFrame& frame, const EmoteVelvetyPartPosture* actual_parent) {
			RequireReady();
			const auto collision_frame = Definition.CollisionReferences.empty() ? 0 : Collisions->PreparedFrameSerial();
			if (!Definition.CollisionReferences.empty() && (collision_frame == 0 || collision_frame == LastCollisionFrame)) {
				throw std::runtime_error("Emote String requires the actual collider-pose phase once before physical dispatch");
			}
			for (const auto& [occurrence, instance] : Instances) {
				static_cast<void>(occurrence);
				for (const auto& collision : instance->Collisions) {
					if (collision->PreparedFrameSerial() != collision_frame) {
						throw std::runtime_error("Emote String collider group was acquired after the actual pose phase");
					}
				}
			}
			if (!std::isfinite(elapsed_ticks) || !(elapsed_ticks > 0) || elapsed_ticks > 4096 * 1.5f) {
				throw std::runtime_error("Emote String control zero/reverse physical step is outside the recovered positive-time domain");
			}
			float scaled = elapsed_ticks;
			for (const auto& name : BoundSpeedVariables) {
				const auto value = Variables->SpeedTransitionValue(name);
				if (!value) {
					throw std::runtime_error("Emote String control lost its actual speed transition: " + name);
				}
				scaled = scaled * *value;
			}
			LastUpdated = 0;
			LastWrites = 0;
			Operating = true;
			try {
				for (auto& [occurrence, owner] : Instances) {
					(void)occurrence;
					if (!owner->Layer.Active()) {
						owner->Reset = true;
						continue;
					}
					// Native2f2e30 tests each actual UID before physical posture, initialization and30da60.
					const auto effective_ticks = scaled * Definition.Physics.Speed;
					const bool unordered_comparison = std::isfinite(scaled) && scaled >= 0 && effective_ticks == 0 && !Definition.Physics.AdhocSimulation && Definition.CollisionReferences.empty();
					if (!std::isfinite(scaled) || scaled < 0 || !std::isfinite(effective_ticks) || (!unordered_comparison && !(effective_ticks > 0)) || effective_ticks > 4096 * 1.5f) {
						throw std::runtime_error("Emote String control scaled time is outside the recovered positive physical domain");
					}
					const auto pose = Posture.Read(owner->Layer, frame, Definition.ReferenceParentMeshPosture ? nullptr : actual_parent);
					Cache.Rotation = pose.Physical.Rotation;
					Cache.Scale = pose.CorrectionScale;
					owner->Physics.SetPosture(pose.Physical);
					std::array<resource::MotionPoint, 3> fixes{};
					std::span<resource::MotionPoint> current_fixes;
					if (Definition.InheritShape && !Definition.Output.Fixes.empty()) {
						current_fixes = std::span(fixes).first(Definition.Output.Fixes.size());
						const auto actual = Posture.WorldFixes();
						std::copy(actual.begin(), actual.end(), current_fixes.begin());
						owner->Prediction.Advance(elapsed_ticks, owner->Reset, PredictionNormalization(pose), current_fixes);
					}
					if (owner->Reset) {
						owner->Physics.Initialize();
						owner->Reset = false;
					}
					owner->Physics.ClearForcesAndMeasureBounds();
					if (unordered_comparison) {
						owner->Physics.UpdateForUnorderedComparison(scaled);
					} else {
						owner->Physics.Update(scaled);
					}
					const auto& lattice = unordered_comparison ? owner->Output.PrepareForUnorderedComparison(pose.Physical, pose.CorrectionAngle, pose.CorrectionScale, current_fixes) : owner->Output.Prepare(pose.Physical, pose.CorrectionAngle, pose.CorrectionScale, current_fixes);
					if (unordered_comparison ? owner->Layer.SetComparedNativeOutput(lattice) : owner->Layer.SetMesh(1, &lattice, true)) {
						++LastWrites;
					}
					++LastUpdated;
				}
				Operating = false;
				LastCollisionFrame = collision_frame;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}

		const MotionPlayerCommands& Commands;
		MotionPlayerLayerUids& Uids;
		EmoteStringControlDefinition Definition;
		EmoteVariableControls* Variables = nullptr;
		EmoteCollisionOwner* Collisions = nullptr;
		std::vector<std::string> BoundSpeedVariables;
		EmoteVelvetyPostureReader Posture;
		std::map<std::uint32_t, std::unique_ptr<StringControlInstance>> Instances;
		std::exception_ptr Failure;
		resource::MotionPoint PixelOrigin;
		resource::MotionPoint PhysicalOrigin;
		EmoteVelvetyPartPosture Cache;
		std::size_t LastUpdated = 0;
		std::size_t LastWrites = 0;
		std::uint64_t LastCollisionFrame = 0;
		bool Subscribed = false;
		bool Operating = false;
		bool Closing = false;
		bool Valid = true;
	};

	EmoteStringControl::EmoteStringControl(const MotionPlayerCommands& actual_primary, EmoteStringControlDefinition definition, EmoteVariableControls* actual_variables, EmoteCollisionOwner* actual_collisions)
	    : m_Implementation(std::make_unique<Implementation>(actual_primary, std::move(definition), actual_variables, actual_collisions)) {
	}
	EmoteStringControl::~EmoteStringControl() noexcept = default;
	void EmoteStringControl::Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent) {
		m_Implementation->Update(elapsed_ticks, actual_frame, actual_parent);
	}
	void EmoteStringControl::RequireReady() const {
		m_Implementation->RequireReady();
	}
	void EmoteStringControl::FinishLayerChanges() {
		m_Implementation->FinishLayerChanges();
	}
	void EmoteStringControl::Close() {
		m_Implementation->Close();
	}
	std::size_t EmoteStringControl::InstanceCount() const noexcept {
		return m_Implementation->Instances.size();
	}
	std::size_t EmoteStringControl::LastUpdatedCount() const noexcept {
		return m_Implementation->LastUpdated;
	}
	std::size_t EmoteStringControl::LastMeshWriteCount() const noexcept {
		return m_Implementation->LastWrites;
	}
	std::string_view EmoteStringControl::Uid() const noexcept {
		return m_Implementation->Definition.Physics.Uid;
	}
	bool EmoteStringControl::UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept {
		return &m_Implementation->Commands == &actual_primary;
	}
	EmoteVelvetyPartPosture EmoteStringControl::PartPosture() const {
		m_Implementation->RequireReady();
		return m_Implementation->Cache;
	}
	void EmoteStringControl::RequireCanAssignState(const EmoteStringControl& source) const {
		const auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.RequireReady();
		from.RequireReady();
		if (Uid() != source.Uid()) {
			throw std::runtime_error("Emote physical assignment requires an exact family UID match");
		}
		for (const auto& [occurrence, instance] : target.Instances) {
			const auto found = from.Instances.find(occurrence);
			if (found == from.Instances.end()) {
				continue;
			}
			instance->Physics.RequireCanAssignState(found->second->Physics);
			instance->Layer.RequireCanAssignMeshFrom(found->second->Layer);
		}
	}

	void EmoteStringControl::AssignState(const EmoteStringControl& source) {
		RequireCanAssignState(source);
		if (this == &source) {
			return;
		}

		auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.Operating = true;
		try {
			target.Posture = from.Posture;
			target.Cache = from.Cache;
			// Native instance maps match occurrence keys; prediction history and destination leases stay local.
			for (auto& [occurrence, instance] : target.Instances) {
				const auto found = from.Instances.find(occurrence);
				if (found == from.Instances.end()) {
					continue;
				}
				instance->Physics.AssignState(found->second->Physics);
				instance->Reset = found->second->Reset;
				instance->Layer.AssignMeshFrom(found->second->Layer);
			}
			target.Operating = false;
		} catch (...) {
			target.Operating = false;
			target.Valid = false;
			throw;
		}
	}

} // namespace sgre::scene
