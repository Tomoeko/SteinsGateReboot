#include "sgre/scene/emote_string_mesh_output.hpp"

#include "motion_native_compared_mesh.hpp"

#include "sgre/scene/emote_mesh_fix_alignment.hpp"
#include "sgre/scene/emote_rigid_region.hpp"
#include "sgre/scene/motion_bezier_inverse.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {
	static const resource::PsbValue& OutputField(const resource::PsbValue& value, std::string_view name) {
		if (const auto* field = value.Find(name)) {
			return *field;
		}
		throw std::runtime_error("Emote String output metadata is missing " + std::string(name));
	}

	static float OutputNumber(const resource::PsbValue& value) {
		float result = 0;
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1 : 0;
		} else {
			throw std::runtime_error("Emote String output scalar requires actual native numeric metadata");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote String output scalar exceeds finite metadata");
		}
		return result;
	}

	static bool OutputFlag(const resource::PsbValue& value, std::string_view name, bool fallback) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			return fallback;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&field->Data)) {
			return static_cast<std::uint8_t>(*integer) != 0;
		}
		if (const auto* boolean = std::get_if<bool>(&field->Data)) {
			return *boolean;
		}
		if (const auto* real = std::get_if<double>(&field->Data)) {
			return *real != 0;
		}
		throw std::runtime_error("Emote String output flag requires actual native numeric metadata");
	}

	std::vector<EmoteStringMeshOutputDefinition> ReadEmoteStringMeshOutputDefinitions(const resource::PsbDocument& document) {
		const auto& metadata = OutputField(document.Root, "metadata");
		if (metadata.Find("velvetyCommonParams") == nullptr) {
			return {};
		}
		const auto* controls = std::get_if<resource::PsbArray>(&OutputField(metadata, "velvetyStringControl").Data);
		if (controls == nullptr || controls->size() > 4096) {
			throw std::runtime_error("Emote String output requires bounded actual String metadata controls");
		}
		std::vector<EmoteStringMeshOutputDefinition> definitions;
		definitions.reserve(controls->size());
		for (const auto& control : *controls) {
			EmoteStringMeshOutputDefinition definition;
			const auto& range = OutputField(control, "rigidRange");
			definition.RigidRange = {OutputNumber(OutputField(range, "l")), OutputNumber(OutputField(range, "t")), OutputNumber(OutputField(range, "r")), OutputNumber(OutputField(range, "b"))};
			const auto& iteration_field = OutputField(control, "rigidIter");
			std::int32_t iterations = 0;
			if (const auto* integer = std::get_if<std::int64_t>(&iteration_field.Data)) {
				iterations = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
			} else if (const auto* boolean = std::get_if<bool>(&iteration_field.Data)) {
				iterations = *boolean ? 1 : 0;
			} else if (const auto* real = std::get_if<double>(&iteration_field.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
				iterations = static_cast<std::int32_t>(*real);
			} else {
				throw std::runtime_error("Emote String output rigid iteration conversion exceeds native supported integer range");
			}
			if (iterations < 0 || iterations > 4096) {
				throw std::runtime_error("Emote String output rigid iterations exceed bounded supported native integer metadata");
			}
			definition.RigidIterations = static_cast<std::uint32_t>(iterations);
			const auto inherit = OutputFlag(control, "inheritShape", true);
			definition.CorrectFix = !inherit && OutputFlag(control, "correctFix", false);
			definition.CorrectScale = !inherit && OutputFlag(control, "correctScale", false);
			const auto* fixes = std::get_if<resource::PsbArray>(&OutputField(control, "fixes").Data);
			if (fixes == nullptr || fixes->size() > 3) {
				throw std::runtime_error("Emote String output fixes exceed actual native three-point storage");
			}
			definition.Fixes.reserve(fixes->size());
			for (const auto& fix : *fixes) {
				definition.Fixes.push_back({OutputNumber(OutputField(fix, "x")), OutputNumber(OutputField(fix, "y"))});
			}
			definitions.push_back(std::move(definition));
		}
		return definitions;
	}

	static resource::MotionPoint TransformStringPoint(const resource::MotionMeshMatrix& matrix, const resource::MotionPoint& point) {
		return {(point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
	}

	static void TransformStringLattice(resource::MotionBezierLattice& lattice, const resource::MotionMeshMatrix& matrix) {
		for (auto& point : lattice.Controls) {
			point = TransformStringPoint(matrix, point);
		}
	}

	static resource::MotionMeshMatrix StringWorldMatrix(const EmoteStringDefinition& definition, const EmotePhysicalPosture& posture) {
		const auto origin_x = -definition.OriginX * definition.SizeHorizontal;
		const auto origin_y = -definition.OriginY * definition.SizeVertical;
		resource::MotionMeshMatrix matrix;
		matrix.XX = definition.SizeHorizontal * posture.Cosine;
		matrix.XY = definition.SizeVertical * -posture.Sine;
		matrix.YX = definition.SizeHorizontal * posture.Sine;
		matrix.YY = definition.SizeVertical * posture.Cosine;
		matrix.TranslationX = (origin_x * posture.Cosine + origin_y * -posture.Sine) + posture.Coordinate[0];
		matrix.TranslationY = (origin_y * posture.Cosine + origin_x * posture.Sine) + posture.Coordinate[1];
		return matrix;
	}

	static resource::MotionMeshMatrix InverseStringMatrix(const resource::MotionMeshMatrix& matrix) {
		const auto determinant = matrix.YY * matrix.XX - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote String output requires its actual finite invertible world matrix");
		}
		const auto reciprocal = 1.0f / determinant;
		resource::MotionMeshMatrix inverse;
		inverse.XX = reciprocal * matrix.YY;
		inverse.XY = -matrix.XY * reciprocal;
		inverse.YX = -matrix.YX * reciprocal;
		inverse.YY = reciprocal * matrix.XX;
		inverse.TranslationX = (matrix.TranslationY * matrix.XY - matrix.TranslationX * matrix.YY) * reciprocal;
		inverse.TranslationY = (matrix.TranslationX * matrix.YX - matrix.TranslationY * matrix.XX) * reciprocal;
		return inverse;
	}

	static resource::MotionMeshMatrix MultiplyStringMatrices(const resource::MotionMeshMatrix& first, const resource::MotionMeshMatrix& second) {
		resource::MotionMeshMatrix result;
		result.XX = first.XY * second.YX + first.XX * second.XX;
		result.XY = first.XY * second.YY + first.XX * second.XY;
		result.YX = first.YY * second.YX + first.YX * second.XX;
		result.YY = first.YY * second.YY + first.YX * second.XY;
		result.TranslationX = (second.TranslationY * first.XY + second.TranslationX * first.XX) + first.TranslationX;
		result.TranslationY = (second.TranslationY * first.YY + second.TranslationX * first.YX) + first.TranslationY;
		return result;
	}

	class EmoteStringMeshOutput::Implementation {
	public:
		Implementation(EmoteStringPhysics& physics, EmoteStringMeshOutputDefinition definition)
		    : Physics(physics), Inputs(physics.Definition()), Definition(std::move(definition)), RigidRegion(physics.EncodedDegree(), Definition.RigidRange, Inputs.SizeHorizontal, Inputs.SizeVertical, physics.HorizontalBones()) {
			if (Inputs.LongitudinalDegree < 3 || Inputs.LongitudinalDegree > 6) {
				throw std::runtime_error("Emote String output requires recovered native degree3..6 inverse basis");
			}
			if (Definition.Fixes.size() > 3) {
				throw std::runtime_error("Emote String output exceeds native three-point fix storage");
			}
			for (const auto value : Definition.RigidRange) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote String rigid-range requires finite actual boundaries");
				}
			}
			if (Definition.RigidIterations > 4096 || (!RigidRegion.Empty() && Definition.RigidIterations > 64000000 / (RigidRegion.Count() * MotionBezierLatticeControlCount(physics.EncodedDegree())))) {
				throw std::runtime_error("Emote String rigid iterations exceed bounded projection work");
			}
			for (const auto& fix : Definition.Fixes) {
				if (!std::isfinite(fix.X) || !std::isfinite(fix.Y)) {
					throw std::runtime_error("Emote String output fix requires finite authored coordinates");
				}
			}
			if (Definition.Fixes.size() > 1) {
				MultipleFixes.emplace(Definition.Fixes, Inputs.SizeHorizontal, Inputs.SizeVertical);
			}
			Output = IdentityMotionBezierLattice(physics.EncodedDegree());
			Baseline = Output.Controls;
			Scratch.resize(Output.Controls.size());
			EmotePhysicalPosture initial;
			initial.Rotation = Inputs.DefaultRotation;
			initial.Cosine = std::cos(initial.Rotation);
			initial.Sine = std::sin(initial.Rotation);
			// Native309af0 captures this before enabling either metadata correction flag.
			Prepare(initial, 0, 1, {}, true);
			for (std::size_t index = 0; index != Baseline.size(); ++index) {
				Baseline[index].X = Baseline[index].X - Output.Controls[index].X;
				Baseline[index].Y = Baseline[index].Y - Output.Controls[index].Y;
			}
		}

		void Prepare(const EmotePhysicalPosture& posture, float correction_angle, float correction_scale, std::span<const resource::MotionPoint> targets, bool initial, bool unordered_comparison = false) {
			for (const auto value : {posture.Coordinate[0], posture.Coordinate[1], posture.Coordinate[2], posture.Rotation, posture.Cosine, posture.Sine, correction_angle, correction_scale}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("Emote String lattice output requires actual finite completed posture and correction inputs");
				}
			}
			if (!initial && Definition.CorrectFix && targets.size() != Definition.Fixes.size()) {
				throw std::runtime_error("Emote String fix correction requires the actual transformed world target for every authored fix");
			}
			for (const auto& target : targets) {
				if (!std::isfinite(target.X) || !std::isfinite(target.Y)) {
					throw std::runtime_error("Emote String fix target exceeds its finite actual world domain");
				}
			}
			const auto samples = unordered_comparison ? Physics.PrepareSamplesForUnorderedComparison() : Physics.PreparePhysicalSamples();
			for (std::size_t index = 0; index != samples.size(); ++index) {
				Output.Controls[index] = {samples[index][0], samples[index][1]};
			}
			const auto internal_degree = static_cast<std::uint8_t>((Inputs.LongitudinalDegree << 4) | 3);
			if (unordered_comparison) {
				ConvertUnorderedMotionBezierSamplesToControls(Output.Controls, internal_degree, {}, Physics.LongitudinalKnots());
			} else {
				ConvertMotionBezierSamplesToControls(Output.Controls, internal_degree, {}, Physics.LongitudinalKnots());
			}
			if (Physics.HorizontalBones()) {
				const auto longitudinal = static_cast<std::size_t>(Inputs.LongitudinalDegree) + 1;
				for (std::size_t row = 0; row != 4; ++row) {
					for (std::size_t column = 0; column != longitudinal; ++column) {
						Scratch[row * longitudinal + column] = Output.Controls[column * 4 + (3 - row)];
					}
				}
				std::copy(Scratch.begin(), Scratch.end(), Output.Controls.begin());
			}
			const auto world = StringWorldMatrix(Inputs, posture);
			const auto inverse = InverseStringMatrix(world);
			// Native30cb2c skips the entire inverse/rigid/world roundtrip when the curve set is empty.
			if (!RigidRegion.Empty()) {
				TransformStringLattice(Output, inverse);
				for (std::uint32_t iteration = 0; iteration != Definition.RigidIterations; ++iteration) {
					const float fraction = Definition.RigidIterations == 1 ? 1.0f : static_cast<float>(iteration) / static_cast<float>(Definition.RigidIterations - 1);
					RigidRegion.ApplyImpl(Output, 1.1f - fraction * 0.1f, unordered_comparison);
				}
				TransformStringLattice(Output, world);
			}
			if (!initial && Definition.CorrectScale) {
				for (auto& point : Output.Controls) {
					point.X = (point.X - posture.Coordinate[0]) * correction_scale + posture.Coordinate[0];
					point.Y = (point.Y - posture.Coordinate[1]) * correction_scale + posture.Coordinate[1];
				}
			}
			if (MultipleFixes) {
				if (unordered_comparison) {
					MultipleFixes->NormalizeForUnorderedComparison(Output, posture, inverse);
				} else {
					MultipleFixes->Normalize(Output, posture, inverse);
				}
			} else {
				auto normalized = inverse;
				if (!Definition.Fixes.empty()) {
					const auto actual_fix = unordered_comparison ? EvaluateUnorderedMotionBezierLattice(Output, Definition.Fixes.front()) : EvaluateMotionBezierLattice(Output, Definition.Fixes.front());
					const auto dx = posture.Coordinate[0] - actual_fix.X;
					const auto dy = posture.Coordinate[1] - actual_fix.Y;
					normalized.TranslationX = (inverse.XX * dx + inverse.XY * dy) + inverse.TranslationX;
					normalized.TranslationY = (inverse.YY * dy + inverse.YX * dx) + inverse.TranslationY;
				}
				TransformStringLattice(Output, normalized);
			}
			if (!initial && Definition.CorrectFix && !Definition.Fixes.empty()) {
				auto corrected = posture;
				corrected.Rotation = correction_angle;
				corrected.Cosine = std::cos(correction_angle);
				corrected.Sine = std::sin(correction_angle);
				const auto correction_inverse = InverseStringMatrix(StringWorldMatrix(Inputs, corrected));
				if (MultipleFixes) {
					if (unordered_comparison) {
						MultipleFixes->CorrectForUnorderedComparison(Output, targets, correction_inverse);
					} else {
						MultipleFixes->Correct(Output, targets, correction_inverse);
					}
				} else {
					auto pivot = InverseStringMatrix(correction_inverse);
					const auto fix = TransformStringPoint(pivot, Definition.Fixes.front());
					pivot.TranslationX = (pivot.TranslationX - fix.X) + targets.front().X;
					pivot.TranslationY = (pivot.TranslationY - fix.Y) + targets.front().Y;
					TransformStringLattice(Output, MultiplyStringMatrices(correction_inverse, pivot));
				}
			}
			if (!initial) {
				for (std::size_t index = 0; index != Output.Controls.size(); ++index) {
					Output.Controls[index].X = Output.Controls[index].X + Baseline[index].X;
					Output.Controls[index].Y = Output.Controls[index].Y + Baseline[index].Y;
				}
			}
			if (!unordered_comparison) {
				ValidateMotionBezierLattice(Output);
			}
		}

		EmoteStringPhysics& Physics;
		const EmoteStringDefinition& Inputs;
		EmoteStringMeshOutputDefinition Definition;
		EmoteRigidRegionConstraints RigidRegion;
		std::optional<EmoteMeshFixAlignment> MultipleFixes;
		resource::MotionBezierLattice Output;
		std::vector<resource::MotionPoint> Baseline;
		std::vector<resource::MotionPoint> Scratch;
		bool Operating = false;
		bool Valid = true;
	};

	EmoteStringMeshOutput::EmoteStringMeshOutput(EmoteStringPhysics& actual_physics, EmoteStringMeshOutputDefinition definition)
	    : m_Implementation(std::make_unique<Implementation>(actual_physics, std::move(definition))) {
	}
	EmoteStringMeshOutput::~EmoteStringMeshOutput() noexcept = default;

	const resource::MotionBezierLattice& EmoteStringMeshOutput::Prepare(const EmotePhysicalPosture& completed_posture, float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets) {
		return PrepareImpl(completed_posture, correction_angle, correction_scale, world_fix_targets, false);
	}

	const resource::MotionBezierLattice& EmoteStringMeshOutput::PrepareForUnorderedComparison(const EmotePhysicalPosture& completed_posture, float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets) {
		return PrepareImpl(completed_posture, correction_angle, correction_scale, world_fix_targets, true);
	}

	const resource::MotionBezierLattice& EmoteStringMeshOutput::PrepareImpl(const EmotePhysicalPosture& completed_posture, float correction_angle, float correction_scale, std::span<const resource::MotionPoint> world_fix_targets, bool unordered_comparison) {
		auto& state = *m_Implementation;
		if (state.Operating || !state.Valid) {
			throw std::runtime_error("Emote String mesh output is active or invalid after a failed preparation");
		}
		state.Operating = true;
		try {
			state.Prepare(completed_posture, correction_angle, correction_scale, world_fix_targets, false, unordered_comparison);
			state.Operating = false;
		} catch (...) {
			state.Operating = false;
			state.Valid = false;
			throw;
		}
		return state.Output;
	}
} // namespace sgre::scene
