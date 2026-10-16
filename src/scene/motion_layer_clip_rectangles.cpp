#include "sgre/scene/motion_layer_clip_rectangles.hpp"

#include "sgre/scene/motion_particle_player_state.hpp"

#include "sgre/scene/motion_child_construction.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaximumClipLayers = 1000000;
	static constexpr std::size_t kMaximumClipAncestorDepth = 128;

	static void RequireFiniteRectangle(const MotionClipRectangle& rectangle) {
		if (!std::isfinite(rectangle.Left) || !std::isfinite(rectangle.Top) || !std::isfinite(rectangle.Right) || !std::isfinite(rectangle.Bottom)) {
			throw std::runtime_error("motion clip rectangle requires finite native edges");
		}
	}

	static float ClipMinimum(float first, float second) {
		return first < second ? first : second;
	}

	static float ClipMaximum(float first, float second) {
		return first > second ? first : second;
	}

	static MotionClipRectangle EvaluateRectangle(const MotionComposedLayer& node, const PreparedMotionFrame& current, const MotionClipRectangle* parent) {
		const auto& matrix = node.Hierarchy.Transform;
		const auto& coordinate = node.Hierarchy.Attributes.Coordinate;
		const auto origin_x = current.Attributes.OriginX;
		const auto origin_y = current.Attributes.OriginY;
		const auto vertical_origin = origin_y * matrix.YY + origin_x * matrix.YX;
		const auto horizontal_origin = origin_y * matrix.XY + origin_x * matrix.XX;
		auto vertical_first = matrix.YX * 16.0f + matrix.YY * 16.0f;
		auto vertical_second = matrix.YX * -16.0f - matrix.YY * 16.0f;
		vertical_first -= vertical_origin;
		vertical_second -= vertical_origin;
		vertical_first += coordinate[1];
		vertical_second += coordinate[1];
		auto horizontal_first = matrix.XX * 16.0f + matrix.XY * 16.0f;
		auto horizontal_second = matrix.XX * -16.0f - matrix.XY * 16.0f;
		horizontal_first -= horizontal_origin;
		horizontal_second -= horizontal_origin;
		horizontal_first += coordinate[0];
		horizontal_second += coordinate[0];
		MotionClipRectangle rectangle;
		rectangle.Left = ClipMinimum(horizontal_first, horizontal_second);
		rectangle.Top = ClipMinimum(vertical_first, vertical_second) + coordinate[2];
		rectangle.Right = ClipMaximum(horizontal_first, horizontal_second);
		rectangle.Bottom = ClipMaximum(vertical_first, vertical_second) + coordinate[2];
		if (parent != nullptr) {
			rectangle.Left = ClipMaximum(rectangle.Left, parent->Left);
			rectangle.Top = ClipMaximum(rectangle.Top, parent->Top);
			rectangle.Right = ClipMinimum(rectangle.Right, parent->Right);
			rectangle.Bottom = ClipMinimum(rectangle.Bottom, parent->Bottom);
		}

		RequireFiniteRectangle(rectangle);

		return rectangle;
	}

	MotionLayerClipRectangles::MotionLayerClipRectangles(const PreparedMotionTimeline& timeline)
	    : m_Timeline(timeline) {
		if (timeline.Layers.size() > kMaximumClipLayers || timeline.Parents.size() != timeline.Layers.size()) {
			throw std::runtime_error("motion clip hierarchy count or parents exceed its selected bound");
		}

		for (std::size_t index = 0; index < timeline.Layers.size(); ++index) {
			const auto& layer = timeline.Layers[index];
			if ((layer.Type != 0 && layer.Type != 1 && layer.Type != 2 && layer.Type != 3 && layer.Type != 4 && layer.Type != 7 && layer.Type != 12) || (layer.Type == 7 && (layer.MeshTransform != 0 || layer.StencilType != 0)) || (timeline.Parents[index] && *timeline.Parents[index] >= index)) {
				throw std::runtime_error("motion clip phase requires its recovered mixed nodes, kind7 without mesh/stencil and actual preceding parents");
			}

			if (layer.Type == 7 && (layer.Frames.size() < 2 || layer.Frames.size() > kMaximumClipLayers)) {
				throw std::runtime_error("motion kind7 clip requires its actual bounded selected frame pair");
			}

			auto parent = timeline.Parents[index];
			std::size_t depth = 0;
			while (parent) {
				if (++depth > kMaximumClipAncestorDepth) {
					throw std::runtime_error("motion clip ancestor depth exceeds its selected bound");
				}

				parent = timeline.Parents[*parent];
			}
		}

		m_Rectangles.resize(timeline.Layers.size());
		m_Clips.resize(timeline.Layers.size());
	}

	void MotionLayerClipRectangles::Compose(std::span<const MotionComposedLayer> layers, std::span<const std::size_t> current_indices, const MotionClipRectangle* root_clip) {
		m_Composed = false;
		if (layers.size() != m_Timeline.Layers.size() || current_indices.size() != layers.size()) {
			throw std::runtime_error("motion clip phase requires every actual current composed node and selected index");
		}

		if (root_clip != nullptr) {
			RequireFiniteRectangle(*root_clip);
		}

		for (std::size_t index = 0; index < layers.size(); ++index) {
			const auto parent = m_Timeline.Parents[index];
			const auto* inherited = parent ? m_Clips[*parent] : root_clip;
			const auto& layer = m_Timeline.Layers[index];
			if (layer.Type != 7 || !layers[index].Active) {
				m_Clips[index] = inherited;

				continue;
			}

			if (current_indices[index] >= layer.Frames.size()) {
				throw std::runtime_error("motion clip current slot exceeds its actual frame storage");
			}

			const auto& current = layer.Frames[current_indices[index]];
			const auto& node = layers[index];
			const auto& matrix = node.Hierarchy.Transform;
			const std::array<float, 9> finite{current.Attributes.OriginX, current.Attributes.OriginY, node.Hierarchy.Attributes.Coordinate[0], node.Hierarchy.Attributes.Coordinate[1], node.Hierarchy.Attributes.Coordinate[2], matrix.XX, matrix.XY, matrix.YX, matrix.YY};
			for (const auto value : finite) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("motion clip requires finite current origin and composed coordinate/matrix");
				}
			}

			m_Rectangles[index] = EvaluateRectangle(node, current, inherited);
			m_Clips[index] = &m_Rectangles[index];
		}

		m_Composed = true;
	}

	std::span<const MotionClipRectangle* const> MotionLayerClipRectangles::Clips() const noexcept {
		return m_Composed ? std::span<const MotionClipRectangle* const>(m_Clips) : std::span<const MotionClipRectangle* const>{};
	}

	void MotionLayerClipRectangles::BindChildClip(std::size_t layer, MotionChildConstructionState& child) const {
		if (!m_Composed || layer >= m_Timeline.Layers.size() || m_Timeline.Layers[layer].Type != 3) {
			throw std::runtime_error("motion child clip transfer requires its actual composed kind3 parent link");
		}
		const auto& prepared = m_Timeline.Layers[layer];
		if (child.Root().MatrixMode() != MotionRootMatrixMode::SuppliedByParent || child.Root().Frame().Hierarchy.CoordinatePlane != prepared.CoordinatePlane || child.Root().TransformOrder() != prepared.TransformOrder) {
			throw std::runtime_error("motion child clip transfer requires the matching genuine child constructor configuration");
		}

		// This copies the borrow even when null; native522906 adds no allocation or root dirty write.
		child.m_InheritedClip = m_Clips[layer];
	}

	void MotionLayerClipRectangles::BindParticleClip(std::size_t layer, MotionParticlePlayerState& child) const {
		if (!m_Composed || layer >= m_Timeline.Layers.size() || m_Timeline.Layers[layer].Type != 4 || child.Root().MatrixMode() != MotionRootMatrixMode::Recomputed || child.m_DeformationTransferOwner == nullptr || child.m_DeformationTransferLayer != layer) {
			throw std::runtime_error("motion particle clip transfer requires the actual composed kind4 child transfer");
		}
		child.m_InheritedClip = m_Clips[layer];
	}

	const MotionClipRectangle& MotionLayerClipRectangles::OwnedRectangle(std::size_t layer) const {
		if (layer >= m_Timeline.Layers.size() || m_Timeline.Layers[layer].Type != 7) {
			throw std::runtime_error("motion owned clip rectangle requires its actual kind7 node");
		}

		return m_Rectangles[layer];
	}

} // namespace sgre::scene
