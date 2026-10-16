#pragma once

#include "sgre/resource/motion.hpp"
#include "sgre/scene/motion_textures.hpp"
#include "sgre/scene/native_texture_reference.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace sgre::scene {

	struct MotionSharedTextureSlot {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const MotionSharedTextureSlot&) const = default;

		[[nodiscard]] std::int32_t EncodedIndex() const noexcept {
			return std::bit_cast<std::int32_t>(~Index);
		}
	};

	struct MotionSharedTexture {
		MotionTextureWrapper::Borrowed Wrapper;
		// A native descriptor is a distinct source identity, even when two descriptors name the same GPU texture.
		const NativeTextureReference* External = nullptr;
		resource::MotionPoint Dimensions;
		std::uint32_t References = 0;
	};

	class MotionSharedTextures {
	public:
		// Registrations borrow the exact wrapper. Its owner outlives every active image and frame using this table.
		explicit MotionSharedTextures(std::size_t max_slots = 1024);
		~MotionSharedTextures();
		MotionSharedTextures(const MotionSharedTextures&) = delete;
		MotionSharedTextures& operator=(const MotionSharedTextures&) = delete;
		MotionSharedTextures(MotionSharedTextures&&) = delete;
		MotionSharedTextures& operator=(MotionSharedTextures&&) = delete;

		[[nodiscard]] MotionSharedTextureSlot Register(MotionTextureWrapper::Borrowed texture, resource::MotionPoint dimensions);
		// Replacement releases the old registration before looking up the new owner, including an identical owner.
		void Replace(std::optional<MotionSharedTextureSlot>& current, MotionTextureWrapper::Borrowed texture, resource::MotionPoint dimensions);
		void Release(MotionSharedTextureSlot slot);
		// A descriptor reservation assigns no slot. It protects callback capacity before genuine UID subscription.
		void ReserveExternal(const NativeTextureReference& reference);
		void UnreserveExternal(const NativeTextureReference& reference) noexcept;
		[[nodiscard]] MotionSharedTextureSlot RegisterReservedExternal(const NativeTextureReference& reference) noexcept;
		void ReleaseReservedExternal(MotionSharedTextureSlot slot) noexcept;
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool Contains(MotionSharedTextureSlot slot) const;
		[[nodiscard]] MotionSharedTexture Get(MotionSharedTextureSlot slot) const;
		// Original negative command IDs omit the authored generation; resolve only within the live prepared frame.
		[[nodiscard]] MotionSharedTexture GetIndex(std::int32_t encoded_index) const;
		[[nodiscard]] std::optional<MotionSharedTexture> At(std::uint32_t index) const;
		[[nodiscard]] std::size_t SlotCount() const noexcept;
		[[nodiscard]] std::size_t RegisteredCount() const noexcept;
		// Clearing borrowed registrations performs no wrapper, cache or GPU disposal.
		void Close() noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
