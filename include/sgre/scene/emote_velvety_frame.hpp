#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <thread>

namespace sgre::scene {
	class MotionPlayerCommands;
	class MotionRootScene;
	class MotionScenePlayer;

	struct EmoteVelvetyFrameDefinition {
		float PixelsPerMeter = 1;
		std::string ReferenceLayer = "move_LR";
		// Native2ddffd initializes actual driver+88 to1; zero disables speed limiting.
		float ReferenceSpeedLimit = 1;
	};

	struct EmoteVelvetyGlobalControls {
		// Values come from the containing MEmote transition/flags/mirror owners, not plain numeric snapshots.
		float Rotation = 0;
		std::uint32_t Flags = 0x201;
		bool Mirror = false;
	};

	[[nodiscard]] std::optional<EmoteVelvetyFrameDefinition> ReadEmoteVelvetyFrameDefinition(const resource::PsbDocument& document);

	class EmoteVelvetyFrameOwner {
	public:
		// These are the same actual plain owner's components; they outlive this driver.
		// View height is the genuine containing Emote+3c4 input used by2e6000, not an invented viewport default.
		EmoteVelvetyFrameOwner(const MotionRootScene& actual_root, const MotionPlayerCommands& actual_commands, const MotionScenePlayer& actual_plain, EmoteVelvetyFrameDefinition definition, float actual_view_height);
		[[nodiscard]] EmoteVelvetyInputFrame Prepare(float elapsed_ticks, const EmoteVelvetyGlobalControls& actual_controls);
		[[nodiscard]] const resource::MotionMeshMatrix& PhysicsToWorld() const;
		void RequireCanAssignHistory(const EmoteVelvetyFrameOwner& source) const;
		void AssignHistory(const EmoteVelvetyFrameOwner& source);
		// Native50FD90 retains physical numeric cells. Capture their value before the actual portable tree retires.
		void ClearPlain(MotionScenePlayer& actual_plain);

	private:
		const MotionRootScene& m_Root;
		const MotionPlayerCommands& m_Commands;
		const MotionScenePlayer& m_Plain;
		EmoteVelvetyFrameDefinition m_Definition;
		std::optional<MotionSceneLayerReference> m_Reference;
		std::optional<resource::MotionPoint> m_ClearedReference;
		const MotionSceneEvaluation* m_Scene = nullptr;
		std::uint64_t m_SelectionSerial = 0;
		std::uint64_t m_GraphGeneration = 0;
		resource::MotionPoint m_PrecedingReference;
		resource::MotionPoint m_AccumulatedReference;
		resource::MotionMeshMatrix m_Inverse;
		float m_DepthScale = 0;
		const std::thread::id m_Thread = std::this_thread::get_id();
		bool m_Lookup = false;
		bool m_HasReference = false;
		bool m_First = true;
		bool m_Prepared = false;
		bool m_Cleared = false;
		bool m_Valid = true;
	};
} // namespace sgre::scene
