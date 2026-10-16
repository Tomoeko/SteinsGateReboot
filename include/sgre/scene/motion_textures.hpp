#pragma once

#include "sgre/scene/texture_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::scene {

	class MotionTextureSlots;

	class MotionTextureWrapper {
	private:
		class Implementation;

	public:
		class Borrowed {
		public:
			Borrowed() = default;
			[[nodiscard]] bool IsLive() const;
			[[nodiscard]] bool SameIdentity(const Borrowed& other) const;
			[[nodiscard]] CachedTextureHandle Texture() const;
			void SetFiltering(std::int32_t minification, std::int32_t magnification) const;
			void SetAddress(std::uint32_t horizontal, std::uint32_t vertical) const;

		private:
			std::weak_ptr<Implementation> m_Implementation;
			friend class MotionTextureWrapper;
			friend class MotionTextureSlots;
		};

		// The cache outlives this owner and every borrowed slot. Construction decodes only on a cache miss.
		MotionTextureWrapper(TextureCache& cache, std::uint64_t document_instance, const resource::ImageTexture& texture, std::span<const std::uint8_t> psb_bytes);
		~MotionTextureWrapper();
		MotionTextureWrapper(const MotionTextureWrapper&) = delete;
		MotionTextureWrapper& operator=(const MotionTextureWrapper&) = delete;
		MotionTextureWrapper(MotionTextureWrapper&&) = delete;
		MotionTextureWrapper& operator=(MotionTextureWrapper&&) = delete;

		[[nodiscard]] Borrowed Borrow() const;
		[[nodiscard]] CachedTextureHandle Texture() const;
		[[nodiscard]] std::int32_t Minification() const;
		[[nodiscard]] std::int32_t Magnification() const;
		void SetFiltering(std::int32_t minification, std::int32_t magnification);
		[[nodiscard]] std::uint32_t AddressU() const;
		[[nodiscard]] std::uint32_t AddressV() const;
		void SetAddress(std::uint32_t horizontal, std::uint32_t vertical);
		void Close();

	private:
		std::shared_ptr<Implementation> m_Implementation;
		friend class MotionTextureSlots;
	};

	struct MotionTextureSlot {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const MotionTextureSlot&) const = default;
	};

	struct MotionTextureSlotInfo {
		CachedTextureHandle Texture;
		std::int32_t Minification = 0;
		std::int32_t Magnification = 0;
		bool Borrowed = false;
	};

	class MotionTextureSlots {
	public:
		// Native motion layers start with eight slots. The configured power-of-two bound is authored safety.
		explicit MotionTextureSlots(TextureCache& cache, std::size_t max_slots = 1024);
		~MotionTextureSlots();
		MotionTextureSlots(const MotionTextureSlots&) = delete;
		MotionTextureSlots& operator=(const MotionTextureSlots&) = delete;
		MotionTextureSlots(MotionTextureSlots&&) = delete;
		MotionTextureSlots& operator=(MotionTextureSlots&&) = delete;

		[[nodiscard]] MotionTextureSlot Import(std::uint64_t document_instance, const resource::ImageTexture& texture, std::span<const std::uint8_t> psb_bytes);
		// Borrowing shares the exact wrapper and never acquires another cache reference or extends its lifetime.
		[[nodiscard]] MotionTextureSlot RegisterBorrowed(MotionTextureWrapper::Borrowed texture);
		void Release(MotionTextureSlot slot);
		[[nodiscard]] bool ReleaseIndex(std::uint32_t index);
		[[nodiscard]] bool Contains(MotionTextureSlot slot) const;
		[[nodiscard]] MotionTextureSlotInfo Get(MotionTextureSlot slot) const;
		[[nodiscard]] std::size_t SlotCount() const;
		[[nodiscard]] std::size_t RegisteredCount() const;
		void SetFiltering(std::int32_t minification, std::int32_t magnification);
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
