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

	struct EmoteSynchronizeDefinition {
		std::string Uid;
		std::string SourceUid;
		float PixelsPerMeter = 1;
		float Width = 1;
		float Height = 1;
		float OriginX = 0.5f;
		float OriginY = 0.5f;
		float Weight = 1;
		float HorizontalWeight = 1;
		float VerticalWeight = 1;
		std::vector<resource::MotionPoint> Fixes;
		std::uint32_t PredictionDegree = 0;
		bool HorizontalFlip = false;
		bool VerticalFlip = false;
		bool CorrectFix = false;
		bool CorrectScale = false;
		bool ReferenceParentMeshPosture = false;
	};

	[[nodiscard]] std::vector<EmoteSynchronizeDefinition> ReadEmoteSynchronizeDefinitions(const resource::PsbDocument& document);

	class EmoteSynchronizeMeshOutput {
	public:
		// Native317f20 entity storage; this owner retains source-independent output and fix preparation.
		explicit EmoteSynchronizeMeshOutput(EmoteSynchronizeDefinition definition);
		~EmoteSynchronizeMeshOutput() noexcept;
		EmoteSynchronizeMeshOutput(const EmoteSynchronizeMeshOutput&) = delete;
		EmoteSynchronizeMeshOutput& operator=(const EmoteSynchronizeMeshOutput&) = delete;
		// Native3191a0 receives the actual source mesh's retained physical origin.
		void SetSourceOrigin(resource::MotionPoint actual_source_origin);
		[[nodiscard]] resource::MotionPoint PixelOrigin() const;
		[[nodiscard]] resource::MotionPoint PhysicalOrigin() const;
		// Native3182b0 consumes an actual source lattice and completed posture. Borrow expires on Prepare/Clear.
		[[nodiscard]] const resource::MotionBezierLattice& Prepare(const resource::MotionBezierLattice& source, const EmoteVelvetyPosture& actual_posture, std::span<const resource::MotionPoint> world_fix_targets);
		void Clear();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

	class EmoteSynchronizeControl {
	public:
		// Actual primary Commands and the selected UID registry enclose this source/target owner.
		EmoteSynchronizeControl(const MotionPlayerCommands& actual_primary, EmoteSynchronizeDefinition definition);
		~EmoteSynchronizeControl() noexcept;
		EmoteSynchronizeControl(const EmoteSynchronizeControl&) = delete;
		EmoteSynchronizeControl& operator=(const EmoteSynchronizeControl&) = delete;
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteSynchronizeControl& source) const;
		void AssignState(const EmoteSynchronizeControl& source);
		// Native Common skip marks retained occurrences without advancing or rebuilding them.
		void RequireCanSkip() const;
		void Skip();
		void RequireReady() const;
		// Native2f3260 consumes source's actual current mesh and writes only pending target controls.
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
