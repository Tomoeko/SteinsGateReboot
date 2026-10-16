#pragma once

#include "sgre/scene/motion_layer_uids.hpp"
#include "sgre/scene/motion_shared_textures.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sgre::scene {

	class MotionPlayerCommands;
	class MotionSelectedLayerNodes;

	struct MotionExternalSourceBinding {
		const NativeTextureReference* Reference = nullptr;
		const MotionSharedTextures* Table = nullptr;
		MotionSharedTextureSlot Slot;
		std::int32_t Left = 0;
		std::int32_t Top = 0;
		std::int32_t Width = 0;
		std::int32_t Height = 0;
		resource::MotionPoint Origin;
		bool Installed = false;
	};

	struct MotionExternalSourceCell {
		std::optional<MotionExternalSourceBinding> Binding;
		std::uint64_t Revision = 0;
		bool RestoreRequested = false;
		bool Dirty = false;
	};

	class MotionAlternateTextures {
	public:
		// One actual ultimate player owns its listeners; native child players share that same UID registry.
		// Descriptors and their actual strong script roots outlive the matching registration.
		MotionAlternateTextures(const MotionPlayerCommands& commands, std::function<void()> require_unqueued, std::size_t maximum_listeners = 1024);
		~MotionAlternateTextures() noexcept;
		MotionAlternateTextures(const MotionAlternateTextures&) = delete;
		MotionAlternateTextures& operator=(const MotionAlternateTextures&) = delete;
		void Register(std::string_view uid, const NativeTextureReference& reference, const MotionBounds& rectangle, resource::MotionPoint origin);
		[[nodiscard]] bool Unregister(std::string_view uid);
		void Clear();
		[[nodiscard]] std::size_t Count() const noexcept;

	private:
		class Listener;
		static void Install(const MotionLayerUidMatch& match, const MotionExternalSourceBinding& binding, MotionSharedTextures& textures) noexcept;
		static void Remove(const MotionLayerUidMatch& match, bool restore, MotionSharedTextures& textures) noexcept;
		void RemoveListener(std::map<std::string, std::unique_ptr<Listener>, std::less<>>::iterator position, bool restore);
		MotionPlayerLayerUids& m_Uids;
		MotionSharedTextures& m_Textures;
		std::function<void()> m_RequireUnqueued;
		std::map<std::string, std::unique_ptr<Listener>, std::less<>> m_Listeners;
		std::size_t m_Maximum = 0;
		bool m_Mutating = false;
	};

} // namespace sgre::scene
