#include "sgre/scene/emote_tress_model.hpp"

#include "sgre/scene/emote_soft_pendulum.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace sgre::scene {
	static constexpr std::size_t kMaximumRigidParticles = 256;
	static constexpr std::size_t kMaximumRigidTrailingPoints = 4096;
	static constexpr float kNativePi = 3.1415927410125732421875f;
	static constexpr float kNativeHalfPi = 1.57079637050628662109375f;

	class EmoteTressRigidRod {
	public:
		explicit EmoteTressRigidRod(const EmoteTressSideDefinition& definition) {
			if (definition.ParticleCount == 0 || definition.ParticleCount > kMaximumRigidParticles || definition.TrailingPoints > kMaximumRigidTrailingPoints || !std::isfinite(definition.Length) || !std::isfinite(definition.TrailingLength) || definition.Length < 0 || (definition.ParticleCount == 1 && definition.Length != 0)) {
				throw std::runtime_error("Emote rigid tress requires bounded actual points and finite nonnegative length");
			}
			m_Particles.resize(definition.ParticleCount);
			m_Trailing.resize(definition.TrailingPoints);
			m_SegmentLength = definition.Length == 0 ? 0 : definition.Length / static_cast<float>(definition.ParticleCount - 1);
			m_TrailingSpacing = definition.TrailingPoints == 0 ? 0 : definition.TrailingLength / static_cast<float>(definition.TrailingPoints);
			m_LocalCosine = std::cos(kNativeHalfPi);
			m_LocalSine = std::sin(kNativeHalfPi);
		}

		void SetPosture(const EmotePhysicalPosture& posture) {
			m_Posture = posture;
			m_Particles[0] = posture.Coordinate;
		}

		void Update() {
			m_Particles[0] = m_Posture.Coordinate;
			const auto x = m_LocalCosine * m_Posture.Cosine - m_Posture.Sine * m_LocalSine;
			const auto y = m_LocalCosine * m_Posture.Sine + m_LocalSine * m_Posture.Cosine;
			const auto step_x = m_SegmentLength * x;
			const auto step_y = m_SegmentLength * y;
			for (std::size_t index = 1; index != m_Particles.size(); ++index) {
				m_Particles[index] = {step_x + m_Particles[index - 1][0], step_y + m_Particles[index - 1][1], m_Particles[index - 1][2] + 0.0f};
			}
			auto position = m_Particles.back();
			const auto trailing_x = m_TrailingSpacing * x;
			const auto trailing_y = m_TrailingSpacing * y;
			for (auto& trailing : m_Trailing) {
				position = {trailing_x + position[0], trailing_y + position[1], position[2] + 0.0f};
				trailing = position;
			}
			for (const auto& points : {std::span<const std::array<float, 3>>(m_Particles), std::span<const std::array<float, 3>>(m_Trailing)}) {
				for (const auto& point : points) {
					for (const auto component : point) {
						if (!std::isfinite(component)) {
							throw std::runtime_error("Emote rigid tress update exceeds its finite physical domain");
						}
					}
				}
			}
		}

		[[nodiscard]] std::size_t PointCount() const noexcept {
			return m_Particles.size() + m_Trailing.size();
		}

		void WritePoints(std::span<std::array<float, 2>> destination) const {
			for (std::size_t index = 0; index != m_Particles.size(); ++index) {
				destination[index] = {m_Particles[index][0], m_Particles[index][1]};
			}
			for (std::size_t index = 0; index != m_Trailing.size(); ++index) {
				destination[m_Particles.size() + index] = {m_Trailing[index][0], m_Trailing[index][1]};
			}
		}

		void WriteSegmentLengths(std::span<float> destination) const {
			std::fill_n(destination.begin(), m_Particles.size() - 1, m_SegmentLength);
			std::fill(destination.begin() + static_cast<std::ptrdiff_t>(m_Particles.size() - 1), destination.end(), m_TrailingSpacing);
		}

	private:
		std::vector<std::array<float, 3>> m_Particles;
		std::vector<std::array<float, 3>> m_Trailing;
		EmotePhysicalPosture m_Posture;
		float m_SegmentLength = 0;
		float m_TrailingSpacing = 0;
		float m_LocalCosine = 0;
		float m_LocalSine = 1;
	};

	struct EmoteTressBranch {
		std::unique_ptr<EmoteSoftPendulum> Soft;
		std::unique_ptr<EmoteTressRigidRod> Rigid;
		std::size_t PointCount = 0;
		float Rotation = 0;
		float Cosine = 1;
		float Sine = 0;
	};

	static void RequirePosture(const EmotePhysicalPosture& posture) {
		for (const auto value : {posture.Coordinate[0], posture.Coordinate[1], posture.Coordinate[2], posture.Rotation, posture.Cosine, posture.Sine}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote tress requires its actual finite six-word posture");
			}
		}
	}

	static EmoteTressBranch CreateBranch(const EmoteTressDefinition& definition, const EmoteTressSideDefinition& side, bool first) {
		EmoteTressBranch branch;
		branch.Rotation = first ? definition.ReferenceRotation + kNativePi : definition.ReferenceRotation;
		branch.Cosine = std::cos(branch.Rotation);
		branch.Sine = std::sin(branch.Rotation);
		if (!std::isfinite(side.Mass) || side.Mass < 0) {
			throw std::runtime_error("Emote tress requires finite nonnegative actual side mass");
		}
		if (side.Mass == 0 && side.TrailingPoints == 0) {
			return branch;
		}
		const auto rigid = first ? definition.RigidSide == EmoteTressRigidSide::First : definition.RigidSide == EmoteTressRigidSide::Second;
		if (rigid) {
			branch.Rigid = std::make_unique<EmoteTressRigidRod>(side);
			branch.PointCount = branch.Rigid->PointCount();
			return branch;
		}
		auto acceleration_angle = definition.ReferenceRotation + definition.GravityRotation;
		acceleration_angle = first ? acceleration_angle - kNativeHalfPi : acceleration_angle + kNativeHalfPi;
		EmoteSoftPendulumDefinition physical;
		physical.Acceleration = {std::cos(acceleration_angle) * definition.Gravity, std::sin(acceleration_angle) * definition.Gravity, 0};
		physical.Mass = side.Mass;
		physical.Elastic = definition.Elastic;
		physical.RotationInheritance = definition.RotationInheritance;
		physical.Length = side.Length;
		physical.ParticleCount = side.ParticleCount;
		physical.ControlRadius = definition.ControlRadius;
		physical.CoordinateRotation = definition.CoordinateRotation;
		physical.CoordinateScale = definition.CoordinateScale;
		physical.ReferenceRotation = branch.Rotation;
		physical.MassTiltedDistribution = definition.MassTiltedDistribution;
		physical.AdhocSimulation = definition.AdhocSimulation;
		physical.CollisionHardness = definition.CollisionHardness;
		physical.CollisionRepulsion = definition.CollisionRepulsion;
		physical.TrailingLength = side.TrailingLength;
		physical.TrailingPoints = side.TrailingPoints;
		physical.FrictionBegin = definition.FrictionBegin;
		physical.FrictionEnd = definition.FrictionEnd;
		branch.Soft = std::make_unique<EmoteSoftPendulum>(physical);
		branch.PointCount = physical.ParticleCount + physical.TrailingPoints;
		return branch;
	}

	class EmoteTressModel::Implementation {
	public:
		explicit Implementation(const EmoteTressDefinition& definition) {
			for (const auto value : {definition.Gravity, definition.GravityRotation, definition.ReferenceRotation}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote tress requires finite actual gravity and rotation inputs");
				}
			}
			if (definition.RigidSide != EmoteTressRigidSide::None && definition.RigidSide != EmoteTressRigidSide::First && definition.RigidSide != EmoteTressRigidSide::Second) {
				throw std::runtime_error("Emote tress rigid-side mode is outside the recovered domain");
			}
			First = CreateBranch(definition, definition.First, true);
			Second = CreateBranch(definition, definition.Second, false);
			if (First.PointCount == 0 && Second.PointCount == 0) {
				throw std::runtime_error("Emote tress requires at least one actual physical branch");
			}
			Count = First.PointCount == 0 ? Second.PointCount : First.PointCount + (Second.PointCount == 0 ? 0 : Second.PointCount - 1);
		}

		void RequireIdle() const {
			if (Operating || !Valid) {
				throw std::runtime_error("Emote tress is active or invalid after a failed physical phase");
			}
		}

		void ApplyPosture(EmoteTressBranch& branch, const EmotePhysicalPosture& posture) {
			if (branch.PointCount == 0) {
				return;
			}
			auto composed = posture;
			composed.Rotation = branch.Rotation + posture.Rotation;
			composed.Cosine = branch.Cosine * posture.Cosine - branch.Sine * posture.Sine;
			composed.Sine = posture.Cosine * branch.Sine + branch.Cosine * posture.Sine;
			RequirePosture(composed);
			if (branch.Soft) {
				EmoteSoftPendulumPosture physical;
				physical.Coordinate = composed.Coordinate;
				physical.Rotation = composed.Rotation;
				branch.Soft->SetPosture(physical);
			} else {
				branch.Rigid->SetPosture(composed);
			}
		}

		EmoteTressBranch First;
		EmoteTressBranch Second;
		std::size_t Count = 0;
		bool PostureSet = false;
		bool Initialized = false;
		bool UnorderedStringComparison = false;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteTressModel::EmoteTressModel(const EmoteTressDefinition& definition)
	    : m_Implementation(std::make_unique<Implementation>(definition)) {
	}
	EmoteTressModel::~EmoteTressModel() noexcept = default;

	void EmoteTressModel::SetPosture(const EmotePhysicalPosture& posture) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		RequirePosture(posture);
		state.Operating = true;
		try {
			state.ApplyPosture(state.First, posture);
			state.ApplyPosture(state.Second, posture);
			state.PostureSet = true;
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}

	void EmoteTressModel::Initialize() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (!state.PostureSet) {
			throw std::runtime_error("Emote tress initialization requires the actual preceding posture");
		}
		state.Operating = true;
		try {
			for (auto* branch : {&state.First, &state.Second}) {
				if (branch->Soft) {
					branch->Soft->Initialize();
				} else if (branch->Rigid) {
					branch->Rigid->Update();
				}
			}
			state.Initialized = true;
			state.UnorderedStringComparison = false;
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}

	void EmoteTressModel::ClearForcesAndMeasureBounds() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (!state.Initialized) {
			throw std::runtime_error("Emote tress force reset requires its actual initialized branches");
		}
		for (auto* branch : {&state.First, &state.Second}) {
			if (branch->Soft) {
				branch->Soft->ClearForcesAndMeasureBounds();
			}
		}
	}

	void EmoteTressModel::AddForce(bool first_side, std::size_t particle, const std::array<float, 3>& force) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		auto& branch = first_side ? state.First : state.Second;
		if (!state.Initialized || !branch.Soft) {
			throw std::runtime_error("Emote tress force requires an actual initialized soft branch");
		}
		branch.Soft->AddForce(particle, force);
	}

	void EmoteTressModel::AttachCollider(EmoteCollider& collider) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		for (auto* branch : {&state.First, &state.Second}) {
			if (branch->Soft) {
				branch->Soft->AttachCollider(collider);
			}
		}
	}

	void EmoteTressModel::Step(float seconds) {
		StepImpl(seconds, false);
	}

	void EmoteTressModel::StepImpl(float seconds, bool unordered_comparison) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (!state.Initialized || !std::isfinite(seconds) || (unordered_comparison ? seconds != 0 : (!(seconds > 0) || state.UnorderedStringComparison))) {
			throw std::runtime_error("Emote tress requires initialized state and positive finite seconds");
		}
		state.Operating = true;
		try {
			for (auto* branch : {&state.First, &state.Second}) {
				if (branch->Soft) {
					if (unordered_comparison) {
						branch->Soft->StepForUnorderedStringComparison(seconds);
					} else {
						branch->Soft->Step(seconds);
					}
				} else if (branch->Rigid) {
					branch->Rigid->Update();
				}
			}
			state.UnorderedStringComparison = unordered_comparison;
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}

	std::size_t EmoteTressModel::PointCount() const noexcept {
		return m_Implementation->Count;
	}

	void EmoteTressModel::WritePoints(std::span<std::array<float, 2>> destination) const {
		WritePointsImpl(destination, false);
	}

	void EmoteTressModel::WritePointsImpl(std::span<std::array<float, 2>> destination, bool unordered_comparison) const {
		const auto& state = *m_Implementation;
		state.RequireIdle();
		if (state.UnorderedStringComparison && !unordered_comparison) {
			throw std::runtime_error("Emote String transient tress points require the private native comparison transaction");
		}
		if (!state.Initialized || destination.size() != state.Count) {
			throw std::runtime_error("Emote tress point output requires its exact actual initialized point count");
		}
		std::size_t offset = 0;
		if (state.First.PointCount != 0) {
			auto first = destination.first(state.First.PointCount);
			if (state.First.Soft) {
				state.First.Soft->WritePointsImpl(first, unordered_comparison);
			} else {
				state.First.Rigid->WritePoints(first);
			}
			std::reverse(first.begin(), first.end());
			offset = state.First.PointCount - 1;
		}
		if (state.Second.PointCount != 0) {
			auto second = destination.subspan(offset, state.Second.PointCount);
			if (state.Second.Soft) {
				state.Second.Soft->WritePointsImpl(second, unordered_comparison);
			} else {
				state.Second.Rigid->WritePoints(second);
			}
		}
	}

	void EmoteTressModel::WriteSegmentLengths(std::span<float> destination) const {
		const auto& state = *m_Implementation;
		state.RequireIdle();
		if (destination.size() != state.Count - 1) {
			throw std::runtime_error("Emote tress segment output requires its exact actual segment count");
		}
		std::size_t offset = 0;
		if (state.First.PointCount != 0) {
			auto first = destination.first(state.First.PointCount - 1);
			if (state.First.Soft) {
				state.First.Soft->WriteSegmentLengths(first);
			} else {
				state.First.Rigid->WriteSegmentLengths(first);
			}
			std::reverse(first.begin(), first.end());
			offset = first.size();
		}
		if (state.Second.PointCount != 0) {
			auto second = destination.subspan(offset, state.Second.PointCount - 1);
			if (state.Second.Soft) {
				state.Second.Soft->WriteSegmentLengths(second);
			} else {
				state.Second.Rigid->WriteSegmentLengths(second);
			}
		}
	}
} // namespace sgre::scene
