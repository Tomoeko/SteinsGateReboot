#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace sgre::scene {
	class EmoteStringPhysics;
	class EmoteJellyPhysics;
	class MotionPlayerCommands;
	class EmoteCollisionOwner;

	class EmoteCollisionGroup {
		class Implementation;

	public:
		explicit EmoteCollisionGroup(std::unique_ptr<Implementation> implementation);
		~EmoteCollisionGroup() noexcept;
		EmoteCollisionGroup(const EmoteCollisionGroup&) = delete;
		EmoteCollisionGroup& operator=(const EmoteCollisionGroup&) = delete;
		void Attach(EmoteStringPhysics& actual_physics, std::uint32_t tress_mask);
		void Attach(EmoteJellyPhysics& actual_physics, std::uint32_t tress_mask);
		[[nodiscard]] std::size_t ColliderCount() const noexcept;
		[[nodiscard]] bool Contacted() const;
		[[nodiscard]] std::uint64_t PreparedFrameSerial() const;

	private:
		friend class EmoteCollisionOwner;
		std::unique_ptr<Implementation> m_Implementation;
	};

	class EmoteCollisionOwner {
	public:
		// Actual metadata and primary Commands enclose this owner. All group leases retire before Close.
		EmoteCollisionOwner(const MotionPlayerCommands& actual_primary, const resource::PsbDocument& document);
		~EmoteCollisionOwner() noexcept;
		EmoteCollisionOwner(const EmoteCollisionOwner&) = delete;
		EmoteCollisionOwner& operator=(const EmoteCollisionOwner&) = delete;
		// Lazy collisionMap lookup follows actual UID instance creation, before the tail's collider-pose phase.
		[[nodiscard]] std::shared_ptr<EmoteCollisionGroup> Acquire(std::string_view uid);
		// Called after actual plain Progress and completed pending UID joins, before any physical family dispatch.
		void Prepare(const EmoteVelvetyInputFrame& actual_frame);
		void Close();
		[[nodiscard]] bool UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept;
		[[nodiscard]] std::uint64_t PreparedFrameSerial() const;
		[[nodiscard]] std::size_t GroupCount() const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
