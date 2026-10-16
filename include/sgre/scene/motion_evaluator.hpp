#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {
	class MotionTimelineVariables;

	enum class MotionAttributeBlendPolicy {
		Unchanged,
		HalveRgbForClearBlendNibble
	};

	struct MotionLayerAttributes {
		std::string Source;
		std::string Icon;
		std::array<float, 3> Coordinate{};
		std::array<std::uint32_t, 4> Colors = {0x808080ff, 0x808080ff, 0x808080ff, 0x808080ff};
		float OriginX = 0;
		float OriginY = 0;
		float Angle = 0;
		float ZoomX = 1;
		float ZoomY = 1;
		float ShearX = 0;
		float ShearY = 0;
		std::uint32_t Opacity = 255;
		std::uint32_t BlendMode = 0x10;
		std::uint32_t ModulationColor = 0x808080ff;
		std::uint32_t Mask = 0;
		std::uint32_t UnresolvedMask = 0;
		std::uint32_t Interval = 0;
		bool FlipX = false;
		bool FlipY = false;
		bool UniformColor = true;
		bool UsesDefaultColor = true;
		std::array<std::optional<std::uint32_t>, 5> EasingIndices;
		std::optional<resource::MotionPath> CoordinatePath;
		resource::MotionDeformation Deformation;
	};

	struct PreparedMotionFrame {
		float Time = 0;
		bool Hidden = true;
		bool Tween = false;
		MotionLayerAttributes Attributes;
	};

	enum class MotionTransformOperation {
		Flip = 0,
		Rotate = 1,
		Scale = 2,
		Shear = 3
	};

	struct PreparedMotionLayer {
		std::string Label;
		std::uint32_t Type = 0;
		std::uint32_t InheritMask = 0;
		std::uint32_t CoordinatePlane = 0;
		std::uint32_t StencilType = 0;
		std::uint32_t MeshTransform = 0;
		std::uint32_t MeshSyncChildMask = 0;
		std::uint32_t MeshDivision = 0;
		bool MeshCombinator = false;
		bool MeshCombine = false;
		bool HasColorModifier = false;
		std::array<MotionTransformOperation, 4> TransformOrder = {MotionTransformOperation::Flip, MotionTransformOperation::Shear, MotionTransformOperation::Scale, MotionTransformOperation::Rotate};
		resource::RotationTransition Rotation = resource::RotationTransition::Shortest;
		std::vector<PreparedMotionFrame> Frames;
	};

	struct MotionLinearTransform {
		float XX = 1;
		float XY = 0;
		float YX = 0;
		float YY = 1;
	};

	struct MotionLayerSample {
		// Borrowed from PreparedMotionLayer, which must outlive this sample.
		std::string_view Source;
		std::string_view Icon;
		std::array<float, 3> Coordinate{};
		std::array<std::uint32_t, 4> Colors{};
		float OriginX = 0;
		float OriginY = 0;
		float Angle = 0;
		float ZoomX = 1;
		float ZoomY = 1;
		float ShearX = 0;
		float ShearY = 0;
		std::uint32_t Opacity = 255;
		std::uint32_t BlendMode = 0x10;
		std::uint32_t ModulationColor = 0x808080ff;
		std::uint32_t UnresolvedMask = 0;
		std::optional<resource::MotionBezierPatch> Deformation;
		bool Hidden = true;
		bool FlipX = false;
		bool FlipY = false;
		bool UniformColor = true;
		bool UsesDefaultColor = true;
	};

	struct MotionHierarchyState {
		MotionLayerSample Attributes;
		MotionLinearTransform Transform;
		std::uint32_t CoordinatePlane = 0;
		std::uint32_t ModulationColor = 0x808080ff;
		std::uint32_t UnresolvedInheritMask = 0;
	};

	struct MotionLayerEvaluationCache {
		float Ratio = 0;
		// No pose is published until a genuine initial frame selection is evaluated.
		std::optional<MotionLayerSample> Sample;
	};

	struct PreparedMotionPriorityFrame {
		float Time = 0;
		std::vector<std::uint32_t> DrawOrder;
	};

	struct PreparedMotionTimeline {
		float LastTime = 0;
		float LoopTime = -1;
		std::vector<PreparedMotionLayer> Layers;
		std::vector<std::optional<std::uint32_t>> Parents;
		std::vector<PreparedMotionPriorityFrame> Priority;
	};

	[[nodiscard]] PreparedMotionLayer PrepareMotionLayer(const resource::MotionLayer& layer, const resource::MotionResource& resource);
	// The actual ultimate player supplies this preparation policy before interpolation; ordinary callers retain Unchanged.
	[[nodiscard]] PreparedMotionLayer PrepareMotionLayer(const resource::MotionLayer& layer, const resource::MotionResource& resource, MotionAttributeBlendPolicy blend_policy);
	// Copies a genuinely selected prepared frame; borrowed resource fields retain their preparation owner.
	[[nodiscard]] MotionLayerSample SamplePreparedMotionFrame(const PreparedMotionFrame& frame);
	// Native force-all evaluation at an actual selected current/following frame pair.
	[[nodiscard]] MotionLayerSample EvaluateSelectedMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index);
	// JOIN owns only the actual decoded physical slot; timing/identity remain the immutable selected pair.
	[[nodiscard]] MotionLayerSample EvaluateSelectedMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, const PreparedMotionFrame& current, const PreparedMotionFrame& next);
	[[nodiscard]] bool EvaluateCachedMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, const PreparedMotionFrame& current, const PreparedMotionFrame& next, bool force, bool frame_dirty, MotionLayerEvaluationCache& cache);
	// Native timeline variables can write numeric position after cursor traversal; only their actual applied owner admits before-key evaluation.
	[[nodiscard]] bool EvaluateCachedMotionVariableLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, const PreparedMotionFrame& current, const PreparedMotionFrame& next, bool force, bool frame_dirty, MotionLayerEvaluationCache& cache, const MotionTimelineVariables& variables);
	// The caller owns selected-frame dirty state and clears it only after complete native composition phases.
	[[nodiscard]] bool EvaluateCachedMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position, std::size_t current_index, bool force, bool frame_dirty, MotionLayerEvaluationCache& cache);
	// Native child direction samples the actual selected coordinate pair directly, without interval/epsilon handling.
	// Nonzero path owners remain unsupported here until their direction derivative is independently verified.
	[[nodiscard]] std::array<float, 3> InterpolateMotionChildCoordinates(const PreparedMotionFrame& current, const PreparedMotionFrame& following, const resource::MotionResource& resource, float ratio, std::uint32_t coordinate_plane);
	[[nodiscard]] MotionLayerSample EvaluateMotionLayer(const PreparedMotionLayer& layer, const resource::MotionResource& resource, float position);
	// Native kind3 root matrix adjustment rotates the already-composed matrix in its original multiply/add order.
	[[nodiscard]] MotionLinearTransform RotateMotionChildTransform(const MotionLinearTransform& parent, float angle_delta, bool reverse_angle);
	[[nodiscard]] MotionLinearTransform EvaluateMotionTransform(const PreparedMotionLayer& layer, const MotionLayerSample& sample);
	// Parent selection, dynamic overrides, and visibility controls must be resolved by the caller.
	[[nodiscard]] MotionHierarchyState ComposeMotionHierarchyLayer(const PreparedMotionLayer& layer, const MotionLayerSample& local, const MotionHierarchyState& parent, const MotionHierarchyState& root, bool normalize_against_root);
	[[nodiscard]] std::uint32_t ComposeMotionColor(std::uint32_t first, std::uint32_t second);
	[[nodiscard]] PreparedMotionTimeline PrepareMotionTimeline(const resource::MotionTimeline& timeline, const resource::MotionResource& resource);
	[[nodiscard]] PreparedMotionTimeline PrepareMotionTimeline(const resource::MotionTimeline& timeline, const resource::MotionResource& resource, MotionAttributeBlendPolicy blend_policy);
	// The returned order borrows prepared priority storage, which must outlive it.
	[[nodiscard]] std::span<const std::uint32_t> EvaluateMotionDrawOrder(const PreparedMotionTimeline& timeline, float position);

} // namespace sgre::scene
