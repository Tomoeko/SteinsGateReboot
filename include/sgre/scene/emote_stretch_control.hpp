#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_velvety_posture.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {
	class MotionPlayerCommands;

	struct EmoteStretchDefinition {
		std::string Uid;
		float PixelsPerMeter = 1;
		float Width = 1;
		float Height = 1;
		float OriginX = 0.5f;
		float OriginY = 0.5f;
		std::vector<resource::MotionPoint> Fixes;
		std::uint32_t PredictionDegree = 0;
		bool CorrectScale = false;
		bool ReferenceParentMeshPosture = false;
	};

	[[nodiscard]] std::vector<EmoteStretchDefinition> ReadEmoteStretchDefinitions(const resource::PsbDocument& document);

	class EmoteStretchMeshOutput {
	public:
		// Native3177a0 owns a real linear/linear lattice and its authored fix preparation.
		explicit EmoteStretchMeshOutput(EmoteStretchDefinition definition);
		~EmoteStretchMeshOutput() noexcept;
		EmoteStretchMeshOutput(const EmoteStretchMeshOutput&) = delete;
		EmoteStretchMeshOutput& operator=(const EmoteStretchMeshOutput&) = delete;
		// Native317920 returns normalized degree11 controls; the next call replaces this borrow.
		[[nodiscard]] const resource::MotionBezierLattice& Prepare(const EmoteVelvetyPosture& actual_posture, std::span<const resource::MotionPoint> actual_world_targets);

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

	class EmoteStretchControl {
	public:
		// The actual primary Commands and its UID registry outlive this occurrence owner.
		EmoteStretchControl(const MotionPlayerCommands& actual_primary, EmoteStretchDefinition definition);
		~EmoteStretchControl() noexcept;
		EmoteStretchControl(const EmoteStretchControl&) = delete;
		EmoteStretchControl& operator=(const EmoteStretchControl&) = delete;
		void RequireReady() const;
		// Native2f2b00 writes pending meshes after the one actual plain Progress operation.
		void Update(float elapsed_ticks, const EmoteVelvetyInputFrame& actual_frame, const EmoteVelvetyPartPosture* actual_parent = nullptr);
		[[nodiscard]] std::string_view Uid() const;
		[[nodiscard]] bool UsesCommands(const MotionPlayerCommands& actual_commands) const noexcept;
		[[nodiscard]] std::size_t InstanceCount() const;
		[[nodiscard]] std::size_t LastUpdatedCount() const;
		[[nodiscard]] std::size_t LastMeshWriteCount() const;
		[[nodiscard]] EmoteVelvetyPartPosture PartPosture() const;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
