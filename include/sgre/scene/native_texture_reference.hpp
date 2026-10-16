#pragma once

#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_clip_bounds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace sgre::scene {
	class MotionSharedTextures;

	struct NativeTextureSnapshot {
		render::SpriteTextureHandle Texture;
		MutableTextureOwner* MutableSource = nullptr;
		resource::MotionPoint Size;
		MotionBounds ActiveBounds;
		std::uint32_t OriginalFormat = 28;
		std::uint32_t Minification = 0;
		std::uint32_t Magnification = 0;
		std::uint32_t AddressU = 3;
		std::uint32_t AddressV = 3;
	};

	class NativeTextureReference {
	public:
		using ReadSource = std::function<NativeTextureSnapshot()>;
		using SetFiltering = std::function<void(std::uint32_t, std::uint32_t)>;
		// The actual source wrapper is strongly rooted by its script adapter; callbacks lend that exact source.
		// Descriptor identity is this object, independently of another wrapper referring to the same GPU texture.
		NativeTextureReference(ReadSource read_source, SetFiltering filtering);
		~NativeTextureReference() noexcept;
		void RequireCanRelease() const;
		NativeTextureReference(const NativeTextureReference&) = delete;
		NativeTextureReference& operator=(const NativeTextureReference&) = delete;
		NativeTextureReference(NativeTextureReference&&) = delete;
		NativeTextureReference& operator=(NativeTextureReference&&) = delete;
		[[nodiscard]] const NativeTextureSnapshot& Description() const noexcept;
		[[nodiscard]] NativeTextureSnapshot Read() const;
		void SetSmoothing(bool enabled);

	private:
		friend class MotionSharedTextures;
		mutable std::size_t m_Reservations = 0;
		ReadSource m_ReadSource;
		SetFiltering m_SetFiltering;
		NativeTextureSnapshot m_Description;
	};

} // namespace sgre::scene
