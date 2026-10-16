#include "sgre/scene/emote_catenary_physics.hpp"

#include "sgre/scene/emote_collider.hpp"
#include "sgre/scene/emote_mesh_fix_alignment.hpp"
#include "sgre/scene/motion_bezier_inverse.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::scene {
	static constexpr std::size_t kMaximumCatenaryParticles = 256;
	static constexpr std::uint32_t kMaximumCatenarySubsteps = 4096;
	static constexpr std::uint32_t kMaximumCatenaryDeformIterations = 4096;
	static constexpr float kMaximumCatenaryConstraintSamples = 16383;
	static constexpr std::size_t kMaximumCatenaryConstraintPasses = 1024 * 1024;

	static resource::MotionPoint CatenaryPoint(const resource::MotionPoint& point, const resource::MotionMeshMatrix& matrix) {
		return {(point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
	}

	static void CatenaryTransform(resource::MotionBezierLattice& lattice, const resource::MotionMeshMatrix& matrix) {
		for (auto& point : lattice.Controls) {
			point = CatenaryPoint(point, matrix);
		}
	}

	static resource::MotionMeshMatrix CatenaryWorld(const EmoteCatenaryDefinition& definition, const EmotePhysicalPosture& posture) {
		const auto x = -definition.Origin.X * definition.SizeHorizontal;
		const auto y = -definition.Origin.Y * definition.SizeVertical;
		resource::MotionMeshMatrix result;
		result.XX = definition.SizeHorizontal * posture.Cosine;
		result.XY = definition.SizeVertical * -posture.Sine;
		result.YX = definition.SizeHorizontal * posture.Sine;
		result.YY = definition.SizeVertical * posture.Cosine;
		result.TranslationX = (x * posture.Cosine + y * -posture.Sine) + posture.Coordinate[0];
		result.TranslationY = (y * posture.Cosine + x * posture.Sine) + posture.Coordinate[1];
		return result;
	}

	static resource::MotionMeshMatrix CatenaryInverse(const resource::MotionMeshMatrix& matrix) {
		const auto determinant = matrix.YY * matrix.XX - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote Catenary requires the actual finite invertible target posture");
		}
		const auto reciprocal = 1.0f / determinant;
		resource::MotionMeshMatrix result;
		result.XX = reciprocal * matrix.YY;
		result.XY = -matrix.XY * reciprocal;
		result.YX = -matrix.YX * reciprocal;
		result.YY = reciprocal * matrix.XX;
		result.TranslationX = (matrix.TranslationY * matrix.XY - matrix.TranslationX * matrix.YY) * reciprocal;
		result.TranslationY = (matrix.TranslationX * matrix.YX - matrix.TranslationY * matrix.XX) * reciprocal;
		return result;
	}

	static std::array<float, 4> MultiplyCatenaryQuaternions(const std::array<float, 4>& left, const std::array<float, 4>& right) {
		return {((left[3] * right[0] - left[2] * right[1]) + left[1] * right[2]) + left[0] * right[3], ((right[0] * left[2] + left[3] * right[1]) - right[2] * left[0]) + left[1] * right[3], ((right[1] * left[0] - right[0] * left[1]) + left[3] * right[2]) + left[2] * right[3], ((-left[0] * right[0] - right[1] * left[1]) - left[2] * right[2]) + left[3] * right[3]};
	}
	static std::array<float, 3> CatenaryAcceleration(float gravity, float angle) {
		const auto half = angle * 0.5f;
		const auto sine = std::sin(half);
		const std::array<float, 4> rotation{sine * 0.0f, sine * 0.0f, sine, std::cos(half)};
		const auto product = MultiplyCatenaryQuaternions(rotation, {gravity, 0, 0, 0});
		const std::array<float, 4> conjugate{-rotation[0], -rotation[1], -rotation[2], rotation[3]};
		return {((conjugate[0] * product[3] - conjugate[1] * product[2]) + conjugate[2] * product[1]) + conjugate[3] * product[0], ((conjugate[1] * product[3] + conjugate[0] * product[2]) - conjugate[2] * product[0]) + conjugate[3] * product[1], ((conjugate[1] * product[0] - conjugate[0] * product[1]) + conjugate[2] * product[3]) + conjugate[3] * product[2]};
	}
	static void RequireCatenaryPosture(const EmotePhysicalPosture& posture) {
		for (const auto value : {posture.Coordinate[0], posture.Coordinate[1], posture.Coordinate[2], posture.Rotation, posture.Cosine, posture.Sine}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Catenary requires its actual finite six-word posture");
			}
		}
	}

	class CatenarySpline {
	public:
		explicit CatenarySpline(std::size_t count)
		    : Knots(count), Intervals(count - 1), Diagonal(count), Points(count), Right(count), Second(count) {
			for (std::size_t index = 0; index != count; ++index) {
				Knots[index] = static_cast<float>(index) / static_cast<float>(count - 1);
			}
			for (std::size_t index = 0; index + 1 < count; ++index) {
				Intervals[index] = Knots[index + 1] - Knots[index];
			}
			for (std::size_t index = 1; index + 1 < count; ++index) {
				const auto span = Knots[index + 1] - Knots[index - 1];
				Diagonal[index] = span + span;
			}
			for (std::size_t index = 1; index + 2 < count; ++index) {
				Diagonal[index + 1] -= (Intervals[index] * Intervals[index]) / Diagonal[index];
			}
		}
		void Prepare(std::span<const std::array<float, 2>> points) {
			if (points.size() != Points.size()) {
				throw std::runtime_error("Emote Catenary spline requires its exact actual particle extent");
			}
			std::copy(points.begin(), points.end(), Points.begin());
			const auto count = Points.size();
			for (std::size_t index = 1; index + 1 < count; ++index) {
				for (std::size_t axis = 0; axis != 2; ++axis) {
					Right[index][axis] = ((Points[index + 1][axis] - Points[index][axis]) / Intervals[index] - (Points[index][axis] - Points[index - 1][axis]) / Intervals[index - 1]) * 6.0f;
				}
			}
			for (std::size_t index = 1; index + 2 < count; ++index) {
				for (std::size_t axis = 0; axis != 2; ++axis) {
					Right[index + 1][axis] -= (Intervals[index] * Right[index][axis]) / Diagonal[index];
				}
			}
			for (std::size_t index = count - 2; index != 0; --index) {
				for (std::size_t axis = 0; axis != 2; ++axis) {
					Second[index][axis] = (Right[index][axis] - Intervals[index] * Second[index + 1][axis]) / Diagonal[index];
				}
			}
		}
		resource::MotionPoint Sample(float position) const {
			if (position <= Knots.front()) {
				return {Points.front()[0], Points.front()[1]};
			}
			if (!(position < Knots.back())) {
				return {Points.back()[0], Points.back()[1]};
			}
			std::size_t interval = 0;
			while (position > Knots[interval + 1]) {
				++interval;
			}
			const auto length = Knots[interval + 1] - Knots[interval];
			const auto right = (position - Knots[interval]) / length;
			const auto left = 1.0f - right;
			const auto left_cubic = (left * left) * left - left;
			const auto right_cubic = (right * right) * right - right;
			const auto x = right_cubic * Second[interval + 1][0] + left_cubic * Second[interval][0];
			const auto y = right_cubic * Second[interval + 1][1] + left_cubic * Second[interval][1];
			return {((x * length) * length) / 6.0f + (right * Points[interval + 1][0] + left * Points[interval][0]), ((y * length) * length) / 6.0f + (left * Points[interval][1] + right * Points[interval + 1][1])};
		}

	private:
		std::vector<float> Knots;
		std::vector<float> Intervals;
		std::vector<float> Diagonal;
		std::vector<std::array<float, 2>> Points;
		std::vector<std::array<float, 2>> Right;
		std::vector<std::array<float, 2>> Second;
	};

	struct CatenaryConstraint {
		float Priority = 0;
		float Position = 0;
		resource::MotionPoint Target;
		std::array<float, 20> Weights{};
	};

	static bool CatenarySettled(const EmoteSoftPendulum& model) {
		const auto maximum = std::bit_cast<double>(std::uint64_t{0x3e45798ee2308c3a});
		for (const auto& particle : model.Particles()) {
			if (particle.Fixed) {
				continue;
			}
			const auto x = particle.Previous[0] - particle.Position[0];
			const auto y = particle.Previous[1] - particle.Position[1];
			const auto z = particle.Previous[2] - particle.Position[2];
			if ((y * y + x * x) + z * z > maximum) {
				return false;
			}
		}
		return true;
	}

	class EmoteCatenaryPhysics::Implementation {
	public:
		explicit Implementation(EmoteCatenaryDefinition definition)
		    : Inputs(std::move(definition)), Spline(CheckedCount()), Points(Inputs.ParticleCount), FixAlignment(Inputs.Fixes, Inputs.SizeHorizontal, Inputs.SizeVertical) {
			RequireDefinition();
			EmoteSoftPendulumDefinition model;
			model.FixedTip = true;
			model.Acceleration = CatenaryAcceleration(Inputs.Gravity, Inputs.GravityRotation);
			model.Mass = Inputs.Mass;
			model.Length = Inputs.Length;
			model.Elastic = Inputs.Elastic;
			model.RotationInheritance = Inputs.Straightness;
			model.ParticleCount = Inputs.ParticleCount;
			model.ControlRadius = Inputs.ControlRadius;
			model.CoordinateRotation = Inputs.DefaultRotation;
			model.CoordinateScale = Inputs.CoordinateScale;
			model.FrictionBegin = Inputs.FrictionTop;
			model.FrictionEnd = Inputs.FrictionBottom;
			model.BottomRatio = Inputs.BottomRatio;
			model.CollisionHardness = Inputs.CollisionHardness;
			model.CollisionRepulsion = Inputs.CollisionRepulsion;
			model.AdhocSimulation = Inputs.AdhocSimulation;
			model.MassTiltedDistribution = Inputs.MassTiltedDistribution;
			Model = std::make_unique<EmoteSoftPendulum>(model);
			Degree = Inputs.SizeHorizontal > Inputs.SizeVertical ? 0x34 : 0x43;
			Bare = IdentityMotionBezierLattice(Degree);
			Output = Bare;
			PrepareConstraints();
		}
		std::size_t CheckedCount() const {
			if (Inputs.ParticleCount < 3 || Inputs.ParticleCount > kMaximumCatenaryParticles) {
				throw std::runtime_error("Emote Catenary requires bounded actual two-anchor particles");
			}
			return Inputs.ParticleCount;
		}
		void RequireDefinition() const {
			if (Inputs.Substeps == 0 || Inputs.Substeps > kMaximumCatenarySubsteps || Inputs.DeformIterations > kMaximumCatenaryDeformIterations || Inputs.Speed < 0 || !(Inputs.Mass > 0) || !(Inputs.Length > 0) || !(Inputs.SizeHorizontal > 0) || !(Inputs.SizeVertical > 0) || (!Inputs.RestPoints.empty() && Inputs.RestPoints.size() != Inputs.ParticleCount)) {
				throw std::runtime_error("Emote Catenary requires bounded actual dimensions/mass/substeps/rest points");
			}
			for (const auto value : {Inputs.Speed, Inputs.Gravity, Inputs.GravityRotation, Inputs.DefaultRotation, Inputs.Mass, Inputs.Straightness, Inputs.SizeHorizontal, Inputs.SizeVertical, Inputs.Length, Inputs.Origin.X, Inputs.Origin.Y, Inputs.ControlRadius, Inputs.FrictionTop, Inputs.FrictionBottom, Inputs.BottomRatio, Inputs.Elastic, Inputs.CollisionRepulsion}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote Catenary requires finite actual definition values");
				}
			}
		}
		void RequireIdle() const {
			if (Thread != std::this_thread::get_id() || Operating || !Valid) {
				throw std::runtime_error("Emote Catenary requires its valid serialized idle actual entity");
			}
		}
		void PrepareConstraints() {
			if (Inputs.RestPoints.empty()) {
				return;
			}
			for (std::size_t index = 0; index != Points.size(); ++index) {
				Points[index] = {Inputs.RestPoints[index].X * Inputs.SizeHorizontal, Inputs.RestPoints[index].Y * Inputs.SizeVertical};
				if (!std::isfinite(Points[index][0]) || !std::isfinite(Points[index][1])) {
					throw std::runtime_error("Emote Catenary rest points exceed finite physical coordinates");
				}
			}
			Spline.Prepare(Points);
			const auto horizontal = static_cast<std::uint32_t>(Degree & 15);
			const auto vertical = static_cast<std::uint32_t>(Degree >> 4);
			const auto spacing = std::min(Inputs.SizeVertical / static_cast<float>(vertical), Inputs.SizeHorizontal / static_cast<float>(horizontal)) * 0.5f;
			const auto raw = std::ceil(Inputs.Length / spacing);
			if (!std::isfinite(raw) || raw > kMaximumCatenaryConstraintSamples) {
				throw std::runtime_error("Emote Catenary constraint sampling exceeds bounded actual work");
			}
			auto count = static_cast<std::uint32_t>(std::max(raw, 3.0f));
			if ((count & 1) == 0) {
				++count;
			}
			Constraints.reserve(count);
			const auto reciprocal = 1.0f / (Inputs.SizeHorizontal * Inputs.SizeVertical);
			const auto x_scale = Inputs.SizeVertical * reciprocal;
			const auto y_scale = reciprocal * Inputs.SizeHorizontal;
			for (std::uint32_t index = 0; index != count; ++index) {
				const auto position = static_cast<float>(index) / static_cast<float>(count - 1);
				const auto physical = Spline.Sample(position);
				const resource::MotionPoint point{physical.X * x_scale, physical.Y * y_scale};
				if (point.X < 0 || point.X >= 1 || point.Y < 0 || point.Y >= 1) {
					continue;
				}
				CatenaryConstraint constraint;
				constraint.Priority = static_cast<float>(std::abs(static_cast<std::int32_t>(count / 2) - static_cast<std::int32_t>(index)));
				constraint.Position = position;
				constraint.Target = point;
				const auto x = MotionBezierBasisWeights(horizontal, point.X);
				const auto y = MotionBezierBasisWeights(vertical, point.Y);
				for (std::uint32_t row = 0; row <= vertical; ++row) {
					for (std::uint32_t column = 0; column <= horizontal; ++column) {
						constraint.Weights[static_cast<std::size_t>(row) * (horizontal + 1) + column] = x[column] * y[row];
					}
				}
				Constraints.push_back(constraint);
			}
			if (Inputs.DeformIterations != 0 && Constraints.size() > kMaximumCatenaryConstraintPasses / Inputs.DeformIterations) {
				throw std::runtime_error("Emote Catenary constraint projection exceeds bounded actual frame work");
			}
			std::stable_sort(Constraints.begin(), Constraints.end(), [](const auto& first, const auto& last) {
				return std::bit_cast<std::uint32_t>(first.Priority) < std::bit_cast<std::uint32_t>(last.Priority);
			});
		}
		void ApplyPosture(const EmotePhysicalPosture& posture, const std::array<resource::MotionPoint, 2>& targets) {
			EmoteSoftPendulumPosture value;
			value.Coordinate = posture.Coordinate;
			value.Rotation = posture.Rotation;
			Model->SetPosture(value);
			Model->SetFixedEnds({targets[0].X, targets[0].Y}, {targets[1].X, targets[1].Y});
		}
		void Initialize() {
			RequireIdle();
			if (!HasPosture || !HasTargets) {
				throw std::runtime_error("Emote Catenary reset requires actual posture and both world fixes");
			}
			ApplyPosture(Requested, Targets);
			Model->Initialize();
			Previous = Requested;
			PreviousTargets = Targets;
			Initialized = true;
			Dirty = true;
			Warmup = true;
		}
		void Update(float elapsed) {
			RequireIdle();
			if (!Initialized) {
				throw std::runtime_error("Emote Catenary update requires its actual preceding reset");
			}
			const auto ticks = elapsed * Inputs.Speed;
			if (!std::isfinite(ticks) || !(ticks > 0) || ticks > static_cast<float>(kMaximumCatenarySubsteps) * 1.5f) {
				throw std::runtime_error("Emote Catenary physical step requires bounded positive actual scaled ticks");
			}
			Operating = true;
			try {
				if (Warmup) {
					for (std::size_t iteration = 0; iteration != 600; ++iteration) {
						Model->Step(1.0f / 60.0f);
						if (CatenarySettled(*Model)) {
							break;
						}
					}
					const auto seconds = (Inputs.Speed / 60.0f) / static_cast<float>(Inputs.Substeps);
					for (std::size_t iteration = 0; iteration != 600; ++iteration) {
						Model->Step(seconds);
						if (CatenarySettled(*Model)) {
							break;
						}
					}
					Warmup = false;
				} else {
					const auto count = std::max(Inputs.Substeps, static_cast<std::uint32_t>(std::ceil(ticks / 1.5f)));
					const auto seconds = (ticks / 60.0f) / static_cast<float>(count);
					for (auto* collider : Colliders) {
						collider->PrepareSubsteps(count);
					}
					if (count == 1) {
						for (auto* collider : Colliders) {
							collider->AdvanceSubstep();
						}
						ApplyPosture(Requested, Targets);
						Model->Step(seconds);
					} else {
						const auto steps = static_cast<float>(count);
						const auto rotation = (Requested.Rotation - Previous.Rotation) / steps;
						const auto sine = std::sin(rotation);
						const auto cosine = std::cos(rotation);
						std::array<float, 3> delta{};
						for (std::size_t axis = 0; axis != 3; ++axis) {
							delta[axis] = (Requested.Coordinate[axis] - Previous.Coordinate[axis]) / steps;
						}
						std::array<resource::MotionPoint, 2> fix_delta{};
						for (std::size_t index = 0; index != 2; ++index) {
							fix_delta[index] = {(Targets[index].X - PreviousTargets[index].X) / steps, (Targets[index].Y - PreviousTargets[index].Y) / steps};
						}
						for (std::uint32_t step = 0; step != count; ++step) {
							for (auto* collider : Colliders) {
								collider->AdvanceSubstep();
							}
							for (std::size_t axis = 0; axis != 3; ++axis) {
								Previous.Coordinate[axis] += delta[axis];
							}
							Previous.Rotation += rotation;
							const auto next_sine = sine * Previous.Cosine + cosine * Previous.Sine;
							Previous.Cosine = cosine * Previous.Cosine - sine * Previous.Sine;
							Previous.Sine = next_sine;
							for (std::size_t index = 0; index != 2; ++index) {
								PreviousTargets[index].X += fix_delta[index].X;
								PreviousTargets[index].Y += fix_delta[index].Y;
							}
							ApplyPosture(Previous, PreviousTargets);
							Model->Step(seconds);
						}
					}
					Previous = Requested;
					PreviousTargets = Targets;
				}
				Dirty = true;
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}
		void PrepareLattice() {
			RequireIdle();
			if (!Initialized) {
				throw std::runtime_error("Emote Catenary output requires its actual initialized physical owner");
			}
			if (Dirty) {
				for (std::size_t row = 0; row != static_cast<std::size_t>((Degree >> 4) + 1); ++row) {
					for (std::size_t column = 0; column != static_cast<std::size_t>((Degree & 15) + 1); ++column) {
						Bare.Controls[row * static_cast<std::size_t>((Degree & 15) + 1) + column] = {static_cast<float>(column) / static_cast<float>(Degree & 15), static_cast<float>(row) / static_cast<float>(Degree >> 4)};
					}
				}
				ConvertMotionBezierSamplesToControls(Bare.Controls, Degree);
				Model->WritePoints(Points);
				Spline.Prepare(Points);
				for (auto& constraint : Constraints) {
					constraint.Target = CatenaryPoint(Spline.Sample(constraint.Position), Inverse);
				}
				for (std::uint32_t iteration = 0; iteration != Inputs.DeformIterations && !Constraints.empty(); ++iteration) {
					const auto ratio = Inputs.DeformIterations == 1 ? 1.0f : static_cast<float>(iteration) / static_cast<float>(Inputs.DeformIterations - 1);
					const auto relaxation = 1.1f - ratio * 0.1f;
					for (const auto& constraint : Constraints) {
						float x = 0, y = 0;
						for (std::size_t index = 0; index != 20; ++index) {
							x += constraint.Weights[index] * Bare.Controls[index].X;
							y += constraint.Weights[index] * Bare.Controls[index].Y;
						}
						const auto dx = (constraint.Target.X - x) * relaxation;
						const auto dy = (constraint.Target.Y - y) * relaxation;
						for (std::size_t index = 0; index != 20; ++index) {
							Bare.Controls[index].X = constraint.Weights[index] * dx + Bare.Controls[index].X;
							Bare.Controls[index].Y = constraint.Weights[index] * dy + Bare.Controls[index].Y;
						}
					}
				}
				Dirty = false;
			}
			std::copy(Bare.Controls.begin(), Bare.Controls.end(), Output.Controls.begin());
			CatenaryTransform(Output, World);
			if (Inputs.CorrectScale) {
				for (auto& point : Output.Controls) {
					point.X = (point.X - Previous.Coordinate[0]) * CorrectionScale + Previous.Coordinate[0];
					point.Y = (point.Y - Previous.Coordinate[1]) * CorrectionScale + Previous.Coordinate[1];
				}
			}
			FixAlignment.Normalize(Output, Previous, Inverse);
			if (Inputs.CorrectFix) {
				auto corrected = Previous;
				corrected.Rotation = CorrectionAngle;
				corrected.Cosine = std::cos(CorrectionAngle);
				corrected.Sine = std::sin(CorrectionAngle);
				FixAlignment.Correct(Output, RawTargets, CatenaryInverse(CatenaryWorld(Inputs, corrected)));
			}
			ValidateMotionBezierLattice(Output);
		}
		EmoteCatenaryDefinition Inputs;
		CatenarySpline Spline;
		std::vector<std::array<float, 2>> Points;
		EmoteMeshFixAlignment FixAlignment;
		std::unique_ptr<EmoteSoftPendulum> Model;
		std::vector<EmoteCollider*> Colliders;
		std::vector<CatenaryConstraint> Constraints;
		resource::MotionBezierLattice Bare;
		resource::MotionBezierLattice Output;
		EmotePhysicalPosture Requested;
		EmotePhysicalPosture Previous;
		std::array<resource::MotionPoint, 2> RawTargets{};
		std::array<resource::MotionPoint, 2> Targets{};
		std::array<resource::MotionPoint, 2> PreviousTargets{};
		resource::MotionMeshMatrix World;
		resource::MotionMeshMatrix Inverse;
		float CorrectionAngle = 0;
		float CorrectionScale = 1;
		std::uint8_t Degree = 0;
		const std::thread::id Thread = std::this_thread::get_id();
		bool HasPosture = false;
		bool HasTargets = false;
		bool Initialized = false;
		bool Dirty = true;
		bool Warmup = false;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteCatenaryPhysics::EmoteCatenaryPhysics(EmoteCatenaryDefinition definition)
	    : m_Implementation(std::make_unique<Implementation>(std::move(definition))) {
	}
	EmoteCatenaryPhysics::~EmoteCatenaryPhysics() noexcept = default;
	void EmoteCatenaryPhysics::SetPosture(const EmotePhysicalPosture& posture) {
		auto& owner = *m_Implementation;
		owner.RequireIdle();
		RequireCatenaryPosture(posture);
		const auto world = CatenaryWorld(owner.Inputs, posture);
		const auto inverse = CatenaryInverse(world);
		owner.Requested = posture;
		owner.World = world;
		owner.Inverse = inverse;
		owner.HasPosture = true;
	}
	void EmoteCatenaryPhysics::SetCorrection(float angle, float scale) {
		auto& owner = *m_Implementation;
		owner.RequireIdle();
		if (!std::isfinite(angle) || !std::isfinite(scale) || !(scale > 0)) {
			throw std::runtime_error("Emote Catenary requires finite correction angle and positive actual scale");
		}
		owner.CorrectionAngle = angle;
		owner.CorrectionScale = scale;
	}
	void EmoteCatenaryPhysics::SetWorldFixes(std::span<const resource::MotionPoint> targets) {
		auto& owner = *m_Implementation;
		owner.RequireIdle();
		if (!owner.HasPosture || targets.size() != 2) {
			throw std::runtime_error("Emote Catenary fixes require actual posture and both actual world targets");
		}
		const auto reciprocal = 1.0f / owner.CorrectionScale;
		std::array<resource::MotionPoint, 2> scaled;
		for (std::size_t index = 0; index != 2; ++index) {
			if (!std::isfinite(targets[index].X) || !std::isfinite(targets[index].Y)) {
				throw std::runtime_error("Emote Catenary world fixes must be finite");
			}
			scaled[index] = {(targets[index].X - owner.Requested.Coordinate[0]) * reciprocal + owner.Requested.Coordinate[0], (targets[index].Y - owner.Requested.Coordinate[1]) * reciprocal + owner.Requested.Coordinate[1]};
			if (!std::isfinite(scaled[index].X) || !std::isfinite(scaled[index].Y)) {
				throw std::runtime_error("Emote Catenary scaled world fixes exceed finite physical coordinates");
			}
		}
		std::copy(targets.begin(), targets.end(), owner.RawTargets.begin());
		owner.Targets = scaled;
		owner.HasTargets = true;
	}
	void EmoteCatenaryPhysics::Initialize() {
		m_Implementation->Initialize();
	}
	void EmoteCatenaryPhysics::ClearForcesAndMeasureBounds() {
		auto& owner = *m_Implementation;
		owner.RequireIdle();
		owner.Model->ClearForcesAndMeasureBounds();
	}
	void EmoteCatenaryPhysics::AddForce(std::size_t particle, const std::array<float, 3>& force) {
		auto& owner = *m_Implementation;
		owner.RequireIdle();
		owner.Model->AddForce(particle, force);
	}
	void EmoteCatenaryPhysics::AttachCollider(EmoteCollider& collider) {
		auto& owner = *m_Implementation;
		owner.RequireIdle();
		if (owner.Colliders.size() >= 4096) {
			throw std::runtime_error("Emote Catenary collider attachment exceeds bounded actual group storage");
		}
		owner.Colliders.reserve(owner.Colliders.size() + 1);
		owner.Model->AttachCollider(collider);
		owner.Colliders.push_back(&collider);
	}
	void EmoteCatenaryPhysics::Update(float elapsed_ticks) {
		m_Implementation->Update(elapsed_ticks);
	}
	const resource::MotionBezierLattice& EmoteCatenaryPhysics::PrepareLattice() {
		auto& owner = *m_Implementation;
		owner.RequireIdle();
		if (!owner.Initialized) {
			throw std::runtime_error("Emote Catenary output requires its actual initialized physical owner");
		}
		try {
			owner.PrepareLattice();
		} catch (...) {
			owner.Valid = false;
			throw;
		}
		return owner.Output;
	}
	const EmoteCatenaryDefinition& EmoteCatenaryPhysics::Definition() const noexcept {
		return m_Implementation->Inputs;
	}
	std::span<const EmoteSoftPendulumParticle> EmoteCatenaryPhysics::Particles() const noexcept {
		return m_Implementation->Model->Particles();
	}
} // namespace sgre::scene
