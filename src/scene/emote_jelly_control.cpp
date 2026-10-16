#include "sgre/scene/emote_jelly_control.hpp"

#include "sgre/scene/emote_collision_owner.hpp"
#include "sgre/scene/emote_point_prediction.hpp"
#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_layer_uids.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::scene {

	static resource::MotionPoint JellyControlOrigin(const EmoteJellyDefinition& definition) {
		if (definition.Fixes.empty()) {
			return {0.5f, 0.5f};
		}
		if (definition.Fixes.size() == 1) {
			return definition.Fixes.front();
		}
		return {(definition.Fixes[0].X + definition.Fixes[1].X) * 0.5f, (definition.Fixes[0].Y + definition.Fixes[1].Y) * 0.5f};
	}

	static resource::MotionMeshMatrix JellyPredictionNormalization(const EmoteVelvetyPosture& posture) {
		// Native2f28a9..2f296c: correction-angle inverse about the actual physical origin; no scale term.
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

	struct JellyControlInstance {
		JellyControlInstance(const MotionLayerUidReference& layer, const EmoteJellyControlDefinition& definition)
		    : Layer(layer), Physics(definition.Physics), Prediction(definition.PredictionDegree, definition.Physics.Fixes.size()) {
		}
		MotionLayerUidReference Layer;
		// Reverse destruction retires physical collider borrows before their shared group leases.
		std::vector<std::shared_ptr<EmoteCollisionGroup>> Collisions;
		EmoteJellyPhysics Physics;
		EmotePointPrediction Prediction;
		bool Reset = true;
		bool PendingColliders = false;
	};

	class EmoteJellyControl::Implementation final : public MotionLayerUidObserver {
	public:
		Implementation(const MotionPlayerCommands& primary, EmoteJellyControlDefinition definition, EmoteVariableControls* variables, EmoteCollisionOwner* collisions)
		    : Commands(primary), Uids(primary.LayerUids()), Definition(std::move(definition)), Variables(variables), Collisions(collisions), Origin(JellyControlOrigin(Definition.Physics)), Posture(Definition.Physics.SizeHorizontal, Definition.Physics.SizeVertical, Origin.X, Origin.Y, Definition.PixelsPerMeter, Definition.Physics.Fixes) {
			if (&Uids.UltimatePlayer() != &primary || Definition.Physics.Uid.size() > kMaximumMotionCommandNameBytes || !(Definition.PixelsPerMeter > 0) || !std::isfinite(Definition.PixelsPerMeter) || Definition.PredictionDegree > 3 || Definition.Physics.Fixes.size() > 3) {
				throw std::runtime_error("Emote Jelly control requires its actual primary UID owner and bounded metadata");
			}
			if (!Definition.CollisionReferences.empty() && (Collisions == nullptr || !Collisions->UsesCommands(primary))) {
				throw std::runtime_error("Emote Jelly actual Bobble collider attachment requires its exact primary collision owner");
			}
			if (Definition.RequiresEmissions) {
				throw std::runtime_error("Emote Jelly control requires its unrecovered actual emission force owner");
			}
			if (Variables != nullptr && !Variables->UsesCommands(primary)) {
				throw std::runtime_error("Emote Jelly speed publisher must use this exact primary plain command owner");
			}
			if (!Definition.SpeedVariables.empty() && Variables == nullptr) {
				throw std::runtime_error("Emote Jelly speed variables require their actual transition publisher");
			}
			BoundSpeedVariables.reserve(Definition.SpeedVariables.size());
			for (const auto& name : Definition.SpeedVariables) {
				if (Variables->BindSpeedTransition(name)) {
					BoundSpeedVariables.push_back(name);
				}
			}
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
					throw std::runtime_error("Emote Jelly Added requires its exact primary UID occurrence and idle owner");
				}
				// Jelly2eee70 differs from String2ef2c0: it creates310740 without calling51f0f0 on the layer.
				auto instance = std::make_unique<JellyControlInstance>(match.Layer, Definition);
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

		void RequireThread() const {
			if (OwnerThread != std::this_thread::get_id()) {
				throw std::runtime_error("Emote Jelly control requires its actual serialized owner thread");
			}
		}

		void RequireReady() const {
			RequireThread();
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Closing || Operating || !Valid) {
				throw std::runtime_error("Emote Jelly control requires its live idle actual UID owner");
			}
			for (const auto& [occurrence, instance] : Instances) {
				static_cast<void>(occurrence);
				if (instance->PendingColliders) {
					throw std::runtime_error("Emote Jelly requires FinishLayerChanges before the actual collider-pose phase");
				}
			}
		}

		void FinishLayerChanges() {
			RequireThread();
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Closing || Operating || !Valid) {
				throw std::runtime_error("Emote Jelly pending collider joins require the live idle actual control");
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
			RequireThread();
			if (!Subscribed) {
				return;
			}
			if (Operating || Closing) {
				throw std::runtime_error("Emote Jelly control cannot close during actual update or retirement");
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
			if (!std::isfinite(elapsed_ticks) || !(elapsed_ticks > 0) || elapsed_ticks > 4096 * 1.5f) {
				throw std::runtime_error("Emote Jelly control requires the recovered positive finite physical step");
			}
			const auto collision_frame = Definition.CollisionReferences.empty() ? 0 : Collisions->PreparedFrameSerial();
			if (!Definition.CollisionReferences.empty() && (collision_frame == 0 || collision_frame == LastCollisionFrame)) {
				throw std::runtime_error("Emote Jelly requires the actual collider-pose phase once before physical dispatch");
			}
			for (const auto& [occurrence, instance] : Instances) {
				static_cast<void>(occurrence);
				for (const auto& collision : instance->Collisions) {
					if (collision->PreparedFrameSerial() != collision_frame) {
						throw std::runtime_error("Emote Jelly collider group was acquired after the actual pose phase");
					}
				}
			}
			float scaled = elapsed_ticks;
			for (const auto& name : BoundSpeedVariables) {
				const auto value = Variables->SpeedTransitionValue(name);
				if (!value) {
					throw std::runtime_error("Emote Jelly control lost its actual speed transition: " + name);
				}
				scaled = scaled * *value;
			}
			LastUpdated = 0;
			LastWrites = 0;
			Operating = true;
			try {
				for (auto& [occurrence, instance] : Instances) {
					static_cast<void>(occurrence);
					if (!instance->Layer.Active()) {
						instance->Reset = true;
						continue;
					}
					// Native2f26e0 tests the actual UID before reading/stepping its physics; absent/hidden owners have no time-domain requirement.
					const bool unordered_comparison = Definition.Physics.Speed == 0 && Definition.Physics.Sensitivity == 0 && !Definition.Physics.AdhocSimulation && Definition.CollisionReferences.empty();
					if (!std::isfinite(scaled) || !(scaled > 0) || !std::isfinite(scaled * Definition.Physics.Speed) || (!unordered_comparison && !(scaled * Definition.Physics.Speed > 0)) || scaled * Definition.Physics.Speed > 4096 * 1.5f) {
						throw std::runtime_error("Emote Jelly active UID scaled time exceeds the recovered positive physical domain");
					}
					const auto pose = Posture.Read(instance->Layer, frame, Definition.ReferenceParentMeshPosture ? nullptr : actual_parent);
					Cache.Rotation = pose.Physical.Rotation;
					Cache.Scale = pose.CorrectionScale;
					instance->Physics.SetPosture(pose.Physical);
					std::array<resource::MotionPoint, 3> fixes{};
					std::span<resource::MotionPoint> current_fixes;
					if (Definition.Physics.CorrectFix && !Definition.Physics.Fixes.empty()) {
						current_fixes = std::span(fixes).first(Definition.Physics.Fixes.size());
						const auto actual = Posture.WorldFixes();
						std::copy(actual.begin(), actual.end(), current_fixes.begin());
						instance->Prediction.Advance(elapsed_ticks, instance->Reset, JellyPredictionNormalization(pose), current_fixes);
					}
					if (instance->Reset) {
						instance->Physics.Initialize();
						instance->Reset = false;
					}
					instance->Physics.ClearForces();
					if (unordered_comparison) {
						instance->Physics.UpdateForUnorderedComparison(scaled);
						const auto& lattice = instance->Physics.PrepareLatticeForUnorderedComparison(pose.CorrectionAngle, pose.CorrectionScale, current_fixes);
						if (instance->Layer.SetComparedNativeOutput(lattice)) {
							++LastWrites;
						}
					} else {
						instance->Physics.Update(scaled);
						const auto& lattice = instance->Physics.PrepareLattice(pose.CorrectionAngle, pose.CorrectionScale, current_fixes);
						if (instance->Layer.SetMesh(1, &lattice, true)) {
							++LastWrites;
						}
					}
					++LastUpdated;
				}
				LastCollisionFrame = collision_frame;
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}

		const MotionPlayerCommands& Commands;
		MotionPlayerLayerUids& Uids;
		EmoteJellyControlDefinition Definition;
		EmoteVariableControls* Variables = nullptr;
		EmoteCollisionOwner* Collisions = nullptr;
		resource::MotionPoint Origin;
		EmoteVelvetyPostureReader Posture;
		std::vector<std::string> BoundSpeedVariables;
		std::map<std::uint32_t, std::unique_ptr<JellyControlInstance>> Instances;
		std::exception_ptr Failure;
		EmoteVelvetyPartPosture Cache;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::size_t LastUpdated = 0;
		std::size_t LastWrites = 0;
		std::uint64_t LastCollisionFrame = 0;
		bool Subscribed = false;
		bool Operating = false;
		bool Closing = false;
		bool Valid = true;
	};

	EmoteJellyControl::EmoteJellyControl(const MotionPlayerCommands& actual_primary, EmoteJellyControlDefinition definition, EmoteVariableControls* actual_variables, EmoteCollisionOwner* actual_collisions)
	    : m_Implementation(std::make_unique<Implementation>(actual_primary, std::move(definition), actual_variables, actual_collisions)) {
	}
	EmoteJellyControl::~EmoteJellyControl() noexcept = default;
	void EmoteJellyControl::RequireCanSkip() const {
		const auto& state = *m_Implementation;
		state.RequireThread();
		if (state.Failure) {
			std::rethrow_exception(state.Failure);
		}
		if (!state.Subscribed || state.Closing || state.Operating || !state.Valid) {
			throw std::runtime_error("Emote Jelly skip requires its live idle actual UID owner");
		}
		// Pending collider joins are not consumed by the native flag-only operation.
	}
	void EmoteJellyControl::Skip() {
		RequireCanSkip();
		for (auto& [occurrence, instance] : m_Implementation->Instances) {
			static_cast<void>(occurrence);
			instance->Reset = true;
		}
	}
	void EmoteJellyControl::RequireReady() const {
		m_Implementation->RequireReady();
	}
	void EmoteJellyControl::FinishLayerChanges() {
		m_Implementation->FinishLayerChanges();
	}
	void EmoteJellyControl::Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent) {
		m_Implementation->Update(elapsed_ticks, actual_frame, actual_parent);
	}
	void EmoteJellyControl::Close() {
		m_Implementation->Close();
	}
	std::string_view EmoteJellyControl::Uid() const noexcept {
		return m_Implementation->Definition.Physics.Uid;
	}
	bool EmoteJellyControl::UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept {
		return &m_Implementation->Commands == &actual_primary;
	}
	std::size_t EmoteJellyControl::InstanceCount() const noexcept {
		return m_Implementation->Instances.size();
	}
	std::size_t EmoteJellyControl::LastUpdatedCount() const noexcept {
		return m_Implementation->LastUpdated;
	}
	std::size_t EmoteJellyControl::LastMeshWriteCount() const noexcept {
		return m_Implementation->LastWrites;
	}
	EmoteVelvetyPartPosture EmoteJellyControl::PartPosture() const {
		m_Implementation->RequireReady();
		return m_Implementation->Cache;
	}

	void EmoteJellyControl::RequireCanAssignState(const EmoteJellyControl& source) const {
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

	void EmoteJellyControl::AssignState(const EmoteJellyControl& source) {
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
