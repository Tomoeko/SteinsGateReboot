#pragma once

#include <cstddef>
#include <memory>

namespace sgre::resource {
	struct MotionTimeline;
}

namespace sgre::scene {
	class MotionResourceManager;
	class MotionManagerRuntime;
	struct PreparedMotionTimeline;

	inline constexpr std::size_t kMaximumMotionPreparationOwners = 256;

	// Shared only by actual selected owners and queued raw-string events; this adds no resource registration or retain.
	class MotionPreparationLease {
	public:
		MotionPreparationLease() = default;
		[[nodiscard]] bool IsEmpty() const noexcept;
		[[nodiscard]] bool UsesRuntime(const MotionManagerRuntime& runtime) const noexcept;
		[[nodiscard]] bool SameOwner(const MotionPreparationLease& other) const noexcept;
		[[nodiscard]] bool Owns(const MotionResourceManager& manager, const resource::MotionTimeline& timeline, const PreparedMotionTimeline* prepared = nullptr) const noexcept;

	private:
		friend class MotionManagerRuntime;
		class Data;
		explicit MotionPreparationLease(std::shared_ptr<const Data> data);
		std::shared_ptr<const Data> m_Data;
	};
} // namespace sgre::scene
