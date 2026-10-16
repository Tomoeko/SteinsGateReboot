#include "sgre/scene/motion_layer_uids.hpp"

#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_selected_mesh_vertices.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::scene {
	static constexpr std::size_t kMaximumUidRegistrations = 65536;
	static constexpr std::size_t kMaximumUidSubscriptions = 65536;
	static constexpr std::size_t kMaximumUidKeys = 65536;
	static constexpr std::size_t kMaximumUidKeyBytes = 8 * 1024 * 1024;
	static constexpr std::size_t kMaximumUidParentDepth = 64;

	static std::string_view SelectedUid(const MotionSelectedLayerNode& node) {
		if (node.Raw == nullptr) {
			throw std::runtime_error("motion UID reference requires its actual raw selected node");
		}

		for (const auto& [name, value] : node.Raw->Properties) {
			if (name == "uid") {
				const auto* text = std::get_if<std::string>(&value.Data);
				if (text == nullptr || text->size() > kMaximumMotionCommandNameBytes) {
					throw std::runtime_error("motion selected UID requires a bounded string");
				}

				return std::string_view(*text).substr(0, text->find('\0'));
			}
		}

		return {};
	}

	MotionLayerUidReference::MotionLayerUidReference(const MotionSelectedLayerNodes& selected, std::uint32_t native_ordinal)
	    : m_Selected(&selected), m_Player(&selected.m_Commands), m_NativeOrdinal(native_ordinal) {
		static_cast<void>(Node());
	}

	const MotionSelectedLayerNode& MotionLayerUidReference::Node() const {
		return Node(m_NativeOrdinal);
	}

	const MotionSelectedLayerNode& MotionLayerUidReference::Node(std::uint32_t native_ordinal) const {
		if (m_Selected == nullptr || native_ordinal == 0 || native_ordinal > m_Selected->m_Nodes.size()) {
			throw std::runtime_error("motion UID reference lies outside its actual selected tree");
		}

		const auto& node = m_Selected->m_Nodes[native_ordinal - 1];
		if (node.NativeOrdinal != native_ordinal || node.Raw == nullptr || node.Prepared == nullptr) {
			throw std::runtime_error("motion UID reference differs from its selected ordinal identity");
		}

		return node;
	}

	const MotionComposedLayer& MotionLayerUidReference::Numeric() const {
		static_cast<void>(Node());
		if (m_Selected->m_CompositionOwner == nullptr) {
			throw std::runtime_error("motion UID physics input requires its actual completed numeric owner");
		}
		const auto layers = m_Selected->m_CompositionOwner->Layers();
		if (layers.size() != m_Selected->m_Nodes.size()) {
			throw std::runtime_error("motion UID physics input requires every actual completed selected numeric cell");
		}
		return layers[m_NativeOrdinal - 1];
	}

	bool MotionLayerUidReference::Active() const {
		return Numeric().Active;
	}

	float MotionLayerUidReference::RotationRadians() const {
		const auto half_angle = Numeric().Hierarchy.Attributes.Angle * 3.1415927410125732421875f;
		return (half_angle + half_angle) / 360.0f;
	}

	float MotionLayerUidReference::Depth() const {
		return Numeric().Hierarchy.Attributes.Coordinate[2];
	}

	float MotionLayerUidReference::DeformationScale() const {
		static_cast<void>(Numeric());
		if (m_Selected->m_VertexOwner == nullptr) {
			throw std::runtime_error("motion UID deformation scale requires its actual current deformation owner");
		}
		return m_Selected->m_VertexOwner->DeformationScale(m_NativeOrdinal - 1);
	}

	void MotionLayerUidReference::TransformPoints(std::span<resource::MotionPoint> points) const {
		static_cast<void>(Numeric());
		if (m_Selected->m_VertexOwner == nullptr) {
			throw std::runtime_error("motion UID point transformation requires its actual current deformation owner");
		}
		m_Selected->m_VertexOwner->TransformPoints(m_NativeOrdinal - 1, points);
	}

	const resource::MotionBezierLattice& MotionLayerUidReference::CurrentMesh() const {
		const auto& node = Node();
		if (node.Prepared->MeshTransform != 1) {
			throw std::runtime_error("motion UID current mesh requires its actual recovered mode1 owner");
		}
		return m_Selected->MeshLocal(m_NativeOrdinal - 1).CurrentLattice();
	}

	resource::MotionPoint MotionLayerUidReference::MeshPhysicalOrigin() const {
		const auto& node = Node();
		if (node.Prepared->MeshTransform != 1) {
			throw std::runtime_error("motion UID physical origin requires its actual recovered mode1 owner");
		}
		return m_Selected->MeshLocal(m_NativeOrdinal - 1).PhysicalOrigin();
	}

	bool MotionLayerUidReference::SetMesh(std::uint32_t mode, const resource::MotionBezierLattice* lattice, bool compare) const {
		const auto& node = Node();
		if (mode != 1 || node.Prepared->MeshTransform != mode) {
			return false;
		}

		return m_Selected->MutableUidMesh(m_NativeOrdinal - 1).AssignLattice(lattice, compare);
	}

	bool MotionLayerUidReference::SetComparedNativeOutput(const resource::MotionBezierLattice& lattice) const {
		const auto& node = Node();
		if (node.Prepared->MeshTransform != 1) {
			return false;
		}
		return m_Selected->MutableUidMesh(m_NativeOrdinal - 1).AssignComparedNativeOutput(lattice);
	}

	void MotionLayerUidReference::InitializeMeshOrigin(const resource::MotionPoint& pixel_origin, const resource::MotionPoint& physical_origin) const {
		const auto& node = Node();
		if (node.Prepared->MeshTransform != 1) {
			throw std::runtime_error("motion UID origin initialization requires its actual mode1 mesh owner");
		}

		m_Selected->MutableUidMesh(m_NativeOrdinal - 1).AssignOrigins(pixel_origin, physical_origin);
	}

	MotionShapeSnapshot MotionLayerUidReference::ShapeSnapshot() const {
		static_cast<void>(Node());
		return m_Selected->Shape(m_NativeOrdinal - 1).Snapshot();
	}

	const MotionPlayerCommands& MotionLayerUidReference::Player() const {
		static_cast<void>(Node());

		return m_Selected->m_Commands;
	}

	std::uint32_t MotionLayerUidReference::NativeOrdinal() const noexcept {
		return m_NativeOrdinal;
	}

	bool MotionLayerUidReference::operator==(const MotionLayerUidReference& other) const noexcept {
		return m_Player == other.m_Player && m_NativeOrdinal == other.m_NativeOrdinal;
	}

	MotionPlayerLayerUids::MotionPlayerLayerUids(const MotionPlayerCommands& commands)
	    : m_Commands(commands) {
	}

	MotionPlayerLayerUids::~MotionPlayerLayerUids() noexcept {
		if (m_Registered != 0 || m_Subscriptions != 0 || m_ChildOwners != 0 || m_Dispatching) {
			std::terminate();
		}

		if (m_Parent != nullptr) {
			--m_Parent->m_ChildOwners;
		}
	}

	void MotionPlayerLayerUids::BindParent(MotionPlayerLayerUids& parent) {
		if (m_Parent != nullptr || m_ChildOwners != 0 || !m_Entries.empty() || !m_Observers.empty() || &m_Commands.Manager() != &parent.m_Commands.Manager() || parent.m_Depth + 1 >= kMaximumUidParentDepth || parent.m_ChildOwners == std::numeric_limits<std::size_t>::max()) {
			throw std::runtime_error("motion UID parent requires the exact fresh containing command owner");
		}

		m_Parent = &parent;
		m_Root = parent.m_Root;
		m_Depth = parent.m_Depth + 1;
		++parent.m_ChildOwners;
	}

	MotionPlayerLayerUids& MotionPlayerLayerUids::Root() noexcept {
		return *m_Root;
	}

	const MotionPlayerLayerUids& MotionPlayerLayerUids::Root() const noexcept {
		return *m_Root;
	}

	const MotionPlayerCommands& MotionPlayerLayerUids::UltimatePlayer() const noexcept {
		return m_Root->m_Commands;
	}

	std::string_view MotionPlayerLayerUids::ReadUid(std::string_view uid) const {
		if (uid.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion UID exceeds its native string bound");
		}

		return uid.substr(0, uid.find('\0'));
	}

	void MotionPlayerLayerUids::RequireIdleMutation() const {
		if (m_Root != this || m_Dispatching) {
			throw std::runtime_error("motion UID mutation requires its exact ultimate owner outside callbacks");
		}
	}

	void MotionPlayerLayerUids::RequireKeyBudget(std::string_view uid) const {
		if (m_KeyBytes > kMaximumUidKeyBytes || uid.size() > kMaximumUidKeyBytes - m_KeyBytes) {
			throw std::runtime_error("motion retained UID keys exceed their byte bound");
		}
	}

	MotionPlayerLayerUids::Bucket& MotionPlayerLayerUids::EntryBucket(std::string_view uid) {
		if (const auto found = m_Entries.find(uid); found != m_Entries.end()) {
			return found->second;
		}

		if (m_Entries.size() == kMaximumUidKeys) {
			throw std::runtime_error("motion UID entry keys exceed their bound");
		}
		RequireKeyBudget(uid);
		const auto [position, inserted] = m_Entries.try_emplace(std::string(uid));
		static_cast<void>(inserted);
		m_KeyBytes += uid.size();

		return position->second;
	}

	std::list<MotionLayerUidObserver*>& MotionPlayerLayerUids::ObserverBucket(std::string_view uid) {
		if (const auto found = m_Observers.find(uid); found != m_Observers.end()) {
			return found->second;
		}

		if (m_Observers.size() == kMaximumUidKeys) {
			throw std::runtime_error("motion UID observer keys exceed their bound");
		}
		RequireKeyBudget(uid);
		const auto [position, inserted] = m_Observers.try_emplace(std::string(uid));
		static_cast<void>(inserted);
		m_KeyBytes += uid.size();

		return position->second;
	}

	void MotionPlayerLayerUids::Register(const MotionSelectedLayerNodes& selected, std::uint32_t native_ordinal) {
		RequireIdleMutation();
		if (&selected.m_Commands.LayerUids() != this || m_Registered == kMaximumUidRegistrations) {
			throw std::runtime_error("motion UID registration requires its actual ultimate selected owner within bounds");
		}

		const MotionLayerUidReference reference(selected, native_ordinal);
		const auto uid = SelectedUid(reference.Node());
		if (uid.empty()) {
			throw std::runtime_error("motion UID registration requires its actual nonempty native string");
		}

		auto& bucket = EntryBucket(uid);
		// Preallocate the observer bucket before publishing a borrowed node; allocation failure cannot orphan a registration.
		auto& observers = ObserverBucket(uid);
		const auto occurrence = bucket.NextOccurrence++;
		bucket.Matches.push_back(MotionLayerUidMatch{reference, occurrence});
		++m_Registered;
		m_Dispatching = true;
		for (auto* observer : observers) {
			observer->LayerAdded(uid, bucket.Matches.back());
		}
		m_Dispatching = false;
	}

	void MotionPlayerLayerUids::Unregister(const MotionSelectedLayerNodes& selected, std::uint32_t native_ordinal) {
		RequireIdleMutation();
		if (&selected.m_Commands.LayerUids() != this) {
			throw std::runtime_error("motion UID retirement requires its exact ultimate selected owner");
		}

		const MotionLayerUidReference reference(selected, native_ordinal);
		const auto uid = SelectedUid(reference.Node());
		auto& bucket = EntryBucket(uid);
		const auto found = std::find_if(bucket.Matches.begin(), bucket.Matches.end(), [&](const auto& match) {
			return match.Layer == reference;
		});
		if (found == bucket.Matches.end()) {
			return;
		}

		auto& observers = ObserverBucket(uid);
		m_Dispatching = true;
		for (auto* observer : observers) {
			observer->LayerRemoved(uid, *found);
		}
		m_Dispatching = false;
		bucket.Matches.erase(found);
		--m_Registered;
	}

	void MotionPlayerLayerUids::Subscribe(std::string_view uid, MotionLayerUidObserver& observer) {
		RequireIdleMutation();
		uid = ReadUid(uid);
		if (m_Subscriptions == kMaximumUidSubscriptions) {
			throw std::runtime_error("motion UID subscriptions exceed their bound");
		}

		auto& entries = EntryBucket(uid);
		auto& observers = ObserverBucket(uid);
		observers.push_back(&observer);
		++m_Subscriptions;
		m_Dispatching = true;
		for (const auto& match : entries.Matches) {
			observer.LayerAdded(uid, match);
		}
		m_Dispatching = false;
	}

	void MotionPlayerLayerUids::Unsubscribe(std::string_view uid, MotionLayerUidObserver& observer) {
		RequireIdleMutation();
		uid = ReadUid(uid);
		auto& entries = EntryBucket(uid);
		auto& observers = ObserverBucket(uid);
		m_Dispatching = true;
		for (const auto& match : entries.Matches) {
			observer.LayerRemoved(uid, match);
		}
		m_Dispatching = false;
		for (auto current = observers.begin(); current != observers.end();) {
			if (*current == &observer) {
				current = observers.erase(current);
				--m_Subscriptions;
			} else {
				++current;
			}
		}
	}

	const std::list<MotionLayerUidMatch>& MotionPlayerLayerUids::Matches(std::string_view uid) const {
		uid = ReadUid(uid);
		const auto found = m_Root->m_Entries.find(uid);
		if (found == m_Root->m_Entries.end()) {
			throw std::runtime_error("motion UID query has no actual retained entry bucket");
		}

		return found->second.Matches;
	}

	std::uint32_t MotionPlayerLayerUids::NextOccurrence(std::string_view uid) const {
		uid = ReadUid(uid);
		const auto found = m_Root->m_Entries.find(uid);
		if (found == m_Root->m_Entries.end()) {
			throw std::runtime_error("motion UID counter query has no actual retained entry bucket");
		}

		return found->second.NextOccurrence;
	}

	std::size_t MotionPlayerLayerUids::RegisteredCount() const noexcept {
		return m_Root->m_Registered;
	}

	std::size_t MotionPlayerLayerUids::SubscriptionCount() const noexcept {
		return m_Root->m_Subscriptions;
	}
} // namespace sgre::scene
