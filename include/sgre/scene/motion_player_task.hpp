#pragma once

#include "sgre/scene/motion_alternate_textures.hpp"
#include "sgre/scene/motion_layer_tasks.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_variable_animations.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {

	class MotionScenePlayer;
	struct MotionSceneLayerReference;
	struct MotionLayerOverrides;

	enum class MotionTaskInsertion {
		Append,
		Prepend
	};

	class MotionPlayerTask {
	public:
		// The Second group exclusively owns the concrete payload through its owning callbacks; this is a weak task view.
		// Runtime/listener service outlive the registration; Layer tasks outlive every weak task view. No VM/self/resource root is added.
		// Async mode must be supplied from actual application state; true remains unsupported, never silently downgraded.
		[[nodiscard]] static MotionPlayerTask Attach(MotionManagerRuntime& runtime, MotionLayerTasks& tasks, bool asynchronous, std::string_view character, std::function<void(const MotionPlayerEvent&)> listener, MotionTaskInsertion insertion = MotionTaskInsertion::Append);
		// Copies actual selected state/geometry into independent destination owners without Play, Seek or a manager random sample.
		[[nodiscard]] static MotionPlayerTask AttachClone(MotionManagerRuntime& runtime, MotionLayerTasks& tasks, const MotionPlayerTask& source, bool asynchronous, std::function<void(const MotionPlayerEvent&)> listener, MotionTaskInsertion insertion = MotionTaskInsertion::Append);
		[[nodiscard]] bool IsLive() const noexcept;
		[[nodiscard]] ParallelTaskHandle Handle() const noexcept;
		// Views expire when the concrete task retires; callers keep the actual task group stable while borrowing them.
		[[nodiscard]] const MotionPlayerCommands& Commands() const;
		[[nodiscard]] const MotionRootScene& Root() const;
		[[nodiscard]] const MotionPlayerRenderControls& RenderControls() const;
		[[nodiscard]] const MotionPlayerRandom& Random() const;
		[[nodiscard]] const MotionSelectedSpritePlayer& Player() const;
		// Actual configured Layer runtimes use this complete selected/nested scene owner; legacy explicit runtimes return null.
		[[nodiscard]] const MotionScenePlayer* GenericPlayer() const;
		[[nodiscard]] bool AllPlaying(std::optional<MotionChildBorrow> actual_child = {}) const;
		void SetCharacter(std::string_view character, MotionCharacterMode mode = MotionCharacterMode::Ordinary);
		void Play(std::string_view motion, MotionPlayFlags flags = MotionPlayFlags::None);
		void Stop();
		void StopChild(const MotionChildBorrow& child);
		void SkipToSync();
		void SkipChildToSync(const MotionChildBorrow& child);
		void ReleaseSyncWait();
		void ReleaseChildSyncWait(const MotionChildBorrow& child);
		void Clear();
		void Progress(float elapsed_ticks);
		// Native setter acquisition forces dirty; ordinary writes dirty only when a field changes.
		void SetLayerOverrides(const MotionSceneLayerReference& reference, const MotionLayerOverrides& overrides, bool force_dirty = false);
		void SetTickCount(float ticks);
		void SetTickLabel(std::string_view label);
		void SetSpeed(float speed);
		void SetVariable(std::string_view identifier, float value, std::uint32_t control_flags = 0);
		void AnimateVariable(std::string_view identifier, float value, std::int32_t duration, std::int32_t curve = 0);
		// The containing weak task fences the actual child token without replacing it with the root player.
		[[nodiscard]] const MotionPlayerCommands& ChildCommands(const MotionChildBorrow& child) const;
		[[nodiscard]] const MotionRootScene& ChildRoot(const MotionChildBorrow& child) const;
		void SetChildVariable(const MotionChildBorrow& child, std::string_view identifier, float value, std::uint32_t control_flags = 0);
		void AnimateChildVariable(const MotionChildBorrow& child, std::optional<MotionVariableAnimations>& actual_wrapper_animations, std::string_view identifier, float value, std::int32_t duration, std::int32_t curve = 0);
		[[nodiscard]] float GetVariable(std::string_view identifier) const;
		[[nodiscard]] std::vector<std::string> GetVariableKeys() const;
		void SetVisible(bool visible);
		void SetChildVisible(const MotionChildBorrow& child, bool visible);
		void SetCoordinate(resource::MotionPoint coordinate);
		void SetChildCoordinate(const MotionChildBorrow& child, resource::MotionPoint coordinate);
		void SetCoordinate3(const std::array<float, 3>& coordinate);
		void SetLeft(float left);
		void SetTop(float top);
		void SetDepth(float depth);
		void SetZoom(float horizontal, float vertical);
		void SetChildZoom(const MotionChildBorrow& child, float horizontal, float vertical);
		void SetOpacity(std::uint32_t opacity);
		void SetChildOpacity(const MotionChildBorrow& child, std::uint32_t opacity);
		void SetAngleDegrees(float degrees);
		void SetAngleRadians(float radians);
		void SetChildAngleDegrees(const MotionChildBorrow& child, float degrees);
		void SetChildAngleRadians(const MotionChildBorrow& child, float radians);
		void SetPriority(float priority);
		void SetRenderControls(const MotionPlayerRenderControls& controls);
		// The actual task owns UID listeners through selected-tree retirement; caller roots each native descriptor.
		void RegisterAlternateTexture(std::string_view uid, const NativeTextureReference& texture, const MotionBounds& rectangle, resource::MotionPoint origin);
		[[nodiscard]] bool UnregisterAlternateTexture(std::string_view uid);
		void ClearAlternateTextures();
		// The native task +10 phase is exposed separately; no encompassing loop placement is fabricated.
		// Authored lifetime guard rejects manual PostUpdate for status2 before the containing group retires it.
		void PostUpdate();
		void RequestRetire();

	private:
		class Implementation;
		MotionPlayerTask(std::weak_ptr<Implementation> implementation, ParallelTaskSet& tasks, ParallelTaskHandle handle);
		[[nodiscard]] std::shared_ptr<Implementation> Require() const;
		std::weak_ptr<Implementation> m_Implementation;
		ParallelTaskSet* m_Tasks = nullptr;
		ParallelTaskHandle m_Handle;
	};

} // namespace sgre::scene
