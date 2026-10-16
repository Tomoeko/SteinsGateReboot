#pragma once

#include "sgre/scene/layer_manager_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace sgre::render {
	class SpritePassRenderer;
}

namespace sgre::scene {

	struct ScreenHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const ScreenHandle&) const = default;
	};

	struct ScreenDescription {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		bool InsertBefore = false;
	};

	struct ScreenState {
		ScreenDescription Description;
		LayerScreenRootHandle LayerRoot;
		std::array<std::int32_t, 4> BackgroundColor{};
		bool Visible = true;
	};

	struct ScreenBounds {
		float Left = 0;
		float Top = 0;
		float Right = 0;
		float Bottom = 0;
		float Width = 0;
		float Height = 0;
	};

	struct ScreenTargetAllocation {
		// The backend supplies the actual allocated extent. Ownership remains valid through queued GPU work.
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::shared_ptr<render::SpritePassRenderer> Renderer;
	};

	struct ScreenDriverServices {
		// Resolve the actual SQDriver BeforeLayersSecond (+130) group at each genuine creation request.
		// One resolved group, the tree, target backend and concrete Layer services outlive all pending payloads.
		std::function<ParallelTaskSet&()> ResolveTasks;
		LayerManagerFrameOperations Layers;
		// Borrows the exact target, manager and raw color state only for this actual task draw.
		// The containing graphics owner supplies the recovered target/camera/model/clip scope.
		// Native4fa250 calls the inner Layer manager Draw with0, independently of the outer task argument.
		std::function<void(ScreenHandle, render::SpritePassRenderer&, LayerManagerFrame&, const ScreenState&, std::uint32_t)> Draw;
	};

	struct ScreenRegistryLimits {
		std::size_t MaxScreens = 64;
		std::uint32_t MaxDimension = 16384;
		std::uint64_t MaxPixels = 64 * 1024 * 1024;
	};

	class ScreenRegistry {
	public:
		using TargetFactory = std::function<ScreenTargetAllocation(const ScreenDescription&)>;

		// The tree and target factory's borrowed services outlive the registry. Creation may allocate during native Update tail insertion; the concrete target factory supplies GPU admission.
		ScreenRegistry(LayerTree& tree, TargetFactory factory, ScreenRegistryLimits limits = {});
		~ScreenRegistry();
		ScreenRegistry(const ScreenRegistry&) = delete;
		ScreenRegistry& operator=(const ScreenRegistry&) = delete;
		ScreenRegistry(ScreenRegistry&&) = delete;
		ScreenRegistry& operator=(ScreenRegistry&&) = delete;

		// Configure once before the first screen. Unconfigured registries retain their immediate legacy lifetime.
		void ConfigureDriver(ScreenDriverServices services);
		[[nodiscard]] ScreenHandle Create(ScreenDescription description);
		[[nodiscard]] bool Contains(ScreenHandle screen) const;
		[[nodiscard]] ScreenState State(ScreenHandle screen) const;
		[[nodiscard]] ScreenBounds Bounds(ScreenHandle screen) const;
		[[nodiscard]] std::size_t Count() const;
		[[nodiscard]] std::size_t WriteOrder(std::span<ScreenHandle> output) const;
		// The returned renderer borrows this live screen; script release invalidates the borrow even when payload retirement is deferred.
		[[nodiscard]] render::SpritePassRenderer& Renderer(ScreenHandle screen) const;
		void SetVisible(ScreenHandle screen, bool visible);
		void SetBackgroundColor(ScreenHandle screen, std::array<std::int32_t, 4> color);
		// Check every fallible lifetime guard before script Object roots begin retirement.
		void RequireCanRelease(ScreenHandle screen) const;
		void Release(ScreenHandle screen);
		[[nodiscard]] bool CanClose() const noexcept;
		// Extracts only this registry's exact task registrations; unrelated sibling borrows are not consulted.
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
