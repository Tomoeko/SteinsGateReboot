#include "sgre/resource/motion.hpp"

#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace sgre::resource {

	static constexpr std::size_t kMaxMotionItems = 1000000;
	static constexpr std::size_t kMaxMotionDepth = 128;
	static constexpr float kMaxMotionNumber = 1000000000;
	static constexpr float kMaxRotationDegrees = 3600000;

	static const PsbValue& RequireField(const PsbValue& value, std::string_view name) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			throw std::runtime_error("motion field is missing: " + std::string(name));
		}

		return *field;
	}

	static const PsbArray& ReadArray(const PsbValue& value, std::string_view context) {
		const auto* array = std::get_if<PsbArray>(&value.Data);
		if (array == nullptr || array->size() > kMaxMotionItems) {
			throw std::runtime_error("motion array is invalid or exceeds its item limit: " + std::string(context));
		}

		return *array;
	}

	static const PsbObject& ReadObject(const PsbValue& value, std::string_view context) {
		const auto* object = std::get_if<PsbObject>(&value.Data);
		if (object == nullptr || object->size() > kMaxMotionItems) {
			throw std::runtime_error("motion object is invalid or exceeds its item limit: " + std::string(context));
		}

		return *object;
	}

	static std::string ReadText(const PsbValue& value, std::string_view name) {
		const auto* text = std::get_if<std::string>(&RequireField(value, name).Data);
		if (text == nullptr) {
			throw std::runtime_error("motion string field is invalid: " + std::string(name));
		}

		return *text;
	}

	static float ReadNumber(const PsbValue& value, std::string_view context) {
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		const auto* real = std::get_if<double>(&value.Data);
		const auto number = integer != nullptr ? static_cast<double>(*integer) : real != nullptr ? *real
		                                                                                         : std::numeric_limits<double>::quiet_NaN();
		if (!std::isfinite(number) || std::abs(number) > kMaxMotionNumber) {
			throw std::runtime_error("motion number is invalid or exceeds its limit: " + std::string(context));
		}

		return static_cast<float>(number);
	}

	static std::uint32_t ReadInteger(const PsbValue& value, std::string_view context) {
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		const auto* real = std::get_if<double>(&value.Data);
		const auto number = integer != nullptr ? static_cast<double>(*integer) : real != nullptr ? *real
		                                                                                         : -1.0;
		if (!std::isfinite(number) || std::trunc(number) != number || number < 0 || number > std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("motion integer is invalid or exceeds its limit: " + std::string(context));
		}

		return static_cast<std::uint32_t>(number);
	}

	EasingCurve ReadEasingCurve(const PsbValue& value) {
		EasingCurve curve;
		const auto& segments = ReadArray(value, "easing segments");
		if (segments.empty()) {
			throw std::runtime_error("easing curve has no segments");
		}

		std::size_t knot_count = 0;
		curve.Segments.reserve(segments.size());
		for (const auto& segment : segments) {
			const auto& x = ReadArray(RequireField(segment, "x"), "easing x");
			const auto& y = ReadArray(RequireField(segment, "y"), "easing y");
			const auto& p = ReadArray(RequireField(segment, "p"), "easing p");
			if (x.size() < 2 || x.size() != y.size() || x.size() != p.size() || x.size() > kMaxMotionItems - knot_count) {
				throw std::runtime_error("easing knot arrays have inconsistent lengths or exceed their item limit");
			}

			knot_count += x.size();
			std::vector<SplineKnot> knots;
			knots.reserve(x.size());
			for (std::size_t index = 0; index < x.size(); ++index) {
				SplineKnot knot;
				knot.X = ReadNumber(x[index], "easing x");
				knot.Y = ReadNumber(y[index], "easing y");
				knot.SecondDerivative = ReadNumber(p[index], "easing p");
				if (!knots.empty() && knot.X <= knots.back().X) {
					throw std::runtime_error("easing x knots must increase strictly");
				}

				knots.push_back(knot);
			}

			if (!curve.Segments.empty() && knots.front().X < curve.Segments.back().back().X) {
				throw std::runtime_error("easing segments overlap or are unordered");
			}

			curve.Segments.push_back(std::move(knots));
		}

		return curve;
	}

	MotionPath ReadMotionPath(const PsbValue& value) {
		const auto& x = ReadArray(RequireField(value, "x"), "path x");
		const auto& y = ReadArray(RequireField(value, "y"), "path y");
		const auto& times = ReadArray(RequireField(value, "t"), "path t");
		const auto& remapping = ReadArray(RequireField(value, "s"), "path s");
		if (times.size() < 2 || x.size() != y.size() || x.size() != (times.size() - 1) * 3 + 1 || remapping.size() != times.size() - 1) {
			throw std::runtime_error("motion path control, time, or remapping counts are inconsistent");
		}

		MotionPath path;
		path.Segments.reserve(times.size() - 1);
		for (std::size_t index = 0; index + 1 < times.size(); ++index) {
			MotionPathSegment segment;
			segment.Begin = ReadNumber(times[index], "path segment begin");
			segment.End = ReadNumber(times[index + 1], "path segment end");
			if (segment.End <= segment.Begin) {
				throw std::runtime_error("motion path times must increase strictly");
			}

			for (std::size_t control = 0; control < 4; ++control) {
				segment.Controls[control].X = ReadNumber(x[index * 3 + control], "path control x");
				segment.Controls[control].Y = ReadNumber(y[index * 3 + control], "path control y");
			}

			PsbValue curve_value;
			curve_value.Data = PsbArray{remapping[index]};
			auto curve = ReadEasingCurve(curve_value);
			segment.Remapping = std::move(curve.Segments.front());
			path.Segments.push_back(std::move(segment));
		}

		return path;
	}

	static MotionIcon ReadIcon(std::string name, const PsbValue& value, const ImageTexture& texture) {
		MotionIcon icon;
		icon.Name = std::move(name);
		icon.Left = ReadNumber(RequireField(value, "left"), "icon left");
		icon.Top = ReadNumber(RequireField(value, "top"), "icon top");
		icon.Width = ReadNumber(RequireField(value, "width"), "icon width");
		icon.Height = ReadNumber(RequireField(value, "height"), "icon height");
		icon.OriginX = ReadNumber(RequireField(value, "originX"), "icon originX");
		icon.OriginY = ReadNumber(RequireField(value, "originY"), "icon originY");
		if (const auto* resolution = value.Find("resolution")) {
			icon.Resolution = ReadNumber(*resolution, "icon resolution");
			if (icon.Resolution <= 0) {
				throw std::runtime_error("motion icon resolution must be positive");
			}
		}

		if (const auto* attribute = value.Find("attr")) {
			icon.AttributeFlags = ReadInteger(*attribute, "icon attribute flags");
		}

		if (icon.Left < 0 || icon.Top < 0 || icon.Width < 0 || icon.Height < 0 || icon.Left >= 2147483648.0f || icon.Top >= 2147483648.0f || icon.Width >= 2147483648.0f || icon.Height >= 2147483648.0f) {
			throw std::runtime_error("motion icon rectangle exceeds the native integer range: " + icon.Name);
		}

		// Native icon lookup truncates rectangle fields before applying texture resolution.
		const auto left = static_cast<float>(static_cast<std::int32_t>(icon.Left));
		const auto top = static_cast<float>(static_cast<std::int32_t>(icon.Top));
		const auto sampled_width = static_cast<float>(static_cast<std::int32_t>(icon.Width)) * icon.Resolution;
		const auto sampled_height = static_cast<float>(static_cast<std::int32_t>(icon.Height)) * icon.Resolution;
		if (left > texture.Width || top > texture.Height || sampled_width > texture.Width - left || sampled_height > texture.Height - top) {
			throw std::runtime_error("motion icon rectangle exceeds its atlas texture: " + icon.Name);
		}

		const auto* mesh = value.Find("mesh");
		if (mesh != nullptr) {
			icon.Mesh = *mesh;
		}

		return icon;
	}

	static MotionSource ReadSource(const PsbDocument& document, std::string name, const PsbValue& value) {
		MotionSource source;
		source.Name = std::move(name);
		source.Texture = ReadImageTexture(document, RequireField(value, "texture"));
		for (const auto& [name, icon] : ReadObject(RequireField(value, "icon"), "source icon")) {
			source.Icons.push_back(ReadIcon(name, icon, source.Texture));
		}

		return source;
	}

	static std::vector<MotionFrame> ReadFrames(const PsbValue& value, std::string_view context, std::size_t& item_count) {
		const auto& array = ReadArray(value, context);
		if (array.size() > kMaxMotionItems - item_count) {
			throw std::runtime_error("motion frame count exceeds the aggregate item limit");
		}

		item_count += array.size();
		std::vector<MotionFrame> frames;
		frames.reserve(array.size());
		for (const auto& value : array) {
			MotionFrame frame;
			frame.Time = ReadNumber(RequireField(value, "time"), "frame time");
			frame.Type = ReadInteger(RequireField(value, "type"), "frame type");
			if (frame.Time < 0 || (!frames.empty() && frame.Time < frames.back().Time) || frame.Type > 3) {
				throw std::runtime_error("motion frame time is negative/unordered or its type is unsupported");
			}

			const auto* content = value.Find("content");
			if (content != nullptr) {
				frame.Content = *content;
			}

			frames.push_back(std::move(frame));
		}

		return frames;
	}

	static MotionLayer ReadLayer(const PsbValue& value, std::size_t depth, std::size_t& layer_count, std::size_t& frame_count) {
		if (depth > kMaxMotionDepth || layer_count >= kMaxMotionItems) {
			throw std::runtime_error("motion layer depth or aggregate count exceeds its limit");
		}

		++layer_count;
		MotionLayer layer;
		layer.Label = ReadText(value, "label");
		layer.Type = ReadInteger(RequireField(value, "type"), "layer type");
		layer.InheritMask = ReadInteger(RequireField(value, "inheritMask"), "inheritMask");
		layer.Coordinate = ReadInteger(RequireField(value, "coordinate"), "coordinate");
		layer.Frames = ReadFrames(RequireField(value, "frameList"), "frameList", frame_count);
		for (const auto& order : ReadArray(RequireField(value, "transformOrder"), "transformOrder")) {
			layer.TransformOrder.push_back(ReadInteger(order, "transform order"));
		}

		for (const auto& child : ReadArray(RequireField(value, "children"), "layer children")) {
			layer.Children.push_back(ReadLayer(child, depth + 1, layer_count, frame_count));
		}

		for (const auto& property : ReadObject(value, "layer properties")) {
			if (property.first != "children" && property.first != "frameList") {
				layer.Properties.push_back(property);
			}
		}

		return layer;
	}

	static MotionTimelineBounds ReadTimelineBounds(const PsbValue& value) {
		const auto& serialized = RequireField(value, "bounds");
		static_cast<void>(ReadObject(serialized, "timeline bounds"));
		MotionTimelineBounds bounds;
		bounds.Left = ReadNumber(RequireField(serialized, "left"), "timeline bounds left");
		bounds.Top = ReadNumber(RequireField(serialized, "top"), "timeline bounds top");
		bounds.Right = ReadNumber(RequireField(serialized, "right"), "timeline bounds right");
		bounds.Bottom = ReadNumber(RequireField(serialized, "bottom"), "timeline bounds bottom");

		return bounds;
	}

	static void ReadDivisionTimeline(MotionTimeline& timeline, const PsbValue& value, std::size_t& division_count) {
		const auto& divisions = ReadArray(RequireField(value, "division"), "timeline division");
		const auto& motions = ReadArray(RequireField(value, "motionList"), "timeline motionList");
		if (divisions.empty() || divisions.size() != motions.size()) {
			throw std::runtime_error("motion division and motionList counts are empty or inconsistent: " + timeline.Name);
		}

		if (divisions.size() > kMaxMotionItems - division_count) {
			throw std::runtime_error("motion division count exceeds the aggregate item limit");
		}

		division_count += divisions.size();
		timeline.Division.reserve(divisions.size());
		timeline.MotionList.reserve(motions.size());
		for (std::size_t index = 0; index < divisions.size(); ++index) {
			timeline.Division.push_back(ReadNumber(divisions[index], "timeline division angle"));
			const auto* name = std::get_if<std::string>(&motions[index].Data);
			if (name == nullptr) {
				throw std::runtime_error("motion division target requires a string: " + timeline.Name);
			}

			timeline.MotionList.push_back(*name);
		}
	}

	static MotionTimeline ReadTimeline(std::string name, const PsbValue& value, std::size_t& layer_count, std::size_t& frame_count, std::size_t& division_count) {
		MotionTimeline timeline;
		timeline.Name = std::move(name);
		const auto type = ReadInteger(RequireField(value, "type"), "timeline type");
		if (type > static_cast<std::uint32_t>(MotionTimelineKind::Division)) {
			throw std::runtime_error("unsupported motion timeline type: " + timeline.Name);
		}

		timeline.Kind = static_cast<MotionTimelineKind>(type);
		if (timeline.Kind == MotionTimelineKind::Division) {
			ReadDivisionTimeline(timeline, value, division_count);

			return timeline;
		}

		timeline.Bounds = ReadTimelineBounds(value);
		timeline.LastTime = ReadNumber(RequireField(value, "lastTime"), "lastTime");
		timeline.LoopTime = ReadNumber(RequireField(value, "loopTime"), "loopTime");
		if (timeline.LastTime < 0 || timeline.LoopTime < -1 || timeline.LoopTime > timeline.LastTime) {
			throw std::runtime_error("motion timeline loop or terminal time is invalid: " + timeline.Name);
		}

		for (const auto& layer : ReadArray(RequireField(value, "layer"), "timeline layer")) {
			timeline.Layers.push_back(ReadLayer(layer, 0, layer_count, frame_count));
		}

		timeline.Priority = ReadFrames(RequireField(value, "priority"), "timeline priority", frame_count);
		timeline.Tags = ReadFrames(RequireField(value, "tag"), "timeline tag", frame_count);

		if (const auto* map = value.Find("layerIndexMap")) {
			timeline.LayerIndexMap = *map;
		}

		timeline.Parameters = RequireField(value, "parameter");
		timeline.Variables = RequireField(value, "variable");
		timeline.Parameterization = RequireField(value, "parameterize");
		const auto* stream = value.Find("stream");
		if (stream != nullptr) {
			timeline.Stream = *stream;
		}

		return timeline;
	}

	MotionResource ReadMotionResource(const PsbDocument& document) {
		if ((document.Header.Version != 3 && document.Header.Version != 4) || document.Header.Flags != 0) {
			throw std::runtime_error("motion resource requires an unfiltered PSB version 3 or 4");
		}

		if (ReadText(document.Root, "id") != "motion" || ReadText(document.Root, "spec") != "win") {
			throw std::runtime_error("motion resource requires id=motion and spec=win");
		}

		if (ReadNumber(RequireField(document.Root, "version"), "version") != 3.03f) {
			throw std::runtime_error("unsupported motion schema version");
		}

		MotionResource resource;
		resource.Label = ReadText(document.Root, "label");
		resource.Chunks = document.Chunks;
		resource.ExtraChunks = document.ExtraChunks;
		for (const auto& easing : ReadArray(RequireField(document.Root, "easing"), "easing")) {
			resource.Easing.push_back(ReadEasingCurve(easing));
		}

		for (const auto& [name, source] : ReadObject(RequireField(document.Root, "source"), "source")) {
			resource.Sources.push_back(ReadSource(document, name, source));
		}

		std::size_t layer_count = 0;
		std::size_t frame_count = 0;
		std::size_t timeline_count = 0;
		std::size_t division_count = 0;
		for (const auto& [name, value] : ReadObject(RequireField(document.Root, "object"), "object")) {
			MotionObject object;
			object.Name = name;
			object.Type = ReadInteger(RequireField(value, "type"), "object type");
			const auto& timelines = ReadObject(RequireField(value, "motion"), "motion timelines");
			if (timelines.size() > kMaxMotionItems - timeline_count) {
				throw std::runtime_error("motion timeline count exceeds the aggregate item limit");
			}

			timeline_count += timelines.size();
			for (const auto& [name, timeline] : timelines) {
				object.Timelines.push_back(ReadTimeline(name, timeline, layer_count, frame_count, division_count));
			}

			resource.Objects.push_back(std::move(object));
		}

		return resource;
	}

	static float EvaluateSplineSegment(std::span<const SplineKnot> segment, float position) {
		for (std::size_t index = 1; index < segment.size(); ++index) {
			const auto& start = segment[index - 1];
			const auto& end = segment[index];
			if (position > end.X) {
				continue;
			}

			const auto width = end.X - start.X;
			if (!std::isfinite(width) || width <= 0 || !std::isfinite(start.Y) || !std::isfinite(end.Y) || !std::isfinite(start.SecondDerivative) || !std::isfinite(end.SecondDerivative)) {
				throw std::runtime_error("easing interval is invalid or nonfinite");
			}

			const auto ratio = (position - start.X) / width;
			const auto inverse = 1.0f - ratio;
			const auto width_squared = width * width;
			const auto linear = inverse * start.Y + ratio * end.Y;
			const auto correction = ((inverse * inverse * inverse - inverse) * start.SecondDerivative + (ratio * ratio * ratio - ratio) * end.SecondDerivative) * width_squared / 6.0f;
			const auto result = correction + linear;
			if (!std::isfinite(result)) {
				throw std::runtime_error("easing interpolation overflows finite coordinates");
			}

			return result;
		}

		throw std::runtime_error("easing position falls outside the stored curve intervals");
	}

	float EvaluateEasing(const EasingCurve& curve, float position) {
		if (!std::isfinite(position)) {
			throw std::runtime_error("easing position is nonfinite");
		}

		for (const auto& segment : curve.Segments) {
			if (segment.size() < 2) {
				throw std::runtime_error("easing segment requires at least two knots");
			}

			if (position >= segment.front().X && position <= segment.back().X) {
				return EvaluateSplineSegment(segment, position);
			}
		}

		throw std::runtime_error("easing position falls outside the stored curve intervals");
	}

	MotionPoint EvaluateMotionPath(const MotionPath& path, float position) {
		if (!std::isfinite(position)) {
			throw std::runtime_error("motion path position is nonfinite");
		}

		for (const auto& segment : path.Segments) {
			if (!std::isfinite(segment.Begin) || !std::isfinite(segment.End) || segment.End <= segment.Begin || segment.Remapping.size() < 2) {
				throw std::runtime_error("motion path segment is invalid");
			}

			if (position < segment.Begin || position > segment.End) {
				continue;
			}

			const auto local = (position - segment.Begin) / (segment.End - segment.Begin);
			float ratio = 0;
			if (local <= segment.Remapping.front().X) {
				ratio = segment.Remapping.front().Y;
			} else if (local > segment.Remapping.back().X) {
				ratio = segment.Remapping.back().Y;
			} else {
				ratio = EvaluateSplineSegment(segment.Remapping, local);
			}

			const auto inverse = 1.0f - ratio;
			const auto three_inverse = inverse * 3.0f;
			const auto weight0 = inverse * inverse * inverse;
			const auto weight1 = three_inverse * inverse * ratio;
			const auto weight2 = three_inverse * ratio * ratio;
			const auto weight3 = ratio * ratio * ratio;
			const auto& controls = segment.Controls;
			MotionPoint point;
			point.X = weight1 * controls[1].X + weight0 * controls[0].X + weight2 * controls[2].X + weight3 * controls[3].X;
			point.Y = weight0 * controls[0].Y + weight1 * controls[1].Y + weight2 * controls[2].Y + weight3 * controls[3].Y;
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("motion path interpolation overflows finite coordinates");
			}

			return point;
		}

		throw std::runtime_error("motion path position falls outside its stored segments");
	}

	static float RemapRatio(float ratio, const EasingCurve* easing) {
		if (!std::isfinite(ratio)) {
			throw std::runtime_error("interpolation ratio is nonfinite");
		}

		return easing == nullptr ? ratio : EvaluateEasing(*easing, ratio);
	}

	float InterpolateScalar(float start, float end, float ratio, const EasingCurve* easing) {
		if (!std::isfinite(start) || !std::isfinite(end)) {
			throw std::runtime_error("scalar interpolation endpoint is nonfinite");
		}

		if (start == end) {
			return start;
		}

		const auto mapped = RemapRatio(ratio, easing);
		const auto result = (1.0f - mapped) * start + mapped * end;
		if (!std::isfinite(result)) {
			throw std::runtime_error("scalar interpolation overflows finite coordinates");
		}

		return result;
	}

	float InterpolateAngle(float start, float end, float ratio, RotationTransition transition, const EasingCurve* easing) {
		if (!std::isfinite(start) || !std::isfinite(end) || std::abs(start) > kMaxRotationDegrees || std::abs(end) > kMaxRotationDegrees) {
			throw std::runtime_error("angle endpoint is nonfinite or exceeds its rotation limit");
		}

		if (transition == RotationTransition::Shortest && start == end) {
			return start;
		}

		if (transition == RotationTransition::Shortest) {
			if (end > start && end - start > 180.0f) {
				end -= 360.0f;
			} else if (end <= start && start - end > 180.0f) {
				end += 360.0f;
			}
		} else if (transition != RotationTransition::Directed) {
			throw std::runtime_error("unsupported rotation transition");
		}

		auto result = InterpolateScalar(start, end, ratio, easing);
		if (std::abs(result) > kMaxRotationDegrees) {
			throw std::runtime_error("angle interpolation exceeds its rotation limit");
		}

		if (transition == RotationTransition::Directed) {
			while (result < 0) {
				result += 360.0f;
			}

			while (result >= 360.0f) {
				result -= 360.0f;
			}
		} else if (result < 0) {
			result += 360.0f;
		} else if (result >= 360.0f) {
			result -= 360.0f;
		}

		return result;
	}

	std::uint32_t InterpolatePackedColor(std::uint32_t start, std::uint32_t end, float ratio, const EasingCurve* easing) {
		if (start == end) {
			return start;
		}

		const auto scaled = RemapRatio(ratio, easing) * 256.0f;
		if (scaled < std::numeric_limits<std::int32_t>::min() || scaled >= 2147483648.0f) {
			throw std::runtime_error("packed color interpolation exceeds its integer limit");
		}

		// Unsigned arithmetic preserves the native modulo-2^32 packed-lane operations.
		const auto factor = static_cast<std::uint32_t>(static_cast<std::int32_t>(scaled));
		const auto inverse = 256u - factor;
		const auto high = ((start >> 8) & 0x00ff00ffu) * inverse + ((end >> 8) & 0x00ff00ffu) * factor;
		const auto low = ((start & 0x00ff00ffu) * inverse + (end & 0x00ff00ffu) * factor) >> 8;

		return ((low ^ high) & 0x00ff00ffu) ^ high;
	}

} // namespace sgre::resource
