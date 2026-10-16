#pragma once

#include "sgre/scene/emote_string_mesh_output.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {
	class EmoteCollisionOwner;
	class EmoteVariableControls;
	class MotionPlayerCommands;

	struct EmoteStringCollisionReference {
		std::string Uid;
		std::uint32_t IgnoreMask = 0;
	};

	struct EmoteStringControlDefinition {
		EmoteStringDefinition Physics;
		EmoteStringMeshOutputDefinition Output;
		float PixelsPerMeter = 1;
		std::uint32_t PredictionDegree = 0;
		bool InheritShape = true;
		bool ReferenceParentMeshPosture = false;
		std::vector<std::string> SpeedVariables;
		std::vector<EmoteStringCollisionReference> CollisionReferences;
		// Resolved collider and emission producers are separate native owners; these are never silently skipped.
		bool RequiresColliders = false;
		bool RequiresEmissions = false;
	};

	[[nodiscard]] std::vector<EmoteStringControlDefinition> ReadEmoteStringControlDefinitions(const resource::PsbDocument& document);

	class EmoteStringControl {
	public:
		// Actual primary plain Commands, optional real variable controller and selected graph enclose this owner.
		// Subscriptions observe the same native UID occurrence registry shared by its real nested child players.
		EmoteStringControl(const MotionPlayerCommands& actual_primary, EmoteStringControlDefinition definition, EmoteVariableControls* actual_variables = nullptr, EmoteCollisionOwner* actual_collisions = nullptr);
		~EmoteStringControl() noexcept;
		EmoteStringControl(const EmoteStringControl&) = delete;
		EmoteStringControl& operator=(const EmoteStringControl&) = delete;
		// Containing MEmote calls this once after actual plain Progress; no second Motion update is performed here.
		// Global frame and parent cache are the real preceding driver/PartsTree inputs, not fabricated clock or poses.
		void Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent = nullptr);
		void RequireReady() const;
		// Complete actual UID transitions outside their noexcept notification, before collider-pose preparation.
		void FinishLayerChanges();
		void Close();
		[[nodiscard]] std::size_t InstanceCount() const noexcept;
		[[nodiscard]] std::size_t LastUpdatedCount() const noexcept;
		[[nodiscard]] std::size_t LastMeshWriteCount() const noexcept;
		[[nodiscard]] EmoteVelvetyPartPosture PartPosture() const;
		[[nodiscard]] std::string_view Uid() const noexcept;
		[[nodiscard]] bool UsesCommands(const MotionPlayerCommands& actual_primary) const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
