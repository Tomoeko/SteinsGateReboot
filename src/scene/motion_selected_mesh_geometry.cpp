#include "sgre/scene/motion_selected_mesh_geometry.hpp"

#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_selected_layer_sources.hpp"

#include <cmath>
#include <stdexcept>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumSelectedMeshLayers = 65536;
	static constexpr std::size_t kMaximumSelectedMeshVertices = 65536;

	MotionSelectedMeshGeometry::MotionSelectedMeshGeometry(MotionSelectedLayerNodes& selected, MotionLayerComposition& composition, MotionSelectedLayerSources& sources, const MotionManagerRuntime& manager) : m_Selected(selected), m_Composition(composition), m_Sources(sources), m_Manager(manager) {
		RequireOwners();
		const auto nodes = selected.Nodes();
		if (nodes.size() > kMaximumSelectedMeshLayers || selected.m_MeshGeometryOwner != nullptr || selected.m_EnclosingChild != nullptr || selected.m_Root.MatrixMode() != MotionRootMatrixMode::Recomputed) {
			throw std::runtime_error("motion selected mesh geometry requires its exclusive bounded ordinary root; actual child transfer remains unsupported");
		}
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			const auto& layer = *nodes[index].Prepared;
			if (layer.Type != 0 || layer.MeshTransform != 1 || layer.MeshCombinator || (layer.MeshSyncChildMask & ~std::uint32_t{8}) != 0 || (nodes[index].Parent && *nodes[index].Parent >= index)) {
				throw std::runtime_error("motion selected mesh geometry requires every actual kind0 mode1 sync0/8 layer and raw parent; unsupported kinds are not omitted");
			}
		}

		m_Nodes.resize(nodes.size());
		m_Links.resize(nodes.size());
		m_Candidates.resize(nodes.size());
		m_Frames.resize(nodes.size());
		m_Caches.resize(nodes.size());
		m_Divisions.resize(nodes.size());
		m_Entries.resize(nodes.size());
		for (std::size_t index = 0; index < nodes.size(); ++index) {
			m_Entries[index].Node = &nodes[index];
		}
		selected.m_MeshGeometryOwner = this;
	}

	MotionSelectedMeshGeometry::~MotionSelectedMeshGeometry() noexcept {
		if (m_Selected.m_MeshGeometryOwner == this) {
			m_Selected.m_MeshGeometryOwner = nullptr;
		}
	}

	void MotionSelectedMeshGeometry::RequireOwners() const {
		const auto& selection = m_Selected.m_Commands.SelectedProject();
		if (!selection || !m_Selected.m_Published || m_Selected.m_Mutating || (m_Selected.m_MeshGeometryOwner != nullptr && m_Selected.m_MeshGeometryOwner != this) || m_Selected.m_SourceOwner != &m_Sources || m_Composition.m_SelectedOwner != &m_Selected || &m_Composition.m_Timeline != &m_Selected.m_Preparation || &m_Composition.m_Root != &m_Selected.m_Root || &m_Manager.Manager() != &m_Selected.m_Commands.Manager() || m_Sources.RequestingProject() != selection->Project) {
			throw std::runtime_error("motion selected mesh geometry requires its exact live selected numeric/source/manager owners");
		}
	}

	void MotionSelectedMeshGeometry::Compose() {
		m_Composed = false;
		m_Geometry.reset();
		RequireOwners();
		const auto* local = m_Selected.m_LocalNumericOwner;
		const auto numeric = m_Composition.Layers();
		const auto selected = m_Selected.Nodes();
		if (local == nullptr || local != m_Composition.m_LocalFrames || !local->m_Selected || local->m_Traversing || !m_Composition.m_ConditionalTrace || m_Composition.m_ComposedSelectionSerial != local->m_SelectionSerial || numeric.size() != m_Entries.size() || selected.size() != m_Entries.size()) {
			throw std::runtime_error("motion mesh geometry requires the current completed actual local/numeric phase");
		}
		const auto projection = m_Manager.DepthProjection();
		const auto ratio = m_Selected.m_Commands.MeshDivisionRatio();
		if (!std::isfinite(projection) || !std::isfinite(ratio) || ratio < 0) {
			throw std::runtime_error("motion mesh geometry requires finite actual manager projection and player division policies");
		}

		for (std::size_t index = 0; index < selected.size(); ++index) {
			const auto layer = static_cast<std::uint32_t>(index);
			const auto& cell = selected[index];
			const auto& mesh = m_Selected.MeshLocal(layer);
			const auto& source = m_Sources.State(layer);
			const auto current = local->State(layer).CurrentIndex;
			if (&cell != m_Entries[index].Node || cell.Prepared != &m_Selected.m_Preparation.Layers[index] || mesh.EvaluatedFrameIndex() != current || mesh.EvaluatedPosition() != local->Position(static_cast<std::uint32_t>(index)) || current >= cell.Prepared->Frames.size() || numeric[index].FrameHidden != cell.Prepared->Frames[current].Hidden) {
				throw std::runtime_error("motion mesh geometry differs from its actual raw/prepared/current/local tuple");
			}
			if (!source.Ready || !source.Binding || !source.Texture || source.Binding->Icon == nullptr || source.Binding->Source == nullptr || source.Texture->Source != source.Binding->Source || !std::holds_alternative<std::monostate>(source.Binding->Icon->Mesh.Data)) {
				throw std::runtime_error("motion mesh geometry requires its genuine ordinary registered source; missing/alternate source geometry remains unsupported");
			}

			auto& node = m_Nodes[index];
			node.Parent = cell.Parent;
			node.InheritMask = cell.Prepared->InheritMask;
			node.MeshTransform = cell.Prepared->MeshTransform;
			node.MeshSyncChildMask = cell.Prepared->MeshSyncChildMask;
			node.MeshDivision = cell.Prepared->MeshDivision;
			node.MeshCombine = cell.Prepared->MeshCombine;
			// Native node+3d8 uses raw absence, never evaluated activity or zero opacity.
			node.Hidden = cell.Prepared->Frames[current].Hidden;
			node.SourceReady = source.Ready;
			const auto& lattice = mesh.CurrentLattice();
			if (lattice.EncodedDegree != 0 && lattice.EncodedDegree != 0x33) {
				throw std::runtime_error("motion generalized mesh requires its actual mixed-scene geometry owner");
			}
			node.HasPatch = lattice.EncodedDegree != 0;
		}
		ResolveMotionDeformationLinks(m_Nodes, m_Candidates);

		for (std::size_t index = 0; index < selected.size(); ++index) {
			const auto layer = static_cast<std::uint32_t>(index);
			auto& entry = m_Entries[index];
			const auto& link = m_Candidates[index];
			entry.Rebuilt = link.Ancestor != m_Links[index].Ancestor || m_Composition.Changed(layer) || (link.Ancestor && m_Entries[*link.Ancestor].Rebuilt);
			m_Links[index] = link;
			if (entry.Rebuilt && !m_Nodes[index].Hidden) {
				m_Frames[index] = PrepareMotionGeometryFrame(*selected[index].Prepared, numeric[index].Hierarchy, *m_Sources.State(layer).Binding->Icon, projection);
				entry.HasFrame = true;
			}
			if (!entry.HasFrame) {
				throw std::runtime_error("motion selected mesh space needs its genuine initial visible source frame; constructor/hidden initial space remains unsupported");
			}
			const auto& lattice = m_Selected.MeshLocal(layer).CurrentLattice();
			m_Frames[index].Patch.reset();
			if (lattice.EncodedDegree != 0) {
				resource::MotionBezierPatch patch;
				std::copy(lattice.Controls.begin(), lattice.Controls.end(), patch.Controls.begin());
				m_Frames[index].Patch = patch;
			}
		}
		m_Geometry.emplace(m_Nodes, m_Frames, m_Links, m_Caches);

		for (std::size_t index = 0; index < selected.size(); ++index) {
			auto& entry = m_Entries[index];
			if (!entry.Rebuilt || m_Nodes[index].Hidden || !numeric[index].Active) {
				continue;
			}
			const auto layer = static_cast<std::uint32_t>(index);
			const auto subdivision = m_Geometry->SelectSubdivision(layer, ratio, m_Divisions);
			const auto count = MotionMeshGridVertexCount(subdivision);
			if (count > kMaximumSelectedMeshVertices) {
				throw std::runtime_error("motion selected mesh vertices exceed their bounded actual work limit");
			}
			entry.Positions.resize(count);
			m_Geometry->EvaluateVertices(layer, subdivision, entry.Positions);
			entry.Subdivision = subdivision;
			entry.Geometry.Positions = entry.Positions;
			entry.Geometry.CachedBounds = MotionBounds{};
			entry.Geometry.Calculated = false;
			entry.Allocated = true;
		}
		m_CompositionSerial = m_Composition.m_CompositionSerial;
		m_Composed = true;
	}

	void MotionSelectedMeshGeometry::RequirePhase(std::uint32_t layer) const {
		RequireOwners();
		if (!m_Composed || !m_Geometry || m_CompositionSerial != m_Composition.m_CompositionSerial || m_Composition.Layers().size() != m_Entries.size() || layer >= m_Entries.size() || &m_Selected.Nodes()[layer] != m_Entries[layer].Node) {
			throw std::runtime_error("motion selected mesh output requires its current actual completed geometry phase");
		}
	}

	const MotionDeformationLink& MotionSelectedMeshGeometry::Link(std::uint32_t layer) const {
		RequirePhase(layer);

		return m_Links[layer];
	}

	bool MotionSelectedMeshGeometry::Changed(std::uint32_t layer) const {
		RequirePhase(layer);

		return m_Entries[layer].Rebuilt;
	}

	bool MotionSelectedMeshGeometry::Allocated(std::uint32_t layer) const {
		RequirePhase(layer);

		return m_Entries[layer].Allocated;
	}

	MotionClipGeometry& MotionSelectedMeshGeometry::Geometry(std::uint32_t layer) {
		RequirePhase(layer);
		if (!m_Entries[layer].Allocated) {
			throw std::runtime_error("motion selected mesh has no actual vertex allocation");
		}

		return m_Entries[layer].Geometry;
	}

	const MotionMeshSubdivision& MotionSelectedMeshGeometry::Subdivision(std::uint32_t layer) const {
		RequirePhase(layer);
		if (!m_Entries[layer].Allocated) {
			throw std::runtime_error("motion selected mesh has no actual subdivision allocation");
		}

		return m_Entries[layer].Subdivision;
	}

} // namespace sgre::scene
