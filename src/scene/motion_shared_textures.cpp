#include "sgre/scene/motion_shared_textures.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	struct MotionSharedTextureRecord {
		MotionSharedTexture Texture;
		std::uint64_t Generation = 1;
	};

	class MotionSharedTextures::Implementation {
	public:
		explicit Implementation(std::size_t max_slots) {
			if (max_slots < 8 || max_slots > 65536 || (max_slots & (max_slots - 1)) != 0) {
				throw std::runtime_error("shared motion texture bound must be a power of two from 8 through 65536");
			}

			Records.resize(max_slots);
			Reservations.reserve(max_slots);
		}

		struct Reservation {
			const NativeTextureReference* Reference = nullptr;
			std::size_t Owners = 0;
		};
		std::vector<MotionSharedTextureRecord> Records;
		std::vector<Reservation> Reservations;
		std::size_t Size = 8;
		std::size_t Count = 0;
		bool Closed = false;

		void RequireLive() const {
			if (Closed) {
				throw std::runtime_error("shared motion texture table has closed");
			}
		}

		bool HasRecord(MotionSharedTextureSlot slot) const {
			return !Closed && slot.Owner == this && slot.Index < Size && Records[slot.Index].Texture.References != 0 && Records[slot.Index].Generation == slot.Generation;
		}

		const MotionSharedTextureRecord& Record(MotionSharedTextureSlot slot) const {
			if (!HasRecord(slot)) {
				throw std::runtime_error("shared motion texture slot is stale or belongs to another owner");
			}

			return Records[slot.Index];
		}

		MotionSharedTextureSlot Handle(std::size_t index) const {
			return {static_cast<std::uint32_t>(index), Records[index].Generation, this};
		}

		MotionSharedTextureSlot Register(MotionTextureWrapper::Borrowed texture, resource::MotionPoint dimensions) {
			RequireLive();
			if (!texture.IsLive()) {
				throw std::runtime_error("shared motion texture registration requires a live borrowed wrapper");
			}

			for (std::size_t index = 0; index < Size; ++index) {
				auto& record = Records[index];
				if (record.Texture.References == 0 || record.Texture.External != nullptr || !record.Texture.Wrapper.SameIdentity(texture)) {
					continue;
				}

				if (record.Texture.References == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
					throw std::runtime_error("shared motion texture references exceed the signed native range");
				}

				++record.Texture.References;

				return Handle(index);
			}

			const auto local_count = std::count_if(Records.begin(), Records.begin() + static_cast<std::ptrdiff_t>(Size), [](const auto& record) {
				return record.Texture.References != 0 && record.Texture.External == nullptr;
			});
			if (static_cast<std::size_t>(local_count) + Reservations.size() == Records.size()) {
				throw std::runtime_error("shared motion texture capacity is reserved by actual external listeners");
			}

			std::size_t index = 0;
			while (index < Size && (Records[index].Texture.References != 0 || Records[index].Generation == std::numeric_limits<std::uint64_t>::max())) {
				++index;
			}

			if (index == Size) {
				if (Size == Records.size()) {
					throw std::runtime_error("shared motion texture capacity is exhausted");
				}

				Size *= 2;
			}

			auto& record = Records[index];
			record.Texture.Wrapper = std::move(texture);
			record.Texture.Dimensions = dimensions;
			record.Texture.References = 1;
			++Count;

			return Handle(index);
		}

		void Release(MotionSharedTextureSlot slot) {
			static_cast<void>(Record(slot));
			auto& record = Records[slot.Index];
			--record.Texture.References;
			if (record.Texture.References != 0) {
				return;
			}

			// The native row drops its borrowed pointer; dimensions remain until the next first registration.
			record.Texture.Wrapper = {};
			record.Texture.External = nullptr;
			if (record.Generation != std::numeric_limits<std::uint64_t>::max()) {
				++record.Generation;
			}
			--Count;
		}

		MotionSharedTexture Get(MotionSharedTextureSlot slot) const {
			const auto& texture = Record(slot).Texture;
			if (texture.External != nullptr) {
				static_cast<void>(texture.External->Read());
			} else if (!texture.Wrapper.IsLive()) {
				throw std::runtime_error("shared motion texture wrapper has expired");
			}

			return texture;
		}

		void Close() noexcept {
			if (Closed) {
				return;
			}

			if (!Reservations.empty()) {
				std::terminate();
			}
			for (std::size_t index = 0; index < Size; ++index) {
				auto& record = Records[index];
				record.Texture.Wrapper = {};
				record.Texture.References = 0;
			}
			Count = 0;
			Closed = true;
		}
	};

	MotionSharedTextures::MotionSharedTextures(std::size_t max_slots) : m_Implementation(std::make_unique<Implementation>(max_slots)) {
	}

	MotionSharedTextures::~MotionSharedTextures() {
		if (!m_Implementation->Reservations.empty()) {
			std::terminate();
		}
	}

	MotionSharedTextureSlot MotionSharedTextures::Register(MotionTextureWrapper::Borrowed texture, resource::MotionPoint dimensions) {
		return m_Implementation->Register(std::move(texture), dimensions);
	}

	void MotionSharedTextures::Replace(std::optional<MotionSharedTextureSlot>& current, MotionTextureWrapper::Borrowed texture, resource::MotionPoint dimensions) {
		if (current) {
			Release(*current);
			current.reset();
		}

		current = Register(std::move(texture), dimensions);
	}

	void MotionSharedTextures::Release(MotionSharedTextureSlot slot) {
		m_Implementation->Release(slot);
	}

	bool MotionSharedTextures::Contains(MotionSharedTextureSlot slot) const {
		if (!m_Implementation->HasRecord(slot)) {
			return false;
		}
		const auto& texture = m_Implementation->Records[slot.Index].Texture;
		if (texture.External != nullptr) {
			static_cast<void>(texture.External->Read());
			return true;
		}

		return texture.Wrapper.IsLive();
	}

	MotionSharedTexture MotionSharedTextures::Get(MotionSharedTextureSlot slot) const {
		return m_Implementation->Get(slot);
	}

	MotionSharedTexture MotionSharedTextures::GetIndex(std::int32_t encoded_index) const {
		if (encoded_index >= 0) {
			throw std::runtime_error("shared motion texture lookup requires a negative encoded index");
		}

		const auto index = ~std::bit_cast<std::uint32_t>(encoded_index);
		if (index >= m_Implementation->Size) {
			throw std::runtime_error("shared motion texture lookup exceeds the table extent");
		}

		return m_Implementation->Get(m_Implementation->Handle(index));
	}

	std::optional<MotionSharedTexture> MotionSharedTextures::At(std::uint32_t index) const {
		m_Implementation->RequireLive();
		if (index >= m_Implementation->Size) {
			throw std::runtime_error("shared motion texture inspection exceeds the table extent");
		}

		if (m_Implementation->Records[index].Texture.References == 0) {
			return {};
		}

		return m_Implementation->Get(m_Implementation->Handle(index));
	}

	std::size_t MotionSharedTextures::SlotCount() const noexcept {
		return m_Implementation->Size;
	}

	std::size_t MotionSharedTextures::RegisteredCount() const noexcept {
		return m_Implementation->Count;
	}

	void MotionSharedTextures::ReserveExternal(const NativeTextureReference& reference) {
		auto& state = *m_Implementation;
		state.RequireLive();
		if (reference.m_Reservations == std::numeric_limits<std::size_t>::max()) {
			throw std::runtime_error("NativeTextureRef descriptor borrow count overflow");
		}
		static_cast<void>(reference.Read());
		const auto found = std::find_if(state.Reservations.begin(), state.Reservations.end(), [&](const auto& entry) {
			return entry.Reference == &reference;
		});
		if (found != state.Reservations.end()) {
			if (found->Owners == static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) / 65536) {
				throw std::runtime_error("motion external descriptor reservation count overflow");
			}
			++found->Owners;
			++reference.m_Reservations;

			return;
		}
		const auto local_count = std::count_if(state.Records.begin(), state.Records.begin() + static_cast<std::ptrdiff_t>(state.Size), [](const auto& record) {
			return record.Texture.References != 0 && record.Texture.External == nullptr;
		});
		if (state.Reservations.size() + static_cast<std::size_t>(local_count) == state.Records.size()) {
			throw std::runtime_error("motion external descriptor reservation capacity exhausted");
		}

		state.Reservations.push_back(Implementation::Reservation{&reference, 1});
		++reference.m_Reservations;
	}

	void MotionSharedTextures::UnreserveExternal(const NativeTextureReference& reference) noexcept {
		auto& state = *m_Implementation;
		const auto found = std::find_if(state.Reservations.begin(), state.Reservations.end(), [&](const auto& entry) {
			return entry.Reference == &reference;
		});
		if (found == state.Reservations.end() || found->Owners == 0 || reference.m_Reservations == 0) {
			std::terminate();
		}
		--reference.m_Reservations;
		if (--found->Owners == 0) {
			if (std::any_of(state.Records.begin(), state.Records.begin() + static_cast<std::ptrdiff_t>(state.Size), [&](const auto& record) {
				    return record.Texture.External == &reference;
			    })) {
				std::terminate();
			}
			state.Reservations.erase(found);
		}
	}

	MotionSharedTextureSlot MotionSharedTextures::RegisterReservedExternal(const NativeTextureReference& reference) noexcept {
		auto& state = *m_Implementation;
		if (state.Closed || std::none_of(state.Reservations.begin(), state.Reservations.end(), [&](const auto& entry) {
			    return entry.Reference == &reference;
		    })) {
			std::terminate();
		}
		std::size_t index = 0;
		while (index < state.Size && state.Records[index].Texture.External != &reference) {
			++index;
		}
		if (index == state.Size) {
			index = 0;
			while (index < state.Size && (state.Records[index].Texture.References != 0 || state.Records[index].Generation == std::numeric_limits<std::uint64_t>::max())) {
				++index;
			}
			if (index == state.Size) {
				if (state.Size == state.Records.size()) {
					std::terminate();
				}
				state.Size *= 2;
			}
			auto& texture = state.Records[index].Texture;
			texture.External = &reference;
			texture.Dimensions = reference.Description().Size;
			++state.Count;
		}
		auto& texture = state.Records[index].Texture;
		if (texture.References == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
			std::terminate();
		}
		++texture.References;

		return state.Handle(index);
	}

	void MotionSharedTextures::ReleaseReservedExternal(MotionSharedTextureSlot slot) noexcept {
		auto& state = *m_Implementation;
		if (!state.HasRecord(slot) || state.Records[slot.Index].Texture.External == nullptr) {
			std::terminate();
		}
		state.Release(slot);
	}

	bool MotionSharedTextures::CanClose() const noexcept {
		return m_Implementation->Reservations.empty();
	}

	void MotionSharedTextures::Close() noexcept {
		m_Implementation->Close();
	}

} // namespace sgre::scene
