#include "sgre/scene/motion_selected_mesh_vertices.hpp"

#include "sgre/scene/motion_particle_player_state.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_selected_layer_sources.hpp"
#include "sgre/scene/motion_stencil_row_policies.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaximumSelectedVertexLayers = 65536;
	static constexpr std::size_t kMaximumSelectedVertexCount = 65536;

	MotionSelectedMeshVertices::MotionSelectedMeshVertices(MotionSelectedMeshAncestry& ancestry)
	    : m_Ancestry(ancestry), m_Selected(ancestry.m_Selected) {
		RequireOwners();
		const auto nodes = m_Selected.Nodes();
		if (m_Selected.m_VertexOwner != nullptr || m_Selected.m_MeshGeometryOwner != nullptr || nodes.size() > kMaximumSelectedVertexLayers) {
			throw std::runtime_error("motion selected vertices require their exclusive bounded actual geometry owner");
		}
		m_Entries.resize(nodes.size());
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			m_Entries[index].Node = &nodes[index];
		}
		m_Selected.m_VertexOwner = this;
	}

	MotionSelectedMeshVertices::~MotionSelectedMeshVertices() noexcept {
		if (m_Selected.m_VertexOwner == this) {
			m_Selected.m_VertexOwner = nullptr;
		}
	}

	void MotionSelectedMeshVertices::RequireOwners() const {
		if (m_Selected.m_DeformationOwner != &m_Ancestry || (m_Selected.m_VertexOwner != nullptr && m_Selected.m_VertexOwner != this)) {
			throw std::runtime_error("motion selected vertices require their exact actual ancestry/selected owner");
		}
		m_Ancestry.RequireOwners();
	}

	const MotionSelectedDeformationState* MotionSelectedMeshVertices::NextAncestor(const MotionSelectedDeformationState& state) const {
		const auto* current = &state;
		const auto* ancestor = current->Ancestor();
		while (current->Node().Prepared->MeshCombine && current->m_Parent != nullptr) {
			current = current->m_Parent;
			if (current == ancestor) {
				ancestor = current->Ancestor();
			}
		}

		return ancestor;
	}

	bool MotionSelectedMeshVertices::CombinedChainReaches(const MotionSelectedDeformationState& state, const MotionSelectedDeformationState& ancestor) const {
		const auto* current = &state;
		if (current == &ancestor) {
			return true;
		}
		while (current->Node().Prepared->MeshCombine && current->m_Parent != nullptr) {
			current = current->m_Parent;
			if (current == &ancestor) {
				return true;
			}
		}

		return false;
	}

	static const MotionDeformationSourceGeometry& RequireSourceGeometry(const MotionSelectedDeformationState& state) {
		const auto* geometry = state.SourceGeometry();
		if (geometry == nullptr || (!geometry->Blank && !geometry->External && (geometry->Source == nullptr || geometry->Icon == nullptr))) {
			throw std::runtime_error("motion selected vertices require genuine retained registered source geometry");
		}

		return *geometry;
	}

	static const PreparedMotionDeformation& RequireWorld(const MotionSelectedDeformationState& state) {
		const auto* world = state.World();
		if (!state.Active() || world == nullptr) {
			throw std::runtime_error("motion inherited vertices require their actual active ancestor world patch");
		}

		return *world;
	}

	MotionMeshSubdivision MotionSelectedMeshVertices::SelectSubdivision(const MotionSelectedDeformationState& state, float ratio, MotionMeshDivisionCache& cache) const {
		const auto* ancestor = state.Active() || state.StopsInheritance() ? &state : state.Ancestor();
		while (ancestor != nullptr && !ancestor->Active()) {
			if (ancestor->StopsInheritance()) {
				return {};
			}
			ancestor = NextAncestor(*ancestor);
		}
		if (ancestor == nullptr) {
			return {};
		}

		const auto& source = RequireSourceGeometry(state);
		const auto& prepared = *state.Node().Prepared;
		std::uint32_t division = 0;
		if (prepared.MeshTransform == 1) {
			division = ScaleMotionMeshDivision(prepared.MeshDivision, ratio);
		} else {
			const auto* reference = state.Ancestor();
			while (reference != nullptr && !reference->Active()) {
				reference = reference->Ancestor();
			}
			if (reference == nullptr) {
				throw std::runtime_error("motion inherited vertices lack their actual active division source");
			}
			const auto& reference_source = RequireSourceGeometry(*reference);
			InheritedMotionMeshDivision request;
			request.Width = source.Width;
			request.Height = source.Height;
			request.AncestorWidth = reference_source.Width;
			request.AncestorHeight = reference_source.Height;
			request.AncestorDivision = reference->Node().Prepared->MeshDivision;
			request.Ratio = ratio;
			division = ResolveInheritedMotionMeshDivision(request, cache);
		}

		return SelectMotionMeshSubdivision(division, source.Width, source.Height);
	}

	static MotionDeformationWindow ReferenceWindow(const MotionDeformationSourceGeometry& source, const MotionDeformationSourceGeometry& reference) {
		MotionDeformationWindow window;
		if (source.Width == reference.Width && source.Height == reference.Height) {
			return window;
		}
		if (reference.Width == 0 || reference.Height == 0) {
			throw std::runtime_error("motion inherited reference rectangle has a zero dimension");
		}
		const auto width = static_cast<float>(reference.Width);
		const auto height = static_cast<float>(reference.Height);
		window.Left = (reference.Origin.X - source.Origin.X) / width;
		window.Top = (reference.Origin.Y - source.Origin.Y) / height;
		window.Right = static_cast<float>(source.Width) / width + window.Left;
		window.Bottom = static_cast<float>(source.Height) / height + window.Top;

		return window;
	}

	resource::MotionPoint MotionSelectedMeshVertices::BoundaryOffset(const MotionSelectedDeformationState& state) const {
		const auto original = state.Anchor();
		auto anchor = original;
		const auto* ancestor = NextAncestor(state);
		while (ancestor != nullptr) {
			if (ancestor->Active()) {
				ApplyMotionDeformation(RequireWorld(*ancestor), std::span(&anchor, 1));
			}
			if (ancestor->StopsInheritance()) {
				break;
			}
			ancestor = NextAncestor(*ancestor);
		}
		resource::MotionPoint offset;
		offset.X = anchor.X - original.X;
		offset.Y = anchor.Y - original.Y;

		return offset;
	}

	void MotionSelectedMeshVertices::AddBoundaryOffsets(const MotionSelectedDeformationState& state, std::span<resource::MotionPoint> positions) const {
		resource::MotionPoint offset;
		const auto* ancestor = &state;
		do {
			if (ancestor->StopsInheritance()) {
				const auto displacement = BoundaryOffset(*ancestor);
				offset.X = offset.X + displacement.X;
				offset.Y = offset.Y + displacement.Y;
			}
			ancestor = NextAncestor(*ancestor);
		} while (ancestor != nullptr);
		if (offset.X != 0 || offset.Y != 0) {
			for (auto& position : positions) {
				position.X = offset.X + position.X;
				position.Y = offset.Y + position.Y;
				if (!std::isfinite(position.X) || !std::isfinite(position.Y)) {
					throw std::runtime_error("motion inherited boundary produced a nonfinite vertex");
				}
			}
		}
	}

	void MotionSelectedMeshVertices::EvaluateVertices(const MotionSelectedDeformationState& state, const MotionMeshSubdivision& subdivision, std::span<resource::MotionPoint> positions) const {
		if (positions.size() != MotionMeshGridVertexCount(subdivision)) {
			throw std::runtime_error("motion actual vertex allocation differs from its selected subdivision");
		}
		const auto& source = RequireSourceGeometry(state);
		const auto* ancestor = state.Active() || state.StopsInheritance() ? &state : state.Ancestor();
		if (ancestor == nullptr) {
			if (subdivision.X != 1 || subdivision.Y != 1) {
				throw std::runtime_error("motion undeformed actual source requires the native quad subdivision");
			}
			std::copy(source.Quad.begin(), source.Quad.end(), positions.begin());

			return;
		}
		if (!ancestor->Active()) {
			if (subdivision.X != 1 || subdivision.Y != 1) {
				throw std::runtime_error("motion stopping source without a patch requires the native quad subdivision");
			}
			EvaluateMotionBilinearGrid(source.Quad, subdivision, positions);
		} else if (CombinedChainReaches(state, *ancestor)) {
			EvaluateMotionDeformationGrid(RequireWorld(*ancestor), subdivision, ReferenceWindow(source, RequireSourceGeometry(*ancestor)), positions);
		} else {
			EvaluateMotionBilinearGrid(source.Quad, subdivision, positions);
			ApplyMotionDeformation(RequireWorld(*ancestor), positions);
		}
		while (!ancestor->StopsInheritance()) {
			ancestor = NextAncestor(*ancestor);
			if (ancestor == nullptr) {
				return;
			}
			if (ancestor->Active()) {
				ApplyMotionDeformation(RequireWorld(*ancestor), positions);
			}
		}
		AddBoundaryOffsets(*ancestor, positions);
	}

	bool MotionSelectedMeshVertices::GeometryChanged(std::uint32_t layer) const {
		RequireOwners();
		m_Ancestry.RequirePhase();
		return m_Ancestry.State(layer).Changed();
	}

	resource::MotionPoint MotionSelectedMeshVertices::DeformedCoordinate(std::uint32_t layer) const {
		RequireOwners();
		m_Ancestry.RequirePhase();
		const auto& state = m_Ancestry.State(layer);
		const auto numeric = m_Ancestry.m_Composition->Layers();
		if (layer >= numeric.size() || &state.Node() != m_Entries.at(layer).Node) {
			throw std::runtime_error("motion deformed coordinate requires its actual current selected numeric owner");
		}
		resource::MotionPoint point;
		point.X = numeric[layer].Hierarchy.Attributes.Coordinate[0];
		point.Y = numeric[layer].Hierarchy.Attributes.Coordinate[1];
		const auto* ancestor = &state;
		while (ancestor != nullptr) {
			if (ancestor->Active()) {
				ApplyMotionDeformation(RequireWorld(*ancestor), std::span(&point, 1));
			}
			if (ancestor->StopsInheritance()) {
				AddBoundaryOffsets(*ancestor, std::span(&point, 1));
				break;
			}
			ancestor = NextAncestor(*ancestor);
		}
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("motion deformed coordinate exceeds the finite supported domain");
		}
		return point;
	}

	void MotionSelectedMeshVertices::TransformPoints(std::uint32_t layer, std::span<resource::MotionPoint> points) const {
		RequireOwners();
		m_Ancestry.RequirePhase();
		const auto& state = m_Ancestry.State(layer);
		const auto numeric = m_Ancestry.m_Composition->Layers();
		if (layer >= numeric.size() || &state.Node() != m_Entries.at(layer).Node || points.size() > kMaximumSelectedVertexCount) {
			throw std::runtime_error("motion UID points require their actual current selected numeric owner and bounded input");
		}
		const auto& hierarchy = numeric[layer].Hierarchy;
		for (const auto& point : points) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("motion UID point transformation requires finite actual input");
			}
		}
		for (auto& point : points) {
			const auto x = point.X;
			const auto y = point.Y;
			point.X = (y * hierarchy.Transform.XY + x * hierarchy.Transform.XX) + hierarchy.Attributes.Coordinate[0];
			point.Y = (y * hierarchy.Transform.YY + x * hierarchy.Transform.YX) + hierarchy.Attributes.Coordinate[1];
		}
		const auto* ancestor = state.Ancestor();
		while (ancestor != nullptr) {
			if (ancestor->Active()) {
				ApplyMotionDeformation(RequireWorld(*ancestor), points);
			}
			if (ancestor->StopsInheritance()) {
				AddBoundaryOffsets(*ancestor, points);
				break;
			}
			ancestor = NextAncestor(*ancestor);
		}
		for (const auto& point : points) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("motion UID transformed points exceed their finite supported domain");
			}
		}
	}

	float MotionSelectedMeshVertices::DeformationScale(std::uint32_t layer) const {
		RequireOwners();
		m_Ancestry.RequirePhase();
		const auto& state = m_Ancestry.State(layer);
		if (&state.Node() != m_Entries.at(layer).Node) {
			throw std::runtime_error("motion UID deformation scale requires its actual selected ancestry cell");
		}
		float scale = 1;
		const auto* ancestor = state.Ancestor();
		while (ancestor != nullptr) {
			if (ancestor->Active()) {
				scale = scale * MeasureMotionBezierLatticeScale(ancestor->m_CombinedLattice);
				if (!std::isfinite(scale)) {
					throw std::runtime_error("motion UID deformation scale exceeds finite actual ancestor product");
				}
			}
			// Native50f8b0 ignores stop-inheritance here but skips each combined physical parent chain.
			ancestor = NextAncestor(*ancestor);
		}
		return scale;
	}

	void MotionSelectedMeshVertices::Compose() {
		m_Composed = false;
		RequireOwners();
		m_Ancestry.RequirePhase();
		if (m_VertexSerial == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("motion actual vertex composition serial is exhausted");
		}
		const auto numeric = m_Ancestry.m_Composition->Layers();
		if (m_RowOwner != nullptr && (m_RowOwner->m_PreparationSerial == 0 || m_RowOwner->m_PreparedMeshAncestrySerial != m_Ancestry.m_AncestrySerial || m_RowOwner->m_PreparedNumeric.data() != numeric.data() || m_RowOwner->m_PreparedNumeric.size() != numeric.size())) {
			throw std::runtime_error("motion mask vertices require their actual prepared row/numeric phase");
		}
		if (numeric.size() != m_Entries.size()) {
			throw std::runtime_error("motion selected vertex numeric cells differ from their actual selected owner");
		}
		auto ratio = m_Selected.m_Commands.MeshDivisionRatio();
		if (m_Selected.m_EnclosingChild != nullptr) {
			ratio = m_Selected.m_EnclosingChild->UltimateParentCommands().MeshDivisionRatio();
		} else if (m_Selected.m_EnclosingParticle != nullptr) {
			ratio = m_Selected.m_EnclosingParticle->UltimateParentCommands().MeshDivisionRatio();
		}
		if (!std::isfinite(ratio) || ratio < 0) {
			throw std::runtime_error("motion selected vertices require the actual finite ultimate player division ratio");
		}
		for (std::size_t index = 0; index < m_Entries.size(); ++index) {
			auto& entry = m_Entries[index];
			const auto layer = static_cast<std::uint32_t>(index);
			const auto& state = m_Ancestry.State(layer);
			const auto& node = state.Node();
			if (&node != entry.Node) {
				throw std::runtime_error("motion actual vertex cell differs from its selected ancestry node");
			}
			if (node.Prepared->Type == 1 || node.Prepared->Type == 2 || node.Prepared->Type == 3 || node.Prepared->Type == 4 || node.Prepared->Type == 7) {
				if (state.SourceGeometry() != nullptr || state.World() != nullptr) {
					throw std::runtime_error("motion structural/child vertex cell acquired substitute source geometry");
				}
				continue;
			}
			if (!state.Changed() || numeric[index].FrameHidden || !numeric[index].Active || !m_Ancestry.m_Sources.State(layer).Ready || m_Ancestry.m_Sources.State(layer).Blank) {
				continue;
			}
			const auto subdivision = SelectSubdivision(state, ratio, entry.Division);
			const auto count = MotionMeshGridVertexCount(subdivision);
			if (count > kMaximumSelectedVertexCount) {
				throw std::runtime_error("motion inherited vertices exceed their bounded actual work limit");
			}
			entry.Positions.resize(count);
			EvaluateVertices(state, subdivision, entry.Positions);
			entry.Subdivision = subdivision;
			entry.Geometry.Positions = entry.Positions;
			entry.Geometry.CachedBounds = MotionBounds{};
			entry.Geometry.Calculated = false;
			entry.Allocated = true;
		}
		if (m_RowOwner != nullptr) {
			m_RowPreparationSerial = m_RowOwner->m_PreparationSerial;
		}
		m_PreparationSerial = m_Ancestry.m_AncestrySerial;
		++m_VertexSerial;
		m_Composed = true;
	}

	void MotionSelectedMeshVertices::RequirePhase(std::uint32_t layer) const {
		RequireOwners();
		m_Ancestry.RequirePhase();
		if (!m_Composed || (m_RowOwner != nullptr && m_RowPreparationSerial != m_RowOwner->m_PreparationSerial) || m_PreparationSerial != m_Ancestry.m_AncestrySerial || layer >= m_Entries.size() || &m_Ancestry.State(layer).Node() != m_Entries[layer].Node || m_Entries[layer].Geometry.Positions.data() != m_Entries[layer].Positions.data() || m_Entries[layer].Geometry.Positions.size() != m_Entries[layer].Positions.size()) {
			throw std::runtime_error("motion selected vertices require their current actual completed owned geometry phase");
		}
	}

	const MotionSelectedLayerNode& MotionSelectedMeshVertices::Node(std::uint32_t layer) const {
		RequirePhase(layer);

		return *m_Entries[layer].Node;
	}

	bool MotionSelectedMeshVertices::Allocated(std::uint32_t layer) const {
		RequirePhase(layer);

		return m_Entries[layer].Allocated;
	}

	MotionClipGeometry& MotionSelectedMeshVertices::Geometry(std::uint32_t layer) {
		RequirePhase(layer);
		if (!m_Entries[layer].Allocated) {
			throw std::runtime_error("motion selected cell has no actual vertex allocation");
		}

		return m_Entries[layer].Geometry;
	}

	const MotionMeshSubdivision& MotionSelectedMeshVertices::Subdivision(std::uint32_t layer) const {
		RequirePhase(layer);
		if (!m_Entries[layer].Allocated) {
			throw std::runtime_error("motion selected cell has no actual subdivision allocation");
		}

		return m_Entries[layer].Subdivision;
	}

} // namespace sgre::scene
