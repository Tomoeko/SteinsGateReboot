#pragma once

#include "sgre/scene/emote_timeline_scheduler.hpp"
#include "sgre/scene/emote_velvety_owner.hpp"
#include "sgre/scene/emote_wind.hpp"
#include "sgre/scene/motion_player_update.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace sgre::scene {
	class MotionManagerRuntime;
	class MotionRootScene;
	class MotionScenePlayer;
	class EmoteVariableControls;

	class EmoteScenePlayer {
	public:
		// Native2b85b0 owns a distinct plain player, registered metadata/control graph and Velvety driver.
		// Runtime/catalog/registered asset and real entropy/listener services enclose this inner owner.
		// SQEmote task attachment, cyclic extension transitions and stereo replicas remain separate producers.
		EmoteScenePlayer(MotionManagerRuntime& actual_runtime, MotionProjectId actual_project, std::function<std::uint32_t()> read_entropy_word, std::function<void(const MotionPlayerEvent&)> actual_listener, EmoteWindRandom* actual_wind_random = nullptr);
		~EmoteScenePlayer() noexcept;
		EmoteScenePlayer(const EmoteScenePlayer&) = delete;
		EmoteScenePlayer& operator=(const EmoteScenePlayer&) = delete;
		void Update();
		// This explicit native delta entry is also used by the containing task; it never advances the shared clock.
		void Progress(float elapsed_ticks);
		void Draw();
		// Native2d0a00/2c02e0 toggles the real contained root; containing MEmote also owns task draw-pause.
		void SetVisible(bool visible);
		void SetCoordinate(const std::array<float, 2>& coordinate, float duration = 0, float easing = 0);
		// Native getLeft/getTop read the retained transition before plain publication.
		[[nodiscard]] std::array<float, 2> Coordinate() const;
		void SetScale(float scale, float duration = 0, float easing = 0);
		// Native getScale reads the retained component before delayed root zoom publication.
		[[nodiscard]] float Scale() const;
		void SetRotation(float radians, float duration = 0, float easing = 0);
		// Native getRot reads the published plain root, after radians-to-degrees publication.
		[[nodiscard]] float Rotation() const;
		// Native plain+490 is consumed by the existing ultimate-player mesh subdivision policy.
		void SetMeshDivisionRatio(float ratio);
		[[nodiscard]] float MeshDivisionRatio() const;
		// Legacy controller families remain rejected at construction; their empty-vector scalar state is native.
		void SetBustScale(float scale);
		[[nodiscard]] float BustScale() const;
		void SetHairScale(float scale);
		[[nodiscard]] float HairScale() const;
		void SetPartsScale(float scale);
		[[nodiscard]] float PartsScale() const;
		void StartWind(EmoteWindParameters parameters);
		void StopWind();
		[[nodiscard]] EmoteWindSnapshot WindState() const;
		void SetColor(std::uint32_t color, float duration = 0, float easing = 0);
		[[nodiscard]] std::uint32_t Color() const;
		// Native2bdd90 clears each actual plain tree without retiring the enclosing task or controls.
		void Clear();
		// Native2BC870 transfers supported owned controls into this existing player; registrations and schema remain destination-owned.
		[[nodiscard]] bool Animating() const;
		void AssignState(const EmoteScenePlayer& source);
		[[nodiscard]] float ReadVariable(std::string_view name) const;
		void SetVariable(std::string_view name, float value, float duration = 0, float easing = 0);
		void SetBlinkEnabled(bool enabled);
		void PlayTimeline(std::string_view name, std::uint32_t flags = 0);
		void Pass();
		void Skip();
		void StopTimeline(std::string_view name = {});
		void SetTimelineBlendRatio(std::string_view name, float ratio, float duration = 0, float easing = 0);
		[[nodiscard]] float TimelineBlendRatio(std::string_view name) const;
		void FadeInTimeline(std::string_view name, float duration = 0, float easing = 0);
		void FadeOutTimeline(std::string_view name, float duration = 0, float easing = 0);
		[[nodiscard]] bool TimelinePlaying(std::string_view name = {}) const;
		[[nodiscard]] const MotionPlayerCommands& Commands() const;
		[[nodiscard]] const MotionRootScene& Root() const;
		[[nodiscard]] const MotionScenePlayer& PlainPlayer() const;
		[[nodiscard]] const EmoteVariableControls& Variables() const;
		[[nodiscard]] EmoteVelvetyOwnerCounts PhysicalCounts() const;
		// A value snapshot of the last completed actual Common conversion, with the same live/idle guards.
		[[nodiscard]] resource::MotionMeshMatrix PhysicsToWorld() const;
		[[nodiscard]] float MetadataScale() const;
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
