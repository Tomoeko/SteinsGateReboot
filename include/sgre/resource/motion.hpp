#pragma once

#include "sgre/resource/image.hpp"
#include "sgre/resource/psb.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sgre::resource {

	struct SplineKnot {
		float X = 0;
		float Y = 0;
		float SecondDerivative = 0;
	};

	struct EasingCurve {
		std::vector<std::vector<SplineKnot>> Segments;
	};

	struct MotionPoint {
		float X = 0;
		float Y = 0;
	};

	struct MotionPathSegment {
		float Begin = 0;
		float End = 0;
		std::array<MotionPoint, 4> Controls;
		std::vector<SplineKnot> Remapping;
	};

	struct MotionPath {
		std::vector<MotionPathSegment> Segments;
	};

	struct MotionFrame {
		float Time = 0;
		std::uint32_t Type = 0;
		PsbValue Content;
	};

	struct MotionLayer {
		std::string Label;
		std::uint32_t Type = 0;
		std::uint32_t InheritMask = 0;
		std::uint32_t Coordinate = 0;
		std::vector<std::uint32_t> TransformOrder;
		std::vector<MotionFrame> Frames;
		std::vector<MotionLayer> Children;
		// Retains observed options without inventing semantics for unrecovered layer kinds.
		PsbObject Properties;
	};

	enum class MotionTimelineKind : std::uint32_t {
		Frames = 0,
		Division = 1
	};

	struct MotionTimelineBounds {
		float Left = 0;
		float Top = 0;
		float Right = 0;
		float Bottom = 0;
	};

	struct MotionTimeline {
		std::string Name;
		MotionTimelineKind Kind = MotionTimelineKind::Frames;
		MotionTimelineBounds Bounds;
		// Division timelines select named ordinary timelines; they do not own a second layer hierarchy.
		std::vector<float> Division;
		std::vector<std::string> MotionList;
		float LastTime = 0;
		float LoopTime = -1;
		std::vector<MotionLayer> Layers;
		std::vector<MotionFrame> Priority;
		// Tag commands are separate from the optional packed frame/mesh/color Stream backend.
		std::vector<MotionFrame> Tags;
		// Native DT4 resolves this serialized map; labels and hierarchy order do not replace it.
		PsbValue LayerIndexMap;
		PsbValue Parameters;
		PsbValue Variables;
		PsbValue Parameterization;
		PsbValue Stream;
	};

	struct MotionObject {
		std::string Name;
		std::uint32_t Type = 0;
		std::vector<MotionTimeline> Timelines;
	};

	struct MotionIcon {
		std::string Name;
		float Left = 0;
		float Top = 0;
		float Width = 0;
		float Height = 0;
		float OriginX = 0;
		float OriginY = 0;
		float Resolution = 1;
		std::uint32_t AttributeFlags = 0;
		PsbValue Mesh;
	};

	struct MotionSource {
		std::string Name;
		ImageTexture Texture;
		std::vector<MotionIcon> Icons;
	};

	struct MotionResource {
		std::string Label;
		std::vector<PsbChunkRange> Chunks;
		std::vector<PsbChunkRange> ExtraChunks;
		std::vector<EasingCurve> Easing;
		std::vector<MotionSource> Sources;
		std::vector<MotionObject> Objects;
	};

	enum class RotationTransition {
		Shortest = 0,
		Directed = 1
	};

	[[nodiscard]] MotionResource ReadMotionResource(const PsbDocument& document);
	[[nodiscard]] EasingCurve ReadEasingCurve(const PsbValue& value);
	[[nodiscard]] MotionPath ReadMotionPath(const PsbValue& value);
	[[nodiscard]] float EvaluateEasing(const EasingCurve& curve, float position);
	[[nodiscard]] MotionPoint EvaluateMotionPath(const MotionPath& path, float position);
	[[nodiscard]] float InterpolateScalar(float start, float end, float ratio, const EasingCurve* easing = nullptr);
	[[nodiscard]] float InterpolateAngle(float start, float end, float ratio, RotationTransition transition, const EasingCurve* easing = nullptr);
	[[nodiscard]] std::uint32_t InterpolatePackedColor(std::uint32_t start, std::uint32_t end, float ratio, const EasingCurve* easing = nullptr);

} // namespace sgre::resource
