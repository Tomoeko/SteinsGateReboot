#include "sgre/scene/emote_bloom_physics.hpp"

#include "motion_native_compared_mesh.hpp"

#include "sgre/scene/emote_mesh_fix_alignment.hpp"
#include "sgre/scene/motion_bezier_inverse.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaximumBloomPoints = 32;
	static constexpr std::uint32_t kMaximumBloomSubsteps = 4096;
	static constexpr float kBloomAngularEpsilon = std::bit_cast<float>(std::uint32_t{0x3727c5ac});
	static constexpr float kBloomFullCircle = std::bit_cast<float>(std::uint32_t{0x40c90fdb});

	static void RequireBloomFinite(float value) {
		if (!std::isfinite(value)) {
			throw std::runtime_error("Emote Bloom value exceeds the recovered finite physical domain");
		}
	}

	static std::array<float, 4> MultiplyBloomQuaternions(const std::array<float, 4>& left, const std::array<float, 4>& right) {
		// Native3039c0 is also used by the actual Jelly pose producer.
		return {((left[3] * right[0] - left[2] * right[1]) + left[1] * right[2]) + left[0] * right[3], ((right[0] * left[2] + left[3] * right[1]) - right[2] * left[0]) + left[1] * right[3], ((right[1] * left[0] - right[0] * left[1]) + left[3] * right[2]) + left[2] * right[3], ((-left[0] * right[0] - right[1] * left[1]) - left[2] * right[2]) + left[3] * right[3]};
	}

	static std::array<float, 3> RotateBloomRest(const std::array<float, 3>& rest, float angle) {
		const float half = angle * 0.5f;
		const float sine = std::sin(half);
		const float cosine = std::cos(half);
		const std::array<float, 4> rotation{sine * 0.0f, sine * 0.0f, sine, cosine};
		const auto product = MultiplyBloomQuaternions(rotation, {rest[0], rest[1], rest[2], 0});
		const std::array<float, 4> conjugate{-rotation[0], -rotation[1], -rotation[2], rotation[3]};
		return {((conjugate[0] * product[3] - conjugate[1] * product[2]) + conjugate[2] * product[1]) + conjugate[3] * product[0], ((conjugate[1] * product[3] + conjugate[0] * product[2]) - conjugate[2] * product[0]) + conjugate[3] * product[1], ((conjugate[1] * product[0] - conjugate[0] * product[1]) + conjugate[2] * product[3]) + conjugate[3] * product[2]};
	}

	static std::array<float, 4> BloomCoordinateFrame(const EmoteBloomDefinition& definition) {
		const float sine = std::sin(definition.DefaultRotation);
		const float cosine = std::cos(definition.DefaultRotation);
		const float inverse_sine = std::sin(-definition.DefaultRotation);
		const float inverse_cosine = std::cos(-definition.DefaultRotation);
		const float first_x = inverse_cosine * definition.CoordinateScale[0] + inverse_sine * 0.0f;
		const float first_y = inverse_sine * definition.CoordinateScale[1] + inverse_cosine * 0.0f;
		const float second_x = -inverse_sine * definition.CoordinateScale[0] + inverse_cosine * 0.0f;
		const float second_y = inverse_cosine * definition.CoordinateScale[1] + -inverse_sine * 0.0f;
		return {first_x * cosine + first_y * -sine, second_x * cosine + second_y * -sine, first_y * cosine + first_x * sine, second_y * cosine + second_x * sine};
	}

	static float BloomAngleDistance(const resource::MotionPoint& direction, const resource::MotionPoint& control) {
		const float cross = direction.Y * control.X - direction.X * control.Y;
		const float dot = direction.X * control.X + direction.Y * control.Y;
		const float angle = std::abs(std::atan2(cross, dot));
		return std::min(kBloomFullCircle - angle, angle);
	}

	static float BloomInverseLength(float squared) {
		const float half = squared * 0.5f;
		float reciprocal = std::bit_cast<float>(std::uint32_t{0x5f3759df} - (std::bit_cast<std::uint32_t>(squared) >> 1));
		reciprocal = (1.5f - (half * reciprocal) * reciprocal) * reciprocal;
		reciprocal = (1.5f - (reciprocal * half) * reciprocal) * reciprocal;
		return reciprocal;
	}

	static resource::MotionMeshMatrix BloomWorldMatrix(const EmoteBloomDefinition& definition, const EmotePhysicalPosture& posture) {
		resource::MotionMeshMatrix result;
		result.XX = definition.SizeHorizontal * posture.Cosine;
		result.XY = definition.SizeVertical * -posture.Sine;
		result.YX = definition.SizeHorizontal * posture.Sine;
		result.YY = definition.SizeVertical * posture.Cosine;
		const float x = -definition.Origin.X * definition.SizeHorizontal;
		const float y = -definition.Origin.Y * definition.SizeVertical;
		result.TranslationX = (x * posture.Cosine + y * -posture.Sine) + posture.Coordinate[0];
		result.TranslationY = (y * posture.Cosine + x * posture.Sine) + posture.Coordinate[1];
		return result;
	}

	static resource::MotionMeshMatrix InverseBloomMatrix(const resource::MotionMeshMatrix& value) {
		const float determinant = value.XX * value.YY - value.YX * value.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote Bloom output requires its actual invertible physical frame");
		}
		const float reciprocal = 1.0f / determinant;
		resource::MotionMeshMatrix result;
		result.XX = value.YY * reciprocal;
		result.XY = -value.XY * reciprocal;
		result.TranslationX = (value.TranslationY * value.XY - value.YY * value.TranslationX) * reciprocal;
		result.YX = -value.YX * reciprocal;
		result.YY = value.XX * reciprocal;
		result.TranslationY = (value.YX * value.TranslationX - value.TranslationY * value.XX) * reciprocal;
		return result;
	}

	static resource::MotionPoint TransformBloomPoint(const resource::MotionPoint& point, const resource::MotionMeshMatrix& matrix) {
		return {(point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
	}

	static resource::MotionMeshMatrix ComposeBloomMatrices(const resource::MotionMeshMatrix& outer, const resource::MotionMeshMatrix& inner) {
		resource::MotionMeshMatrix result;
		result.XX = outer.XY * inner.YX + outer.XX * inner.XX;
		result.XY = outer.XY * inner.YY + outer.XX * inner.XY;
		result.YX = outer.YY * inner.YX + outer.YX * inner.XX;
		result.YY = outer.YY * inner.YY + outer.YX * inner.XY;
		result.TranslationX = (inner.TranslationY * outer.XY + inner.TranslationX * outer.XX) + outer.TranslationX;
		result.TranslationY = (inner.TranslationY * outer.YY + inner.TranslationX * outer.YX) + outer.TranslationY;
		return result;
	}

	class EmoteBloomPhysics::Implementation {
	public:
		explicit Implementation(EmoteBloomDefinition definition) : Inputs(std::move(definition)), CoordinateFrame(BloomCoordinateFrame(Inputs)) {
			if (Inputs.Substeps == 0 || Inputs.Substeps > kMaximumBloomSubsteps || Inputs.Tresses.empty() || Inputs.Tresses.size() > kMaximumBloomPoints || !(Inputs.Mass > 0) || !(Inputs.SizeHorizontal > 0) || !(Inputs.SizeVertical > 0) || Inputs.Speed < 0 || Inputs.LimitLengthRatio < 0) {
				throw std::runtime_error("Emote Bloom requires bounded actual tresses, substeps and positive physical dimensions/mass and nonnegative speed");
			}
			for (const auto value : {Inputs.DefaultRotation, Inputs.Speed, Inputs.Mass, Inputs.LimitLengthRatio, Inputs.SizeHorizontal, Inputs.SizeVertical, Inputs.Origin.X, Inputs.Origin.Y, Inputs.Center.X, Inputs.Center.Y, Inputs.Elastic, Inputs.Friction, Inputs.RotateWeight, Inputs.TwistWeight, Inputs.TrapeziumWeight, Inputs.PitchWeight, Inputs.ControlPointSize, Inputs.CoordinateScale[0], Inputs.CoordinateScale[1], Inputs.CoordinateScale[2], Inputs.CollisionRepulsion, Inputs.Straightness}) {
				RequireBloomFinite(value);
			}
			Mass = Inputs.Mass / static_cast<float>(Inputs.Tresses.size());
			if (!(Mass > 0) || !std::isfinite(Mass)) {
				throw std::runtime_error("Emote Bloom per-control mass exceeds its finite supported domain");
			}
			const float center_x = Inputs.Center.X * Inputs.SizeHorizontal;
			const float center_y = Inputs.Center.Y * Inputs.SizeVertical;
			Points.reserve(Inputs.Tresses.size());
			for (const auto& tress : Inputs.Tresses) {
				RequireBloomFinite(tress.X);
				RequireBloomFinite(tress.Y);
				EmoteBloomPoint point;
				point.Relative = {tress.X * Inputs.SizeHorizontal - center_x, tress.Y * Inputs.SizeVertical - center_y};
				const float distance = std::sqrt(point.Relative.X * point.Relative.X + point.Relative.Y * point.Relative.Y);
				if (!(distance > 0) || !std::isfinite(distance)) {
					throw std::runtime_error("Emote Bloom control direction requires a finite point distinct from its actual center");
				}
				point.Direction = {point.Relative.X / distance, point.Relative.Y / distance};
				point.Rest = {point.Relative.X, point.Relative.Y, 0};
				point.Limit = distance * Inputs.LimitLengthRatio;
				RequireBloomFinite(point.Limit);
				Points.push_back(point);
			}
			PrepareInfluences(center_x, center_y);
			Output = IdentityMotionBezierLattice(0x33);
			if (Inputs.Fixes.size() > 3) {
				throw std::runtime_error("Emote Bloom correction exceeds its actual three-fix owner");
			}
			for (const auto& fix : Inputs.Fixes) {
				RequireBloomFinite(fix.X);
				RequireBloomFinite(fix.Y);
			}
			if (Inputs.Fixes.size() > 1) {
				FixAlignment.emplace(Inputs.Fixes, Inputs.SizeHorizontal, Inputs.SizeVertical);
			}
		}

		void RequireIdle() const {
			if (Thread != std::this_thread::get_id() || Operating || Failed) {
				throw std::runtime_error("Emote Bloom requires its valid serialized idle physical owner");
			}
		}

		void PrepareInfluences(float center_x, float center_y) {
			if (Points.size() == 1) {
				Points.front().Influences.fill(1);
				return;
			}
			std::array<float, kMaximumBloomPoints> dots{};
			std::array<std::size_t, kMaximumBloomPoints> order{};
			for (std::size_t y = 0; y != 4; ++y) {
				for (std::size_t x = 0; x != 4; ++x) {
					const float relative_x = static_cast<float>(x) * Inputs.SizeHorizontal / 3.0f - center_x;
					const float relative_y = static_cast<float>(y) * Inputs.SizeVertical / 3.0f - center_y;
					const float reciprocal = 1.0f / std::sqrt(relative_y * relative_y + relative_x * relative_x);
					const resource::MotionPoint direction{reciprocal * relative_x, reciprocal * relative_y};
					RequireBloomFinite(direction.X);
					RequireBloomFinite(direction.Y);
					std::iota(order.begin(), order.begin() + Points.size(), std::size_t{0});
					for (std::size_t index = 0; index != Points.size(); ++index) {
						dots[index] = direction.Y * Points[index].Direction.Y + direction.X * Points[index].Direction.X;
					}
					// Up to32 indices use the native302110 stable insertion domain, ordered by descending float dot.
					std::stable_sort(order.begin(), order.begin() + Points.size(), [&](std::size_t left, std::size_t right) {
						return dots[left] > dots[right];
					});
					const auto first = order[0];
					const auto second = order[1];
					const auto sample = y * 4 + x;
					if (1.0f - dots[first] < kBloomAngularEpsilon) {
						Points[first].Influences[sample] = 1;
						continue;
					}
					const float first_angle = BloomAngleDistance(direction, Points[first].Direction);
					const float second_angle = BloomAngleDistance(direction, Points[second].Direction);
					const float total = second_angle + first_angle;
					Points[first].Influences[sample] = total >= kBloomAngularEpsilon ? second_angle / total : 0.5f;
					Points[second].Influences[sample] = total >= kBloomAngularEpsilon ? first_angle / total : 0.5f;
				}
			}
		}

		void RefreshTargets() {
			const float coordinate_x = Current.Coordinate[1] * CoordinateFrame[1] + Current.Coordinate[0] * CoordinateFrame[0];
			const float coordinate_y = Current.Coordinate[0] * CoordinateFrame[2] + Current.Coordinate[1] * CoordinateFrame[3];
			TargetRotation = Current.Rotation;
			CoordinateOffset = {coordinate_x - Current.Coordinate[0], coordinate_y - Current.Coordinate[1]};
			const float center_x = (Inputs.Center.X - Inputs.Origin.X) * Inputs.SizeHorizontal;
			const float center_y = (Inputs.Center.Y - Inputs.Origin.Y) * Inputs.SizeVertical;
			Center = {(center_x * Current.Cosine - center_y * Current.Sine + 0.0f) + coordinate_x, (center_y * Current.Cosine + center_x * Current.Sine + 0.0f) + coordinate_y, Current.Coordinate[2] * Inputs.CoordinateScale[2]};
			for (auto& point : Points) {
				const auto rotated = RotateBloomRest(point.Rest, Current.Rotation);
				for (std::size_t axis = 0; axis != 3; ++axis) {
					point.Target[axis] = (rotated[axis] - point.Rest[axis]) * Inputs.Straightness + point.Rest[axis] + Center[axis];
					RequireBloomFinite(point.Target[axis]);
				}
			}
		}

		void Step(float seconds) {
			for (auto& point : Points) {
				const float force_scale = seconds / Mass;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					point.Velocity[axis] = force_scale * point.Force[axis] + point.Velocity[axis];
					point.Velocity[axis] = (point.Target[axis] - point.Coordinate[axis]) * Inputs.Elastic / Mass * seconds + point.Velocity[axis];
					point.Predicted[axis] = seconds * point.Velocity[axis] + point.Coordinate[axis];
				}
				const std::array<float, 3> displacement{point.Predicted[0] - Center[0], point.Predicted[1] - Center[1], point.Predicted[2] - Center[2]};
				const float squared = displacement[1] * displacement[1] + displacement[0] * displacement[0] + displacement[2] * displacement[2];
				const float length = std::sqrt(squared);
				if (length > point.Limit) {
					for (std::size_t axis = 0; axis != 3; ++axis) {
						point.Predicted[axis] = displacement[axis] / length * point.Limit + Center[axis];
					}
				}
				const float reciprocal = 1.0f / seconds;
				const float friction = seconds * Inputs.Friction;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					point.Velocity[axis] = (point.Predicted[axis] - point.Coordinate[axis]) * reciprocal;
					point.Previous[axis] = point.Coordinate[axis];
					point.Coordinate[axis] = point.Predicted[axis];
					point.Velocity[axis] = point.Velocity[axis] - point.Velocity[axis] * friction;
					if (!UnorderedOutput) {
						RequireBloomFinite(point.Coordinate[axis]);
						RequireBloomFinite(point.Velocity[axis]);
					}
				}
			}
		}

		void Update(float elapsed_ticks, bool unordered_comparison = false) {
			RequireIdle();
			const float scaled = elapsed_ticks * Inputs.Speed;
			if (!Initialized || !std::isfinite(elapsed_ticks) || elapsed_ticks < 0 || !std::isfinite(scaled) || (unordered_comparison ? (Inputs.Speed != 0 || scaled != 0) : (!(elapsed_ticks > 0) || !(scaled > 0) || UnorderedOutput)) || scaled > static_cast<float>(kMaximumBloomSubsteps) * 1.5f) {
				throw std::runtime_error("Emote Bloom update requires its actual initialization and bounded positive elapsed ticks");
			}
			UnorderedOutput = unordered_comparison;
			Operating = true;
			try {
				const auto subdivisions = std::max(Inputs.Substeps, static_cast<std::uint32_t>(std::ceil(scaled / 1.5f)));
				const float count = static_cast<float>(subdivisions);
				const float seconds = (scaled / 60.0f) / count;
				if (subdivisions == 1) {
					Current = Requested;
					RefreshTargets();
					Step(seconds);
				} else {
					std::array<float, 3> translation{};
					for (std::size_t axis = 0; axis != 3; ++axis) {
						translation[axis] = (Requested.Coordinate[axis] - Current.Coordinate[axis]) / count;
					}
					const float angle = (Requested.Rotation - Current.Rotation) / count;
					const float cosine = std::cos(angle);
					const float sine = std::sin(angle);
					for (std::uint32_t step = 0; step != subdivisions; ++step) {
						for (std::size_t axis = 0; axis != 3; ++axis) {
							Current.Coordinate[axis] = translation[axis] + Current.Coordinate[axis];
						}
						Current.Rotation = angle + Current.Rotation;
						const float next_cosine = Current.Cosine * cosine - Current.Sine * sine;
						Current.Sine = Current.Cosine * sine + Current.Sine * cosine;
						Current.Cosine = next_cosine;
						RefreshTargets();
						Step(seconds);
					}
				}
				Current = Requested;
				Dirty = true;
				Operating = false;
			} catch (...) {
				Operating = false;
				Failed = true;
				throw;
			}
		}

		void PrepareSamples(bool unordered_comparison = false) {
			if (!Initialized) {
				throw std::runtime_error("Emote Bloom output requires actual physical initialization");
			}
			if (UnorderedOutput != unordered_comparison) {
				throw std::runtime_error("Emote Bloom output must retain its actual finite or native-comparison consumer");
			}
			if (!Dirty) {
				return;
			}
			const auto world = BloomWorldMatrix(Inputs, Requested);
			auto inverse = InverseBloomMatrix(world);
			std::array<resource::MotionPoint, 16> baseline{};
			for (std::size_t y = 0; y != 4; ++y) {
				for (std::size_t x = 0; x != 4; ++x) {
					const resource::MotionPoint normalized{static_cast<float>(x) / 3.0f, static_cast<float>(y) / 3.0f};
					auto point = TransformBloomPoint(normalized, world);
					point.X += CoordinateOffset.X;
					point.Y += CoordinateOffset.Y;
					baseline[y * 4 + x] = point;
				}
			}
			const float twist_base = ((1.0f - Inputs.Straightness) * -TargetRotation) * Inputs.TwistWeight;
			const float maximum_size = std::max(Inputs.SizeHorizontal, Inputs.SizeVertical);
			for (std::size_t point_index = 0; point_index != Points.size(); ++point_index) {
				const auto& point = Points[point_index];
				auto& samples = PerPointSamples[point_index];
				const float target_x = point.Target[0] - Center[0];
				const float target_y = point.Target[1] - Center[1];
				const float target_squared = target_x * target_x + target_y * target_y;
				if (!(target_squared > 0) || !(point.Limit > 0)) {
					throw std::runtime_error("Emote Bloom sample output requires nonzero actual target direction and radius limit");
				}
				const float reciprocal = BloomInverseLength(target_squared);
				const float direction_x = reciprocal * target_x;
				const float direction_y = reciprocal * target_y;
				const float current_x = point.Coordinate[0] - Center[0];
				const float current_y = point.Coordinate[1] - Center[1];
				const float projection = current_y * direction_y + current_x * direction_x;
				const float perpendicular_x = current_x - projection * direction_x;
				const float perpendicular_y = current_y - projection * direction_y;
				const float inverse_limit = 1.0f / point.Limit;
				const float bend = (perpendicular_y * inverse_limit) * direction_x - (perpendicular_x * inverse_limit) * direction_y;
				Bends[point_index] = bend;
				const float angle = (bend * Inputs.RotateWeight - (1.0f - Inputs.Straightness) * TargetRotation) - twist_base;
				const float sine = std::sin(angle);
				const float cosine = std::cos(angle);
				const float translate_x = (-Center[0] * cosine + -sine * -Center[1]) + Center[0];
				const float translate_y = (-Center[1] * cosine + -Center[0] * sine) + Center[1];
				for (std::size_t index = 0; index != samples.size(); ++index) {
					samples[index] = {(baseline[index].Y * -sine + baseline[index].X * cosine) + translate_x, (baseline[index].Y * cosine + baseline[index].X * sine) + translate_y};
				}
			}
			if (Inputs.TwistWeight != 0 || twist_base != 0) {
				for (std::size_t point_index = 0; point_index != Points.size(); ++point_index) {
					const auto& point = Points[point_index];
					const float twist = Bends[point_index] * Inputs.TwistWeight + twist_base;
					auto& samples = PerPointSamples[point_index];
					for (std::size_t index = 0; index != samples.size(); ++index) {
						if (point.Influences[index] == 0) {
							continue;
						}
						const float x = samples[index].X - Center[0];
						const float y = samples[index].Y - Center[1];
						const float angle = (std::sqrt(y * y + x * x) * twist) / maximum_size;
						const float sine = std::sin(angle);
						const float cosine = std::cos(angle);
						samples[index] = {(x * cosine - y * sine + 0.0f) + Center[0], (y * cosine + x * sine + 0.0f) + Center[1]};
					}
				}
			}
			for (const bool pitch : {false, true}) {
				const float weight = pitch ? Inputs.PitchWeight : Inputs.TrapeziumWeight;
				if (weight == 0) {
					continue;
				}
				for (std::size_t point_index = 0; point_index != Points.size(); ++point_index) {
					const auto& point = Points[point_index];
					const float dx = point.Target[0] - point.Coordinate[0];
					const float dy = point.Target[1] - point.Coordinate[1];
					const float squared = dy * dy + dx * dx;
					const float length = std::sqrt(squared);
					// Native305360 exits this whole point loop at its first tiny displacement.
					if (length < 1.0e-4f) {
						break;
					}
					const float ratio = length / point.Limit;
					const float reciprocal = BloomInverseLength(squared);
					const float direction_x = reciprocal * dx;
					const float direction_y = reciprocal * dy;
					auto& samples = PerPointSamples[point_index];
					for (std::size_t index = 0; index != samples.size(); ++index) {
						if (point.Influences[index] == 0) {
							continue;
						}
						const float x = samples[index].X - Center[0];
						const float y = samples[index].Y - Center[1];
						const float projection = y * direction_y + x * direction_x;
						if (pitch) {
							const float factor = 1.0f - std::abs(ratio * weight);
							const float projected_x = projection * direction_x;
							const float projected_y = projection * direction_y;
							samples[index] = {(factor * projected_x + (x - projected_x)) + Center[0], (factor * projected_y + (y - projected_y)) + Center[1]};
						} else {
							const float factor = ((projection / maximum_size) * ratio) * weight + 1.0f;
							samples[index] = {factor * x + Center[0], factor * y + Center[1]};
						}
					}
				}
			}
			Samples.fill({});
			for (std::size_t point_index = 0; point_index != Points.size(); ++point_index) {
				for (std::size_t index = 0; index != Samples.size(); ++index) {
					const float influence = Points[point_index].Influences[index];
					if (influence != 0) {
						Samples[index].X = influence * PerPointSamples[point_index][index].X + Samples[index].X;
						Samples[index].Y = influence * PerPointSamples[point_index][index].Y + Samples[index].Y;
					}
				}
			}
			inverse.TranslationX = (-CoordinateOffset.X * inverse.XX + inverse.XY * -CoordinateOffset.Y) + inverse.TranslationX;
			inverse.TranslationY = (-CoordinateOffset.X * inverse.YX + inverse.YY * -CoordinateOffset.Y) + inverse.TranslationY;
			for (auto& point : Samples) {
				point = TransformBloomPoint(point, inverse);
				if (!unordered_comparison) {
					RequireBloomFinite(point.X);
					RequireBloomFinite(point.Y);
				}
			}
			Dirty = false;
		}

		void PrepareLattice(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> targets, bool unordered_comparison = false) {
			if (!std::isfinite(correction_angle) || !std::isfinite(correction_scale) || (Inputs.CorrectFix && targets.size() != Inputs.Fixes.size())) {
				throw std::runtime_error("Emote Bloom correction requires actual finite driver values and every real fix target");
			}
			for (const auto& target : targets) {
				RequireBloomFinite(target.X);
				RequireBloomFinite(target.Y);
			}
			PrepareSamples(unordered_comparison);
			std::copy(Samples.begin(), Samples.end(), Output.Controls.begin());
			if (unordered_comparison) {
				ConvertUnorderedMotionBezierSamplesToControls(Output.Controls, 0x33);
			} else {
				ConvertMotionBezierSamplesToControls(Output.Controls, 0x33);
			}
			const auto world = BloomWorldMatrix(Inputs, Requested);
			const auto inverse = InverseBloomMatrix(world);
			for (auto& point : Output.Controls) {
				point = TransformBloomPoint(point, world);
			}
			if (Inputs.CorrectScale) {
				for (auto& point : Output.Controls) {
					point.X = (point.X - Current.Coordinate[0]) * correction_scale + Current.Coordinate[0];
					point.Y = (point.Y - Current.Coordinate[1]) * correction_scale + Current.Coordinate[1];
				}
			}
			if (FixAlignment) {
				if (unordered_comparison) {
					FixAlignment->NormalizeForUnorderedComparison(Output, Current, inverse);
				} else {
					FixAlignment->Normalize(Output, Current, inverse);
				}
			} else {
				auto normalization = inverse;
				if (!Inputs.Fixes.empty()) {
					const auto point = unordered_comparison ? EvaluateUnorderedMotionBezierLattice(Output, Inputs.Fixes.front()) : EvaluateMotionBezierLattice(Output, Inputs.Fixes.front());
					const float dx = Current.Coordinate[0] - point.X;
					const float dy = Current.Coordinate[1] - point.Y;
					normalization.TranslationX = (inverse.XX * dx + inverse.XY * dy) + inverse.TranslationX;
					normalization.TranslationY = (inverse.YY * dy + inverse.YX * dx) + inverse.TranslationY;
				}
				for (auto& point : Output.Controls) {
					point = TransformBloomPoint(point, normalization);
				}
			}
			if (Inputs.CorrectFix && !Inputs.Fixes.empty()) {
				auto corrected = Current;
				corrected.Rotation = correction_angle;
				corrected.Cosine = std::cos(correction_angle);
				corrected.Sine = std::sin(correction_angle);
				const auto corrected_inverse = InverseBloomMatrix(BloomWorldMatrix(Inputs, corrected));
				if (FixAlignment) {
					if (unordered_comparison) {
						FixAlignment->CorrectForUnorderedComparison(Output, targets, corrected_inverse);
					} else {
						FixAlignment->Correct(Output, targets, corrected_inverse);
					}
				} else {
					auto pivot = InverseBloomMatrix(corrected_inverse);
					const auto point = TransformBloomPoint(Inputs.Fixes.front(), pivot);
					pivot.TranslationX = (pivot.TranslationX - point.X) + targets.front().X;
					pivot.TranslationY = (pivot.TranslationY - point.Y) + targets.front().Y;
					const auto correction = ComposeBloomMatrices(corrected_inverse, pivot);
					for (auto& control : Output.Controls) {
						control = TransformBloomPoint(control, correction);
					}
				}
			}
			if (!unordered_comparison) {
				ValidateMotionBezierLattice(Output);
			}
		}

		EmoteBloomDefinition Inputs;
		std::array<float, 4> CoordinateFrame;
		std::vector<EmoteBloomPoint> Points;
		std::array<resource::MotionPoint, 16> Samples{};
		std::array<std::array<resource::MotionPoint, 16>, kMaximumBloomPoints> PerPointSamples{};
		std::array<float, kMaximumBloomPoints> Bends{};
		resource::MotionBezierLattice Output;
		std::optional<EmoteMeshFixAlignment> FixAlignment;
		resource::MotionPoint CoordinateOffset;
		float TargetRotation = 0;
		bool Dirty = true;
		float Mass = 0;
		std::array<float, 3> Center{};
		EmotePhysicalPosture Current;
		EmotePhysicalPosture Requested;
		const std::thread::id Thread = std::this_thread::get_id();
		bool Operating = false;
		bool Initialized = false;
		bool UnorderedOutput = false;
		bool Failed = false;
	};

	EmoteBloomPhysics::EmoteBloomPhysics(EmoteBloomDefinition definition) : m_Implementation(std::make_unique<Implementation>(std::move(definition))) {
	}
	EmoteBloomPhysics::~EmoteBloomPhysics() noexcept = default;
	void EmoteBloomPhysics::SetPosture(const EmotePhysicalPosture& actual_posture) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		for (const auto value : {actual_posture.Coordinate[0], actual_posture.Coordinate[1], actual_posture.Coordinate[2], actual_posture.Rotation, actual_posture.Cosine, actual_posture.Sine}) {
			RequireBloomFinite(value);
		}
		state.Requested = actual_posture;
		state.Dirty = true;
	}
	void EmoteBloomPhysics::Initialize() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		state.Current = state.Requested;
		state.RefreshTargets();
		for (auto& point : state.Points) {
			point.Coordinate = point.Target;
			point.Previous = point.Target;
			point.Velocity = {};
		}
		state.UnorderedOutput = false;
		state.Initialized = true;
		state.Dirty = true;
	}
	void EmoteBloomPhysics::ClearForces() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		for (auto& point : state.Points) {
			point.Force = {};
		}
	}
	void EmoteBloomPhysics::AddForce(std::size_t point, const std::array<float, 3>& actual_force) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (point >= state.Points.size()) {
			throw std::runtime_error("Emote Bloom force point exceeds the actual owned controls");
		}
		for (std::size_t axis = 0; axis != 3; ++axis) {
			RequireBloomFinite(actual_force[axis]);
			RequireBloomFinite(actual_force[axis] + state.Points[point].Force[axis]);
		}
		for (std::size_t axis = 0; axis != 3; ++axis) {
			state.Points[point].Force[axis] = actual_force[axis] + state.Points[point].Force[axis];
		}
	}
	void EmoteBloomPhysics::Update(float elapsed_ticks) {
		m_Implementation->Update(elapsed_ticks);
	}
	void EmoteBloomPhysics::UpdateForUnorderedComparison(float elapsed_ticks) {
		m_Implementation->Update(elapsed_ticks, true);
	}
	const resource::MotionBezierLattice& EmoteBloomPhysics::PrepareLatticeForUnorderedComparison(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		state.PrepareLattice(correction_angle, correction_scale, world_fix_targets, true);
		return state.Output;
	}

	std::span<const resource::MotionPoint> EmoteBloomPhysics::PrepareSamples() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		state.PrepareSamples();
		return state.Samples;
	}
	const resource::MotionBezierLattice& EmoteBloomPhysics::PrepareLattice() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		state.PrepareLattice(0, 1, {});
		return state.Output;
	}
	const resource::MotionBezierLattice& EmoteBloomPhysics::PrepareLattice(float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		state.PrepareLattice(correction_angle, correction_scale, world_fix_targets);
		return state.Output;
	}
	std::span<const EmoteBloomPoint> EmoteBloomPhysics::Points() const noexcept {
		return m_Implementation->Points;
	}
	const EmoteBloomDefinition& EmoteBloomPhysics::Definition() const noexcept {
		return m_Implementation->Inputs;
	}

} // namespace sgre::scene
