#pragma once

#include "sgre/scene/motion_alternate_textures.hpp"
#include "sgre/scene/motion_blank_source.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_resources.hpp"

#include <functional>
#include <optional>
#include <string_view>

namespace sgre::scene {

	struct MotionStaticSourceServices {
		// Observational lookup returns this exact resolved source registration; it cannot reenter the binding.
		std::function<std::optional<RegisteredMotionSource>(const ResolvedMotionIcon&)> FindTexture;
	};

	struct MotionStaticSourceState {
		// Names borrow the immutable selected prepared frame owner, including cached failed names.
		std::string_view Source;
		std::string_view Icon;
		std::optional<ResolvedMotionIcon> Binding;
		std::optional<RegisteredMotionSource> Texture;
		// The actual blank branch owns this descriptor and suppresses ordinary textured rows.
		std::optional<MotionBlankSourceRectangle> Blank;
		// Actual +ba external source is separate from normal serialized source/icon and blank descriptors.
		std::optional<MotionExternalSourceBinding> External;
		bool Ready = false;
	};

	struct MotionBoundSpriteCommand {
		MotionSpriteCommand Sprite;
		MotionTextureSlot Slot;
		CachedTextureHandle Texture;
	};

	class MotionStaticSourceBinding {
	public:
		// Registry, requesting project, immutable frame/resource storage and actual texture owners outlive this cache and its published draws.
		MotionStaticSourceBinding(const PreparedMotionSourceRegistry& registry, MotionProjectIdentity project, MotionStaticSourceServices services, const MotionExternalSourceCell* external = nullptr);
		MotionStaticSourceBinding(const MotionStaticSourceBinding&) = delete;
		MotionStaticSourceBinding& operator=(const MotionStaticSourceBinding&) = delete;
		// Hidden and equal-name frames preserve the existing source state; an equal failed pair is not retried.
		[[nodiscard]] bool Bind(const PreparedMotionFrame& frame);
		bool Bind(const PreparedMotionFrame&&) = delete;
		[[nodiscard]] const MotionStaticSourceState& State() const;
		// Static undeformed ordinary sprite geometry/material is recovered independently; consumer publishes this actual texture-bound value.
		[[nodiscard]] std::optional<MotionBoundSpriteCommand> BuildSprite(const PreparedMotionLayer& layer, const MotionHierarchyState& state, float depth_projection) const;

	private:
		friend class MotionSceneEvaluation;
		void SynchronizeExternal() const;
		[[nodiscard]] bool BindAuthored(const PreparedMotionFrame& frame) const;
		const PreparedMotionSourceRegistry& m_Registry;
		MotionProjectIdentity m_Project;
		MotionStaticSourceServices m_Services;
		mutable MotionStaticSourceState m_State;
		const MotionExternalSourceCell* m_External = nullptr;
		const PreparedMotionFrame* m_CurrentFrame = nullptr;
		mutable std::uint64_t m_ExternalRevision = 0;
		mutable bool m_Binding = false;
	};

} // namespace sgre::scene
