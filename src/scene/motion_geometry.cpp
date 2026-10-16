#include "sgre/scene/motion_geometry.hpp"

#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_grid_draw.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {

	MotionGeometryFrame PrepareMotionGeometryFrame(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionIcon& icon, float depth_projection) {
		const auto ignored_mask = layer.MeshTransform == 0 ? std::uint32_t{0x40000} : 0;
		if (layer.Type != 0 || layer.MeshTransform > 1 || layer.MeshCombinator || (state.Attributes.UnresolvedMask & ~ignored_mask) != 0) {
			throw std::runtime_error("motion geometry frame requires resolved type-0 sprite attributes");
		}

		MotionGeometryFrame frame;
		frame.Patch = state.Attributes.Deformation;
		frame.Space = MotionSpriteDeformationSpace(state, icon, depth_projection);
		frame.Quad = EvaluateMotionSpriteQuad(state, icon, depth_projection);
		frame.Width = static_cast<std::uint32_t>(icon.Width);
		frame.Height = static_cast<std::uint32_t>(icon.Height);
		frame.Origin.X = icon.OriginX + state.Attributes.OriginX;
		frame.Origin.Y = icon.OriginY + state.Attributes.OriginY;
		frame.Anchor.X = state.Attributes.Coordinate[0];
		frame.Anchor.Y = state.Attributes.Coordinate[1];

		return frame;
	}

	static void ValidatePoint(const resource::MotionPoint& point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("motion geometry frame has nonfinite coordinates");
		}
	}

	static void ValidateFrames(std::span<const MotionDeformationNode> nodes, std::span<const MotionGeometryFrame> frames) {
		static constexpr auto kNativeIntegerLimit = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& node = nodes[index];
			const auto& frame = frames[index];
			if (node.MeshTransform > 1 || node.HasPatch != frame.Patch.has_value() || (node.MeshTransform == 0 && (node.MeshCombine || frame.Patch)) || frame.Width > kNativeIntegerLimit || frame.Height > kNativeIntegerLimit) {
				throw std::runtime_error("motion geometry frame has unsupported mesh state or dimensions");
			}

			for (const auto coefficient : {frame.Space.XX, frame.Space.XY, frame.Space.TranslationX, frame.Space.YX, frame.Space.YY, frame.Space.TranslationY}) {
				if (!std::isfinite(coefficient)) {
					throw std::runtime_error("motion geometry frame has nonfinite space coefficients");
				}
			}

			if (frame.Patch) {
				for (const auto& control : frame.Patch->Controls) {
					ValidatePoint(control);
				}
			}

			ValidatePoint(frame.Origin);
			ValidatePoint(frame.Anchor);
			for (const auto& point : frame.Quad) {
				ValidatePoint(point);
			}
		}
	}

	PreparedMotionGeometry::PreparedMotionGeometry(std::span<const MotionDeformationNode> nodes, std::span<const MotionGeometryFrame> frames, std::span<MotionDeformationLink> links, std::span<MotionGeometryCache> cache) : m_Nodes(nodes), m_Frames(frames), m_Links(links), m_Cache(cache) {
		if (frames.size() != nodes.size() || cache.size() != nodes.size()) {
			throw std::runtime_error("motion geometry frame/cache counts differ from its hierarchy");
		}

		ValidateFrames(nodes, frames);
		ResolveMotionDeformationLinks(nodes, links);
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			MotionGeometryCache prepared;
			prepared.CombinedPatch = frames[index].Patch;
			auto parent = static_cast<std::uint32_t>(index);
			while (nodes[parent].MeshCombine && nodes[parent].Parent) {
				parent = *nodes[parent].Parent;
				if (links[parent].Active) {
					prepared.CombinedPatch = CombineMotionBezierPatches(prepared.CombinedPatch, cache[parent].CombinedPatch);

					break;
				}
			}

			if (links[index].Active) {
				if (!prepared.CombinedPatch) {
					throw std::runtime_error("active motion geometry lacks recovered bicubic controls");
				}

				prepared.Warp = PrepareMotionDeformation(*prepared.CombinedPatch, frames[index].Space);
			}

			cache[index] = prepared;
		}
	}

	std::optional<std::uint32_t> PreparedMotionGeometry::NextAncestor(std::uint32_t index) const {
		auto ancestor = m_Links[index].Ancestor;
		while (m_Nodes[index].MeshCombine && m_Nodes[index].Parent) {
			index = *m_Nodes[index].Parent;
			if (ancestor == index) {
				ancestor = m_Links[index].Ancestor;
			}
		}

		return ancestor;
	}

	bool PreparedMotionGeometry::CombinedChainReaches(std::uint32_t index, std::uint32_t ancestor) const {
		if (index == ancestor) {
			return true;
		}

		while (m_Nodes[index].MeshCombine && m_Nodes[index].Parent) {
			index = *m_Nodes[index].Parent;
			if (index == ancestor) {
				return true;
			}
		}

		return false;
	}

	void PreparedMotionGeometry::ApplyWarp(std::uint32_t index, std::span<resource::MotionPoint> positions) const {
		if (!m_Cache[index].Warp) {
			throw std::runtime_error("active motion geometry has no prepared deformation space");
		}

		ApplyMotionDeformation(*m_Cache[index].Warp, positions);
	}

	static MotionDeformationWindow ReferenceWindow(const MotionGeometryFrame& frame, const MotionGeometryFrame& reference) {
		MotionDeformationWindow window;
		if (frame.Width == reference.Width && frame.Height == reference.Height) {
			return window;
		}

		if (reference.Width == 0 || reference.Height == 0) {
			throw std::runtime_error("motion geometry reference rectangle has a zero dimension");
		}

		const auto reference_width = static_cast<float>(reference.Width);
		const auto reference_height = static_cast<float>(reference.Height);
		window.Left = (reference.Origin.X - frame.Origin.X) / reference_width;
		window.Top = (reference.Origin.Y - frame.Origin.Y) / reference_height;
		window.Right = static_cast<float>(frame.Width) / reference_width + window.Left;
		window.Bottom = static_cast<float>(frame.Height) / reference_height + window.Top;

		return window;
	}

	resource::MotionPoint PreparedMotionGeometry::BoundaryOffset(std::uint32_t index) const {
		const auto original = m_Frames[index].Anchor;
		auto anchor = original;
		auto ancestor = NextAncestor(index);
		while (ancestor) {
			if (m_Links[*ancestor].Active) {
				ApplyWarp(*ancestor, std::span(&anchor, 1));
			}

			if (m_Links[*ancestor].StopsInheritance) {
				break;
			}

			ancestor = NextAncestor(*ancestor);
		}

		resource::MotionPoint offset;
		offset.X = anchor.X - original.X;
		offset.Y = anchor.Y - original.Y;

		return offset;
	}

	void PreparedMotionGeometry::AddBoundaryOffsets(std::uint32_t index, std::span<resource::MotionPoint> positions) const {
		resource::MotionPoint offset;
		auto ancestor = std::optional(index);
		do {
			if (m_Links[*ancestor].StopsInheritance) {
				const auto displacement = BoundaryOffset(*ancestor);
				offset.X = offset.X + displacement.X;
				offset.Y = offset.Y + displacement.Y;
			}

			ancestor = NextAncestor(*ancestor);
		} while (ancestor);

		if (offset.X != 0 || offset.Y != 0) {
			for (auto& position : positions) {
				position.X = offset.X + position.X;
				position.Y = offset.Y + position.Y;
				ValidatePoint(position);
			}
		}
	}

	MotionMeshSubdivision PreparedMotionGeometry::SelectSubdivision(std::uint32_t index, float ratio, std::span<MotionMeshDivisionCache> division_cache) const {
		if (index >= m_Nodes.size() || division_cache.size() != m_Nodes.size() || m_Nodes[index].Hidden || !m_Nodes[index].SourceReady) {
			throw std::runtime_error("motion geometry subdivision index/cache is invalid, hidden, or unbound");
		}

		auto ancestor = m_Links[index].Active || m_Links[index].StopsInheritance ? std::optional(index) : m_Links[index].Ancestor;
		while (ancestor && !m_Links[*ancestor].Active) {
			if (m_Links[*ancestor].StopsInheritance) {
				return {};
			}

			ancestor = NextAncestor(*ancestor);
		}

		if (!ancestor) {
			return {};
		}

		const auto& frame = m_Frames[index];
		std::uint32_t division = 0;
		if (m_Nodes[index].MeshTransform == 1) {
			division = ScaleMotionMeshDivision(m_Nodes[index].MeshDivision, ratio);
		} else {
			auto source = m_Links[index].Ancestor;
			while (source && !m_Links[*source].Active) {
				source = m_Links[*source].Ancestor;
			}

			if (!source) {
				throw std::runtime_error("inherited motion geometry has no active division source");
			}

			InheritedMotionMeshDivision request;
			request.Width = frame.Width;
			request.Height = frame.Height;
			request.AncestorWidth = m_Frames[*source].Width;
			request.AncestorHeight = m_Frames[*source].Height;
			request.AncestorDivision = m_Nodes[*source].MeshDivision;
			request.Ratio = ratio;
			division = ResolveInheritedMotionMeshDivision(request, division_cache[index]);
		}

		return SelectMotionMeshSubdivision(division, frame.Width, frame.Height);
	}

	void PreparedMotionGeometry::EvaluateVertices(std::uint32_t index, const MotionMeshSubdivision& subdivision, std::span<resource::MotionPoint> positions) const {
		if (index >= m_Nodes.size() || m_Nodes[index].Hidden || !m_Nodes[index].SourceReady) {
			throw std::runtime_error("motion geometry index is invalid, hidden, or unbound");
		}

		if (positions.size() != MotionMeshGridVertexCount(subdivision)) {
			throw std::runtime_error("motion geometry output differs from its prepared subdivision");
		}

		auto ancestor = m_Links[index].Active || m_Links[index].StopsInheritance ? std::optional(index) : m_Links[index].Ancestor;
		if (!ancestor) {
			if (subdivision.X != 1 || subdivision.Y != 1) {
				throw std::runtime_error("undeformed motion geometry requires the native quad subdivision");
			}

			std::copy(m_Frames[index].Quad.begin(), m_Frames[index].Quad.end(), positions.begin());

			return;
		}

		const auto initial = *ancestor;
		if (!m_Links[initial].Active) {
			if (subdivision.X != 1 || subdivision.Y != 1) {
				throw std::runtime_error("stopped motion geometry without a patch requires the native quad subdivision");
			}

			EvaluateMotionBilinearGrid(m_Frames[index].Quad, subdivision, positions);
		} else if (CombinedChainReaches(index, initial)) {
			const auto window = ReferenceWindow(m_Frames[index], m_Frames[initial]);
			EvaluateMotionBezierGrid(m_Cache[initial].Warp->WorldPatch, subdivision, window, positions);
		} else {
			EvaluateMotionBilinearGrid(m_Frames[index].Quad, subdivision, positions);
			ApplyWarp(initial, positions);
		}

		while (!m_Links[*ancestor].StopsInheritance) {
			ancestor = NextAncestor(*ancestor);
			if (!ancestor) {
				return;
			}

			if (m_Links[*ancestor].Active) {
				ApplyWarp(*ancestor, positions);
			}
		}

		AddBoundaryOffsets(*ancestor, positions);
	}

} // namespace sgre::scene
