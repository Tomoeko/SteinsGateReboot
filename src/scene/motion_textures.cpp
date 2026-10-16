#include "sgre/scene/motion_textures.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	class MotionTextureWrapper::Implementation {
	public:
		Implementation(TextureCache& cache, std::uint64_t document_instance, const resource::ImageTexture& texture, std::span<const std::uint8_t> bytes) : Cache(cache), Handle(cache.Acquire(document_instance, texture, bytes)) {
		}

		TextureCache& Cache;
		CachedTextureHandle Handle;
		std::int32_t Minification = 0;
		std::int32_t Magnification = 0;
		std::uint32_t AddressU = 0;
		std::uint32_t AddressV = 0;
		bool Closed = false;
		bool Closing = false;

		bool IsLive() const {
			return !Closed && Cache.Contains(Handle);
		}

		void RequireLive() const {
			if (!IsLive()) {
				throw std::runtime_error("motion texture wrapper has expired");
			}
		}

		void SetFiltering(std::int32_t minification, std::int32_t magnification) {
			RequireLive();
			if (Closing) {
				throw std::runtime_error("motion texture wrapper mutation during release");
			}

			Minification = minification;
			Magnification = magnification;
			Cache.SetFiltering(Handle, minification, magnification);
		}

		void SetAddress(std::uint32_t horizontal, std::uint32_t vertical) {
			RequireLive();
			if (Closing) {
				throw std::runtime_error("motion texture wrapper address mutation during release");
			}

			Cache.SetAddress(Handle, horizontal, vertical);
			AddressU = horizontal;
			AddressV = vertical;
		}

		void Close() {
			if (Closed) {
				return;
			}

			if (Closing) {
				throw std::runtime_error("motion texture wrapper recursive release");
			}

			Closing = true;
			std::exception_ptr failure;
			try {
				Cache.ReleaseTexture(Handle);
			} catch (...) {
				failure = std::current_exception();
			}
			Closing = false;
			Closed = true;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	MotionTextureWrapper::MotionTextureWrapper(TextureCache& cache, std::uint64_t document_instance, const resource::ImageTexture& texture, std::span<const std::uint8_t> bytes) : m_Implementation(std::make_shared<Implementation>(cache, document_instance, texture, bytes)) {
	}

	MotionTextureWrapper::~MotionTextureWrapper() {
		try {
			Close();
		} catch (...) {
			// Explicit Close reports platform cleanup failures; destruction still expires every borrowed wrapper.
		}
	}

	bool MotionTextureWrapper::Borrowed::IsLive() const {
		const auto state = m_Implementation.lock();

		return state && state->IsLive();
	}

	bool MotionTextureWrapper::Borrowed::SameIdentity(const Borrowed& other) const {
		const auto state = m_Implementation.lock();
		const auto other_state = other.m_Implementation.lock();

		return state && other_state && state == other_state;
	}

	CachedTextureHandle MotionTextureWrapper::Borrowed::Texture() const {
		const auto state = m_Implementation.lock();
		if (!state) {
			throw std::runtime_error("borrowed motion texture wrapper has expired");
		}

		state->RequireLive();

		return state->Handle;
	}

	void MotionTextureWrapper::Borrowed::SetFiltering(std::int32_t minification, std::int32_t magnification) const {
		const auto state = m_Implementation.lock();
		if (!state) {
			throw std::runtime_error("borrowed motion texture sampler owner has expired");
		}
		state->SetFiltering(minification, magnification);
	}

	void MotionTextureWrapper::Borrowed::SetAddress(std::uint32_t horizontal, std::uint32_t vertical) const {
		const auto state = m_Implementation.lock();
		if (!state) {
			throw std::runtime_error("borrowed motion texture sampler owner has expired");
		}
		state->SetAddress(horizontal, vertical);
	}

	MotionTextureWrapper::Borrowed MotionTextureWrapper::Borrow() const {
		m_Implementation->RequireLive();
		Borrowed result;
		result.m_Implementation = m_Implementation;

		return result;
	}

	CachedTextureHandle MotionTextureWrapper::Texture() const {
		m_Implementation->RequireLive();

		return m_Implementation->Handle;
	}

	std::int32_t MotionTextureWrapper::Minification() const {
		m_Implementation->RequireLive();

		return m_Implementation->Minification;
	}

	std::int32_t MotionTextureWrapper::Magnification() const {
		m_Implementation->RequireLive();

		return m_Implementation->Magnification;
	}

	void MotionTextureWrapper::SetFiltering(std::int32_t minification, std::int32_t magnification) {
		m_Implementation->SetFiltering(minification, magnification);
	}

	std::uint32_t MotionTextureWrapper::AddressU() const {
		m_Implementation->RequireLive();

		return m_Implementation->AddressU;
	}

	std::uint32_t MotionTextureWrapper::AddressV() const {
		m_Implementation->RequireLive();

		return m_Implementation->AddressV;
	}

	void MotionTextureWrapper::SetAddress(std::uint32_t horizontal, std::uint32_t vertical) {
		m_Implementation->SetAddress(horizontal, vertical);
	}

	void MotionTextureWrapper::Close() {
		m_Implementation->Close();
	}

	struct MotionTextureSlotRecord {
		std::unique_ptr<MotionTextureWrapper> Owned;
		MotionTextureWrapper::Borrowed Wrapper;
		std::uint64_t Generation = 1;
		bool Active = false;
	};

	class MotionTextureSlots::Implementation {
	public:
		Implementation(TextureCache& cache, std::size_t max_slots) : Cache(cache) {
			if (max_slots < 8 || max_slots > 65536 || (max_slots & (max_slots - 1)) != 0) {
				throw std::runtime_error("motion texture slot bound must be a power of two from 8 through 65536");
			}

			Records.resize(max_slots);
		}

		TextureCache& Cache;
		std::vector<MotionTextureSlotRecord> Records;
		std::size_t Size = 8;
		std::size_t Count = 0;
		std::int32_t Minification = 0;
		std::int32_t Magnification = 0;
		bool Releasing = false;
		bool Closing = false;
		bool Closed = false;

		void RequireMutation() const {
			if (Releasing || Closing || Closed) {
				throw std::runtime_error("motion texture slot mutation requires its live preparation owner outside release");
			}
		}

		bool HasRecord(MotionTextureSlot slot) const {
			return slot.Owner == this && slot.Index < Size && Records[slot.Index].Active && Records[slot.Index].Generation == slot.Generation;
		}

		MotionTextureSlotRecord& Record(MotionTextureSlot slot) {
			if (!HasRecord(slot)) {
				throw std::runtime_error("motion texture slot is stale or belongs to another layer");
			}

			return Records[slot.Index];
		}

		MotionTextureSlot Handle(std::size_t index) const {
			return {static_cast<std::uint32_t>(index), Records[index].Generation, this};
		}

		MotionTextureSlot Register(MotionTextureWrapper::Borrowed wrapper, std::unique_ptr<MotionTextureWrapper> owned) {
			const auto state = wrapper.m_Implementation.lock();
			if (!state || &state->Cache != &Cache) {
				throw std::runtime_error("borrowed motion texture must have a live owner in this layer's shared cache");
			}

			// Native registration updates the wrapper and shared physical sampler before choosing the first empty slot.
			state->SetFiltering(Minification, Magnification);
			std::size_t index = 0;
			while (index < Size && (Records[index].Active || Records[index].Generation == std::numeric_limits<std::uint64_t>::max())) {
				++index;
			}

			if (index == Size) {
				if (Size == Records.size()) {
					throw std::runtime_error("motion texture slot capacity is exhausted");
				}

				Size *= 2;
			}

			auto& record = Records[index];
			record.Wrapper = std::move(wrapper);
			record.Owned = std::move(owned);
			record.Active = true;
			++Count;

			return Handle(index);
		}

		void Release(std::size_t index) {
			auto& record = Records[index];
			std::exception_ptr failure;
			Releasing = true;
			if (record.Owned) {
				try {
					record.Owned->Close();
				} catch (...) {
					failure = std::current_exception();
				}
			}
			// Native wrapper/GPU disposal precedes clearing the slot; borrowed wrappers have no disposal here.
			record.Owned.reset();
			record.Wrapper = {};
			record.Active = false;
			++record.Generation;
			--Count;
			Releasing = false;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Close() {
			if (Closed) {
				return;
			}

			RequireMutation();
			Closing = true;
			std::exception_ptr failure;
			for (std::size_t index = 0; index < Size; ++index) {
				if (!Records[index].Active) {
					continue;
				}

				try {
					Release(index);
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}

			Closing = false;
			Closed = true;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	MotionTextureSlots::MotionTextureSlots(TextureCache& cache, std::size_t max_slots) : m_Implementation(std::make_unique<Implementation>(cache, max_slots)) {
	}

	MotionTextureSlots::~MotionTextureSlots() {
		try {
			Close();
		} catch (...) {
			// Explicit Close reports cleanup failures after all owned slot wrappers have been retired.
		}
	}

	MotionTextureSlot MotionTextureSlots::Import(std::uint64_t document_instance, const resource::ImageTexture& texture, std::span<const std::uint8_t> bytes) {
		auto& state = *m_Implementation;
		state.RequireMutation();
		auto owned = std::make_unique<MotionTextureWrapper>(state.Cache, document_instance, texture, bytes);
		auto wrapper = owned->Borrow();

		return state.Register(std::move(wrapper), std::move(owned));
	}

	MotionTextureSlot MotionTextureSlots::RegisterBorrowed(MotionTextureWrapper::Borrowed texture) {
		m_Implementation->RequireMutation();

		return m_Implementation->Register(std::move(texture), {});
	}

	void MotionTextureSlots::Release(MotionTextureSlot slot) {
		auto& state = *m_Implementation;
		state.RequireMutation();
		static_cast<void>(state.Record(slot));
		state.Release(slot.Index);
	}

	bool MotionTextureSlots::ReleaseIndex(std::uint32_t index) {
		auto& state = *m_Implementation;
		state.RequireMutation();
		if (index >= state.Size) {
			throw std::runtime_error("motion texture slot index exceeds the native vector extent");
		}

		if (!state.Records[index].Active) {
			return false;
		}

		state.Release(index);

		return true;
	}

	bool MotionTextureSlots::Contains(MotionTextureSlot slot) const {
		const auto& state = *m_Implementation;

		return state.HasRecord(slot) && state.Records[slot.Index].Wrapper.IsLive();
	}

	MotionTextureSlotInfo MotionTextureSlots::Get(MotionTextureSlot slot) const {
		const auto& state = *m_Implementation;
		if (!state.HasRecord(slot)) {
			throw std::runtime_error("motion texture slot is stale or belongs to another layer");
		}

		const auto& record = state.Records[slot.Index];
		const auto wrapper = record.Wrapper.m_Implementation.lock();
		if (!wrapper) {
			throw std::runtime_error("borrowed motion texture wrapper has expired");
		}

		wrapper->RequireLive();

		return {wrapper->Handle, wrapper->Minification, wrapper->Magnification, !record.Owned};
	}

	std::size_t MotionTextureSlots::SlotCount() const {
		return m_Implementation->Size;
	}

	std::size_t MotionTextureSlots::RegisteredCount() const {
		return m_Implementation->Count;
	}

	void MotionTextureSlots::SetFiltering(std::int32_t minification, std::int32_t magnification) {
		auto& state = *m_Implementation;
		state.RequireMutation();
		state.Minification = minification;
		state.Magnification = magnification;
		for (std::size_t index = 0; index < state.Size; ++index) {
			const auto& record = state.Records[index];
			if (!record.Active) {
				continue;
			}

			const auto wrapper = record.Wrapper.m_Implementation.lock();
			if (!wrapper) {
				throw std::runtime_error("borrowed motion texture wrapper has expired");
			}

			wrapper->SetFiltering(minification, magnification);
		}
	}

	void MotionTextureSlots::Close() {
		m_Implementation->Close();
	}

} // namespace sgre::scene
