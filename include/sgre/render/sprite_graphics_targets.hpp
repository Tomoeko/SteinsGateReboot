#pragma once

#include "sgre/render/sprite_presentation.hpp"
#include "sgre/scene/render_target_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace sgre::render {
	class SpriteFrameEncoder;

	struct GraphicsMaskControlState {
		std::uint8_t Operation = 0;
		float ThresholdMinimum = 0;
		float ThresholdMaximum = 0;
	};

	class SpriteGraphicsTargets {
	public:
		// Allocations and shader preparation occur before traversal. This owner never creates a window.
		SpriteGraphicsTargets(SpritePassDescription primary_description, const std::filesystem::path& shader_source, std::size_t maximum_targets = 64);
		~SpriteGraphicsTargets();
		SpriteGraphicsTargets(const SpriteGraphicsTargets&) = delete;
		SpriteGraphicsTargets& operator=(const SpriteGraphicsTargets&) = delete;
		SpriteGraphicsTargets(SpriteGraphicsTargets&&) = delete;
		SpriteGraphicsTargets& operator=(SpriteGraphicsTargets&&) = delete;
		[[nodiscard]] std::shared_ptr<SpritePassRenderer> PrimaryTarget() const;
		[[nodiscard]] std::shared_ptr<SpritePassRenderer> AllocateTarget(SpritePassDescription description);
		// Registered physical mono targets only. Null unbinds; a changed nonnull target clears measured texture slots.
		// The selected target is weak. Its concrete allocation owner must enclose drawing and restoration.
		[[nodiscard]] std::weak_ptr<SpritePassRenderer> SelectTarget(const std::shared_ptr<SpritePassRenderer>& target);
		[[nodiscard]] std::shared_ptr<SpritePassRenderer> CurrentTarget() const;
		[[nodiscard]] std::shared_ptr<SpritePassRenderer> RequireRegisteredTarget(const SpritePassRenderer& actual_target) const;
		// Typed live target binding: same encoder/buffer, actual registry membership and full mono viewport/clip.
		void SelectFrameTarget(SpriteFrameEncoder& actual_encoder, const std::shared_ptr<SpritePassRenderer>& actual_target, const std::optional<std::array<float, 4>>& clear_color);
		[[nodiscard]] scene::RenderTargetLayout CurrentLayout() const;
		// Native4c6100 stores minimum, maximum, then operation byte, without numeric conversion.
		// Full byte is stored unchanged; shader selection interprets nonzero as the additional-coefficient variant.
		void ConfigureMaskControl(std::uint8_t operation, float threshold_minimum, float threshold_maximum);
		void ConfigureOrdinaryMaskControl(float threshold_minimum, float threshold_maximum);
		[[nodiscard]] GraphicsMaskControlState MaskControl() const;
		// Native4c6120/4c4920 store/read the complete graphics+F0 word. Rendering consumers select their supported mode separately.
		void SetMaskMode(std::uint32_t mode);
		[[nodiscard]] std::uint32_t MaskMode() const;
		// Allocation uses the actual primary graphics resource owner; region copies resolve the current target once.
		[[nodiscard]] std::unique_ptr<scene::MutableTextureBackend> CreateMutableTextureBackend();
		[[nodiscard]] SpritePresentationSubmission Submit(std::span<const SpritePassDraw> draws, const std::array<float, 16>& projection_registers, const std::array<float, 4>& clear_color = {}, SpritePassLoadAction load_action = SpritePassLoadAction::Clear);

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::render
