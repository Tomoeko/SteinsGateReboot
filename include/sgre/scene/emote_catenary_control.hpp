#pragma once

#include "sgre/scene/emote_catenary_definition.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace sgre::scene {

	class EmoteVariableControls;
	class MotionPlayerCommands;

	class EmoteCatenaryControl {
	public:
		// Actual primary Commands/metadata/variables enclose this UID owner and every physical occurrence.
		EmoteCatenaryControl(const MotionPlayerCommands& actual_primary, EmoteCatenaryControlDefinition definition, EmoteVariableControls* actual_variables = nullptr);
		~EmoteCatenaryControl() noexcept;
		EmoteCatenaryControl(const EmoteCatenaryControl&) = delete;
		EmoteCatenaryControl& operator=(const EmoteCatenaryControl&) = delete;
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteCatenaryControl& source) const;
		void AssignState(const EmoteCatenaryControl& source);
		// Native Common skip marks retained occurrences without advancing or rebuilding them.
		void RequireCanSkip() const;
		void Skip();
		void RequireReady() const;
		// Native2f22c0 runs after actual plain Progress/global/collider phases, never a second Motion update.
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
