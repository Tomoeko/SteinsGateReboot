#pragma once

#include "sgre/scene/motion_blank_source.hpp"
#include "sgre/scene/motion_evaluator.hpp"
#include "sgre/scene/motion_source_registry.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sgre::scene {

	enum class MotionDrawTopology {
		TriangleStrip,
		TriangleList
	};

	struct MotionSpriteMaterial {
		// Resource identities borrow the supplied source and icon.
		std::string_view Source;
		std::string_view Icon;
		std::array<std::uint32_t, 4> Colors{};
		resource::PixelFormat TextureFormat = resource::PixelFormat::Bc7;
		std::uint32_t TextureAttributes = 0;
		std::uint32_t BlendMode = 0x10;
	};

	struct MotionSpriteVertex {
		resource::MotionPoint Position;
		resource::MotionPoint Uv;
		std::uint32_t Color = 0x808080ff;
	};

	struct MotionSpriteCommand {
		// Names borrow the supplied MotionSource/MotionIcon, which must outlive this command.
		std::string_view Source;
		std::string_view Icon;
		// Row-major TL, TR, BL, BR storage; the native strip traverses TL, BL, TR, BR.
		std::array<MotionSpriteVertex, 4> Vertices;
		std::array<std::uint16_t, 4> Indices = {0, 2, 1, 3};
		MotionDrawTopology Topology = MotionDrawTopology::TriangleStrip;
		resource::PixelFormat TextureFormat = resource::PixelFormat::Bc7;
		std::uint32_t TextureAttributes = 0;
		std::uint32_t BlendMode = 0x10;
		float Depth = 0;
	};

	[[nodiscard]] std::optional<MotionSpriteMaterial> EvaluateMotionExternalMaterial(const MotionHierarchyState& state);
	[[nodiscard]] std::array<resource::MotionPoint, 4> EvaluateMotionBlankQuad(const MotionHierarchyState& state, const MotionBlankSourceRectangle& rectangle, float depth_projection);
	[[nodiscard]] std::array<resource::MotionPoint, 4> EvaluateMotionSpriteQuad(const MotionHierarchyState& state, const resource::MotionIcon& icon, float depth_projection);
	[[nodiscard]] std::optional<MotionSpriteMaterial> EvaluateMotionSpriteMaterial(const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon);
	[[nodiscard]] std::optional<MotionSpriteMaterial> EvaluateMotionSpriteMaterial(const MotionHierarchyState& state, const ResolvedMotionIcon& binding);

	// Native depth projection and inherited deformation state are resolved by the scene owner.
	[[nodiscard]] std::optional<MotionSpriteCommand> BuildMotionSpriteCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, float depth_projection, bool inherited_deformation);
	[[nodiscard]] std::optional<MotionSpriteCommand> BuildMotionSpriteCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const ResolvedMotionIcon& binding, float depth_projection, bool inherited_deformation);

} // namespace sgre::scene
