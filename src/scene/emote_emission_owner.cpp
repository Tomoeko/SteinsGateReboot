#include "sgre/scene/emote_emission_owner.hpp"

#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/motion_player_commands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sgre::scene {
	static constexpr std::size_t kMaximumEmissionGroups = 4096;
	static constexpr std::size_t kMaximumOwnedEmissionCapacity = 1024 * 1024;
	static constexpr std::size_t kMaximumEmissionWarmupSteps = 65536;

	struct EmissionOwnerPhase {
		std::uint64_t Serial = 0;
		bool Operating = false;
		bool Valid = true;
		bool Closed = false;
	};

	static float EmissionSourceAngle(float y, float x) {
		static constexpr float kPi = 3.1415927410125732421875f;
		static constexpr float kHalfPi = 1.57079637050628662109375f;
		float angle = 0;
		if (x == 0) {
			angle = y > 0 ? kHalfPi : y < 0 ? -kHalfPi
			                                : 0;
		} else if (std::abs(x) > std::abs(y)) {
			const auto ratio = y / x;
			const auto magnitude = std::abs(ratio);
			const auto correction = ((magnitude - 1.0f) * ratio) * (magnitude * 0.066299997270107269287109375f + 0.24469999969005584716796875f);
			angle = ratio * 0.785398185253143310546875f - correction;
			if (x < 0) {
				angle += y < 0 ? -kPi : kPi;
			}
		} else {
			const auto ratio = x / y;
			const auto magnitude = std::abs(ratio);
			const auto correction = ((magnitude - 1.0f) * ratio) * (magnitude * 0.066299997270107269287109375f + 0.24469999969005584716796875f);
			angle = (y > 0 ? kHalfPi : -kHalfPi) - (ratio * 0.785398185253143310546875f - correction);
		}
		angle += kHalfPi;
		while (angle < 0) {
			angle += 6.283185482025146484375f;
		}
		while (angle >= 6.283185482025146484375f) {
			angle += -6.283185482025146484375f;
		}
		return angle - kPi;
	}

	static void RequireEmissionFrame(const EmoteVelvetyInputFrame& frame, float ticks) {
		for (const auto value : {frame.WorldToPhysics.XX, frame.WorldToPhysics.XY, frame.WorldToPhysics.TranslationX, frame.WorldToPhysics.YX, frame.WorldToPhysics.YY, frame.WorldToPhysics.TranslationY, frame.DepthScale, ticks}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote emission phase requires finite actual global conversion and elapsed ticks");
			}
		}
		if (ticks < 0) {
			throw std::runtime_error("Emote emission reverse-time integration remains outside the recovered pool domain");
		}
	}

	static void RequireEmissionGroup(const EmoteEmissionGroupDefinition& definition) {
		if (!definition.CollisionReferences.empty()) {
			throw std::runtime_error("Emote emission collisionRefs require genuine particle collision/response attachment");
		}
		for (const auto& emitter : definition.Emitters) {
			if (emitter.FollowMesh) {
				throw std::runtime_error("Emote emission followMesh requires its actual particle-IO transformation owner");
			}
			if (emitter.Trigger != 0 || !emitter.TriggerVariables.empty()) {
				throw std::runtime_error("Emote emission trigger/variable routing requires its genuine live producer");
			}
		}
	}

	class EmoteEmissionGroup::Implementation final : public MotionLayerUidObserver {
	public:
		Implementation(const MotionPlayerCommands& primary, EmoteVariableControls* variables, EmoteEmissionRandomState& random, EmissionOwnerPhase& phase, EmoteEmissionGroupDefinition definition)
		    : Commands(primary), Uids(primary.LayerUids()), Variables(variables), Phase(phase), Definition(std::move(definition)), Pool(Definition.Pool) {
			Emitters.reserve(Definition.Emitters.size());
			for (const auto& metadata : Definition.Emitters) {
				Emitters.push_back(std::make_unique<EmoteEmissionEmitter>(Pool, random, metadata.Definition));
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
				Uids.Unsubscribe(Definition.Uid, *this);
				Subscribed = false;
				throw;
			}
		}

		~Implementation() noexcept override {
			try {
				if (Subscribed) {
					Uids.Unsubscribe(Definition.Uid, *this);
				}
			} catch (...) {
				std::terminate();
			}
		}

		void LayerAdded(std::string_view uid, const MotionLayerUidMatch& match) noexcept override {
			try {
				if (Operating || Phase.Operating || uid != Definition.Uid || &match.Layer.Player().LayerUids().UltimatePlayer() != &Commands || Matches == 65536) {
					throw std::runtime_error("Emote emission Added requires its actual idle primary UID registration");
				}
				LastAnchor = match.Layer;
				++Matches;
			} catch (...) {
				if (!Failure) {
					Failure = std::current_exception();
				}
			}
		}

		void LayerRemoved(std::string_view uid, const MotionLayerUidMatch& match) noexcept override {
			if (Operating || Phase.Operating || uid != Definition.Uid || Matches == 0) {
				std::terminate();
			}
			--Matches;
			if (LastAnchor && *LastAnchor == match.Layer) {
				LastAnchor.reset();
			}
		}

		void RequireReady() const {
			if (Failure) {
				std::rethrow_exception(Failure);
			}
			if (!Subscribed || Operating || !Valid || Phase.Closed || !Phase.Valid) {
				throw std::runtime_error("Emote emission group requires its actual live idle primary UID owner");
			}
		}

		void RequirePrepared() const {
			RequireReady();
			if (Phase.Operating || FrameSerial == 0 || FrameSerial != Phase.Serial) {
				throw std::runtime_error("Emote emission force sampling requires the actual completed emission phase");
			}
		}

		void PreparePosture(const EmoteVelvetyInputFrame& frame) {
			if (Matches != 0 && !LastAnchor) {
				throw std::runtime_error("Emote emission's native last anchor retired while another same-UID occurrence remained");
			}
			Active = Matches != 0 && LastAnchor->Active();
			if (!Active) {
				return;
			}
			std::array<resource::MotionPoint, 2> points{resource::MotionPoint{0, 0}, resource::MotionPoint{0, 1}};
			LastAnchor->TransformPoints(points);
			for (auto& point : points) {
				if (frame.Mirror) {
					point.X = -point.X;
				}
				const auto x = point.X;
				point.X = (point.Y * frame.WorldToPhysics.XY + x * frame.WorldToPhysics.XX) + frame.WorldToPhysics.TranslationX;
				point.Y = (point.Y * frame.WorldToPhysics.YY + x * frame.WorldToPhysics.YX) + frame.WorldToPhysics.TranslationY;
			}
			Posture.Coordinate = points[0];
			Posture.Depth = LastAnchor->Depth();
			Posture.Rotation = EmissionSourceAngle(points[1].Y - points[0].Y, points[1].X - points[0].X);
			Posture.Cosine = std::cos(Posture.Rotation);
			Posture.Sine = std::sin(Posture.Rotation);
		}

		void Generate(float ticks) {
			for (auto& emitter : Emitters) {
				emitter->SetPosture(Posture);
				emitter->Step(ticks);
			}
		}

		void Update(float elapsed_ticks, bool consumed) {
			RequireReady();
			float ticks = elapsed_ticks;
			for (const auto& name : BoundSpeedVariables) {
				const auto value = Variables->SpeedTransitionValue(name);
				if (!value) {
					throw std::runtime_error("Emote emission speed lost its genuine bound scalar transition");
				}
				ticks = ticks * *value;
			}
			if (!std::isfinite(ticks) || ticks < 0) {
				throw std::runtime_error("Emote emission effective ticks exceed the recovered finite nonnegative domain");
			}
			Operating = true;
			try {
				if (Active && consumed) {
					if (FirstActivation) {
						FirstActivation = false;
						if (Definition.StartWithParticles) {
							float remaining = 0;
							for (const auto& metadata : Definition.Emitters) {
								remaining = std::max(remaining, metadata.Definition.Particle.Lifespan.Value);
							}
							std::size_t steps = 0;
							while (remaining > 0) {
								if (++steps > kMaximumEmissionWarmupSteps) {
									throw std::runtime_error("Emote emission initial-particle prewarm exceeds bounded actual work");
								}
								remaining = remaining + -0.01666666753590106964111328125f;
								Generate(1);
								Pool.Update(1);
							}
						}
					}
					Generate(ticks);
				}
				// Native2f0dd0 ages the retained pool even without a live anchor or external physical lease.
				Pool.Update(ticks);
				FrameSerial = Phase.Serial + 1;
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}

		const MotionPlayerCommands& Commands;
		MotionPlayerLayerUids& Uids;
		EmoteVariableControls* Variables = nullptr;
		EmissionOwnerPhase& Phase;
		EmoteEmissionGroupDefinition Definition;
		EmoteEmissionPool Pool;
		std::vector<std::unique_ptr<EmoteEmissionEmitter>> Emitters;
		std::vector<std::string> BoundSpeedVariables;
		std::optional<MotionLayerUidReference> LastAnchor;
		std::exception_ptr Failure;
		EmoteEmissionEmitterPosture Posture;
		std::uint64_t FrameSerial = 0;
		std::uint32_t Matches = 0;
		std::size_t AutomaticPins = 0;
		bool FirstActivation = true;
		bool Active = false;
		bool Subscribed = false;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteEmissionGroup::EmoteEmissionGroup(std::unique_ptr<Implementation> implementation) : m_Implementation(std::move(implementation)) {
		if (m_Implementation == nullptr) {
			throw std::runtime_error("Emote emission group requires its actual owned primary implementation");
		}
	}
	EmoteEmissionGroup::~EmoteEmissionGroup() noexcept = default;

	void EmoteEmissionGroup::PrepareBounds(float left, float top, float right, float bottom, float extra_radius) {
		m_Implementation->RequirePrepared();
		m_Implementation->Pool.PrepareBounds(left, top, right, bottom, extra_radius);
	}

	std::array<float, 3> EmoteEmissionGroup::SamplePoint(const resource::MotionPoint& point, float radius) const {
		m_Implementation->RequirePrepared();
		return m_Implementation->Pool.SamplePoint(point, radius);
	}

	std::array<float, 3> EmoteEmissionGroup::SampleEllipse(const EmoteCollisionEllipse& ellipse) const {
		m_Implementation->RequirePrepared();
		return m_Implementation->Pool.SampleEllipse(ellipse);
	}

	std::span<const EmoteEmissionParticle> EmoteEmissionGroup::Particles() const {
		m_Implementation->RequirePrepared();
		return m_Implementation->Pool.Particles();
	}

	std::uint64_t EmoteEmissionGroup::PreparedFrameSerial() const {
		m_Implementation->RequirePrepared();
		return m_Implementation->FrameSerial;
	}

	class EmoteEmissionOwner::Implementation {
	public:
		Implementation(const MotionPlayerCommands& primary, const resource::PsbDocument& document, EmoteVariableControls* variables, std::uint32_t entropy)
		    : Commands(primary), Variables(variables), Catalog(document) {
			if (&primary.LayerUids().UltimatePlayer() != &primary || (variables != nullptr && !variables->UsesCommands(primary))) {
				throw std::runtime_error("Emote emission owner requires its actual primary Commands and scalar-variable publisher");
			}
			Random.Increment = (~std::uint64_t{entropy} * 2) | 1;
			Random.State = (Random.Increment + entropy) * std::uint64_t{0x5851f42d4c957f2d} + Random.Increment;
		}

		void RequireIdle() const {
			if (Phase.Closed || Phase.Operating || !Phase.Valid) {
				throw std::runtime_error("Emote emission owner is closed, active or invalid");
			}
		}

		const MotionPlayerCommands& Commands;
		EmoteVariableControls* Variables = nullptr;
		EmoteEmissionCatalog Catalog;
		EmissionOwnerPhase Phase;
		EmoteEmissionRandomState Random;
		std::map<std::string, std::shared_ptr<EmoteEmissionGroup>, std::less<>> Groups;
		std::vector<std::shared_ptr<EmoteEmissionGroup>> Automatic;
		std::size_t KeyBytes = 0;
		std::size_t ParticleCapacity = 0;
	};

	EmoteEmissionOwner::EmoteEmissionOwner(const MotionPlayerCommands& actual_primary, const resource::PsbDocument& document, EmoteVariableControls* actual_variables, std::uint32_t actual_entropy_word)
	    : m_Implementation(std::make_unique<Implementation>(actual_primary, document, actual_variables, actual_entropy_word)) {
		auto& state = *m_Implementation;
		state.Automatic.reserve(state.Catalog.AutomaticGroups().size());
		for (const auto& uid : state.Catalog.AutomaticGroups()) {
			if (auto group = Acquire(uid)) {
				++group->m_Implementation->AutomaticPins;
				state.Automatic.push_back(std::move(group));
			}
		}
	}

	EmoteEmissionOwner::~EmoteEmissionOwner() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	std::shared_ptr<EmoteEmissionGroup> EmoteEmissionOwner::Acquire(std::string_view uid) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		uid = uid.substr(0, uid.find('\0'));
		if (uid.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("Emote emission UID exceeds bounded actual string storage");
		}
		if (const auto found = state.Groups.find(uid); found != state.Groups.end()) {
			return found->second;
		}
		auto definition = state.Catalog.Read(uid);
		if (!definition) {
			return {};
		}
		RequireEmissionGroup(*definition);
		if (!definition->SpeedVariables.empty() && state.Variables == nullptr) {
			throw std::runtime_error("Emote emission speedVariables require the genuine containing scalar-variable owner");
		}
		const auto capacity = static_cast<std::size_t>(definition->Pool.MaximumParticles) + definition->Pool.MaximumPendingBirths;
		if (state.Groups.size() == kMaximumEmissionGroups || uid.size() > 1024 * 1024 - state.KeyBytes || capacity > kMaximumOwnedEmissionCapacity - state.ParticleCapacity) {
			throw std::runtime_error("Emote emission group inventory exceeds bounded actual owned storage");
		}
		auto implementation = std::make_unique<EmoteEmissionGroup::Implementation>(state.Commands, state.Variables, state.Random, state.Phase, std::move(*definition));
		auto group = std::make_shared<EmoteEmissionGroup>(std::move(implementation));
		state.Groups.emplace(uid, group);
		state.KeyBytes += uid.size();
		state.ParticleCapacity += capacity;
		return group;
	}

	void EmoteEmissionOwner::Prepare(const EmoteVelvetyInputFrame& actual_frame, float elapsed_ticks) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		RequireEmissionFrame(actual_frame, elapsed_ticks);
		if (state.Phase.Serial == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("Emote emission actual phase generation exhausted");
		}
		state.Phase.Operating = true;
		try {
			// All source anchors complete before any emitter consumes the shared random stream.
			for (auto& [uid, group] : state.Groups) {
				static_cast<void>(uid);
				group->m_Implementation->RequireReady();
				group->m_Implementation->PreparePosture(actual_frame);
			}
			for (auto& [uid, group] : state.Groups) {
				static_cast<void>(uid);
				group->m_Implementation->Update(elapsed_ticks, group.use_count() > 1);
			}
			++state.Phase.Serial;
			state.Phase.Operating = false;
		} catch (...) {
			state.Phase.Operating = false;
			state.Phase.Valid = false;
			throw;
		}
	}

	void EmoteEmissionOwner::Close() {
		auto& state = *m_Implementation;
		if (state.Phase.Closed) {
			return;
		}
		if (state.Phase.Operating) {
			throw std::runtime_error("Emote emission Close cannot interrupt the actual emission phase");
		}
		for (const auto& [uid, group] : state.Groups) {
			static_cast<void>(uid);
			if (static_cast<std::size_t>(group.use_count()) != 1 + group->m_Implementation->AutomaticPins) {
				throw std::runtime_error("Emote emission Close requires retirement of actual physical group leases");
			}
		}
		state.Automatic.clear();
		state.Groups.clear();
		state.Phase.Closed = true;
	}

	bool EmoteEmissionOwner::UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept {
		return &m_Implementation->Commands == &actual_primary;
	}

	std::uint64_t EmoteEmissionOwner::PreparedFrameSerial() const {
		m_Implementation->RequireIdle();
		return m_Implementation->Phase.Serial;
	}

	std::size_t EmoteEmissionOwner::GroupCount() const noexcept {
		return m_Implementation->Groups.size();
	}

	EmoteEmissionRandomState EmoteEmissionOwner::RandomState() const {
		m_Implementation->RequireIdle();
		return m_Implementation->Random;
	}
	void EmoteEmissionOwner::RequireCanAssignState(const EmoteEmissionOwner& source) const {
		const auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.RequireIdle();
		from.RequireIdle();
		for (const auto& [uid, group] : target.Groups) {
			const auto found = from.Groups.find(uid);
			if (found == from.Groups.end()) {
				continue;
			}
			const auto& receiving = *group->m_Implementation;
			const auto& original = *found->second->m_Implementation;
			receiving.RequireReady();
			original.RequireReady();
			if (receiving.Emitters.size() > original.Emitters.size()) {
				throw std::runtime_error("Emote emission assignment requires each destination emitter's actual source ordinal");
			}
			for (std::size_t index = 0; index != receiving.Emitters.size(); ++index) {
				receiving.Emitters[index]->RequireCanAssignState(*original.Emitters[index]);
			}
			receiving.Pool.RequireCanAssignState(original.Pool);
		}
	}

	void EmoteEmissionOwner::AssignState(const EmoteEmissionOwner& source) {
		RequireCanAssignState(source);
		if (this == &source) {
			return;
		}

		auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.Phase.Operating = true;
		try {
			target.Random = from.Random;
			for (auto& [uid, group] : target.Groups) {
				const auto found = from.Groups.find(uid);
				if (found == from.Groups.end()) {
					continue;
				}
				auto& receiving = *group->m_Implementation;
				const auto& original = *found->second->m_Implementation;
				for (std::size_t index = 0; index != receiving.Emitters.size(); ++index) {
					receiving.Emitters[index]->AssignState(*original.Emitters[index]);
				}
				receiving.Pool.AssignState(original.Pool);
			}
			target.Phase.Operating = false;
		} catch (...) {
			target.Phase.Operating = false;
			target.Phase.Valid = false;
			throw;
		}
	}

} // namespace sgre::scene
