#include "sgre/scene/emote_jelly_physics.hpp"

#include "sgre/scene/emote_physical_math.hpp"

#include "motion_native_compared_mesh.hpp"
#include "sgre/scene/emote_bobble_sections.hpp"
#include "sgre/scene/emote_collider.hpp"
#include "sgre/scene/emote_jelly_profile.hpp"
#include "sgre/scene/emote_jelly_sample_fit.hpp"
#include "sgre/scene/emote_jelly_section_rings.hpp"
#include "sgre/scene/emote_jelly_surface_bindings.hpp"
#include "sgre/scene/emote_mesh_fix_alignment.hpp"
#include "sgre/scene/motion_bezier_inverse.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	static std::array<float, 4> MultiplyJellyQuaternions(const std::array<float, 4>& left, const std::array<float, 4>& right) {
		// Native3039c0's separate products and left-to-right reductions.
		return {((left[3] * right[0] - left[2] * right[1]) + left[1] * right[2]) + left[0] * right[3], ((right[0] * left[2] + left[3] * right[1]) - right[2] * left[0]) + left[1] * right[3], ((right[1] * left[0] - right[0] * left[1]) + left[3] * right[2]) + left[2] * right[3], ((-left[0] * right[0] - right[1] * left[1]) - left[2] * right[2]) + left[3] * right[3]};
	}

	static std::array<float, 3> RotateJellyVector(const std::array<float, 4>& rotation, const std::array<float, 3>& vector) {
		const auto product = MultiplyJellyQuaternions(rotation, {vector[0], vector[1], vector[2], 0});
		const std::array<float, 4> conjugate{-rotation[0], -rotation[1], -rotation[2], rotation[3]};
		// Native3037e0 and inline3265d7 use this operand order for the second product.
		return {((conjugate[0] * product[3] - conjugate[1] * product[2]) + conjugate[2] * product[1]) + conjugate[3] * product[0], ((conjugate[1] * product[3] + conjugate[0] * product[2]) - conjugate[2] * product[0]) + conjugate[3] * product[1], ((conjugate[1] * product[0] - conjugate[0] * product[1]) + conjugate[2] * product[3]) + conjugate[3] * product[2]};
	}

	static constexpr float kJellyRingPi = std::bit_cast<float>(std::uint32_t{0x40490fdb});

	static resource::MotionMeshMatrix ComposeRingMatrices(const resource::MotionMeshMatrix& outer, const resource::MotionMeshMatrix& inner) {
		resource::MotionMeshMatrix result;
		result.XX = outer.XY * inner.YX + outer.XX * inner.XX;
		result.XY = outer.XY * inner.YY + outer.XX * inner.XY;
		result.YX = outer.YY * inner.YX + outer.YX * inner.XX;
		result.YY = outer.YY * inner.YY + outer.YX * inner.XY;
		result.TranslationX = (inner.TranslationY * outer.XY + inner.TranslationX * outer.XX) + outer.TranslationX;
		result.TranslationY = (inner.TranslationY * outer.YY + inner.TranslationX * outer.YX) + outer.TranslationY;

		return result;
	}

	static resource::MotionMeshMatrix InitialJellyRingMatrix(const EmoteBobbleSection& section, const std::array<float, 2>& offset) {
		if (section.Radius[0] == 0 || section.Radius[1] == 0) {
			throw std::runtime_error("Emote Jelly ring requires a finite invertible actual cross-section");
		}

		const float sine = PrepareEmotePhysicalSine(-section.Angle);
		const float cosine = PrepareEmotePhysicalCosine(-section.Angle);
		resource::MotionMeshMatrix translation;
		translation.TranslationX = -section.Center[0];
		translation.TranslationY = -section.Center[1];
		resource::MotionMeshMatrix rotation;
		rotation.XX = cosine;
		rotation.XY = -sine;
		rotation.YX = sine;
		rotation.YY = cosine;
		resource::MotionMeshMatrix scale;
		scale.XX = 1.0f / section.Radius[0];
		scale.YY = 1.0f / section.Radius[1];
		const auto normalization = ComposeRingMatrices(scale, ComposeRingMatrices(rotation, translation));
		const float determinant = normalization.YY * normalization.XX - normalization.YX * normalization.XY;
		if (determinant == 0 || !std::isfinite(determinant)) {
			throw std::runtime_error("Emote Jelly section normalization exceeded its finite native inverse");
		}

		const float inverse_determinant = 1.0f / determinant;
		resource::MotionMeshMatrix inverse;
		inverse.XX = inverse_determinant * normalization.YY;
		inverse.XY = -normalization.XY * inverse_determinant;
		inverse.YX = -normalization.YX * inverse_determinant;
		inverse.YY = inverse_determinant * normalization.XX;
		inverse.TranslationX = (normalization.XY * normalization.TranslationY - normalization.YY * normalization.TranslationX) * inverse_determinant;
		inverse.TranslationY = (normalization.YX * normalization.TranslationX - normalization.XX * normalization.TranslationY) * inverse_determinant;
		resource::MotionMeshMatrix relative;
		relative.XX = PrepareEmotePhysicalCosine(section.RelativeAngle);
		relative.XY = -PrepareEmotePhysicalSine(section.RelativeAngle);
		relative.YX = -relative.XY;
		relative.YY = relative.XX;
		auto output = ComposeRingMatrices(inverse, relative);
		output.TranslationX = output.TranslationX - offset[0];
		output.TranslationY = output.TranslationY - offset[1];

		return output;
	}

	struct JellyTressState {
		EmoteJellyProfile Profile;
		EmoteBobbleDefinition Physical;
		EmoteBobbleSectionDefinition Sections;
		resource::MotionPoint Anchor;
		std::vector<resource::MotionPoint> Initial;
		std::vector<EmoteJellyRingSample> Bindings;
		std::size_t TargetOffset = 0;
		EmotePhysicalPosture Previous;
		EmotePhysicalPosture Current;
		std::array<float, 3> StepDelta{};
		float StepRotation = 0;
		float StepCosine = 1;
		float StepSine = 0;
		std::unique_ptr<EmoteBobbleModel> Model;
		std::unique_ptr<EmoteJellySectionRings> Rings;
	};

	static resource::MotionMeshMatrix JellyWorldMatrix(const EmoteJellyDefinition& definition, const resource::MotionPoint& origin, const EmotePhysicalPosture& posture) {
		const float origin_x = -origin.X * definition.SizeHorizontal;
		const float origin_y = -origin.Y * definition.SizeVertical;
		resource::MotionMeshMatrix matrix;
		matrix.XX = definition.SizeHorizontal * posture.Cosine;
		matrix.XY = definition.SizeVertical * -posture.Sine;
		matrix.YX = definition.SizeHorizontal * posture.Sine;
		matrix.YY = definition.SizeVertical * posture.Cosine;
		matrix.TranslationX = (origin_x * posture.Cosine + origin_y * -posture.Sine) + posture.Coordinate[0];
		matrix.TranslationY = (origin_y * posture.Cosine + origin_x * posture.Sine) + posture.Coordinate[1];

		return matrix;
	}

	static resource::MotionMeshMatrix InverseJellyWorldMatrix(const resource::MotionMeshMatrix& matrix) {
		const float determinant = matrix.YY * matrix.XX - matrix.YX * matrix.XY;
		if (determinant == 0 || !std::isfinite(determinant)) {
			throw std::runtime_error("Emote Jelly output requires its actual finite invertible world matrix");
		}

		const float reciprocal = 1.0f / determinant;
		resource::MotionMeshMatrix result;
		result.XX = reciprocal * matrix.YY;
		result.XY = -matrix.XY * reciprocal;
		result.YX = -matrix.YX * reciprocal;
		result.YY = reciprocal * matrix.XX;
		result.TranslationX = (matrix.XY * matrix.TranslationY - matrix.YY * matrix.TranslationX) * reciprocal;
		result.TranslationY = (matrix.YX * matrix.TranslationX - matrix.XX * matrix.TranslationY) * reciprocal;

		return result;
	}

	static void TransformJellyLattice(resource::MotionBezierLattice& lattice, const resource::MotionMeshMatrix& matrix) {
		for (auto& point : lattice.Controls) {
			point = {(point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
		}
	}

	static void RequireJellyPosture(const EmotePhysicalPosture& posture) {
		for (const auto value : {posture.Coordinate[0], posture.Coordinate[1], posture.Coordinate[2], posture.Rotation, posture.Cosine, posture.Sine}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Jelly posture requires its six actual finite physical fields");
			}
		}
	}

	static void SetJellyModelPosture(JellyTressState& state, const EmotePhysicalPosture& posture) {
		EmoteBobblePosture physical;
		physical.Coordinate = posture.Coordinate;
		physical.Rotation = posture.Rotation;
		state.Model->SetPosture(physical, posture.Cosine, posture.Sine);
	}

	static std::array<float, 3> InitialJellyTangent(const std::array<float, 3>& normal) {
		std::array<float, 3> axis{-normal[1], normal[0], normal[1] * 0.0f - normal[0] * 0.0f};
		if (axis[0] == 0 && axis[1] == 0 && axis[2] == 0) {
			return {1, 0, 0};
		}

		const float inverse = 1.0f / std::sqrt((axis[1] * axis[1] + axis[0] * axis[0]) + axis[2] * axis[2]);
		const float sine = PrepareEmotePhysicalSine(std::bit_cast<float>(std::uint32_t{0x3f490fdb}));
		const std::array<float, 4> rotation{(inverse * axis[0]) * sine, (axis[1] * inverse) * sine, (axis[2] * inverse) * sine, PrepareEmotePhysicalCosine(std::bit_cast<float>(std::uint32_t{0x3f490fdb}))};

		return RotateJellyVector(rotation, normal);
	}

	static std::array<float, 3> InitialJellyDirection(const std::array<float, 3>& normal, const std::array<float, 3>& tip) {
		const float length = std::sqrt(tip[0] * tip[0] + tip[1] * tip[1]);
		const float axis_x = length <= 0 ? 1.0f : -(tip[1] / length);
		const float axis_y = length <= 0 ? 0.0f : tip[0] / length;
		const float half_angle = PrepareEmotePhysicalArcCosine(tip[2]) * 0.5f;
		const float sine = PrepareEmotePhysicalSine(half_angle);
		const std::array<float, 4> rotation{sine * axis_x, sine * axis_y, sine * 0.0f, PrepareEmotePhysicalCosine(half_angle)};

		return RotateJellyVector(rotation, normal);
	}

	static std::vector<resource::MotionPoint> InitialJellyRings(const JellyTressState& state) {
		const auto count = state.Profile.Radii.size();
		std::vector<resource::MotionPoint> result((count + 1) * 16);
		const float angle = PrepareEmotePhysicalAngle(state.Sections.Normal[1], state.Sections.Normal[0]);
		const float relative = PrepareEmotePhysicalAngle(state.Sections.Tangent[1], state.Sections.Tangent[0]) - angle;
		for (std::size_t index = 0; index < count; ++index) {
			EmoteBobbleSection section;
			const float fraction = static_cast<float>(index) / static_cast<float>(count - 1);
			section.Center = {(state.Physical.Direction[0] * state.Profile.Length) * fraction + state.Anchor.X, (state.Physical.Direction[1] * state.Profile.Length) * fraction + state.Anchor.Y};
			section.Radius = {state.Sections.Normal[2] * state.Profile.Radii[index], state.Profile.Radii[index]};
			section.Angle = angle;
			section.RelativeAngle = relative;
			const auto matrix = InitialJellyRingMatrix(section, {});
			for (std::size_t direction = 0; direction < 16; ++direction) {
				const float theta = (static_cast<float>(direction) * kJellyRingPi) / 8.0f;
				const float sine = PrepareEmotePhysicalSine(theta);
				const float cosine = PrepareEmotePhysicalCosine(theta);
				const float x = cosine * 0.0f - sine;
				const float y = sine * 0.0f + cosine;
				result[index * 16 + direction] = {(y * matrix.XY + x * matrix.XX) + matrix.TranslationX, (y * matrix.YY + x * matrix.YX) + matrix.TranslationY};
			}
		}

		const resource::MotionPoint tip{state.Physical.Direction[0] * state.Profile.Length + state.Anchor.X, state.Physical.Direction[1] * state.Profile.Length + state.Anchor.Y};
		std::fill(result.end() - 16, result.end(), tip);

		return result;
	}

	class EmoteJellyPhysics::Implementation {
	public:
		explicit Implementation(EmoteJellyDefinition definition)
		    : Inputs(std::move(definition)) {
			if (Inputs.Substeps == 0 || Inputs.Substeps > 4096 || Inputs.ParticleCount < 2 || Inputs.ParticleCount > 256 || Inputs.Tresses.size() > 4096 || Inputs.ParticleCount > 65536 / std::max<std::size_t>(1, Inputs.Tresses.size()) || Inputs.Fixes.size() > 3 || !(Inputs.SizeHorizontal > 0) || !(Inputs.SizeVertical > 0) || !(Inputs.Density > 0) || !(Inputs.HeightRate > 0) || Inputs.Speed < 0 || (!Inputs.AutoMass && !(Inputs.Mass > 0))) {
				throw std::runtime_error("Emote Jelly requires bounded actual models and positive physical dimensions/mass and nonnegative speed");
			}
			for (const auto value : {Inputs.Speed, Inputs.Gravity, Inputs.Mass, Inputs.Density, Inputs.SizeHorizontal, Inputs.SizeVertical, Inputs.DefaultRotation, Inputs.TiltFactor, Inputs.StretchFactor, Inputs.HeightRate, Inputs.Friction, Inputs.ElasticHorizontal, Inputs.ElasticVertical, Inputs.Sensitivity, Inputs.CollisionRepulsion, Inputs.SampleDensity, Inputs.LimitLengthRatio, Inputs.TipTilt[0], Inputs.TipTilt[1], Inputs.TipTilt[2], Inputs.CoordinateScale[0], Inputs.CoordinateScale[1], Inputs.CoordinateScale[2]}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote Jelly constructor requires finite actual scalar/vector inputs");
				}
			}
			if (Inputs.TipTilt[2] < -1 || Inputs.TipTilt[2] > 1) {
				throw std::runtime_error("Emote Jelly tip angle exceeds the finite native acos domain");
			}

			Origin = {0.5f, 0.5f};
			for (const auto& fix : Inputs.Fixes) {
				if (!std::isfinite(fix.X) || !std::isfinite(fix.Y)) {
					throw std::runtime_error("Emote Jelly fixes require finite actual normalized coordinates");
				}
			}
			if (Inputs.Fixes.size() == 1) {
				Origin = Inputs.Fixes.front();
			} else if (Inputs.Fixes.size() > 1) {
				Origin = {(Inputs.Fixes[0].X + Inputs.Fixes[1].X) * 0.5f, (Inputs.Fixes[0].Y + Inputs.Fixes[1].Y) * 0.5f};
				FixAlignment.emplace(Inputs.Fixes, Inputs.SizeHorizontal, Inputs.SizeVertical);
			}

			Degree = Inputs.Tresses.size() == 1 ? 0x33 : (Inputs.SizeHorizontal <= Inputs.SizeVertical ? 0x43 : 0x34);
			Width = static_cast<std::uint32_t>(Degree & 15) + 1;
			Height = static_cast<std::uint32_t>(Degree >> 4) + 1;
			Tresses.reserve(Inputs.Tresses.size());
			float total_mass = 0;
			for (const auto& tress : Inputs.Tresses) {
				auto state = std::make_unique<JellyTressState>();
				if (!std::isfinite(tress.Coordinate.X) || !std::isfinite(tress.Coordinate.Y) || !std::isfinite(tress.Radius) || !std::isfinite(tress.Tilt[0]) || !std::isfinite(tress.Tilt[1]) || !std::isfinite(tress.Tilt[2])) {
					throw std::runtime_error("Emote Jelly tress requires finite actual coordinate/radius/tilt");
				}
				const float normal_square = tress.Tilt[1] * tress.Tilt[1] + tress.Tilt[0] * tress.Tilt[0];
				if (normal_square > 1) {
					throw std::runtime_error("Emote Jelly tress normal exceeds its finite native sqrt domain");
				}

				EmoteJellyProfileDefinition profile;
				profile.ParticleCount = Inputs.ParticleCount;
				profile.Radius = std::min(Inputs.SizeHorizontal, Inputs.SizeVertical) * tress.Radius;
				profile.HeightRate = Inputs.HeightRate;
				profile.Density = Inputs.Density;
				state->Profile = BuildEmoteJellyProfile(profile);
				total_mass = total_mass + state->Profile.Mass;
				state->Anchor = {(tress.Coordinate.X - Origin.X) * Inputs.SizeHorizontal, (tress.Coordinate.Y - Origin.Y) * Inputs.SizeVertical};
				state->Sections.Normal = {tress.Tilt[0], tress.Tilt[1], std::sqrt(1.0f - normal_square)};
				state->Sections.Tangent = InitialJellyTangent(state->Sections.Normal);
				state->Sections.OrientationInheritance = Inputs.TiltFactor;
				state->Sections.RadiusInheritance = Inputs.StretchFactor;
				state->Sections.Radii = state->Profile.Radii;
				state->Physical.Gravity = Inputs.Gravity;
				state->Physical.ReferenceRotation = Inputs.DefaultRotation;
				state->Physical.Direction = InitialJellyDirection(state->Sections.Normal, Inputs.TipTilt);
				state->Physical.Length = state->Profile.Length;
				state->Physical.MassScale = state->Profile.Mass;
				state->Physical.MassWeights = state->Profile.MassWeights;
				state->Physical.AlignmentSpring = Inputs.ElasticHorizontal;
				state->Physical.LengthSpring = Inputs.ElasticVertical;
				state->Physical.ControlRadius = Inputs.LimitLengthRatio;
				state->Physical.CollisionRepulsion = Inputs.CollisionRepulsion;
				state->Physical.FrictionBegin = Inputs.Friction;
				state->Physical.FrictionEnd = Inputs.Friction;
				state->Physical.AdhocSimulation = Inputs.AdhocSimulation;
				state->Physical.CoordinateScale = Inputs.CoordinateScale;
				state->Initial = InitialJellyRings(*state);
				EmoteJellySurfaceSamplingDefinition sampling;
				sampling.Size = {Inputs.SizeHorizontal, Inputs.SizeVertical};
				sampling.Origin = Origin;
				sampling.ProjectionDirection = {state->Physical.Direction[0], state->Physical.Direction[1]};
				sampling.EncodedDegree = Degree;
				sampling.SampleDensity = Inputs.SampleDensity;
				sampling.ReverseOrder = Inputs.ReverseSampleOrder;
				state->Bindings = BuildEmoteJellySurfaceBindings(sampling, state->Initial);
				state->TargetOffset = Targets.size();
				for (const auto& binding : state->Bindings) {
					if (Targets.size() == 65536) {
						throw std::runtime_error("Emote Jelly complete actual surface exceeds bounded fitting storage");
					}
					FitBindings.push_back({binding.Priority, binding.Order, binding.Rest});
					Targets.push_back(binding.Rest);
				}
				Tresses.push_back(std::move(state));
			}

			if (!Tresses.empty() && (!(total_mass > 0) || !std::isfinite(total_mass))) {
				throw std::runtime_error("Emote Jelly actual total model mass exceeds its finite native domain");
			}
			for (auto& state : Tresses) {
				if (!Inputs.AutoMass) {
					state->Physical.MassScale = (Inputs.Mass / total_mass) * state->Physical.MassScale;
				}
				state->Model = std::make_unique<EmoteBobbleModel>(state->Physical, state->Sections);
				state->Rings = std::make_unique<EmoteJellySectionRings>(*state->Model, state->Sections);
			}

			PrepareRigidBindings();
			Fit.emplace(Width, Height, FitBindings);
			Samples.resize(static_cast<std::size_t>(Width) * Height);
			Output = IdentityMotionBezierLattice(Degree);
			SetPosture({});
		}

		void RequireIdle() const {
			if (Operating || !Valid) {
				throw std::runtime_error("Emote Jelly is active or invalid after a failed physical operation");
			}
		}

		void AddRigidRegion(const std::array<float, 4>& region, const resource::MotionPoint& anchor, std::uint32_t columns, std::uint32_t rows) {
			if (columns < 2 || rows < 2 || columns > 65536 / rows || Targets.size() > 65536 - static_cast<std::size_t>(columns) * rows) {
				throw std::runtime_error("Emote Jelly rigid sample region exceeds finite bounded native lattice counts");
			}
			for (std::uint32_t row = 0; row < rows; ++row) {
				const float v = static_cast<float>(row) / static_cast<float>(rows - 1);
				const float y = (region[3] - region[1]) * v + region[1];
				for (std::uint32_t column = 0; column < columns; ++column) {
					const float u = static_cast<float>(column) / static_cast<float>(columns - 1);
					const float x = u * (region[2] - region[0]) + region[0];
					const float dx = x - anchor.X;
					const float dy = y - anchor.Y;
					FitBindings.push_back({1, dy * dy + dx * dx, {x, y}});
					Targets.push_back({x, y});
				}
			}
		}

		void PrepareRigidBindings() {
			for (const auto value : Inputs.RigidRange) {
				if (!std::isfinite(value) || value < 0 || value > 1) {
					throw std::runtime_error("Emote Jelly rigid range requires finite normalized actual boundaries");
				}
			}
			const auto& range = Inputs.RigidRange;
			if (range[1] > 0) {
				AddRigidRegion({0, 0, 1, range[1]}, {0.5f, range[1]}, Width * 2 - 2, static_cast<std::uint32_t>(std::ceil(static_cast<float>(Height) * range[1]) + 1.0f));
			}
			if (range[3] < 1) {
				AddRigidRegion({0, range[3], 1, 1}, {0.5f, range[3]}, Width * 2 - 2, static_cast<std::uint32_t>(std::ceil((1.0f - range[3]) * static_cast<float>(Height)) + 1.0f));
			}
			if (range[0] > 0) {
				AddRigidRegion({0, 0, range[0], 1}, {range[0], 0.5f}, static_cast<std::uint32_t>(std::ceil(static_cast<float>(Width) * range[0]) + 1.0f), Height * 2 - 2);
			}
			if (range[2] < 1) {
				AddRigidRegion({range[2], 0, 1, 1}, {range[2], 0.5f}, static_cast<std::uint32_t>(std::ceil((1.0f - range[2]) * static_cast<float>(Width)) + 1.0f), Height * 2 - 2);
			}
		}

		void SetPosture(const EmotePhysicalPosture& posture) {
			RequireJellyPosture(posture);
			Current = posture;
			for (auto& state : Tresses) {
				state->Current = posture;
				state->Current.Coordinate[0] = (state->Anchor.X * posture.Cosine + posture.Coordinate[0]) - state->Anchor.Y * posture.Sine;
				state->Current.Coordinate[1] = (state->Anchor.X * posture.Sine + posture.Coordinate[1]) + state->Anchor.Y * posture.Cosine;
			}
		}

		void Reset() {
			UnorderedOutput = false;
			Previous = Current;
			for (auto& state : Tresses) {
				state->Previous = state->Current;
				SetJellyModelPosture(*state, state->Current);
				state->Model->Initialize();
			}
			Initialized = true;
			Dirty = true;
		}

		void Update(float elapsed_ticks, bool unordered_comparison = false) {
			if (!Initialized || !(elapsed_ticks > 0) || !std::isfinite(elapsed_ticks)) {
				throw std::runtime_error("Emote Jelly update requires positive finite actual tick delta");
			}
			if (unordered_comparison && (Inputs.Speed != 0 || Inputs.Sensitivity != 0 || Inputs.AdhocSimulation || !Colliders.empty())) {
				throw std::runtime_error("native unordered Jelly output requires measured zero-speed/sensitivity ordinary models without colliders");
			}
			if (!unordered_comparison && UnorderedOutput) {
				throw std::runtime_error("unordered Jelly output requires actual reinitialization before ordinary finite output");
			}
			const float elapsed = elapsed_ticks * Inputs.Speed;
			if (!unordered_comparison && !(elapsed > 0)) {
				throw std::runtime_error("Emote Jelly zero effective solver time remains outside the finite native step domain");
			}
			// Native5810c0 is ROUNDSS+infinity (imm0x0a), not truncation/floor.
			const float minimum = std::ceil(elapsed / 1.5f);
			if (!std::isfinite(elapsed) || !std::isfinite(minimum) || minimum > 4096) {
				throw std::runtime_error("Emote Jelly update exceeds bounded actual solver subdivisions");
			}
			const auto count = std::max(Inputs.Substeps, static_cast<std::uint32_t>(minimum));
			const float divisions = static_cast<float>(count);
			const float seconds = (elapsed / 60.0f) / divisions;
			if (!unordered_comparison && !(seconds > 0)) {
				throw std::runtime_error("Emote Jelly effective solver seconds underflow outside the finite native step domain");
			}
			for (auto* collider : Colliders) {
				collider->PrepareSubsteps(count);
			}
			if (count != 1) {
				for (auto& state : Tresses) {
					state->StepRotation = (state->Current.Rotation - state->Previous.Rotation) / divisions;
					state->StepCosine = PrepareEmotePhysicalCosine(state->StepRotation);
					state->StepSine = PrepareEmotePhysicalSine(state->StepRotation);
					for (std::size_t axis = 0; axis < state->StepDelta.size(); ++axis) {
						state->StepDelta[axis] = (state->Current.Coordinate[axis] - state->Previous.Coordinate[axis]) / divisions;
					}
				}
			}

			for (std::uint32_t step = 0; step < count; ++step) {
				for (auto* collider : Colliders) {
					collider->AdvanceSubstep();
				}
				for (auto& state : Tresses) {
					if (count == 1) {
						SetJellyModelPosture(*state, state->Current);
					} else {
						for (std::size_t axis = 0; axis < state->StepDelta.size(); ++axis) {
							state->Previous.Coordinate[axis] = state->StepDelta[axis] + state->Previous.Coordinate[axis];
						}
						state->Previous.Rotation = state->StepRotation + state->Previous.Rotation;
						const float next_sine = state->Previous.Cosine * state->StepSine + state->StepCosine * state->Previous.Sine;
						state->Previous.Cosine = state->StepCosine * state->Previous.Cosine - state->StepSine * state->Previous.Sine;
						state->Previous.Sine = next_sine;
						SetJellyModelPosture(*state, state->Previous);
					}

					if (unordered_comparison) {
						state->Model->StepForUnorderedJellyComparison(seconds);
					} else {
						state->Model->Step(seconds);
					}
				}
			}

			Previous = Current;
			for (auto& state : Tresses) {
				state->Previous = state->Current;
			}
			UnorderedOutput = unordered_comparison;
			Dirty = true;
		}

		void PrepareSamples(bool unordered_comparison = false) {
			if (!Initialized) {
				throw std::runtime_error("Emote Jelly output requires actual activation initialization");
			}
			if (UnorderedOutput != unordered_comparison) {
				throw std::runtime_error("Emote Jelly output must retain its actual finite or native-comparison consumer");
			}
			if (!Dirty) {
				return;
			}

			const auto inverse = InverseJellyWorldMatrix(JellyWorldMatrix(Inputs, Origin, Previous));
			for (auto& state : Tresses) {
				state->Rings->Refresh();
				const auto rings = state->Rings->Rings();
				for (std::size_t index = 0; index < state->Bindings.size(); ++index) {
					const auto& binding = state->Bindings[index];
					const auto next = (binding.Direction + 1) % 16;
					const auto first = static_cast<std::size_t>(binding.Section) * 16;
					const auto second = first + 16;
					const float b = binding.Circumference;
					const float v = binding.Longitudinal;
					const float left_x = (rings[first + next].X - rings[first + binding.Direction].X) * b + rings[first + binding.Direction].X;
					const float left_y = (rings[first + next].Y - rings[first + binding.Direction].Y) * b + rings[first + binding.Direction].Y;
					const float right_x = (rings[second + next].X - rings[second + binding.Direction].X) * b + rings[second + binding.Direction].X;
					const float right_y = (rings[second + next].Y - rings[second + binding.Direction].Y) * b + rings[second + binding.Direction].Y;
					const float x = (right_x - left_x) * v + left_x;
					const float y = (right_y - left_y) * v + left_y;
					const float normalized_x = (x * inverse.XX + y * inverse.XY) + inverse.TranslationX;
					const float normalized_y = (y * inverse.YY + x * inverse.YX) + inverse.TranslationY;
					Targets[state->TargetOffset + index] = {(normalized_x - binding.Rest.X) * Inputs.Sensitivity + binding.Rest.X, (normalized_y - binding.Rest.Y) * Inputs.Sensitivity + binding.Rest.Y};
				}
			}

			InitializeEmoteJellySampleGrid(Samples, Width, Height);
			if (unordered_comparison) {
				Fit->ApplyForUnorderedComparison(Samples, Targets);
			} else {
				Fit->Apply(Samples, Targets);
			}
			Dirty = false;
		}

		void PrepareLattice(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> targets, bool unordered_comparison = false) {
			if (!std::isfinite(correction_angle) || !std::isfinite(correction_scale) || (Inputs.CorrectFix && targets.size() != Inputs.Fixes.size())) {
				throw std::runtime_error("Emote Jelly correction requires real finite driver values and every actual fix target");
			}
			for (const auto& target : targets) {
				if (!std::isfinite(target.X) || !std::isfinite(target.Y)) {
					throw std::runtime_error("Emote Jelly correction targets require finite actual world points");
				}
			}

			PrepareSamples(unordered_comparison);
			std::copy(Samples.begin(), Samples.end(), Output.Controls.begin());
			if (unordered_comparison) {
				ConvertUnorderedMotionBezierSamplesToControls(Output.Controls, Degree);
			} else {
				ConvertMotionBezierSamplesToControls(Output.Controls, Degree);
			}
			const auto world = JellyWorldMatrix(Inputs, Origin, Previous);
			const auto inverse = InverseJellyWorldMatrix(world);
			TransformJellyLattice(Output, world);
			if (Inputs.CorrectScale) {
				for (auto& point : Output.Controls) {
					point.X = (point.X - Previous.Coordinate[0]) * correction_scale + Previous.Coordinate[0];
					point.Y = (point.Y - Previous.Coordinate[1]) * correction_scale + Previous.Coordinate[1];
				}
			}
			if (FixAlignment) {
				if (unordered_comparison) {
					FixAlignment->NormalizeForUnorderedComparison(Output, Previous, inverse);
				} else {
					FixAlignment->Normalize(Output, Previous, inverse);
				}
			} else {
				auto normalization = inverse;
				if (!Inputs.Fixes.empty()) {
					const auto point = unordered_comparison ? EvaluateUnorderedMotionBezierLattice(Output, Inputs.Fixes.front()) : EvaluateMotionBezierLattice(Output, Inputs.Fixes.front());
					const float dx = Previous.Coordinate[0] - point.X;
					const float dy = Previous.Coordinate[1] - point.Y;
					normalization.TranslationX = (inverse.XX * dx + inverse.XY * dy) + inverse.TranslationX;
					normalization.TranslationY = (inverse.YY * dy + inverse.YX * dx) + inverse.TranslationY;
				}
				TransformJellyLattice(Output, normalization);
			}
			if (Inputs.CorrectFix && !Inputs.Fixes.empty()) {
				auto corrected = Previous;
				corrected.Rotation = correction_angle;
				corrected.Cosine = PrepareEmotePhysicalCosine(correction_angle);
				corrected.Sine = PrepareEmotePhysicalSine(correction_angle);
				const auto corrected_inverse = InverseJellyWorldMatrix(JellyWorldMatrix(Inputs, Origin, corrected));
				if (FixAlignment) {
					if (unordered_comparison) {
						FixAlignment->CorrectForUnorderedComparison(Output, targets, corrected_inverse);
					} else {
						FixAlignment->Correct(Output, targets, corrected_inverse);
					}
				} else {
					auto pivot = InverseJellyWorldMatrix(corrected_inverse);
					const auto& fix = Inputs.Fixes.front();
					const resource::MotionPoint point{(fix.Y * pivot.XY + fix.X * pivot.XX) + pivot.TranslationX, (fix.Y * pivot.YY + fix.X * pivot.YX) + pivot.TranslationY};
					pivot.TranslationX = (pivot.TranslationX - point.X) + targets.front().X;
					pivot.TranslationY = (pivot.TranslationY - point.Y) + targets.front().Y;
					TransformJellyLattice(Output, ComposeRingMatrices(corrected_inverse, pivot));
				}
			}

			if (!unordered_comparison) {
				ValidateMotionBezierLattice(Output);
			}
		}

		template <class Operation>
		void Mutate(Operation operation) {
			RequireIdle();
			Operating = true;
			try {
				operation();
				Operating = false;
			} catch (...) {
				Operating = false;
				Valid = false;
				throw;
			}
		}

		JellyTressState& RequireTress(std::size_t index) const {
			if (index >= Tresses.size()) {
				throw std::runtime_error("Emote Jelly tress index does not belong to this actual owner");
			}

			return *Tresses[index];
		}

		EmoteJellyDefinition Inputs;
		resource::MotionPoint Origin;
		std::uint8_t Degree = 0;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::vector<std::unique_ptr<JellyTressState>> Tresses;
		std::vector<EmoteCollider*> Colliders;
		std::vector<EmoteJellySampleBinding> FitBindings;
		std::vector<resource::MotionPoint> Targets;
		std::vector<resource::MotionPoint> Samples;
		std::optional<EmoteJellySampleFit> Fit;
		std::optional<EmoteMeshFixAlignment> FixAlignment;
		resource::MotionBezierLattice Output;
		EmotePhysicalPosture Current;
		EmotePhysicalPosture Previous;
		bool UnorderedOutput = false;
		bool Dirty = true;
		bool Initialized = false;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteJellyPhysics::EmoteJellyPhysics(EmoteJellyDefinition definition)
	    : m_Implementation(std::make_unique<Implementation>(std::move(definition))) {
	}

	EmoteJellyPhysics::~EmoteJellyPhysics() noexcept = default;

	void EmoteJellyPhysics::SetPosture(const EmotePhysicalPosture& posture) {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			state.SetPosture(posture);
		});
	}

	void EmoteJellyPhysics::Initialize() {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			state.Reset();
		});
	}

	void EmoteJellyPhysics::ClearForces() {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			for (auto& tress : state.Tresses) {
				tress->Model->ClearForces();
			}
		});
	}

	void EmoteJellyPhysics::AttachCollider(EmoteCollider& collider, std::uint32_t tress_mask) {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			if (state.Colliders.size() >= 4096 || state.Tresses.size() >= 32) {
				throw std::runtime_error("Emote Jelly collider attachment exceeds the bounded native tress mask domain");
			}
			state.Colliders.push_back(&collider);
			for (std::size_t index = 0; index != state.Tresses.size(); ++index) {
				if ((tress_mask & (std::uint32_t{1} << index)) != 0) {
					state.Tresses[index]->Model->AttachCollider(collider);
				}
			}
		});
	}

	void EmoteJellyPhysics::AddForce(std::size_t tress, std::size_t particle, const std::array<float, 3>& force) {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			state.RequireTress(tress).Model->AddForce(particle, force);
		});
	}

	void EmoteJellyPhysics::Update(float elapsed_ticks) {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			state.Update(elapsed_ticks);
		});
	}

	void EmoteJellyPhysics::UpdateForUnorderedComparison(float elapsed_ticks) {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			state.Update(elapsed_ticks, true);
		});
	}

	const resource::MotionBezierLattice& EmoteJellyPhysics::PrepareLatticeForUnorderedComparison(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets) {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			state.PrepareLattice(correction_angle, correction_scale, world_fix_targets, true);
		});
		return state.Output;
	}

	std::span<const resource::MotionPoint> EmoteJellyPhysics::PrepareSamples() {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			state.PrepareSamples();
		});

		return state.Samples;
	}

	const resource::MotionBezierLattice& EmoteJellyPhysics::PrepareLattice(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets) {
		auto& state = *m_Implementation;
		state.Mutate([&] {
			state.PrepareLattice(correction_angle, correction_scale, world_fix_targets);
		});

		return state.Output;
	}

	const EmoteJellyDefinition& EmoteJellyPhysics::Definition() const noexcept {
		return m_Implementation->Inputs;
	}

	std::uint8_t EmoteJellyPhysics::EncodedDegree() const noexcept {
		return m_Implementation->Degree;
	}

	resource::MotionPoint EmoteJellyPhysics::Origin() const noexcept {
		return m_Implementation->Origin;
	}

	std::size_t EmoteJellyPhysics::TressCount() const noexcept {
		return m_Implementation->Tresses.size();
	}

	std::span<const resource::MotionPoint> EmoteJellyPhysics::InitialRings(std::size_t tress) const {
		const auto& state = *m_Implementation;
		state.RequireIdle();

		return state.RequireTress(tress).Initial;
	}

	std::span<const EmoteJellyRingSample> EmoteJellyPhysics::Bindings(std::size_t tress) const {
		const auto& state = *m_Implementation;
		state.RequireIdle();

		return state.RequireTress(tress).Bindings;
	}

	std::span<const EmoteBobbleParticle> EmoteJellyPhysics::Particles(std::size_t tress) const {
		const auto& state = *m_Implementation;
		state.RequireIdle();

		return state.RequireTress(tress).Model->Particles();
	}

	std::span<const resource::MotionPoint> EmoteJellyPhysics::PreparedRings(std::size_t tress) const {
		const auto& state = *m_Implementation;
		state.RequireIdle();
		if (state.Dirty) {
			throw std::runtime_error("Emote Jelly rings require completed actual sample publication");
		}

		return state.RequireTress(tress).Rings->Rings();
	}

	std::span<const resource::MotionPoint> EmoteJellyPhysics::PreparedTargets(std::size_t tress) const {
		const auto& state = *m_Implementation;
		state.RequireIdle();
		if (state.Dirty) {
			throw std::runtime_error("Emote Jelly targets require completed actual sample publication");
		}

		const auto& part = state.RequireTress(tress);
		return std::span<const resource::MotionPoint>(state.Targets).subspan(part.TargetOffset, part.Bindings.size());
	}

	void EmoteJellyPhysics::RequireCanAssignState(const EmoteJellyPhysics& source) const {
		m_Implementation->RequireIdle();
		source.m_Implementation->RequireIdle();
		const auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		for (std::size_t index = 0; index != std::min(target.Tresses.size(), from.Tresses.size()); ++index) {
			target.Tresses[index]->Model->RequireCanAssignState(*from.Tresses[index]->Model);
		}
	}

	void EmoteJellyPhysics::AssignState(const EmoteJellyPhysics& source) {
		RequireCanAssignState(source);
		if (this == &source) {
			return;
		}

		auto& target = *m_Implementation;
		const auto& from = *source.m_Implementation;
		target.Previous = from.Previous;
		for (std::size_t index = 0; index != std::min(target.Tresses.size(), from.Tresses.size()); ++index) {
			target.Tresses[index]->Previous = from.Tresses[index]->Previous;
			target.Tresses[index]->Model->AssignState(*from.Tresses[index]->Model);
		}
		target.Initialized = from.Initialized;
	}

} // namespace sgre::scene
