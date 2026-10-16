#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/motion_shape_owner.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace sgre::scene {
	struct MotionComposedLayer;
	class MotionPlayerCommands;
	class MotionAlternateTextures;
	class MotionSelectedLayerNodes;
	struct MotionSelectedLayerNode;
	class MotionPlayerLayerUids;

	class MotionLayerUidReference {
	public:
		// The actual selected tree remains alive through the matching removal callback.
		[[nodiscard]] const MotionSelectedLayerNode& Node() const;
		// Native local-ordinal resolution can also observe other actual nodes during per-node finalization.
		[[nodiscard]] const MotionSelectedLayerNode& Node(std::uint32_t native_ordinal) const;
		[[nodiscard]] const MotionPlayerCommands& Player() const;
		[[nodiscard]] MotionShapeSnapshot ShapeSnapshot() const;
		// Native Velvety input reads the actual completed plain-player numeric/deformation owners.
		[[nodiscard]] bool Active() const;
		[[nodiscard]] float RotationRadians() const;
		// Native519b20 reads the actual completed node+364 hierarchy coordinate.
		[[nodiscard]] float Depth() const;
		// Native50f8b0: actual combined ancestor deformation scale, excluding this own lattice.
		[[nodiscard]] float DeformationScale() const;
		void TransformPoints(std::span<resource::MotionPoint> points) const;
		// Native518e60/51cb50 and519470 borrow the genuine current mode1 controls/origin.
		// This reference expires at the next actual local evaluation or pending mesh replacement.
		[[nodiscard]] const resource::MotionBezierLattice& CurrentMesh() const;
		[[nodiscard]] resource::MotionPoint MeshPhysicalOrigin() const;
		// Native51b5b0/526df0 changes the pending real mesh only; completed frame geometry remains borrowed.
		bool SetMesh(std::uint32_t mode, const resource::MotionBezierLattice* lattice, bool compare) const;
		void RequireCanAssignMeshFrom(const MotionLayerUidReference& source) const;
		void AssignMeshFrom(const MotionLayerUidReference& source) const;
		// Native51f0f0 initializes the actual mesh override latch and retained boundary origins without forcing a numeric phase.
		void InitializeMeshOrigin(const resource::MotionPoint& pixel_origin, const resource::MotionPoint& physical_origin) const;
		[[nodiscard]] std::uint32_t NativeOrdinal() const noexcept;
		[[nodiscard]] bool operator==(const MotionLayerUidReference& other) const noexcept;

	private:
		friend class MotionPlayerLayerUids;
		friend class MotionAlternateTextures;
		friend class EmoteJellyControl;
		friend class EmoteStringControl;
		friend class EmoteBloomControl;
		bool SetComparedNativeOutput(const resource::MotionBezierLattice& lattice) const;
		MotionLayerUidReference(const MotionSelectedLayerNodes& selected, std::uint32_t native_ordinal);
		[[nodiscard]] const MotionComposedLayer& Numeric() const;
		const MotionSelectedLayerNodes* m_Selected = nullptr;
		const MotionPlayerCommands* m_Player = nullptr;
		std::uint32_t m_NativeOrdinal = 0;
	};

	struct MotionLayerUidMatch {
		MotionLayerUidReference Layer;
		std::uint32_t Occurrence = 0;
	};

	class MotionLayerUidObserver {
	public:
		virtual ~MotionLayerUidObserver() = default;
		virtual void LayerAdded(std::string_view uid, const MotionLayerUidMatch& match) noexcept = 0;
		virtual void LayerRemoved(std::string_view uid, const MotionLayerUidMatch& match) noexcept = 0;
	};

	class MotionPlayerLayerUids {
	public:
		~MotionPlayerLayerUids() noexcept;
		MotionPlayerLayerUids(const MotionPlayerLayerUids&) = delete;
		MotionPlayerLayerUids& operator=(const MotionPlayerLayerUids&) = delete;
		// All child commands borrow this exact ultimate command owner's lists and counters.
		[[nodiscard]] const MotionPlayerCommands& UltimatePlayer() const noexcept;
		// Borrowed observers outlive every subscription. Duplicate subscriptions remain distinct.
		void Subscribe(std::string_view uid, MotionLayerUidObserver& observer);
		void Unsubscribe(std::string_view uid, MotionLayerUidObserver& observer);
		// The returned list borrows actual selected nodes until their removal notification finishes.
		[[nodiscard]] const std::list<MotionLayerUidMatch>& Matches(std::string_view uid) const;
		[[nodiscard]] std::uint32_t NextOccurrence(std::string_view uid) const;
		[[nodiscard]] std::size_t RegisteredCount() const noexcept;
		[[nodiscard]] std::size_t SubscriptionCount() const noexcept;

	private:
		friend class MotionPlayerCommands;
		friend class MotionAlternateTextures;
		friend class MotionSelectedLayerNodes;
		struct Bucket {
			std::list<MotionLayerUidMatch> Matches;
			std::uint32_t NextOccurrence = 0;
		};
		explicit MotionPlayerLayerUids(const MotionPlayerCommands& commands);
		void BindParent(MotionPlayerLayerUids& parent);
		[[nodiscard]] MotionPlayerLayerUids& Root() noexcept;
		[[nodiscard]] const MotionPlayerLayerUids& Root() const noexcept;
		[[nodiscard]] std::string_view ReadUid(std::string_view uid) const;
		void RequireIdleMutation() const;
		void RequireKeyBudget(std::string_view uid) const;
		[[nodiscard]] Bucket& EntryBucket(std::string_view uid);
		[[nodiscard]] std::list<MotionLayerUidObserver*>& ObserverBucket(std::string_view uid);
		void Register(const MotionSelectedLayerNodes& selected, std::uint32_t native_ordinal);
		void Unregister(const MotionSelectedLayerNodes& selected, std::uint32_t native_ordinal);
		const MotionPlayerCommands& m_Commands;
		MotionAlternateTextures* m_AlternateTextures = nullptr;
		MotionPlayerLayerUids* m_Parent = nullptr;
		MotionPlayerLayerUids* m_Root = this;
		std::map<std::string, Bucket, std::less<>> m_Entries;
		std::map<std::string, std::list<MotionLayerUidObserver*>, std::less<>> m_Observers;
		std::size_t m_ChildOwners = 0;
		std::size_t m_Depth = 0;
		std::size_t m_Registered = 0;
		std::size_t m_Subscriptions = 0;
		std::size_t m_KeyBytes = 0;
		bool m_Dispatching = false;
	};
} // namespace sgre::scene
