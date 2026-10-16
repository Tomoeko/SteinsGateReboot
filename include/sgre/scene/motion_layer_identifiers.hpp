#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace sgre::scene {

	class MotionLayerIdentifierPool;

	class MotionLayerIdentifier {
	public:
		[[nodiscard]] std::uint32_t Value() const noexcept;
		bool operator==(const MotionLayerIdentifier&) const = default;

	private:
		friend class MotionLayerIdentifierPool;
		const MotionLayerIdentifierPool* m_Owner = nullptr;
		std::uint32_t m_Value = 0;
		std::uint64_t m_Generation = 0;
	};

	class MotionLayerIdentifierPool {
	public:
		// One actual MMotionManager owns this pool and outlives every selected node using its identifiers.
		// Native numbering starts at1; zero is the root sentinel, not a selected-node allocation.
		explicit MotionLayerIdentifierPool(std::size_t maximum_identifiers = 65536);
		MotionLayerIdentifierPool(const MotionLayerIdentifierPool&) = delete;
		MotionLayerIdentifierPool& operator=(const MotionLayerIdentifierPool&) = delete;
		[[nodiscard]] MotionLayerIdentifier Acquire();
		// Actual selected-tree retirement returns an identifier before destroying its associated child owner.
		void Retire(MotionLayerIdentifier identifier);
		[[nodiscard]] bool Contains(MotionLayerIdentifier identifier) const;
		[[nodiscard]] std::size_t LiveCount() const;
		[[nodiscard]] std::size_t RetiredCount() const;
		[[nodiscard]] std::uint32_t NextIdentifier() const;

	private:
		struct Entry {
			std::uint64_t Generation = 0;
			bool Live = false;
		};
		[[nodiscard]] bool ContainsUnlocked(MotionLayerIdentifier identifier) const;
		mutable std::mutex m_Mutex;
		std::vector<Entry> m_Entries;
		std::vector<std::uint32_t> m_Retired;
		std::uint32_t m_Next = 1;
	};

} // namespace sgre::scene
