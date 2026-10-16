#pragma once

#include "sgre/scene/emote_jelly_definition.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace sgre::scene {

	class EmoteVariableControls;
	class EmoteCollisionOwner;
	class MotionPlayerCommands;

	class EmoteJellyControl {
	public:
		// Actual primary Commands/metadata/variables enclose this UID owner and every physical occurrence.
		EmoteJellyControl(const MotionPlayerCommands& actual_primary, EmoteJellyControlDefinition definition, EmoteVariableControls* actual_variables = nullptr, EmoteCollisionOwner* actual_collisions = nullptr);
		~EmoteJellyControl() noexcept;
		EmoteJellyControl(const EmoteJellyControl&) = delete;
		EmoteJellyControl& operator=(const EmoteJellyControl&) = delete;
		void RequireReady() const;
		// Complete actual UID joins outside noexcept notifications, before the containing collider-pose phase.
		void FinishLayerChanges();
		// Native2f26e0 runs after actual plain Progress/global/collider phases, never a second Motion update.
		void Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent = nullptr);
		void Close();
		[[nodiscard]] std::string_view Uid() const noexcept;
		[[nodiscard]] bool UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept;
		[[nodiscard]] std::size_t InstanceCount() const noexcept;
		[[nodiscard]] std::size_t LastUpdatedCount() const noexcept;
		[[nodiscard]] std::size_t LastMeshWriteCount() const noexcept;
		[[nodiscard]] EmoteVelvetyPartPosture PartPosture() const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
