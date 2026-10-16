#include "sgre/scene/emote_catenary_control.hpp"

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

	static resource::MotionMeshMatrix CatenaryPredictionNormalization(const EmoteVelvetyPosture& posture) {
		// Native2f22c0 fix-prediction phase: correction-angle inverse about the actual physical origin; no scale term.
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

	struct CatenaryControlInstance {
		CatenaryControlInstance(const MotionLayerUidReference& layer, const EmoteCatenaryControlDefinition& definition)
		    : Layer(layer), Physics(definition.Physics), Prediction(definition.PredictionDegree, definition.Physics.Fixes.size()) {
		}
		MotionLayerUidReference Layer;
		EmoteCatenaryPhysics Physics;
		EmotePointPrediction Prediction;
		bool Reset = true;
	};

	class EmoteCatenaryControl::Implementation final : public MotionLayerUidObserver {
	public:
		Implementation(const MotionPlayerCommands& primary, EmoteCatenaryControlDefinition definition, EmoteVariableControls* variables)
		    : Commands(primary), Uids(primary.LayerUids()), Definition(std::move(definition)), Variables(variables), Origin(Definition.Physics.Origin), Posture(Definition.Physics.SizeHorizontal, Definition.Physics.SizeVertical, Origin.X, Origin.Y, Definition.PixelsPerMeter, Definition.Physics.Fixes) {
			if (&Uids.UltimatePlayer() != &primary || Definition.Uid.size() > kMaximumMotionCommandNameBytes || !(Definition.PixelsPerMeter > 0) || !std::isfinite(Definition.PixelsPerMeter) || Definition.PredictionDegree > 3) {
				throw std::runtime_error("Emote Catenary control requires its actual primary UID owner and bounded metadata");
			}
			if (!Definition.CollisionReferences.empty()) {
				// Native2e9050 requires the actual Catenary base-model collision attachment owner.
				throw std::runtime_error("Emote Catenary control requires its actual unmasked base-collider attachment owner");
			}
			if (Definition.RequiresEmissions) {
				throw std::runtime_error("Emote Catenary control requires its unrecovered actual emission force owner");
			}
			if (Variables != nullptr && !Variables->UsesCommands(primary)) {
				throw std::runtime_error("Emote Catenary speed publisher must use this exact primary plain command owner");
			}
			if (!Definition.SpeedVariables.empty() && Variables == nullptr) {
				throw std::runtime_error("Emote Catenary speed variables require their actual transition publisher");
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
					throw std::runtime_error("Emote Catenary Added requires its exact primary UID occurrence and idle owner");
				}
				// Native2eec00 initializes the real UID origin before constructing306190.
				const resource::MotionPoint physical{(Origin.X - 0.5f) * Definition.Physics.SizeHorizontal, (Origin.Y - 0.5f) * Definition.Physics.SizeVertical};
				const resource::MotionPoint pixel{physical.X * Definition.PixelsPerMeter, physical.Y * Definition.PixelsPerMeter};
				match.Layer.InitializeMeshOrigin(pixel, physical);
				auto instance = std::make_unique<CatenaryControlInstance>(match.Layer, Definition);
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
				throw std::runtime_error("Emote Catenary control requires its actual serialized owner thread");
			}
		}

		void RequireReady() const {
			RequireThread();
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Closing || Operating || !Valid) {
				throw std::runtime_error("Emote Catenary control requires its live idle actual UID owner");
			}
		}

		void Close() {
			RequireThread();
			if (!Subscribed) {
				return;
			}
			if (Operating || Closing) {
				throw std::runtime_error("Emote Catenary control cannot close during actual update or retirement");
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
				throw std::runtime_error("Emote Catenary control requires the recovered positive finite physical step");
			}
			float scaled = elapsed_ticks;
			for (const auto& name : BoundSpeedVariables) {
				const auto value = Variables->SpeedTransitionValue(name);
				if (!value) {
					throw std::runtime_error("Emote Catenary control lost its actual speed transition: " + name);
				}
				scaled = scaled * *value;
			}
			if (!std::isfinite(scaled) || !std::isfinite(scaled * Definition.Physics.Speed) || scaled * Definition.Physics.Speed > 4096 * 1.5f) {
				throw std::runtime_error("Emote Catenary control scaled time exceeds the recovered positive physical domain");
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

					if (!(scaled * Definition.Physics.Speed > 0)) {
						throw std::runtime_error("Emote Catenary active UID requires its recovered positive scaled physical step");
					}
					const auto pose = Posture.Read(instance->Layer, frame, Definition.ReferenceParentMeshPosture ? nullptr : actual_parent);
					Cache.Rotation = pose.Physical.Rotation;
					Cache.Scale = pose.CorrectionScale;
					instance->Physics.SetPosture(pose.Physical);
					instance->Physics.SetCorrection(pose.CorrectionAngle, pose.CorrectionScale);
					std::array<resource::MotionPoint, 2> fixes;
					const auto actual = Posture.WorldFixes();
					if (actual.size() != fixes.size()) {
						throw std::runtime_error("Emote Catenary lost its two actual world-fix points");
					}
					std::copy(actual.begin(), actual.end(), fixes.begin());
					instance->Prediction.Advance(elapsed_ticks, instance->Reset, CatenaryPredictionNormalization(pose), fixes);
					instance->Physics.SetWorldFixes(fixes);
					if (instance->Reset) {
						instance->Physics.Initialize();
						instance->Reset = false;
					}
					instance->Physics.ClearForcesAndMeasureBounds();
					instance->Physics.Update(scaled);
					const auto& lattice = instance->Physics.PrepareLattice();
					if (instance->Layer.SetMesh(1, &lattice, true)) {
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
		EmoteCatenaryControlDefinition Definition;
		EmoteVariableControls* Variables = nullptr;
		resource::MotionPoint Origin;
		EmoteVelvetyPostureReader Posture;
		std::vector<std::string> BoundSpeedVariables;
		std::map<std::uint32_t, std::unique_ptr<CatenaryControlInstance>> Instances;
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

	EmoteCatenaryControl::EmoteCatenaryControl(const MotionPlayerCommands& actual_primary, EmoteCatenaryControlDefinition definition, EmoteVariableControls* actual_variables)
	    : m_Implementation(std::make_unique<Implementation>(actual_primary, std::move(definition), actual_variables)) {
	}
	EmoteCatenaryControl::~EmoteCatenaryControl() noexcept = default;
	void EmoteCatenaryControl::RequireReady() const {
		m_Implementation->RequireReady();
	}
	void EmoteCatenaryControl::Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent) {
		m_Implementation->Update(elapsed_ticks, actual_frame, actual_parent);
	}
	void EmoteCatenaryControl::Close() {
		m_Implementation->Close();
	}
	std::string_view EmoteCatenaryControl::Uid() const noexcept {
		return m_Implementation->Definition.Uid;
	}
	bool EmoteCatenaryControl::UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept {
		return &m_Implementation->Commands == &actual_primary;
	}
	std::size_t EmoteCatenaryControl::InstanceCount() const noexcept {
		return m_Implementation->Instances.size();
	}
	std::size_t EmoteCatenaryControl::LastUpdatedCount() const noexcept {
		return m_Implementation->LastUpdated;
	}
	std::size_t EmoteCatenaryControl::LastMeshWriteCount() const noexcept {
		return m_Implementation->LastWrites;
	}
	EmoteVelvetyPartPosture EmoteCatenaryControl::PartPosture() const {
		m_Implementation->RequireReady();
		return m_Implementation->Cache;
	}

} // namespace sgre::scene
