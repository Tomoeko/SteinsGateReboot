#pragma once

#include "sgre/render/presentation_rectangle.hpp"
#include "sgre/render/sprite_pass.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace sgre::render {

	class PresentationSynchronization;

	struct SpritePresentationSubmission {
		// The actual renderer that returned Identity; this tag adds no owner retention.
		const SpritePassRenderer* Owner = nullptr;
		std::uint64_t Identity = 0;
	};

	enum class SpritePresentationStatus {
		QueuedHidden,
		DrawableUnavailable,
		Busy
	};

	struct SpritePresentationDescription {
		// Native onscreen configuration is two; the live macOS layer supplies the checked capacity.
		std::uint32_t BufferCount = 2;
		// Opt-in platform UI. Construction remains offscreen; ShowWindow is the explicit activation boundary.
		bool VisibleWindow = false;
		std::uint32_t WindowWidthPoints = 1280;
		std::uint32_t WindowHeightPoints = 720;
		bool WindowCentered = true;
		bool Fullscreen = false;
		std::string Title = "STEINS;GATE RE:BOOT";
	};

	struct SpritePresentationGeometry {
		std::uint32_t WidthPixels = 0;
		std::uint32_t HeightPixels = 0;
		double BackingScale = 0;
		double WidthPoints = 0;
		double HeightPoints = 0;
		std::uint32_t SourceWidthPixels = 0;
		std::uint32_t SourceHeightPixels = 0;
		PresentationRectangle Content;
		bool Fullscreen = false;
		bool Miniaturized = false;
		bool DrawableAvailable = true;
		// Retain the windowed size while native fullscreen changes the content extent.
		double WindowedWidthPoints = 0;
		double WindowedHeightPoints = 0;
		bool WindowedSizeChanged = false;
	};

	class SpritePassPresentation {
	public:
		// Main-thread macOS presentation; default construction preserves the hidden diagnostic policy.
		// The actual renderer outlives this owner; its expiry is also checked before frame access.
		// Prepare transfer shaders at startup. Only explicit ShowWindow may launch, activate or show an app.
		// This existing diagnostic constructor preserves its three-drawable policy.
		SpritePassPresentation(SpritePassRenderer& renderer, const std::filesystem::path& transfer_shader_source);
		SpritePassPresentation(SpritePassRenderer& renderer, const std::filesystem::path& transfer_shader_source, SpritePresentationDescription description);
		~SpritePassPresentation();
		// Requires an interactive description and a bound actual event-pump borrower. Idempotent.
		void ShowWindow();
		// Authored launch status only. Empty text removes it; hidden observations
		// never create status controls or show/activate a window.
		void SetLoadingMessage(std::string_view message);
		SpritePassPresentation(const SpritePassPresentation&) = delete;
		SpritePassPresentation& operator=(const SpritePassPresentation&) = delete;
		SpritePassPresentation(SpritePassPresentation&&) = delete;
		SpritePassPresentation& operator=(SpritePassPresentation&&) = delete;

		// Serialize actual Submit -> TryPresentLatest -> next Submit on the same main rendering owner.
		// Requires the exact latest identity from the tagged renderer, with matching physical dimensions.
		// Apple's drawable request can wait up to one second. Nil never triggers show or retry.
		// QueuedHidden is the retained status name for GPU scheduling in either policy, not a compositor receipt.
		[[nodiscard]] SpritePresentationStatus TryPresentLatest(SpritePresentationSubmission submission);
		// Reads the exact live renderer Submit identity and validates its current physical target without a drawable request.
		[[nodiscard]] SpritePresentationSubmission CurrentSubmission() const;
		// Hidden diagnostic join for native interval0/1. Only a genuinely queued transfer adopts the pending request.
		// Root Draw/target selection, native GPU heap allocation ownership and displayed timing remain open.
		[[nodiscard]] SpritePresentationStatus TryPresentCurrent(PresentationSynchronization& synchronization, bool force_unsynchronized);
		// Reports asynchronous GPU errors and releases completed drawables without waiting or reading pixels.
		[[nodiscard]] bool PollTransfers();
		[[nodiscard]] SpritePresentationGeometry Geometry() const;
		// Reads the actual configured layer capacity while the renderer is alive and encoding is idle.
		// This metadata query requires no initialized submission or drawable and supplies no completion fence.
		[[nodiscard]] std::uint32_t DrawableCapacity() const;

	private:
		friend class FrameHostPool;
		void RequireRenderer(const SpritePassRenderer& actual_renderer) const;
		friend class PresentationSynchronization;
		void RegisterSynchronization(const PresentationSynchronization& synchronization) const;
		void RequireSynchronization(const PresentationSynchronization& synchronization) const;
		void ReleaseSynchronization(const PresentationSynchronization& synchronization) const;
		friend class SpritePassApplicationPumpBridge;
		friend class SpritePassDebugInspectorBridge;
#if defined(SGRE_SPRITE_PRESENTATION_FIXTURE)
		friend class SpritePassPresentationFixtureBridge;
#endif
		[[nodiscard]] SpritePresentationStatus TryPresentFrame(SpritePresentationSubmission submission);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::render
