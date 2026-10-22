#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace sgre::scene {
	class MotionLayerComposition;
	struct MotionNumericStorageState;

	struct MotionLayerNumbers {
		std::array<float, 3> Coordinate{};
		float Angle = 0;
		float ZoomX = 1;
		float ZoomY = 1;
		float ShearX = 0;
		float ShearY = 0;
		std::uint32_t Opacity = 255;
		bool FlipX = false;
		bool FlipY = false;
		bool Visible = true;
		bool BranchVisible = true;
	};

	class MotionNumericBorrow {
	public:
		// Numeric cells follow the physical player's allocation, independently of selected-tree identities.
		[[nodiscard]] MotionLayerNumbers Read() const;

	private:
		friend class MotionNumericStorage;
		friend class MotionSceneEvaluation;
		std::weak_ptr<MotionNumericStorageState> m_Storage;
		std::uint64_t m_Allocation = 0;
		std::uint32_t m_Ordinal = 0;
	};

	class MotionNumericStorage {
	public:
		MotionNumericStorage();
		~MotionNumericStorage();
		MotionNumericStorage(const MotionNumericStorage&) = delete;
		MotionNumericStorage& operator=(const MotionNumericStorage&) = delete;
		[[nodiscard]] MotionNumericBorrow Borrow(std::uint32_t native_ordinal) const;

	private:
		friend class MotionLayerComposition;
		friend class MotionSceneEvaluation;
		friend class MotionPlayerCommands;
		void Install(MotionLayerComposition& composition);
		void Retire(MotionLayerComposition& composition) noexcept;
		void CopyForClone(const MotionNumericStorage& source);
		[[nodiscard]] bool Owns(const MotionNumericBorrow& reference) const;
		std::shared_ptr<MotionNumericStorageState> m_State;
	};

} // namespace sgre::scene
