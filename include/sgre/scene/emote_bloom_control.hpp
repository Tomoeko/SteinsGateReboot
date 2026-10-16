#pragma once

#include "sgre/scene/emote_bloom_definition.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace sgre::scene {

	class EmoteVariableControls;
	class MotionPlayerCommands;

	class EmoteBloomControl {
	public:
		// Actual primary Commands/metadata/variables enclose this UID owner and every physical occurrence.
		EmoteBloomControl(const MotionPlayerCommands& actual_primary, EmoteBloomControlDefinition definition, EmoteVariableControls* actual_variables = nullptr);
		~EmoteBloomControl() noexcept;
		EmoteBloomControl(const EmoteBloomControl&) = delete;
		EmoteBloomControl& operator=(const EmoteBloomControl&) = delete;
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteBloomControl& source) const;
		void AssignState(const EmoteBloomControl& source);
		// Native Common skip marks retained occurrences without advancing or rebuilding them.
		void RequireCanSkip() const;
		void Skip();
		void RequireReady() const;
		// Native2f1e90 runs after actual plain Progress/global/collider phases, never a second Motion update.
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
