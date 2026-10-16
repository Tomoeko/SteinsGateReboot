#include "sgre/scene/emote_bloom_control.hpp"

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

	static resource::MotionMeshMatrix BloomPredictionNormalization(const EmoteVelvetyPosture& posture) {
		// Native2f1e90 fix-prediction phase: correction-angle inverse about the actual physical origin; no scale term.
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

	struct BloomControlInstance {
		BloomControlInstance(const MotionLayerUidReference& layer, const EmoteBloomControlDefinition& definition) : Layer(layer), Physics(definition.Physics), Prediction(definition.PredictionDegree, definition.Physics.Fixes.size()) {
		}
		MotionLayerUidReference Layer;
		EmoteBloomPhysics Physics;
		EmotePointPrediction Prediction;
		bool Reset = true;
	};

	class EmoteBloomControl::Implementation final : public MotionLayerUidObserver {
	public:
		Implementation(const MotionPlayerCommands& primary, EmoteBloomControlDefinition definition, EmoteVariableControls* variables) : Commands(primary), Uids(primary.LayerUids()), Definition(std::move(definition)), Variables(variables), Origin(Definition.Physics.Origin), Posture(Definition.Physics.SizeHorizontal, Definition.Physics.SizeVertical, Origin.X, Origin.Y, Definition.PixelsPerMeter, Definition.Physics.Fixes) {
			if (&Uids.UltimatePlayer() != &primary || Definition.Uid.size() > kMaximumMotionCommandNameBytes || !(Definition.PixelsPerMeter > 0) || !std::isfinite(Definition.PixelsPerMeter) || Definition.PredictionDegree > 3 || Definition.Physics.Fixes.size() > 3) {
				throw std::runtime_error("Emote Bloom control requires its actual primary UID owner and bounded metadata");
			}
			if (!Definition.CollisionReferences.empty()) {
				// Native2fb750 attaches every collider to Bloom's base vector; +8 is RET, not a tress-mask filter.
				throw std::runtime_error("Emote Bloom control requires its actual unmasked base-collider attachment owner");
			}
			if (Definition.RequiresEmissions) {
				throw std::runtime_error("Emote Bloom control requires its unrecovered actual emission force owner");
			}
			if (Variables != nullptr && !Variables->UsesCommands(primary)) {
				throw std::runtime_error("Emote Bloom speed publisher must use this exact primary plain command owner");
			}
			if (!Definition.SpeedVariables.empty() && Variables == nullptr) {
				throw std::runtime_error("Emote Bloom speed variables require their actual transition publisher");
			}
			BoundSpeedVariables.reserve(Definition.SpeedVariables.size());
			for (const auto& name : Definition.SpeedVariables) {
				if (Variables->BindSpeedTransition(name)) {
					BoundSpeedVariables.push_back(name);
				}
			}
			Uids.Subscribe(Definition.Uid, *this);
			Subscribed = true;
			try {
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
				if (Operating || Closing || uid != Definition.Uid || &match.Layer.Player().LayerUids().UltimatePlayer() != &Commands || Instances.contains(match.Occurrence) || Instances.size() >= 65536) {
					throw std::runtime_error("Emote Bloom Added requires its exact primary UID occurrence and idle owner");
				}
				// Native2ee9a0 creates3025a0 without String's51f0f0 origin mutation.
				auto instance = std::make_unique<BloomControlInstance>(match.Layer, Definition);
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
				throw std::runtime_error("Emote Bloom control requires its actual serialized owner thread");
			}
		}

		void RequireReady() const {
			RequireThread();
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Closing || Operating || !Valid) {
				throw std::runtime_error("Emote Bloom control requires its live idle actual UID owner");
			}
		}

		void Close() {
			RequireThread();
			if (!Subscribed) {
				return;
			}
			if (Operating || Closing) {
				throw std::runtime_error("Emote Bloom control cannot close during actual update or retirement");
			}
			Closing = true;
			try {
				Uids.Unsubscribe(Definition.Uid, *this);
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
				throw std::runtime_error("Emote Bloom control requires the recovered positive finite physical step");
			}
			float scaled = elapsed_ticks;
			for (const auto& name : BoundSpeedVariables) {
				const auto value = Variables->SpeedTransitionValue(name);
				if (!value) {
					throw std::runtime_error("Emote Bloom control lost its actual speed transition: " + name);
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
					// Native2f1e90 tests519b00 before reaching the physical solver. Inactive zero-speed owners only retain Reset.
					const auto effective_ticks = scaled * Definition.Physics.Speed;
					const bool unordered_comparison = Definition.Physics.Speed == 0 && std::isfinite(scaled) && scaled >= 0 && effective_ticks == 0 && Definition.CollisionReferences.empty() && !Definition.RequiresEmissions;
					if (!std::isfinite(scaled) || scaled < 0 || !std::isfinite(effective_ticks) || (!unordered_comparison && !(effective_ticks > 0)) || effective_ticks > 4096 * 1.5f) {
						throw std::runtime_error("Emote Bloom control scaled time exceeds the recovered positive physical domain");
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
						instance->Prediction.Advance(elapsed_ticks, instance->Reset, BloomPredictionNormalization(pose), current_fixes);
					}
					if (instance->Reset) {
						instance->Physics.Initialize();
						instance->Reset = false;
					}
					instance->Physics.ClearForces();
					if (unordered_comparison) {
						instance->Physics.UpdateForUnorderedComparison(scaled);
					} else {
						instance->Physics.Update(scaled);
					}
					const auto& lattice = unordered_comparison ? instance->Physics.PrepareLatticeForUnorderedComparison(pose.CorrectionAngle, pose.CorrectionScale, current_fixes) : instance->Physics.PrepareLattice(pose.CorrectionAngle, pose.CorrectionScale, current_fixes);
					if (unordered_comparison ? instance->Layer.SetComparedNativeOutput(lattice) : instance->Layer.SetMesh(1, &lattice, true)) {
						++LastWrites;
					}
					++LastUpdated;
				}
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}

		const MotionPlayerCommands& Commands;
		MotionPlayerLayerUids& Uids;
		EmoteBloomControlDefinition Definition;
		EmoteVariableControls* Variables = nullptr;
		resource::MotionPoint Origin;
		EmoteVelvetyPostureReader Posture;
		std::vector<std::string> BoundSpeedVariables;
		std::map<std::uint32_t, std::unique_ptr<BloomControlInstance>> Instances;
		std::exception_ptr Failure;
		EmoteVelvetyPartPosture Cache;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::size_t LastUpdated = 0;
		std::size_t LastWrites = 0;
		bool Subscribed = false;
		bool Operating = false;
		bool Closing = false;
		bool Valid = true;
	};

	EmoteBloomControl::EmoteBloomControl(const MotionPlayerCommands& actual_primary, EmoteBloomControlDefinition definition, EmoteVariableControls* actual_variables) : m_Implementation(std::make_unique<Implementation>(actual_primary, std::move(definition), actual_variables)) {
	}
	EmoteBloomControl::~EmoteBloomControl() noexcept = default;
	void EmoteBloomControl::RequireCanSkip() const {
		m_Implementation->RequireReady();
	}
	void EmoteBloomControl::Skip() {
		RequireCanSkip();
		for (auto& [occurrence, instance] : m_Implementation->Instances) {
			static_cast<void>(occurrence);
			instance->Reset = true;
		}
	}
	void EmoteBloomControl::RequireReady() const {
		m_Implementation->RequireReady();
	}
	void EmoteBloomControl::Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent) {
		m_Implementation->Update(elapsed_ticks, actual_frame, actual_parent);
	}
	void EmoteBloomControl::Close() {
		m_Implementation->Close();
	}
	std::string_view EmoteBloomControl::Uid() const noexcept {
		return m_Implementation->Definition.Uid;
	}
	bool EmoteBloomControl::UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept {
		return &m_Implementation->Commands == &actual_primary;
	}
	std::size_t EmoteBloomControl::InstanceCount() const noexcept {
		return m_Implementation->Instances.size();
	}
	std::size_t EmoteBloomControl::LastUpdatedCount() const noexcept {
		return m_Implementation->LastUpdated;
	}
	std::size_t EmoteBloomControl::LastMeshWriteCount() const noexcept {
		return m_Implementation->LastWrites;
	}
	EmoteVelvetyPartPosture EmoteBloomControl::PartPosture() const {
		m_Implementation->RequireReady();
		return m_Implementation->Cache;
	}

	void EmoteBloomControl::RequireCanAssignState(const EmoteBloomControl& source) const {
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

	void EmoteBloomControl::AssignState(const EmoteBloomControl& source) {
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
