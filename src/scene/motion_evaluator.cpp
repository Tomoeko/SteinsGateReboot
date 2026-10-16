#include "sgre/scene/motion_evaluator.hpp"

#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_timeline_variables.hpp"

#include "sgre/scene/motion_deformation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace sgre::scene {

	static constexpr std::uint32_t kKnownAttributeMask = 0x8403ffff;
	static constexpr std::size_t kCoordinateEasing = 0;
	static constexpr std::size_t kColorEasing = 1;
	static constexpr std::size_t kAngleEasing = 2;
	static constexpr std::size_t kZoomEasing = 3;
	static constexpr std::size_t kShearEasing = 4;

	static const resource::PsbValue& RequireField(const resource::PsbValue& value, std::string_view name) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			throw std::runtime_error("layer attribute is missing: " + std::string(name));
		}

		return *field;
	}

	static float ReadNumber(const resource::PsbValue& value, std::string_view name) {
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		const auto* real = std::get_if<double>(&value.Data);
		const auto number = integer != nullptr ? static_cast<double>(*integer) : real != nullptr ? *real
		                                                                                         : std::numeric_limits<double>::quiet_NaN();
		if (!std::isfinite(number) || std::abs(number) > 1000000000) {
			throw std::runtime_error("layer numeric attribute is invalid: " + std::string(name));
		}

		return static_cast<float>(number);
	}

	static std::uint32_t ReadWord(const resource::PsbValue& value, std::string_view name) {
		if (const auto* real = std::get_if<double>(&value.Data)) {
			if (!std::isfinite(*real) || *real < std::numeric_limits<std::int32_t>::min() || *real > std::numeric_limits<std::int32_t>::max()) {
				throw std::runtime_error("layer floating integer attribute is invalid: " + std::string(name));
			}

			// Native 0x1404ca8a0 uses truncating SSE conversions for floating PSB values.
			return static_cast<std::uint32_t>(static_cast<std::int32_t>(*real));
		}

		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1u : 0u;
		}

		const auto* number = std::get_if<std::int64_t>(&value.Data);
		if (number == nullptr || *number < std::numeric_limits<std::int32_t>::min() || *number > std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("layer integer attribute is invalid: " + std::string(name));
		}

		return static_cast<std::uint32_t>(*number);
	}

	static bool ReadBoolean(const resource::PsbValue& value, std::string_view name) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}

		return ReadNumber(value, name) != 0;
	}

	static std::string ReadText(const resource::PsbValue& value, std::string_view name) {
		const auto* text = std::get_if<std::string>(&value.Data);
		if (text == nullptr) {
			throw std::runtime_error("layer string attribute is invalid: " + std::string(name));
		}

		return *text;
	}

	static void ReadEasingIndex(MotionLayerAttributes& attributes, const resource::PsbValue& value, std::string_view name, std::size_t slot, const resource::MotionResource& resource) {
		const auto* field = value.Find(name);
		if (field == nullptr || std::holds_alternative<std::monostate>(field->Data)) {
			return;
		}

		const auto index = ReadWord(*field, name);
		if (index >= resource.Easing.size()) {
			throw std::runtime_error("layer easing index exceeds the resource curve table: " + std::string(name));
		}

		attributes.EasingIndices[slot] = index;
	}

	static MotionLayerAttributes ReadAttributes(const resource::MotionFrame& frame, const resource::MotionResource& resource, bool decode_mesh, bool decode_source_names, MotionAttributeBlendPolicy blend_policy) {
		MotionLayerAttributes attributes;
		const auto& value = frame.Content;
		attributes.Mask = ReadWord(RequireField(value, "mask"), "mask");
		attributes.UnresolvedMask = attributes.Mask & ~kKnownAttributeMask;
		if (decode_mesh && (attributes.Mask & 0x02000000) != 0) {
			attributes.Deformation = resource::ReadMotionDeformation(RequireField(value, "mesh"), resource);
			attributes.UnresolvedMask &= ~std::uint32_t{0x02000000};
		}

		if ((attributes.Mask & 0x80000000) != 0) {
			attributes.ModulationColor = ReadWord(RequireField(value, "cw"), "cw");
		}

		if (decode_source_names) {
			if (const auto* source = value.Find("src")) {
				attributes.Source = ReadText(*source, "src");
			}

			if (const auto* icon = value.Find("icon")) {
				attributes.Icon = ReadText(*icon, "icon");
			}
		}

		if ((attributes.Mask & 1) != 0) {
			attributes.OriginX = ReadNumber(RequireField(value, "ox"), "ox");
			attributes.OriginY = ReadNumber(RequireField(value, "oy"), "oy");
		}

		if ((attributes.Mask & 2) != 0) {
			const auto* coordinate = std::get_if<resource::PsbArray>(&RequireField(value, "coord").Data);
			if (coordinate == nullptr || coordinate->size() != 3) {
				throw std::runtime_error("layer coordinate attribute requires three components");
			}

			for (std::size_t index = 0; index < 3; ++index) {
				attributes.Coordinate[index] = ReadNumber((*coordinate)[index], "coord");
			}
		}

		if ((attributes.Mask & 0x0c) != 0) {
			attributes.FlipX = ReadBoolean(RequireField(value, "fx"), "fx");
			attributes.FlipY = ReadBoolean(RequireField(value, "fy"), "fy");
		}

		if ((attributes.Mask & 0x10) != 0) {
			attributes.Angle = ReadNumber(RequireField(value, "angle"), "angle");
		}

		if ((attributes.Mask & 0x60) != 0) {
			attributes.ZoomX = ReadNumber(RequireField(value, "zx"), "zx");
			attributes.ZoomY = ReadNumber(RequireField(value, "zy"), "zy");
		}

		if ((attributes.Mask & 0x180) != 0) {
			attributes.ShearX = ReadNumber(RequireField(value, "sx"), "sx");
			attributes.ShearY = ReadNumber(RequireField(value, "sy"), "sy");
		}

		if ((attributes.Mask & 0x400) != 0) {
			attributes.Opacity = ReadWord(RequireField(value, "opa"), "opa");
			if (attributes.Opacity > 255) {
				throw std::runtime_error("layer opacity exceeds its byte range");
			}
		}

		if ((attributes.Mask & 0x20000) != 0) {
			attributes.BlendMode = ReadWord(RequireField(value, "bm"), "bm");
		}

		if ((attributes.Mask & 0x200) != 0) {
			attributes.UsesDefaultColor = false;
			const auto& color = RequireField(value, "color");
			if (const auto* corners = std::get_if<resource::PsbArray>(&color.Data)) {
				if (corners->size() != 4) {
					throw std::runtime_error("layer corner color attribute requires four colors");
				}

				attributes.UniformColor = false;
				attributes.Colors[0] = ReadWord((*corners)[0], "color");
				attributes.Colors[1] = ReadWord((*corners)[1], "color");
				attributes.Colors[2] = ReadWord((*corners)[3], "color");
				attributes.Colors[3] = ReadWord((*corners)[2], "color");
			} else {
				attributes.Colors.fill(ReadWord(color, "color"));
			}
		} else if ((attributes.BlendMode & 0xf0) == 0) {
			attributes.Colors.fill(0xffffffff);
		}

		if (blend_policy == MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble && (attributes.BlendMode & 0xf0) == 0) {
			// Native 5131c6..513236 preserves alpha and halves each decoded corner before tween evaluation.
			attributes.BlendMode = (attributes.BlendMode & 0xffffff1f) | 0x10;
			for (auto& color : attributes.Colors) {
				color = ((color >> 1) & 0x7f7f7f00) | (color & 0xff);
			}
		}

		if (frame.Type == 3) {
			if ((attributes.Mask & 0x04000000) != 0) {
				attributes.Interval = ReadWord(RequireField(value, "ti"), "ti");
			}

			if ((attributes.Mask & 0x800) != 0) {
				ReadEasingIndex(attributes, value, "ccc", kCoordinateEasing, resource);
			}

			if ((attributes.Mask & 0x8000) != 0) {
				ReadEasingIndex(attributes, value, "occ", kColorEasing, resource);
			}

			if ((attributes.Mask & 0x1000) != 0) {
				ReadEasingIndex(attributes, value, "acc", kAngleEasing, resource);
			}

			if ((attributes.Mask & 0x2000) != 0) {
				ReadEasingIndex(attributes, value, "zcc", kZoomEasing, resource);
			}

			if ((attributes.Mask & 0x4000) != 0) {
				ReadEasingIndex(attributes, value, "scc", kShearEasing, resource);
			}
		}

		if ((attributes.Mask & 0x10000) != 0) {
			const auto* path = value.Find("cp");
			if (path != nullptr && !std::holds_alternative<std::monostate>(path->Data)) {
				attributes.CoordinatePath = resource::ReadMotionPath(*path);
			}
		}

		return attributes;
	}

	PreparedMotionLayer PrepareMotionLayer(const resource::MotionLayer& layer, const resource::MotionResource& resource) {
		return PrepareMotionLayer(layer, resource, MotionAttributeBlendPolicy::Unchanged);
	}

	PreparedMotionLayer PrepareMotionLayer(const resource::MotionLayer& layer, const resource::MotionResource& resource, MotionAttributeBlendPolicy blend_policy) {
		if (blend_policy != MotionAttributeBlendPolicy::Unchanged && blend_policy != MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble) {
			throw std::runtime_error("motion attribute preparation requires a supported player blend policy");
		}

		if (layer.Frames.size() < 2) {
			throw std::runtime_error("motion layer requires current and following frames");
		}

		PreparedMotionLayer prepared;
		prepared.Label = layer.Label;
		prepared.Type = layer.Type;
		prepared.InheritMask = layer.InheritMask;
		prepared.CoordinatePlane = layer.Coordinate;
		if (layer.TransformOrder.size() != prepared.TransformOrder.size()) {
			throw std::runtime_error("motion transform order requires four operations");
		}

		for (std::size_t index = 0; index < prepared.TransformOrder.size(); ++index) {
			if (layer.TransformOrder[index] > 3) {
				throw std::runtime_error("unsupported motion transform operation");
			}

			prepared.TransformOrder[index] = static_cast<MotionTransformOperation>(layer.TransformOrder[index]);
		}

		for (const auto& [name, value] : layer.Properties) {
			if (name == "rotateTransition") {
				prepared.Rotation = ReadWord(value, name) == 1 ? resource::RotationTransition::Directed : resource::RotationTransition::Shortest;
			} else if (name == "stencilType") {
				prepared.StencilType = ReadWord(value, name);
			} else if (name == "meshTransform") {
				prepared.MeshTransform = ReadWord(value, name);
			} else if (name == "hasColorModifier") {
				prepared.HasColorModifier = ReadBoolean(value, name);
			}
		}

		if (prepared.MeshTransform != 0) {
			for (const auto& [name, value] : layer.Properties) {
				if (name == "meshSyncChildMask") {
					prepared.MeshSyncChildMask = ReadWord(value, name);
				} else if (name == "meshDivision") {
					prepared.MeshDivision = ReadWord(value, name);
				} else if (name == "meshCombinator") {
					// Native lookup records this property's presence, including a null value.
					prepared.MeshCombinator = true;
				} else if (name == "meshCombine") {
					prepared.MeshCombine = ReadBoolean(value, name);
				}
			}
		}

		prepared.Frames.reserve(layer.Frames.size());
		for (const auto& frame : layer.Frames) {
			if (!std::isfinite(frame.Time) || frame.Time < 0 || frame.Type > 3 || (!prepared.Frames.empty() && frame.Time < prepared.Frames.back().Time)) {
				throw std::runtime_error("layer frame time or type is invalid");
			}

			PreparedMotionFrame state;
			state.Time = frame.Time;
			state.Hidden = frame.Type == 0;
			state.Tween = frame.Type == 3;
			if (!state.Hidden) {
				// Native512e40 mask0x1849 excludes structural kind2/kind7 from source/icon decoding.
				state.Attributes = ReadAttributes(frame, resource, prepared.MeshTransform == 1, layer.Type != 2 && layer.Type != 7, blend_policy);
			}

			prepared.Frames.push_back(std::move(state));
		}

		return prepared;
	}

	static const resource::EasingCurve* EasingAt(const MotionLayerAttributes& attributes, std::size_t slot, const resource::MotionResource& resource) {
		const auto index = attributes.EasingIndices[slot];
		if (!index.has_value()) {
			return nullptr;
		}

		if (*index >= resource.Easing.size()) {
			throw std::runtime_error("prepared layer easing index exceeds its resource");
		}

		return &resource.Easing[*index];
	}

	MotionLayerSample SamplePreparedMotionFrame(const PreparedMotionFrame& frame) {
		const auto& attributes = frame.Attributes;
		MotionLayerSample sample;
		sample.Source = attributes.Source;
		sample.Icon = attributes.Icon;
		sample.Coordinate = attributes.Coordinate;
		sample.Colors = attributes.Colors;
		sample.OriginX = attributes.OriginX;
		sample.OriginY = attributes.OriginY;
		sample.Angle = attributes.Angle;
		sample.ZoomX = attributes.ZoomX;
		sample.ZoomY = attributes.ZoomY;
		sample.ShearX = attributes.ShearX;
		sample.ShearY = attributes.ShearY;
		sample.Opacity = attributes.Opacity;
		sample.BlendMode = attributes.BlendMode;
		sample.ModulationColor = attributes.ModulationColor;
		sample.UnresolvedMask = attributes.UnresolvedMask;
		sample.Deformation = attributes.Deformation.Patch;
		sample.Hidden = frame.Hidden;
		sample.FlipX = attributes.FlipX;
		sample.FlipY = attributes.FlipY;
		sample.UniformColor = attributes.UniformColor;
		sample.UsesDefaultColor = attributes.UsesDefaultColor;

		return sample;
	}

	static std::array<float, 3> InterpolateCoordinates(const MotionLayerAttributes& start, const MotionLayerAttributes& end, float ratio, const resource::EasingCurve* easing, std::uint32_t plane) {
		if (start.Coordinate == end.Coordinate) {
			return start.Coordinate;
		}

		const auto mapped = easing == nullptr ? ratio : resource::EvaluateEasing(*easing, ratio);
		std::array<float, 3> coordinate{};
		if (!start.CoordinatePath.has_value()) {
			for (std::size_t component = 0; component < 3; ++component) {
				coordinate[component] = (1.0f - mapped) * start.Coordinate[component] + mapped * end.Coordinate[component];
			}

			return coordinate;
		}

		if (plane > 1) {
			throw std::runtime_error("unsupported motion coordinate path plane");
		}

		const auto point = resource::EvaluateMotionPath(*start.CoordinatePath, mapped);
		const std::size_t planar_axis = plane == 0 ? 1 : 2;
		const std::size_t linear_axis = plane == 0 ? 2 : 1;
		const auto delta_x = end.Coordinate[0] - start.Coordinate[0];
		const auto delta_planar = end.Coordinate[planar_axis] - start.Coordinate[planar_axis];
		coordinate[0] = delta_x * point.X + start.Coordinate[0] - delta_planar * point.Y;
		coordinate[planar_axis] = delta_planar * point.X + start.Coordinate[planar_axis] + delta_x * point.Y;
		coordinate[linear_axis] = (1.0f - mapped) * start.Coordinate[linear_axis] + mapped * end.Coordinate[linear_axis];

		return coordinate;
	}

	std::array<float, 3> InterpolateMotionChildCoordinates(const PreparedMotionFrame& current, const PreparedMotionFrame& following, const resource::MotionResource& resource, float ratio, std::uint32_t coordinate_plane) {
		if (coordinate_plane > 1 || current.Attributes.CoordinatePath.has_value()) {
			throw std::runtime_error("motion child coordinate derivative requires its recovered XY/XZ pair without an unverified path owner");
		}

		if (!std::isfinite(ratio)) {
			throw std::runtime_error("motion child coordinate derivative requires a finite selected ratio");
		}

		for (std::size_t index = 0; index < 3; ++index) {
			if (!std::isfinite(current.Attributes.Coordinate[index]) || !std::isfinite(following.Attributes.Coordinate[index])) {
				throw std::runtime_error("motion child coordinate derivative requires finite selected endpoints");
			}
		}

		if (current.Attributes.Coordinate == following.Attributes.Coordinate) {
			return current.Attributes.Coordinate;
		}

		return InterpolateCoordinates(current.Attributes, following.Attributes, ratio, EasingAt(current.Attributes, kCoordinateEasing, resource), coordinate_plane);
	}

	MotionLayerSample EvaluateMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position) {
		if (!std::isfinite(position) || layer.Frames.size() < 2 || position < layer.Frames.front().Time) {
			throw std::runtime_error("motion layer position is nonfinite or before its frame domain");
		}

		std::size_t current_index = 0;
		while (current_index + 2 < layer.Frames.size() && position >= layer.Frames[current_index + 1].Time) {
			++current_index;
		}

		return EvaluateSelectedMotionLayer(layer, resource, position, current_index);
	}

	static void RequireSelectedFrame(const PreparedMotionLayer& layer, float position, std::size_t current_index, bool variable_position = false) {
		if (!std::isfinite(position) || layer.Frames.size() < 2 || current_index >= layer.Frames.size() - 1 || !std::isfinite(layer.Frames[current_index].Time) || position < 0 || (!variable_position && position < layer.Frames[current_index].Time)) {
			throw std::runtime_error("selected motion layer interval or evaluation position is outside its recovered domain");
		}
	}

	static float SelectedTweenRatio(const PreparedMotionFrame& current, const PreparedMotionFrame& next, float position) {
		const auto duration = next.Time - current.Time;
		if (!std::isfinite(duration) || duration <= 0) {
			throw std::runtime_error("motion layer tween requires a finite positive adjacent duration");
		}

		auto elapsed = position - current.Time;
		if (current.Attributes.Interval != 0) {
			const auto interval = current.Attributes.Interval;
			const auto steps = elapsed / static_cast<float>(interval);
			if (!(steps >= -2147483648.0f && steps < 2147483648.0f)) {
				throw std::runtime_error("motion tween interval count exceeds its integer limit");
			}

			elapsed = static_cast<float>(static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(steps)) * interval));
		}

		const auto ratio = elapsed / duration;
		if (!std::isfinite(ratio)) {
			throw std::runtime_error("motion selected tween extrapolation exceeds its finite ratio domain");
		}

		return ratio;
	}

	static std::uint32_t RoundedMotionOpacity(float value) {
		const auto rounded = std::round(value);
		// Native50c0b7 truncates to signed64, then50c0bc stores only EAX. Integer-indefinite has a zero low word.
		if (!(rounded >= -0x1p63f && rounded < 0x1p63f)) {
			return 0;
		}

		return static_cast<std::uint32_t>(static_cast<std::int64_t>(rounded));
	}

	MotionLayerSample EvaluateSelectedMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index) {
		RequireSelectedFrame(layer, position, current_index);
		return EvaluateSelectedMotionLayer(layer, resource, position, current_index, layer.Frames[current_index], layer.Frames[current_index + 1]);
	}

	static MotionLayerSample EvaluateSelectedMotionLayerValues(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, const PreparedMotionFrame& current, const PreparedMotionFrame& next, bool variable_position) {
		RequireSelectedFrame(layer, position, current_index, variable_position);
		if (current.Time != layer.Frames[current_index].Time || current.Hidden != layer.Frames[current_index].Hidden || current.Tween != layer.Frames[current_index].Tween || next.Time != layer.Frames[current_index + 1].Time || next.Hidden != layer.Frames[current_index + 1].Hidden || next.Tween != layer.Frames[current_index + 1].Tween) {
			throw std::runtime_error("motion decoded JOIN slots must preserve the actual selected pair timing and visibility");
		}
		auto sample = SamplePreparedMotionFrame(current);
		if (current.Hidden || !current.Tween || next.Hidden) {
			return sample;
		}

		const auto& start = current.Attributes;
		const auto& end = next.Attributes;
		if (start.Opacity > 255 || end.Opacity > 255) {
			throw std::runtime_error("motion selected tween opacity exceeds its recovered byte domain");
		}

		const auto ratio = SelectedTweenRatio(current, next, position);
		if (std::abs(ratio) < std::numeric_limits<float>::epsilon()) {
			return sample;
		}

		const auto* coordinate_easing = EasingAt(start, kCoordinateEasing, resource);
		sample.Coordinate = InterpolateCoordinates(start, end, ratio, coordinate_easing, layer.CoordinatePlane);

		sample.Angle = resource::InterpolateAngle(start.Angle, end.Angle, ratio, layer.Rotation, EasingAt(start, kAngleEasing, resource));
		sample.ZoomX = resource::InterpolateScalar(start.ZoomX, end.ZoomX, ratio, EasingAt(start, kZoomEasing, resource));
		sample.ZoomY = resource::InterpolateScalar(start.ZoomY, end.ZoomY, ratio, EasingAt(start, kZoomEasing, resource));
		sample.ShearX = resource::InterpolateScalar(start.ShearX, end.ShearX, ratio, EasingAt(start, kShearEasing, resource));
		sample.ShearY = resource::InterpolateScalar(start.ShearY, end.ShearY, ratio, EasingAt(start, kShearEasing, resource));
		// Native opacity performs this raw arithmetic even for equal endpoints and never uses a color curve.
		const auto opacity = (1.0f - ratio) * static_cast<float>(start.Opacity) + ratio * static_cast<float>(end.Opacity);
		sample.Opacity = RoundedMotionOpacity(opacity);
		sample.UniformColor = start.UniformColor && end.UniformColor;
		sample.UsesDefaultColor = start.UsesDefaultColor && end.UsesDefaultColor;
		if (layer.MeshTransform == 1) {
			const auto index = start.Deformation.EasingIndex;
			if (index && *index >= resource.Easing.size()) {
				throw std::runtime_error("prepared mesh easing index exceeds its resource");
			}

			const auto* mesh_easing = index ? &resource.Easing[*index] : nullptr;
			sample.Deformation = InterpolateMotionBezierPatch(start.Deformation.Patch, end.Deformation.Patch, ratio, mesh_easing);
		}

		if (layer.HasColorModifier) {
			sample.ModulationColor = resource::InterpolatePackedColor(start.ModulationColor, end.ModulationColor, ratio, EasingAt(start, kColorEasing, resource));
		}

		for (std::size_t corner = 0; corner < sample.Colors.size(); ++corner) {
			sample.Colors[corner] = resource::InterpolatePackedColor(start.Colors[corner], end.Colors[corner], ratio, EasingAt(start, kColorEasing, resource));
		}

		return sample;
	}

	MotionLayerSample EvaluateSelectedMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, const PreparedMotionFrame& current, const PreparedMotionFrame& next) {
		return EvaluateSelectedMotionLayerValues(layer, resource, position, current_index, current, next, false);
	}

	bool EvaluateCachedMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, bool force, bool frame_dirty, MotionLayerEvaluationCache& cache) {
		RequireSelectedFrame(layer, position, current_index);
		return EvaluateCachedMotionLayer(layer, resource, position, current_index, layer.Frames[current_index], layer.Frames[current_index + 1], force, frame_dirty, cache);
	}

	static bool EvaluateCachedMotionLayerValues(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, const PreparedMotionFrame& current, const PreparedMotionFrame& next, bool force, bool frame_dirty, MotionLayerEvaluationCache& cache, bool variable_position) {
		RequireSelectedFrame(layer, position, current_index, variable_position);
		if (current.Time != layer.Frames[current_index].Time || current.Hidden != layer.Frames[current_index].Hidden || current.Tween != layer.Frames[current_index].Tween || next.Time != layer.Frames[current_index + 1].Time || next.Hidden != layer.Frames[current_index + 1].Hidden || next.Tween != layer.Frames[current_index + 1].Tween) {
			throw std::runtime_error("motion decoded JOIN slots must preserve the actual selected pair timing and visibility");
		}
		if (!std::isfinite(cache.Ratio)) {
			throw std::runtime_error("motion local ratio cache is outside its recovered finite domain");
		}

		bool changed = force || frame_dirty;
		auto resulting_ratio = cache.Ratio;
		if (!current.Hidden && current.Tween && !next.Hidden) {
			const auto ratio = SelectedTweenRatio(current, next, position);
			const auto difference = std::abs(cache.Ratio - ratio);
			if (std::abs(ratio) < std::numeric_limits<float>::epsilon()) {
				// Native updates tiny ratios even if no pose copy is needed.
				resulting_ratio = ratio;
				changed = changed || difference >= std::numeric_limits<float>::epsilon();
			} else if (changed || difference >= std::numeric_limits<float>::epsilon()) {
				resulting_ratio = ratio;
				changed = true;
			}
		}

		if (changed) {
			cache.Sample = EvaluateSelectedMotionLayerValues(layer, resource, position, current_index, current, next, variable_position);
		} else if (!cache.Sample.has_value()) {
			throw std::runtime_error("cached motion layer requires a genuine initial dirty selection before suppression");
		}

		cache.Ratio = resulting_ratio;

		return changed;
	}

	bool EvaluateCachedMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, const PreparedMotionFrame& current, const PreparedMotionFrame& next, bool force, bool frame_dirty, MotionLayerEvaluationCache& cache) {
		return EvaluateCachedMotionLayerValues(layer, resource, position, current_index, current, next, force, frame_dirty, cache, false);
	}

	bool EvaluateCachedMotionVariableLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, const PreparedMotionFrame& current, const PreparedMotionFrame& next, bool force, bool frame_dirty, MotionLayerEvaluationCache& cache, const MotionTimelineVariables& variables) {
		static_cast<void>(variables.RequireApplied(variables.m_Commands.ClockState().EvaluationTime));
		if (&resource != &variables.m_Resource || variables.Empty()) {
			throw std::runtime_error("motion variable numeric interpolation requires its actual applied resource owner");
		}

		return EvaluateCachedMotionLayerValues(layer, resource, position, current_index, current, next, force, frame_dirty, cache, true);
	}

	static MotionLinearTransform DefaultTransform(const MotionLayerSample& sample, float sine, float cosine) {
		const auto flip_x = sample.FlipX ? -1.0f : 1.0f;
		const auto flip_y = sample.FlipY ? -1.0f : 1.0f;
		const auto diagonal_x = sample.ShearX * 0.0f + flip_x;
		const auto diagonal_y = sample.ShearY * 0.0f + flip_y;
		const auto off_x = sample.ShearX * flip_y + 0.0f;
		const auto off_y = sample.ShearY * flip_x + 0.0f;
		const auto xx = diagonal_x * sample.ZoomX + off_y * 0.0f;
		const auto xy = off_x * sample.ZoomX + diagonal_y * 0.0f;
		const auto yx = off_y * sample.ZoomY + diagonal_x * 0.0f;
		const auto yy = diagonal_y * sample.ZoomY + off_x * 0.0f;
		MotionLinearTransform transform;
		transform.XX = -sine * yx + cosine * xx;
		transform.XY = -sine * yy + cosine * xy;
		transform.YX = cosine * yx + sine * xx;
		transform.YY = cosine * yy + sine * xy;

		return transform;
	}

	static MotionLinearTransform ApplyTransformOperation(const MotionLinearTransform& current, MotionTransformOperation operation, const MotionLayerSample& sample, float sine, float cosine) {
		MotionLinearTransform next;
		switch (operation) {
		case MotionTransformOperation::Flip:
			next.XX = current.XX * (sample.FlipX ? -1.0f : 1.0f) + current.YX * 0.0f;
			next.XY = current.XY * (sample.FlipX ? -1.0f : 1.0f) + current.YY * 0.0f;
			next.YX = current.YX * (sample.FlipY ? -1.0f : 1.0f) + current.XX * 0.0f;
			next.YY = current.YY * (sample.FlipY ? -1.0f : 1.0f) + current.XY * 0.0f;
			break;
		case MotionTransformOperation::Rotate:
			next.XX = -sine * current.YX + cosine * current.XX;
			next.XY = -sine * current.YY + cosine * current.XY;
			next.YX = cosine * current.YX + sine * current.XX;
			next.YY = cosine * current.YY + sine * current.XY;
			break;
		case MotionTransformOperation::Scale:
			next.XX = current.XX * sample.ZoomX + current.YX * 0.0f;
			next.XY = current.XY * sample.ZoomX + current.YY * 0.0f;
			next.YX = current.YX * sample.ZoomY + current.XX * 0.0f;
			next.YY = current.YY * sample.ZoomY + current.XY * 0.0f;
			break;
		case MotionTransformOperation::Shear:
			next.XX = current.YX * sample.ShearX + current.XX;
			next.XY = current.YY * sample.ShearX + current.XY;
			next.YX = current.XX * sample.ShearY + current.YX;
			next.YY = current.XY * sample.ShearY + current.YY;
			break;
		default:
			throw std::runtime_error("unsupported motion transform operation");
		}

		return next;
	}

	static double SinePolynomial(double radians) {
		const auto squared = radians * radians;
		const auto fourth = squared * squared;
		const auto correction = ((2.7557319223985893e-06 * squared - 0.00019841269841269841) * fourth + 0.0083333333333333332 * squared - 0.16666666666666666) * (radians * squared);

		return radians + correction;
	}

	static double CosinePolynomial(double radians) {
		const auto squared = radians * radians;
		const auto fourth = squared * squared;
		const auto correction = ((-2.7557319223985888e-07 * squared + 2.4801587301587298e-05) * fourth - 0.0013888888888888887 * squared + 0.041666666666666664) * fourth;

		return (1.0 - 0.5 * squared) + correction;
	}

	static std::array<float, 2> EvaluateRotationTrig(float radians) {
		// The original SSE CRT branch uses double reduction and polynomials before rounding to float.
		const auto magnitude_bits = std::bit_cast<std::uint32_t>(radians) & 0x7fffffff;
		const auto absolute = static_cast<double>(std::abs(radians));
		double sine = 0;
		double cosine = 1;
		if (magnitude_bits <= 0x3f490fdb) {
			const auto signed_radians = static_cast<double>(radians);
			sine = magnitude_bits < 0x39000000 ? signed_radians : magnitude_bits < 0x3c000000 ? signed_radians - signed_radians * signed_radians * signed_radians * 0.16666666666666666
			                                                                                  : SinePolynomial(signed_radians);
			cosine = CosinePolynomial(signed_radians);
		} else {
			const auto quadrant = static_cast<std::uint32_t>(absolute * 0.63661977236758138 + 0.5);
			const auto multiple = static_cast<double>(quadrant);
			const auto first = absolute - multiple * 1.5707963267341256;
			auto reduced = first - multiple * 6.0771005065061922e-11;
			const auto original_exponent = (std::bit_cast<std::uint64_t>(absolute) >> 52) & 0x7ff;
			const auto reduced_exponent = (std::bit_cast<std::uint64_t>(reduced) >> 52) & 0x7ff;
			if (original_exponent > reduced_exponent + 15) {
				const auto correction = multiple * 6.077100506303966e-11;
				const auto second = first - correction;
				reduced = second - (multiple * 2.0222662487959506e-21 - (first - second - correction));
			}

			const auto reduced_sine = reduced_exponent >= 0x3f2 ? SinePolynomial(reduced) : reduced_exponent > 0x3de ? reduced - 0.16666666666666666 * reduced * (reduced * reduced)
			                                                                                                         : reduced;
			const auto reduced_cosine = reduced_exponent >= 0x3f2 ? CosinePolynomial(reduced) : reduced_exponent > 0x3de ? 1.0 - 0.5 * (reduced * reduced)
			                                                                                                             : 1.0;
			sine = (quadrant & 1) != 0 ? reduced_cosine : reduced_sine;
			cosine = (quadrant & 1) != 0 ? reduced_sine : reduced_cosine;
			if (((quadrant >> 1) & 1) != static_cast<std::uint32_t>(std::signbit(radians))) {
				sine = 0.0 - sine;
			}

			if (((quadrant + 1) & 2) != 0) {
				cosine = 0.0 - cosine;
			}
		}

		return {static_cast<float>(sine), static_cast<float>(cosine)};
	}

	MotionLinearTransform RotateMotionChildTransform(const MotionLinearTransform& parent, float angle_delta, bool reverse_angle) {
		if (!std::isfinite(angle_delta) || std::abs(angle_delta) > 3600000.0f) {
			throw std::runtime_error("motion child rotation exceeds its finite recovered trigonometric range");
		}

		auto radians = angle_delta * 0.0174532924f;
		if (reverse_angle) {
			radians = -radians;
		}

		const auto trigonometry = EvaluateRotationTrig(radians);
		const auto sine = trigonometry[0];
		const auto cosine = trigonometry[1];
		MotionLinearTransform transform;
		transform.XX = parent.XY * sine + parent.XX * cosine;
		transform.XY = parent.XX * -sine + parent.XY * cosine;
		transform.YX = parent.YY * sine + parent.YX * cosine;
		transform.YY = parent.YX * -sine + parent.YY * cosine;

		return transform;
	}

	MotionLinearTransform EvaluateMotionTransform(const PreparedMotionLayer& layer, const MotionLayerSample& sample) {
		if (!std::isfinite(sample.Angle) || !std::isfinite(sample.ZoomX) || !std::isfinite(sample.ZoomY) || !std::isfinite(sample.ShearX) || !std::isfinite(sample.ShearY)) {
			throw std::runtime_error("motion transform contains nonfinite attributes");
		}

		if (std::abs(sample.Angle) > 3600000.0f) {
			throw std::runtime_error("motion rotation exceeds the recovered trigonometric range");
		}

		const auto radians = sample.Angle * 0.0174532924f;
		const auto trigonometry = EvaluateRotationTrig(radians);
		const auto sine = trigonometry[0];
		const auto cosine = trigonometry[1];
		const std::array<MotionTransformOperation, 4> default_order = {MotionTransformOperation::Flip, MotionTransformOperation::Shear, MotionTransformOperation::Scale, MotionTransformOperation::Rotate};
		if (layer.TransformOrder == default_order) {
			return DefaultTransform(sample, sine, cosine);
		}

		MotionLinearTransform transform;
		for (const auto operation : layer.TransformOrder) {
			transform = ApplyTransformOperation(transform, operation, sample, sine, cosine);
		}

		return transform;
	}

	static MotionLinearTransform MultiplyTransforms(const MotionLinearTransform& parent, const MotionLinearTransform& local) {
		MotionLinearTransform transform;
		transform.XX = parent.XY * local.YX + parent.XX * local.XX;
		transform.XY = local.YY * parent.XY + local.XY * parent.XX;
		transform.YX = parent.YY * local.YX + parent.YX * local.XX;
		transform.YY = local.YY * parent.YY + local.XY * parent.YX;

		return transform;
	}

	static std::array<float, 3> TransformCoordinates(const MotionHierarchyState& parent, const std::array<float, 3>& coordinate) {
		const auto& matrix = parent.Transform;
		const auto& position = parent.Attributes.Coordinate;
		std::array<float, 3> transformed{};
		if (parent.CoordinatePlane == 0) {
			transformed[0] = coordinate[1] * matrix.XY + coordinate[0] * matrix.XX + position[0];
			transformed[1] = coordinate[0] * matrix.YX + coordinate[1] * matrix.YY + position[1];
			transformed[2] = coordinate[2] + position[2];
		} else if (parent.CoordinatePlane == 1) {
			transformed[0] = coordinate[0] * matrix.XX + coordinate[2] * matrix.XY + position[0];
			transformed[1] = coordinate[1] + position[1];
			transformed[2] = coordinate[0] * matrix.YX + coordinate[2] * matrix.YY + position[2];
		} else {
			throw std::runtime_error("unsupported motion hierarchy coordinate plane");
		}

		return transformed;
	}

	static void InheritTransformAttributes(MotionLayerSample& local, const MotionLayerSample& parent, std::uint32_t mask) {
		if ((mask & 4) != 0) {
			local.FlipX = local.FlipX != parent.FlipX;
		}

		if ((mask & 8) != 0) {
			local.FlipY = local.FlipY != parent.FlipY;
		}

		if ((mask & 0x10) != 0) {
			local.Angle = parent.Angle + local.Angle;
		}

		if ((mask & 0x20) != 0) {
			local.ZoomX = parent.ZoomX * local.ZoomX;
		}

		if ((mask & 0x40) != 0) {
			local.ZoomY = parent.ZoomY * local.ZoomY;
		}

		if ((mask & 0x80) != 0) {
			local.ShearX = parent.ShearX + local.ShearX;
		}

		if ((mask & 0x100) != 0) {
			local.ShearY = parent.ShearY + local.ShearY;
		}
	}

	static void RemoveRootAttributes(MotionLayerSample& sample, const MotionLayerSample& root, std::uint32_t mask) {
		if (((mask & 0x20) != 0 && root.ZoomX == 0) || ((mask & 0x40) != 0 && root.ZoomY == 0)) {
			throw std::runtime_error("motion root normalization requires nonzero inherited scale");
		}

		if ((mask & 4) != 0) {
			sample.FlipX = sample.FlipX != root.FlipX;
		}

		if ((mask & 8) != 0) {
			sample.FlipY = sample.FlipY != root.FlipY;
		}

		if ((mask & 0x10) != 0) {
			sample.Angle = sample.Angle - root.Angle;
		}

		if ((mask & 0x20) != 0) {
			sample.ZoomX = sample.ZoomX / root.ZoomX;
		}

		if ((mask & 0x40) != 0) {
			sample.ZoomY = sample.ZoomY / root.ZoomY;
		}

		if ((mask & 0x80) != 0) {
			sample.ShearX = sample.ShearX - root.ShearX;
		}

		if ((mask & 0x100) != 0) {
			sample.ShearY = sample.ShearY - root.ShearY;
		}
	}

	MotionHierarchyState ComposeMotionHierarchyLayer(const PreparedMotionLayer& layer, const MotionLayerSample& local, const MotionHierarchyState& parent, const MotionHierarchyState& root, bool normalize_against_root) {
		MotionHierarchyState state;
		state.Attributes = local;
		state.CoordinatePlane = layer.CoordinatePlane;
		state.ModulationColor = (layer.InheritMask & 0x200) != 0 ? parent.ModulationColor : 0x808080ff;
		state.UnresolvedInheritMask = layer.InheritMask & ~std::uint32_t{0x7fc};
		if (local.Hidden) {
			state.Attributes.Coordinate = parent.Attributes.Coordinate;
			state.Attributes.Angle = parent.Attributes.Angle;
			state.Attributes.ZoomX = parent.Attributes.ZoomX;
			state.Attributes.ZoomY = parent.Attributes.ZoomY;
			state.Attributes.ShearX = parent.Attributes.ShearX;
			state.Attributes.ShearY = parent.Attributes.ShearY;
			state.Attributes.Opacity = parent.Attributes.Opacity;
			state.Attributes.FlipX = parent.Attributes.FlipX;
			state.Attributes.FlipY = parent.Attributes.FlipY;
			state.Transform = parent.Transform;

			return state;
		}

		state.Attributes.Coordinate = TransformCoordinates(parent, local.Coordinate);
		if (layer.HasColorModifier) {
			state.ModulationColor = ComposeMotionColor(state.ModulationColor, local.ModulationColor);
		}

		if (local.Opacity > 255 || parent.Attributes.Opacity > 255 || root.Attributes.Opacity > 255) {
			throw std::runtime_error("motion hierarchy opacity exceeds its byte range");
		}

		if ((layer.InheritMask & 0x400) != 0) {
			state.Attributes.Opacity = parent.Attributes.Opacity * local.Opacity / 255;
		} else if (normalize_against_root) {
			state.Attributes.Opacity = root.Attributes.Opacity * local.Opacity / 255;
		}

		if ((layer.InheritMask & 0x1fc) == 0x1fc) {
			state.Transform = MultiplyTransforms(parent.Transform, EvaluateMotionTransform(layer, local));
			InheritTransformAttributes(state.Attributes, parent.Attributes, layer.InheritMask);

			return state;
		}

		InheritTransformAttributes(state.Attributes, parent.Attributes, layer.InheritMask);
		if (!normalize_against_root) {
			state.Transform = EvaluateMotionTransform(layer, state.Attributes);

			return state;
		}

		RemoveRootAttributes(state.Attributes, root.Attributes, layer.InheritMask);
		state.Transform = MultiplyTransforms(root.Transform, EvaluateMotionTransform(layer, state.Attributes));
		InheritTransformAttributes(state.Attributes, root.Attributes, layer.InheritMask);

		return state;
	}

	std::uint32_t ComposeMotionColor(std::uint32_t first, std::uint32_t second) {
		if (first == 0x808080ff) {
			return second;
		}

		if (second == 0x808080ff) {
			return first;
		}

		std::uint32_t result = (first & 0xff) * (second & 0xff) / 255;
		for (std::size_t shift = 8; shift <= 24; shift += 8) {
			const auto product = (((first >> shift) & 0xff) * ((second >> shift) & 0xff)) >> 7;
			result |= (product > 255 ? 255 : product) << shift;
		}

		return result;
	}

	static void PrepareTimelineLayers(PreparedMotionTimeline& prepared, const std::vector<resource::MotionLayer>& layers, const resource::MotionResource& resource, std::optional<std::uint32_t> parent, std::size_t depth, MotionAttributeBlendPolicy blend_policy) {
		if (depth > 128) {
			throw std::runtime_error("motion preparation exceeds its hierarchy depth limit");
		}

		for (const auto& layer : layers) {
			if (prepared.Layers.size() >= 1000000) {
				throw std::runtime_error("motion preparation exceeds its layer count limit");
			}

			const auto index = static_cast<std::uint32_t>(prepared.Layers.size());
			prepared.Layers.push_back(PrepareMotionLayer(layer, resource, blend_policy));
			prepared.Parents.push_back(parent);
			PrepareTimelineLayers(prepared, layer.Children, resource, index, depth + 1, blend_policy);
		}
	}

	PreparedMotionTimeline PrepareMotionTimeline(const resource::MotionTimeline& timeline, const resource::MotionResource& resource) {
		return PrepareMotionTimeline(timeline, resource, MotionAttributeBlendPolicy::Unchanged);
	}

	PreparedMotionTimeline PrepareMotionTimeline(const resource::MotionTimeline& timeline, const resource::MotionResource& resource, MotionAttributeBlendPolicy blend_policy) {
		if (blend_policy != MotionAttributeBlendPolicy::Unchanged && blend_policy != MotionAttributeBlendPolicy::HalveRgbForClearBlendNibble) {
			throw std::runtime_error("motion timeline preparation requires a supported player blend policy");
		}

		if (timeline.Priority.size() < 2 || timeline.Priority.size() > 1000000) {
			throw std::runtime_error("motion priority requires a bounded track with a terminal frame");
		}

		PreparedMotionTimeline prepared;
		prepared.LastTime = timeline.LastTime;
		prepared.LoopTime = timeline.LoopTime;
		PrepareTimelineLayers(prepared, timeline.Layers, resource, std::nullopt, 0, blend_policy);
		for (std::size_t index = 0; index < timeline.Priority.size(); ++index) {
			const auto& frame = timeline.Priority[index];
			if (!std::isfinite(frame.Time) || frame.Time < 0 || frame.Type != 1 || (!prepared.Priority.empty() && frame.Time < prepared.Priority.back().Time)) {
				throw std::runtime_error("motion priority time or frame type is invalid");
			}

			PreparedMotionPriorityFrame priority;
			priority.Time = frame.Time;
			const auto* array = std::get_if<resource::PsbArray>(&frame.Content.Data);
			if (array == nullptr) {
				if (index + 1 != timeline.Priority.size() || !std::holds_alternative<std::monostate>(frame.Content.Data)) {
					throw std::runtime_error("motion priority content requires a layer index array");
				}
			} else {
				if (array->size() != prepared.Layers.size()) {
					throw std::runtime_error("motion priority index count differs from the flattened hierarchy");
				}

				priority.DrawOrder.reserve(array->size());
				for (auto iterator = array->rbegin(); iterator != array->rend(); ++iterator) {
					const auto layer_index = ReadWord(*iterator, "priority layer index");
					if (layer_index >= prepared.Layers.size()) {
						throw std::runtime_error("motion priority index exceeds the flattened hierarchy");
					}

					priority.DrawOrder.push_back(layer_index);
				}
			}

			prepared.Priority.push_back(std::move(priority));
		}

		return prepared;
	}

	std::span<const std::uint32_t> EvaluateMotionDrawOrder(const PreparedMotionTimeline& timeline, float position) {
		if (!std::isfinite(position) || timeline.Priority.size() < 2 || position < timeline.Priority.front().Time) {
			throw std::runtime_error("motion priority position is nonfinite or before its frame domain");
		}

		std::size_t current = 0;
		while (current + 2 < timeline.Priority.size() && position >= timeline.Priority[current + 1].Time) {
			++current;
		}

		return timeline.Priority[current].DrawOrder;
	}

} // namespace sgre::scene
