#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace sgre::scene {

	struct LayerHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const LayerHandle&) const = default;
	};

	struct LayerScreenRootHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const LayerScreenRootHandle&) const = default;
	};

	struct LayerPoint {
		float X = 0;
		float Y = 0;
		bool operator==(const LayerPoint&) const = default;
	};

	struct LayerClip {
		float Left = 1;
		float Top = 1;
		float Right = -1;
		float Bottom = -1;
		bool operator==(const LayerClip&) const = default;
	};

	struct LayerTransform {
		LayerPoint Center;
		LayerPoint Offset;
		LayerPoint Scale{1, 1};
		float AngleRadians = 0;
	};

	struct LayerAffineTransform {
		float XX = 1;
		float XY = 0;
		float TranslationX = 0;
		float YX = 0;
		float YY = 1;
		float TranslationY = 0;
	};

	[[nodiscard]] LayerAffineTransform PrepareLayerLocalTransform(const LayerTransform& transform);
	[[nodiscard]] LayerAffineTransform ComposeLayerTransforms(const LayerAffineTransform& local, const LayerAffineTransform& parent);
	[[nodiscard]] LayerPoint TransformLayerPoint(const LayerAffineTransform& transform, LayerPoint point);
	[[nodiscard]] LayerPoint InverseTransformLayerPoint(const LayerAffineTransform& transform, LayerPoint point);

	enum class LayerKind : std::uint8_t {
		Folder,
		Motion,
		RawTexture,
		Base
	};

	struct LayerState {
		LayerKind Kind = LayerKind::Folder;
		std::optional<LayerHandle> Owner;
		std::optional<LayerScreenRootHandle> ScreenOwner;
		LayerTransform Transform;
		LayerClip BaseClip;
		LayerClip Clip;
		float Priority = 0;
		bool Attached = true;
		bool Visible = true;
		bool Smoothing = false;
	};

	class LayerTree {
	public:
		explicit LayerTree(std::size_t max_layers = 65536);
		~LayerTree();
		LayerTree(const LayerTree&) = delete;
		LayerTree& operator=(const LayerTree&) = delete;
		LayerTree(LayerTree&&) = delete;
		LayerTree& operator=(LayerTree&&) = delete;

		// SQLayerBase-derived process owners use the same leaf membership without a child Layer manager.
		[[nodiscard]] LayerHandle CreateBase(std::optional<LayerHandle> owner = {});
		[[nodiscard]] LayerHandle CreateBaseInScreen(LayerScreenRootHandle screen);
		[[nodiscard]] LayerHandle CreateFolder(std::optional<LayerHandle> owner = {});
		[[nodiscard]] LayerHandle CreateFolderInScreen(LayerScreenRootHandle screen);
		// Motion leaves share common camera/clip state but cannot own a child manager.
		[[nodiscard]] LayerHandle CreateMotion(std::optional<LayerHandle> owner = {});
		[[nodiscard]] LayerHandle CreateMotionInScreen(LayerScreenRootHandle screen);
		[[nodiscard]] LayerHandle CreateRawTexture(std::optional<LayerHandle> owner = {});
		[[nodiscard]] LayerHandle CreateRawTextureInScreen(LayerScreenRootHandle screen);
		[[nodiscard]] LayerScreenRootHandle CreateScreenRoot();
		[[nodiscard]] bool ContainsScreenRoot(LayerScreenRootHandle screen) const;
		void ClearScreenRoot(LayerScreenRootHandle screen);
		void ReleaseScreenRoot(LayerScreenRootHandle screen);
		void RequireCanReleaseScreenRoot(LayerScreenRootHandle screen) const;
		void SetScreenOwner(LayerHandle layer, LayerScreenRootHandle screen);
		[[nodiscard]] std::size_t ScreenChildCount(LayerScreenRootHandle screen) const;
		void RequireCanClearScreenChildren(LayerScreenRootHandle screen) const;
		void VisitScreenChildren(LayerScreenRootHandle screen, const std::function<void(LayerHandle)>& visit, std::size_t max_visits = 65536);
		void PrepareScreenChildren(LayerScreenRootHandle screen);
		[[nodiscard]] std::size_t WriteScreenChildren(LayerScreenRootHandle screen, std::span<LayerHandle> output) const;
		[[nodiscard]] bool Contains(LayerHandle layer) const;
		[[nodiscard]] LayerState State(LayerHandle layer) const;
		[[nodiscard]] LayerAffineTransform WorldTransform(LayerHandle layer) const;
		void SetOwner(LayerHandle layer, std::optional<LayerHandle> owner);
		// A null owner selects the root manager. Clear detaches children instead of attaching them there.
		void Clear(LayerHandle folder);
		// The actual default SQDriver root manager detaches retained children during its own retirement.
		void ClearRoot();
		// Unlinks a live common leaf while retaining its typed record for ordered disposal.
		void Detach(LayerHandle layer);
		void Release(LayerHandle layer);
		// Native managers borrow children. Preflight before concrete owner resources are retired.
		void RequireCanRelease(LayerHandle layer) const;
		[[nodiscard]] std::size_t ChildCount(std::optional<LayerHandle> owner = {}) const;
		void RequireCanClearChildren(std::optional<LayerHandle> owner = {}) const;
		// Visit the actual live list: tail appends and future/prior removals take effect in this pass.
		// Current-member removal, same-manager recursion/sort and destroying the manager owner are rejected.
		void VisitChildren(std::optional<LayerHandle> owner, const std::function<void(LayerHandle)>& visit, std::size_t max_visits = 65536);
		// Native managers sort only when membership or priority changes, preserving existing tie order.
		void PrepareChildren(std::optional<LayerHandle> owner = {});
		// Writes manager registration order into caller storage without allocating.
		[[nodiscard]] std::size_t WriteChildren(std::optional<LayerHandle> owner, std::span<LayerHandle> output) const;
		void SetPriority(LayerHandle layer, float priority);
		void SetVisible(LayerHandle layer, bool visible);
		void SetSmoothing(LayerHandle layer, bool smoothing);
		void SetOffset(LayerHandle layer, LayerPoint offset);
		void SetCenter(LayerHandle layer, LayerPoint center);
		void SetScale(LayerHandle layer, LayerPoint scale);
		void SetZoom(LayerHandle layer, float zoom);
		void SetAngleRadians(LayerHandle layer, float radians);
		void SetAngleDegrees(LayerHandle layer, float degrees);
		[[nodiscard]] float AngleDegrees(LayerHandle layer) const;
		void SetBaseClip(LayerHandle layer, LayerPoint origin, LayerPoint extent);
		void ResetBaseClip(LayerHandle layer);
		void SetClip(LayerHandle layer, LayerPoint origin, LayerPoint extent);
		void ResetClip(LayerHandle layer);

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
