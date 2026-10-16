#include "sgre/scene/emote_string_physics.hpp"

#include "sgre/scene/emote_collider.hpp"
#include "sgre/scene/emote_tress_spline.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sgre::scene {
	static constexpr float kStringHalfPi = 1.57079637050628662109375f;
	static constexpr float kStringDerivativeStep = 0.001000000047497451305389404296875f;

	struct EmoteStringTressState {
		EmoteStringLine Line;
		float FixPosition = 0;
		float FixRatio = 0;
		float TopAtZero = 0;
		float BottomAtOne = 0;
		float Length = 0;
		float Angle = 0;
		float Cosine = 1;
		float Sine = 0;
		float RelativeCosine = 1;
		float RelativeSine = 0;
		std::array<float, 2> Anchor{};
		std::unique_ptr<EmoteTressModel> Model;
		std::unique_ptr<EmoteTressSpline> Spline;
		std::vector<float> Weights;
	};

	static void RequireStringPosture(const EmotePhysicalPosture& posture) {
		for (const auto value : {posture.Coordinate[0], posture.Coordinate[1], posture.Coordinate[2], posture.Rotation, posture.Cosine, posture.Sine}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote String posture requires its actual finite six-word input");
			}
		}
	}

	static std::uint32_t StringParticleCount(float fraction, float length, float density) {
		const auto count = ((fraction * length) * density) + 1.5f;
		if (!std::isfinite(count) || count < 0 || count >= 257) {
			throw std::runtime_error("Emote String physical particle count exceeds bounded actual model storage");
		}
		return static_cast<std::uint32_t>(std::max(2, static_cast<int>(count)));
	}

	static float NativeStringReciprocalLength(float square, bool unordered_comparison) {
		if (!unordered_comparison && (!(square > 0) || !std::isfinite(square))) {
			throw std::runtime_error("Emote String tangent requires finite nonzero actual separation");
		}
		const auto half = square * 0.5f;
		const auto initial = std::bit_cast<float>(std::uint32_t{0x5f3759df} - (std::bit_cast<std::uint32_t>(square) >> 1));
		const auto first = (1.5f - (half * initial) * initial) * initial;
		return (1.5f - (half * first) * first) * first;
	}

	class EmoteStringPhysics::Implementation {
	public:
		explicit Implementation(EmoteStringDefinition definition)
		    : Definition(std::move(definition)) {
			if (Definition.LongitudinalDegree < 1 || Definition.LongitudinalDegree > 15 || Definition.Substeps == 0 || Definition.Substeps > 4096 || Definition.Tresses.size() > 4096 || !(Definition.SizeHorizontal > 0) || !(Definition.SizeVertical > 0) || !(Definition.Mass > 0) || Definition.ParticlesPerMeter < 0 || Definition.Speed < 0) {
				throw std::runtime_error("Emote String requires bounded actual geometry, mass and substeps");
			}
			for (const auto value : {Definition.Gravity, Definition.DefaultRotation, Definition.Straightness, Definition.Roughness, Definition.Mass, Definition.SizeHorizontal, Definition.SizeVertical, Definition.OriginX, Definition.OriginY, Definition.SplitLeft, Definition.SplitRight, Definition.ParticlesPerMeter, Definition.ControlRadius, Definition.Speed, Definition.FrictionTop, Definition.FrictionBottom, Definition.Elastic, Definition.CompressWiden}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote String requires finite actual constructor scalars");
				}
			}
			Width = Definition.HorizontalBones ? Definition.SizeVertical : Definition.SizeHorizontal;
			Height = Definition.HorizontalBones ? Definition.SizeHorizontal : Definition.SizeVertical;
			BoneRotation = Definition.HorizontalBones ? -kStringHalfPi : 0;
			BoneCosine = std::cos(BoneRotation);
			BoneSine = std::sin(BoneRotation);
			Rows = static_cast<std::size_t>(Definition.LongitudinalDegree) + 1;
			Samples.resize(Rows * 4);
			LongitudinalKnots.resize(Rows);
			PrepareKnots();
			Tresses.reserve(Definition.Tresses.size());
			float total_length = 0;
			for (const auto& line : Definition.Tresses) {
				for (const auto value : {line.TopX, line.TopY, line.BottomX, line.BottomY}) {
					if (!std::isfinite(value)) {
						throw std::runtime_error("Emote String authored tress requires finite actual endpoints");
					}
				}
				if (line.TopY == line.BottomY) {
					continue;
				}
				EmoteStringTressState state;
				state.Line = line;
				const auto dx = line.BottomX - line.TopX;
				const auto dy = line.BottomY - line.TopY;
				state.TopAtZero = ((line.TopX - line.BottomX) / dy) * line.TopY + line.TopX;
				state.BottomAtOne = (dx / dy) * (1.0f - line.BottomY) + line.BottomX;
				state.FixPosition = Definition.SplitLeft;
				bool parallel = false;
				if (Definition.SplitLeft != Definition.SplitRight) {
					const auto split_delta = Definition.SplitRight - Definition.SplitLeft;
					const auto determinant = line.TopY * line.BottomX - line.BottomY * line.TopX;
					const auto denominator = dy * -1.0f - -dx * split_delta;
					if (denominator == 0) {
						state.FixPosition = 0;
						parallel = true;
					} else {
						state.FixPosition = (determinant * split_delta - (Definition.SplitLeft - Definition.SplitRight * 0.0f) * dy) / denominator;
					}
				}
				state.FixRatio = parallel ? 0 : (state.FixPosition - line.TopY) / dy;
				const auto scaled_x = dx * Width;
				const auto scaled_y = dy * Height;
				state.Length = std::sqrt(scaled_y * scaled_y + scaled_x * scaled_x);
				state.Angle = (std::atan2(scaled_y, scaled_x) + BoneRotation) - kStringHalfPi;
				state.Cosine = std::cos(state.Angle);
				state.Sine = std::sin(state.Angle);
				state.RelativeCosine = std::cos(state.Angle - BoneRotation);
				state.RelativeSine = std::sin(state.Angle - BoneRotation);
				const auto fix_x = (state.FixRatio * dx + line.TopX) * Width;
				const auto fix_y = (state.FixRatio * dy + line.TopY) * Height;
				state.Anchor = {fix_x - Definition.OriginX * Definition.SizeHorizontal, fix_y - Definition.OriginY * Definition.SizeVertical};
				if (Definition.HorizontalBones) {
					state.Anchor = {fix_y - Definition.OriginX * Definition.SizeHorizontal, (Definition.SizeVertical - fix_x) - Definition.OriginY * Definition.SizeVertical};
				}
				if (!std::isfinite(state.Length) || !(state.Length > 0) || !std::isfinite(state.FixRatio) || state.FixRatio < 0 || state.FixRatio > 1) {
					throw std::runtime_error("Emote String physical fix is outside its finite authored tress segment domain");
				}
				for (const auto value : {state.TopAtZero, state.BottomAtOne, state.FixPosition, state.Angle, state.Anchor[0], state.Anchor[1]}) {
					if (!std::isfinite(value)) {
						throw std::runtime_error("Emote String constructor geometry exceeds its finite authored domain");
					}
				}
				state.Weights.resize(Samples.size());
				total_length += state.Length;
				Tresses.push_back(std::move(state));
			}
			if (Tresses.empty() || !std::isfinite(total_length) || !(total_length > 0)) {
				throw std::runtime_error("Emote String requires nonempty finite actual tress geometry");
			}
			for (auto& state : Tresses) {
				EmoteTressDefinition physical;
				physical.Gravity = Definition.Gravity;
				physical.GravityRotation = (1.0f - Definition.Straightness) * Definition.DefaultRotation;
				physical.ReferenceRotation = state.Angle;
				physical.RigidSide = Definition.RigidSide;
				const auto mass = (state.Length / total_length) * Definition.Mass;
				physical.First.Mass = mass * state.FixRatio;
				physical.First.Length = state.Length * state.FixRatio;
				physical.First.ParticleCount = StringParticleCount(state.FixRatio, Height, Definition.ParticlesPerMeter);
				physical.Second.Mass = (1.0f - state.FixRatio) * mass;
				physical.Second.Length = (1.0f - state.FixRatio) * state.Length;
				physical.Second.ParticleCount = StringParticleCount(1.0f - state.FixRatio, Height, Definition.ParticlesPerMeter);
				if (state.Line.TopY != 0) {
					physical.First.TrailingPoints = 1;
					if (physical.First.Length == 0) {
						physical.First.ParticleCount = 1;
						physical.First.TrailingLength = (state.Line.TopY * state.Length) / (state.Line.BottomY - state.Line.TopY);
					} else {
						physical.First.TrailingLength = (state.Line.TopY * physical.First.Length) / (state.FixPosition - state.Line.TopY);
					}
				}
				if (state.Line.BottomY != 1) {
					physical.Second.TrailingPoints = 1;
					if (physical.Second.Length == 0) {
						physical.Second.ParticleCount = 1;
						physical.Second.TrailingLength = ((1.0f - state.Line.BottomY) * state.Length) / (state.Line.BottomY - state.Line.TopY);
					} else {
						physical.Second.TrailingLength = ((1.0f - state.Line.BottomY) * physical.Second.Length) / (state.Line.BottomY - state.FixPosition);
					}
				}
				physical.ControlRadius = Definition.ControlRadius;
				physical.RotationInheritance = Definition.Straightness;
				physical.FrictionBegin = Definition.FrictionTop;
				physical.FrictionEnd = Definition.FrictionBottom;
				physical.Elastic = Definition.Elastic;
				physical.CoordinateRotation = Definition.DefaultRotation;
				physical.CoordinateScale = Definition.CoordinateScale;
				physical.MassTiltedDistribution = Definition.MassTiltedDistribution;
				physical.AdhocSimulation = Definition.AdhocSimulation;
				physical.CollisionHardness = Definition.CollisionHardness;
				physical.CollisionRepulsion = Definition.CollisionRepulsion;
				state.Model = std::make_unique<EmoteTressModel>(physical);
				state.Spline = std::make_unique<EmoteTressSpline>(*state.Model);
			}
			PrepareWeights();
			Current.Rotation = Definition.DefaultRotation;
			Current.Cosine = std::cos(Current.Rotation);
			Current.Sine = std::sin(Current.Rotation);
			Reset();
		}

		void PrepareKnots() {
			const auto degree = Definition.LongitudinalDegree;
			const auto pivot = Definition.SplitLeft;
			if (Definition.SplitLeft != Definition.SplitRight || pivot == 0 || pivot == 1 || degree < 2) {
				for (std::size_t index = 0; index != Rows; ++index) {
					LongitudinalKnots[index] = static_cast<float>(index) / static_cast<float>(degree);
				}
				return;
			}
			if (!(pivot > 0 && pivot < 1)) {
				throw std::runtime_error("Emote String longitudinal pivot is outside its recovered finite interior domain");
			}
			const auto pivot_index = static_cast<std::size_t>(std::clamp(static_cast<int>((static_cast<float>(degree) * pivot - 0.5f) + 1.0f), 1, static_cast<int>(degree) - 1));
			for (std::size_t index = 0; index < pivot_index; ++index) {
				LongitudinalKnots[index] = (static_cast<float>(index) * pivot) / static_cast<float>(pivot_index);
			}
			LongitudinalKnots[pivot_index] = pivot;
			for (std::size_t index = pivot_index + 1; index != Rows; ++index) {
				LongitudinalKnots[index] = (static_cast<float>(index - pivot_index) * (1.0f - pivot)) / static_cast<float>(degree - pivot_index) + pivot;
			}
			LongitudinalKnots.front() = 0;
			LongitudinalKnots.back() = 1;
		}

		void PrepareWeights() {
			std::vector<std::pair<float, std::size_t>> order;
			order.reserve(Tresses.size());
			for (std::size_t row = 0; row != Rows; ++row) {
				order.clear();
				const auto position = LongitudinalKnots[row];
				for (std::size_t index = 0; index != Tresses.size(); ++index) {
					const auto& state = Tresses[index];
					order.emplace_back((1.0f - position) * state.TopAtZero + position * state.BottomAtOne, index);
				}
				std::stable_sort(order.begin(), order.end(), [](const auto& first, const auto& second) {
					return first.first < second.first;
				});
				for (std::size_t column = 0; column != 4; ++column) {
					const auto coordinate = static_cast<float>(column) / 3.0f;
					const auto at = std::lower_bound(order.begin(), order.end(), coordinate, [](const auto& entry, float value) {
						return entry.first < value;
					});
					const auto output = row * 4 + column;
					if (at == order.begin()) {
						Tresses[at->second].Weights[output] = 1;
					} else if (at == order.end()) {
						Tresses[order.back().second].Weights[output] = 1;
					} else {
						const auto& previous = *(at - 1);
						const auto interval = at->first - previous.first;
						Tresses[previous.second].Weights[output] = (at->first - coordinate) / interval;
						Tresses[at->second].Weights[output] = (coordinate - previous.first) / interval;
					}
				}
			}
		}

		void RequireIdle() const {
			if (Operating || !Valid) {
				throw std::runtime_error("Emote String physical owner is active or invalid after a failed phase");
			}
		}

		void ApplyPosture(EmoteStringTressState& state, const EmotePhysicalPosture& posture) {
			auto actual = posture;
			actual.Coordinate[0] = (posture.Cosine * state.Anchor[0] + posture.Coordinate[0]) - state.Anchor[1] * posture.Sine;
			actual.Coordinate[1] = (posture.Sine * state.Anchor[0] + posture.Coordinate[1]) + state.Anchor[1] * posture.Cosine;
			state.Model->SetPosture(actual);
		}

		void Reset() {
			UnorderedComparison = false;
			Previous = Current;
			for (auto& state : Tresses) {
				ApplyPosture(state, Previous);
				state.Model->Initialize();
			}
		}

		std::array<float, 2> ExtendedSample(const EmoteStringTressState& state, float position) const {
			if (position <= 1) {
				return state.Spline->SampleImpl(position, UnorderedComparison);
			}
			const auto end = state.Spline->SampleImpl(1, UnorderedComparison);
			const auto mirrored = state.Spline->SampleImpl(1.0f - (position - 1.0f), UnorderedComparison);
			return {(end[0] - mirrored[0]) + end[0], (end[1] - mirrored[1]) + end[1]};
		}

		void SampleRow(const EmoteStringTressState& state, float position, std::span<std::array<float, 2>> row) const {
			const auto point = ExtendedSample(state, position);
			const auto base_x = Previous.Cosine * BoneCosine - Previous.Sine * BoneSine;
			const auto base_y = BoneCosine * Previous.Sine + Previous.Cosine * BoneSine;
			const auto first_half = position <= state.FixPosition;
			const auto rigid = position == state.FixPosition || (first_half && Definition.RigidSide == EmoteTressRigidSide::First) || (!first_half && Definition.RigidSide == EmoteTressRigidSide::Second);
			float direction_x = base_x;
			float direction_y = base_y;
			float width_scale = 1;
			if (!rigid) {
				const auto next = ExtendedSample(state, position + kStringDerivativeStep);
				const auto negative_dx = point[0] - next[0];
				const auto dy = next[1] - point[1];
				const auto reciprocal = NativeStringReciprocalLength((negative_dx * negative_dx + dy * dy) + 0.0f, UnorderedComparison);
				const auto normalized_x = reciprocal * negative_dx;
				const auto normalized_y = reciprocal * dy;
				const auto physical_y = state.RelativeCosine * normalized_x - state.RelativeSine * normalized_y;
				const auto physical_x = state.RelativeCosine * normalized_y + state.RelativeSine * normalized_x;
				float reference_x = 0;
				float reference_y = 0;
				float projection_x = 0;
				float projection_y = 0;
				if (Definition.Straightness == 0) {
					const auto half_cosine = std::cos(kStringHalfPi);
					const auto half_sine = std::sin(kStringHalfPi);
					projection_x = half_cosine * state.Cosine - half_sine * state.Sine;
					projection_y = state.Cosine * half_sine + half_cosine * state.Sine;
					const auto default_cosine = std::cos(Definition.DefaultRotation);
					const auto default_sine = std::sin(Definition.DefaultRotation);
					reference_x = BoneCosine * default_cosine - BoneSine * default_sine;
					reference_y = BoneSine * default_cosine + BoneCosine * default_sine;
				} else {
					const auto projection_angle = (Definition.Straightness * Previous.Rotation + state.Angle) + kStringHalfPi;
					projection_x = std::cos(projection_angle);
					projection_y = std::sin(projection_angle);
					const auto reference_angle = (Previous.Rotation - Definition.DefaultRotation) * Definition.Straightness + (Definition.DefaultRotation + BoneRotation);
					reference_x = std::cos(reference_angle);
					reference_y = std::sin(reference_angle);
				}
				const auto projected_length = std::abs((next[0] - point[0]) * projection_x + projection_y * dy + 0.0f) / kStringDerivativeStep;
				const auto contraction = ((state.Length - projected_length) / static_cast<float>(state.Model->PointCount() - 1)) * static_cast<float>(Tresses.size());
				width_scale = (contraction * Definition.CompressWiden + Width) / Width;
				const auto ratio = first_half ? (state.FixPosition - position) / state.FixPosition : (position - state.FixPosition) / (1.0f - state.FixPosition);
				const auto current_x = (physical_x - base_x) * ratio + base_x;
				const auto current_y = (physical_y - base_y) * ratio + base_y;
				reference_x = (reference_x - base_x) * ratio + base_x;
				reference_y = (reference_y - base_y) * ratio + base_y;
				direction_x = (current_x - reference_x) * Definition.Roughness + reference_x;
				direction_y = (current_y - reference_y) * Definition.Roughness + reference_y;
			}
			const auto center = ((1.0f - position) * state.TopAtZero + position * state.BottomAtOne) - 0.5f;
			for (std::size_t column = 0; column != 4; ++column) {
				const auto distance = (((static_cast<float>(column) / 3.0f - 0.5f) - center) * Width) * width_scale;
				row[column] = {direction_x * distance + point[0], direction_y * distance + point[1]};
			}
		}

		EmoteStringDefinition Definition;
		std::vector<EmoteStringTressState> Tresses;
		std::vector<EmoteCollider*> Colliders;
		std::vector<float> LongitudinalKnots;
		std::vector<std::array<float, 2>> Samples;
		EmotePhysicalPosture Current;
		EmotePhysicalPosture Previous;
		std::size_t Rows = 0;
		float Width = 0;
		float Height = 0;
		float BoneRotation = 0;
		float BoneCosine = 1;
		float BoneSine = 0;
		bool UnorderedComparison = false;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteStringPhysics::EmoteStringPhysics(EmoteStringDefinition definition)
	    : m_Implementation(std::make_unique<Implementation>(std::move(definition))) {
	}
	EmoteStringPhysics::~EmoteStringPhysics() noexcept = default;

	void EmoteStringPhysics::SetPosture(const EmotePhysicalPosture& posture) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		RequireStringPosture(posture);
		state.Current = posture;
	}

	void EmoteStringPhysics::Initialize() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		try {
			state.Reset();
		} catch (...) {
			state.Valid = false;
			throw;
		}
	}

	void EmoteStringPhysics::ClearForcesAndMeasureBounds() {
		auto& state = *m_Implementation;
		state.RequireIdle();
		for (auto& tress : state.Tresses) {
			tress.Model->ClearForcesAndMeasureBounds();
		}
	}

	void EmoteStringPhysics::AddForce(std::size_t tress, bool first_side, std::size_t particle, const std::array<float, 3>& force) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (tress >= state.Tresses.size()) {
			throw std::runtime_error("Emote String force requires its actual tress owner");
		}
		state.Tresses[tress].Model->AddForce(first_side, particle, force);
	}

	void EmoteStringPhysics::AttachCollider(EmoteCollider& collider, std::uint32_t tress_mask) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (state.Colliders.size() >= 4096 || state.Tresses.size() >= 32) {
			throw std::runtime_error("Emote String collider attachment exceeds native bounded mask domain");
		}
		state.Colliders.push_back(&collider);
		for (std::size_t index = 0; index != state.Tresses.size(); ++index) {
			if ((tress_mask & (std::uint32_t{1} << index)) != 0) {
				state.Tresses[index].Model->AttachCollider(collider);
			}
		}
	}

	void EmoteStringPhysics::Update(float elapsed_ticks) {
		UpdateImpl(elapsed_ticks, false);
	}

	void EmoteStringPhysics::UpdateForUnorderedComparison(float elapsed_ticks) {
		UpdateImpl(elapsed_ticks, true);
	}

	void EmoteStringPhysics::UpdateImpl(float elapsed_ticks, bool unordered_comparison) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		const auto ticks = elapsed_ticks * state.Definition.Speed;
		if (!std::isfinite(elapsed_ticks) || elapsed_ticks < 0 || !std::isfinite(ticks) || (unordered_comparison ? (ticks != 0 || state.Definition.AdhocSimulation || !state.Colliders.empty()) : (!(ticks > 0) || state.UnorderedComparison)) || ticks > 4096 * 1.5f) {
			throw std::runtime_error("Emote String update requires bounded positive actual scaled ticks");
		}
		state.UnorderedComparison = unordered_comparison;
		const auto count = std::max(state.Definition.Substeps, static_cast<std::uint32_t>(std::ceil(ticks / 1.5f)));
		const auto seconds = (ticks / 60.0f) / static_cast<float>(count);
		state.Operating = true;
		try {
			for (auto* collider : state.Colliders) {
				collider->PrepareSubsteps(count);
			}
			if (count == 1) {
				for (auto* collider : state.Colliders) {
					collider->AdvanceSubstep();
				}
				for (auto& tress : state.Tresses) {
					state.ApplyPosture(tress, state.Current);
					tress.Model->StepImpl(seconds, unordered_comparison);
				}
			} else {
				const auto delta_rotation = (state.Current.Rotation - state.Previous.Rotation) / static_cast<float>(count);
				const auto cosine = std::cos(delta_rotation);
				const auto sine = std::sin(delta_rotation);
				std::array<float, 3> delta;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					delta[axis] = (state.Current.Coordinate[axis] - state.Previous.Coordinate[axis]) / static_cast<float>(count);
				}
				for (std::uint32_t step = 0; step != count; ++step) {
					for (auto* collider : state.Colliders) {
						collider->AdvanceSubstep();
					}
					for (std::size_t axis = 0; axis != 3; ++axis) {
						state.Previous.Coordinate[axis] += delta[axis];
					}
					state.Previous.Rotation += delta_rotation;
					const auto next_sine = cosine * state.Previous.Sine + sine * state.Previous.Cosine;
					state.Previous.Cosine = cosine * state.Previous.Cosine - sine * state.Previous.Sine;
					state.Previous.Sine = next_sine;
					for (auto& tress : state.Tresses) {
						state.ApplyPosture(tress, state.Previous);
						tress.Model->StepImpl(seconds, unordered_comparison);
					}
				}
			}
			state.Previous = state.Current;
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
	}

	std::span<const std::array<float, 2>> EmoteStringPhysics::PreparePhysicalSamples() {
		return PrepareSamplesImpl(false);
	}

	std::span<const std::array<float, 2>> EmoteStringPhysics::PrepareSamplesForUnorderedComparison() {
		return PrepareSamplesImpl(true);
	}

	std::span<const std::array<float, 2>> EmoteStringPhysics::PrepareSamplesImpl(bool unordered_comparison) {
		auto& state = *m_Implementation;
		state.RequireIdle();
		if (state.UnorderedComparison != unordered_comparison) {
			throw std::runtime_error("Emote String sample output requires its matching finite or private native comparison phase");
		}
		std::fill(state.Samples.begin(), state.Samples.end(), std::array<float, 2>{});
		try {
			std::array<std::array<float, 2>, 4> row;
			for (auto& tress : state.Tresses) {
				tress.Spline->PrepareImpl(unordered_comparison);
				for (std::size_t index = 0; index != state.Rows; ++index) {
					state.SampleRow(tress, state.LongitudinalKnots[index], row);
					for (std::size_t column = 0; column != 4; ++column) {
						const auto output = index * 4 + column;
						for (std::size_t axis = 0; axis != 2; ++axis) {
							state.Samples[output][axis] = row[column][axis] * tress.Weights[output] + state.Samples[output][axis];
							if (!unordered_comparison && !std::isfinite(state.Samples[output][axis])) {
								throw std::runtime_error("Emote String actual physical samples exceed their finite output domain");
							}
						}
					}
				}
			}
		} catch (...) {
			state.Valid = false;
			throw;
		}
		return state.Samples;
	}

	std::size_t EmoteStringPhysics::TressCount() const noexcept {
		return m_Implementation->Tresses.size();
	}

	std::string_view EmoteStringPhysics::Uid() const noexcept {
		return m_Implementation->Definition.Uid;
	}

	const EmoteStringDefinition& EmoteStringPhysics::Definition() const noexcept {
		return m_Implementation->Definition;
	}

	std::span<const float> EmoteStringPhysics::LongitudinalKnots() const noexcept {
		return m_Implementation->LongitudinalKnots;
	}

	bool EmoteStringPhysics::HorizontalBones() const noexcept {
		return m_Implementation->Definition.HorizontalBones;
	}

	std::uint8_t EmoteStringPhysics::EncodedDegree() const noexcept {
		const auto& definition = m_Implementation->Definition;
		return definition.HorizontalBones ? static_cast<std::uint8_t>(definition.LongitudinalDegree | 0x30) : static_cast<std::uint8_t>((definition.LongitudinalDegree << 4) | 3);
	}
} // namespace sgre::scene
