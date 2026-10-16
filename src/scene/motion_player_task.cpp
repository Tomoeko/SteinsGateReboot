#include "sgre/scene/motion_player_task.hpp"

#include "sgre/scene/motion_scene_player.hpp"
#include "sgre/scene/motion_variable_animations.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	struct MotionTaskControlScope {
		bool& Active;

		explicit MotionTaskControlScope(bool& active)
		    : Active(active) {
			if (active) {
				throw std::runtime_error("motion task controls cannot reenter actual installation");
			}

			active = true;
		}

		~MotionTaskControlScope() {
			Active = false;
		}
	};

	class MotionPlayerTask::Implementation {
	public:
		class RuntimeBorrow {
		public:
			explicit RuntimeBorrow(MotionManagerRuntime& runtime)
			    : Runtime(runtime) {
				Runtime.AcquirePlayer();
			}

			~RuntimeBorrow() noexcept {
				Runtime.ReleasePlayer();
			}

			MotionManagerRuntime& Runtime;
		};

		[[nodiscard]] std::function<void(const ResolvedMotionTimeline&, MotionPlayFlags)> MakeInstallationService() {
			return [this](const ResolvedMotionTimeline& selection, MotionPlayFlags) {
				if (ScenePlayer) {
					ScenePlayer->Rebuild(selection);
				} else {
					const auto prepared = Borrow.Runtime.Prepare(selection);
					Player->Rebuild(prepared.Resource, *selection.Timeline, prepared.Timeline, prepared.Sources, prepared.Owner);
					Sources = &prepared.Sources;
				}
			};
		}

		Implementation(MotionManagerRuntime& runtime, MotionLayerTasks& tasks, std::string_view character, std::function<void(const MotionPlayerEvent&)> listener)
		    : Borrow(runtime), Tasks(tasks), Random(runtime.NextUnitFloat()), Commands(runtime.Manager(), MakeInstallationService(), [this] {
			      if (!ScenePlayer) {
				      throw std::runtime_error("motion JOIN requires the actual generic selected scene owner");
			      }
			      ScenePlayer->CaptureJoin();
		      }) {
			if (runtime.HasSceneServices()) {
				ScenePlayer = std::make_unique<MotionScenePlayer>(runtime, Commands, Root, Render, std::move(listener), &Random);
				Alternates = std::make_unique<MotionAlternateTextures>(Commands, [this] {
					if (!ScenePlayer || !ScenePlayer->CanMutate()) {
						throw std::runtime_error("motion alternate texture mutation requires the actual unqueued generic selected scene");
					}
				});
				Commands.SetCharacter(character);
				return;
			}

			MotionSelectedSpriteServices services;
			services.ReadFrameDelta = [&runtime] {
				return runtime.ReadFrameDelta();
			};
			services.ReadDepthProjection = [&runtime] {
				return runtime.DepthProjection();
			};
			services.Construction.ReadManagerRandom = [&runtime](const MotionResourceManager& manager) {
				if (&manager != &runtime.Manager()) {
					throw std::runtime_error("motion selected constructor requires the actual enclosing manager");
				}

				return runtime.NextUnitFloat();
			};
			services.Textures.FindTexture = [&runtime](const ResolvedMotionIcon& binding) {
				return runtime.FindTexture(binding);
			};
			services.DeliverEvent = std::move(listener);
			services.ResourceBorrowsLive = [this] {
				return Borrow.Runtime.ResourceBorrowsLive() && (!Sources || Borrow.Runtime.RegistrationOrderMatches(*Sources));
			};
			Player = std::make_unique<MotionSelectedSpritePlayer>(Commands, Root, Render, runtime.Identifiers(), runtime.Context(), std::move(services));
			Commands.SetCharacter(character);
		}

		Implementation(MotionManagerRuntime& runtime, MotionLayerTasks& tasks, const Implementation& source, std::function<void(const MotionPlayerEvent&)> listener)
		    : Borrow(runtime), Tasks(tasks), Random(source.Random), Commands(runtime.Manager(), MakeInstallationService(), [this] {
			      if (!ScenePlayer) {
				      throw std::runtime_error("motion cloned JOIN requires its actual generic selected scene");
			      }
			      ScenePlayer->CaptureJoin();
		      }),
		      Root(source.Root), Render(source.Render) {
			source.RequireMutable();
			if (!runtime.HasSceneServices() || !source.ScenePlayer || !source.ScenePlayer->ResourcesLive() || (source.Alternates && source.Alternates->Count() != 0)) {
				throw std::runtime_error("motion clone requires actual generic scene ownership without unrecovered external texture wrapper copies");
			}
			Commands.CopyForClone(source.Commands);
			// Native504CF0 marks only the new parentless root pending; copied completed geometry stays intact.
			Root.m_Overrides.Dirty = true;
			ScenePlayer = std::make_unique<MotionScenePlayer>(runtime, Commands, Root, Render, std::move(listener), &Random, *source.ScenePlayer);
			Alternates = std::make_unique<MotionAlternateTextures>(Commands, [this] {
				if (!ScenePlayer || !ScenePlayer->CanMutate()) {
					throw std::runtime_error("motion cloned alternate texture mutation requires its actual unqueued scene");
				}
			});
		}

		RuntimeBorrow Borrow;
		MotionLayerTasks& Tasks;
		MotionPlayerRandom Random;
		MotionPlayerCommands Commands;
		MotionRootScene Root;
		MotionPlayerRenderControls Render;
		const PreparedMotionSourceRegistry* Sources = nullptr;
		std::unique_ptr<MotionSelectedSpritePlayer> Player;
		std::unique_ptr<MotionScenePlayer> ScenePlayer;
		std::unique_ptr<MotionAlternateTextures> Alternates;
		std::optional<MotionVariableAnimations> Animations;
		bool Controlling = false;

		void RequireMutable() const {
			if (Controlling || (!Player && !ScenePlayer) || (Player && !Player->CanMutate()) || (ScenePlayer && !ScenePlayer->CanMutate())) {
				throw std::runtime_error("motion task control requires a live idle player outside queued row borrows");
			}
		}

		bool CanRelease() const noexcept {
			return !Controlling && (!Player || Player->CanMutate()) && (!ScenePlayer || ScenePlayer->CanMutate());
		}

		void Release() {
			RequireMutable();
			if (Animations) {
				Animations->RequestRetire();
			}
			Alternates.reset();
			ScenePlayer.reset();
			Player.reset();
		}
	};

	MotionPlayerTask::MotionPlayerTask(std::weak_ptr<Implementation> implementation, ParallelTaskSet& tasks, ParallelTaskHandle handle)
	    : m_Implementation(std::move(implementation)), m_Tasks(&tasks), m_Handle(handle) {
	}

	MotionPlayerTask MotionPlayerTask::Attach(MotionManagerRuntime& runtime, MotionLayerTasks& tasks, bool asynchronous, std::string_view character, std::function<void(const MotionPlayerEvent&)> listener, MotionTaskInsertion insertion) {
		if (asynchronous || !listener || (insertion != MotionTaskInsertion::Append && insertion != MotionTaskInsertion::Prepend)) {
			throw std::runtime_error("motion new task requires actual synchronous mode, a real event listener and supported Second insertion");
		}

		runtime.RequireLayerTaskOwner(tasks);

		auto implementation = std::make_shared<Implementation>(runtime, tasks, character, std::move(listener));
		auto& second = tasks.Group(MotionTaskGroup::Second);
		ParallelTaskCallbacks callbacks;
		callbacks.Update = [implementation](ParallelTaskHandle) {
			if (implementation->ScenePlayer) {
				implementation->ScenePlayer->Update();
			} else {
				implementation->Player->Update();
			}
		};
		callbacks.Draw = [implementation](ParallelTaskHandle, std::uint32_t) {
			// Native ordinary player Draw ignores the group's forwarded argument and appends its actual row pointers.
			if (implementation->ScenePlayer) {
				implementation->ScenePlayer->Draw();
			} else {
				implementation->Player->Draw();
			}
		};
		callbacks.CanRelease = [implementation](ParallelTaskHandle) {
			return implementation->CanRelease();
		};
		callbacks.Release = [implementation](ParallelTaskHandle) {
			implementation->Release();
		};
		const auto handle = insertion == MotionTaskInsertion::Prepend ? second.Prepend(std::move(callbacks)) : second.Append(std::move(callbacks));

		return MotionPlayerTask(implementation, second, handle);
	}

	MotionPlayerTask MotionPlayerTask::AttachClone(MotionManagerRuntime& runtime, MotionLayerTasks& tasks, const MotionPlayerTask& source, bool asynchronous, std::function<void(const MotionPlayerEvent&)> listener, MotionTaskInsertion insertion) {
		if (asynchronous || !listener || (insertion != MotionTaskInsertion::Append && insertion != MotionTaskInsertion::Prepend)) {
			throw std::runtime_error("motion cloned task requires actual synchronous receiving, a new listener and supported Second insertion");
		}
		runtime.RequireLayerTaskOwner(tasks);
		const auto original = source.Require();
		original->RequireMutable();
		if (source.m_Tasks->State(source.m_Handle).Status == TaskStatus::Retire) {
			throw std::runtime_error("motion clone rejects an actual pending-retirement source task");
		}
		auto implementation = std::make_shared<Implementation>(runtime, tasks, *original, std::move(listener));
		auto& second = tasks.Group(MotionTaskGroup::Second);
		ParallelTaskCallbacks callbacks;
		callbacks.Update = [implementation](ParallelTaskHandle) {
			implementation->ScenePlayer->Update();
		};
		callbacks.Draw = [implementation](ParallelTaskHandle, std::uint32_t) {
			implementation->ScenePlayer->Draw();
		};
		callbacks.CanRelease = [implementation](ParallelTaskHandle) {
			return implementation->CanRelease();
		};
		callbacks.Release = [implementation](ParallelTaskHandle) {
			implementation->Release();
		};
		const auto handle = insertion == MotionTaskInsertion::Prepend ? second.Prepend(std::move(callbacks)) : second.Append(std::move(callbacks));

		return MotionPlayerTask(implementation, second, handle);
	}

	std::shared_ptr<MotionPlayerTask::Implementation> MotionPlayerTask::Require() const {
		auto implementation = m_Implementation.lock();
		if (!implementation || (!implementation->Player && !implementation->ScenePlayer) || !m_Tasks->Contains(m_Handle)) {
			throw std::runtime_error("motion task view has expired after concrete retirement");
		}

		return implementation;
	}

	bool MotionPlayerTask::IsLive() const noexcept {
		const auto owner = m_Implementation.lock();

		return owner && (owner->Player || owner->ScenePlayer) && m_Tasks->Contains(m_Handle);
	}

	ParallelTaskHandle MotionPlayerTask::Handle() const noexcept {
		return m_Handle;
	}

	const MotionPlayerCommands& MotionPlayerTask::Commands() const {
		return Require()->Commands;
	}

	const MotionRootScene& MotionPlayerTask::Root() const {
		return Require()->Root;
	}

	const MotionPlayerRenderControls& MotionPlayerTask::RenderControls() const {
		return Require()->Render;
	}

	const MotionPlayerRandom& MotionPlayerTask::Random() const {
		return Require()->Random;
	}

	const MotionSelectedSpritePlayer& MotionPlayerTask::Player() const {
		const auto owner = Require();
		if (!owner->Player) {
			throw std::runtime_error("generic motion scene tasks do not expose the legacy single-sprite inspection view");
		}
		return *owner->Player;
	}

	const MotionScenePlayer* MotionPlayerTask::GenericPlayer() const {
		return Require()->ScenePlayer.get();
	}

	void MotionPlayerTask::SetCharacter(std::string_view character, MotionCharacterMode mode) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Commands.SetCharacter(character, mode);
	}

	void MotionPlayerTask::Play(std::string_view motion, MotionPlayFlags flags) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		if ((static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(MotionPlayFlags::Delay1 | MotionPlayFlags::Delay2)) != 0) {
			throw std::runtime_error("motion containing delayed selection requires the real update-entry installation phase");
		}

		owner->Commands.Play(motion, flags);
	}

	void MotionPlayerTask::Stop() {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Commands.Stop();
	}

	void MotionPlayerTask::SkipToSync() {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Commands.SkipToSync();
	}

	void MotionPlayerTask::SkipChildToSync(const MotionChildBorrow& child) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		static_cast<void>(ChildCommands(child));
		child.Require().Commands().SkipToSync();
	}

	void MotionPlayerTask::ReleaseSyncWait() {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Commands.ReleaseSyncWait();
	}

	void MotionPlayerTask::ReleaseChildSyncWait(const MotionChildBorrow& child) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		static_cast<void>(ChildCommands(child));
		child.Require().Commands().ReleaseSyncWait();
	}

	void MotionPlayerTask::Clear() {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		if (owner->ScenePlayer) {
			owner->ScenePlayer->Clear();
		} else {
			owner->Player->Clear();
		}
	}

	void MotionPlayerTask::Progress(float elapsed_ticks) {
		const auto owner = Require();
		owner->RequireMutable();
		if (m_Tasks->State(m_Handle).Status == TaskStatus::Retire) {
			throw std::runtime_error("motion Progress rejects pending retirement before wrapper listener delivery");
		}
		if (owner->ScenePlayer) {
			owner->ScenePlayer->Progress(elapsed_ticks);
		} else {
			owner->Player->Progress(elapsed_ticks);
		}
	}

	void MotionPlayerTask::SetTickCount(float ticks) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Commands.SetTickCount(ticks);
	}

	void MotionPlayerTask::SetTickLabel(std::string_view label) {
		const auto owner = Require();
		owner->RequireMutable();
		if (owner->ScenePlayer ? !owner->ScenePlayer->ResourcesLive() : !owner->Borrow.Runtime.ResourceBorrowsLive()) {
			throw std::runtime_error("motion tick label requires its actual live selected resource borrows");
		}
		const MotionTaskControlScope control(owner->Controlling);
		owner->Commands.SetTickLabel(label);
	}

	void MotionPlayerTask::SetSpeed(float speed) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		if (!std::isfinite(speed)) {
			throw std::runtime_error("motion task speed requires the measured finite domain");
		}

		owner->Commands.SetSpeed(speed);
	}

	void MotionPlayerTask::SetVariable(std::string_view identifier, float value, std::uint32_t control_flags) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Commands.ParameterValues().WriteLocal(identifier, value, control_flags);
	}

	void MotionPlayerTask::AnimateVariable(std::string_view identifier, float value, std::int32_t duration, std::int32_t curve) {
		const auto owner = Require();
		owner->RequireMutable();
		if (m_Tasks->State(m_Handle).Status == TaskStatus::Retire) {
			throw std::runtime_error("motion animation rejects a pending retired containing player");
		}
		const MotionTaskControlScope control(owner->Controlling);
		identifier = identifier.substr(0, identifier.find('\0'));
		const auto initial = owner->Commands.ParameterValues().ReadLocal(identifier);
		MotionVariableAnimations::Validate(identifier, initial, value, duration, curve);
		if (!owner->Animations || !owner->Animations->IsLive()) {
			owner->Animations = MotionVariableAnimations::Attach(*this, owner->Borrow.Runtime, owner->Tasks.Group(MotionTaskGroup::CommonFirst));
		}
		owner->Animations->Append(identifier, initial, value, duration, curve);
	}

	const MotionPlayerCommands& MotionPlayerTask::ChildCommands(const MotionChildBorrow& child) const {
		const auto owner = Require();
		auto& target = child.Require();
		if (&target.UltimateParentCommands() != &owner->Commands) {
			throw std::runtime_error("borrowed Motion child belongs to a different containing player");
		}

		return target.Commands();
	}

	const MotionRootScene& MotionPlayerTask::ChildRoot(const MotionChildBorrow& child) const {
		static_cast<void>(ChildCommands(child));

		return child.Require().Root();
	}

	void MotionPlayerTask::SetChildVariable(const MotionChildBorrow& child, std::string_view identifier, float value, std::uint32_t control_flags) {
		const auto owner = Require();
		owner->RequireMutable();
		static_cast<void>(ChildCommands(child));
		const MotionTaskControlScope control(owner->Controlling);
		child.Require().Commands().ParameterValues().WriteLocal(identifier, value, control_flags);
	}

	void MotionPlayerTask::AnimateChildVariable(const MotionChildBorrow& child, std::optional<MotionVariableAnimations>& actual_wrapper_animations, std::string_view identifier, float value, std::int32_t duration, std::int32_t curve) {
		const auto owner = Require();
		owner->RequireMutable();
		const auto& commands = ChildCommands(child);
		if (m_Tasks->State(m_Handle).Status == TaskStatus::Retire) {
			throw std::runtime_error("child Motion animation rejects a pending retired containing player");
		}
		const MotionTaskControlScope control(owner->Controlling);
		identifier = identifier.substr(0, identifier.find('\0'));
		const auto initial = commands.ParameterValues().ReadLocal(identifier);
		MotionVariableAnimations::Validate(identifier, initial, value, duration, curve);
		if (!actual_wrapper_animations || !actual_wrapper_animations->IsLive()) {
			actual_wrapper_animations = MotionVariableAnimations::Attach(*this, owner->Borrow.Runtime, owner->Tasks.Group(MotionTaskGroup::CommonFirst), child);
		}
		actual_wrapper_animations->Append(identifier, initial, value, duration, curve, child);
	}

	float MotionPlayerTask::GetVariable(std::string_view identifier) const {
		return Require()->Commands.ParameterValues().ReadLocal(identifier);
	}

	std::vector<std::string> MotionPlayerTask::GetVariableKeys() const {
		return Require()->Commands.ParameterValues().RegisteredKeys();
	}

	void MotionPlayerTask::SetVisible(bool visible) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetVisible(visible);
	}

	void MotionPlayerTask::SetCoordinate(resource::MotionPoint coordinate) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetCoordinate(coordinate);
	}

	void MotionPlayerTask::SetChildCoordinate(const MotionChildBorrow& child, resource::MotionPoint coordinate) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		static_cast<void>(ChildCommands(child));
		child.Require().Root().SetCoordinate(coordinate);
	}

	void MotionPlayerTask::SetCoordinate3(const std::array<float, 3>& coordinate) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetCoordinate3(coordinate);
	}

	void MotionPlayerTask::SetLeft(float left) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetLeft(left);
	}

	void MotionPlayerTask::SetTop(float top) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetTop(top);
	}

	void MotionPlayerTask::SetDepth(float depth) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetDepth(depth);
	}

	void MotionPlayerTask::SetZoom(float horizontal, float vertical) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetZoom(horizontal, vertical);
	}

	void MotionPlayerTask::SetChildZoom(const MotionChildBorrow& child, float horizontal, float vertical) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		static_cast<void>(ChildCommands(child));
		child.Require().Root().SetZoom(horizontal, vertical);
	}

	void MotionPlayerTask::SetAngleDegrees(float degrees) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetAngleDegrees(degrees);
	}

	void MotionPlayerTask::SetAngleRadians(float radians) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetAngleRadians(radians);
	}

	void MotionPlayerTask::SetChildAngleDegrees(const MotionChildBorrow& child, float degrees) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		static_cast<void>(ChildCommands(child));
		child.Require().Root().SetAngleDegrees(degrees);
	}

	void MotionPlayerTask::SetChildAngleRadians(const MotionChildBorrow& child, float radians) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		static_cast<void>(ChildCommands(child));
		child.Require().Root().SetAngleRadians(radians);
	}

	void MotionPlayerTask::SetOpacity(std::uint32_t opacity) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		owner->Root.SetOpacity(opacity);
	}

	void MotionPlayerTask::SetChildOpacity(const MotionChildBorrow& child, std::uint32_t opacity) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		static_cast<void>(ChildCommands(child));
		child.Require().Root().SetOpacity(opacity);
	}

	void MotionPlayerTask::SetPriority(float priority) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		if (!std::isfinite(priority)) {
			throw std::runtime_error("motion task priority requires the measured finite domain");
		}

		owner->Root.SetPriority(priority);
	}

	void MotionPlayerTask::SetRenderControls(const MotionPlayerRenderControls& controls) {
		const auto owner = Require();
		owner->RequireMutable();
		const MotionTaskControlScope control(owner->Controlling);
		if (controls.EmissiveColor != 0 || controls.AmbientFactor != 1) {
			throw std::runtime_error("motion containing auxiliary lighting requires its actual render context producer");
		}

		owner->Render = controls;
	}

	void MotionPlayerTask::RegisterAlternateTexture(std::string_view uid, const NativeTextureReference& texture, const MotionBounds& rectangle, resource::MotionPoint origin) {
		const auto owner = Require();
		owner->RequireMutable();
		if (!owner->Alternates) {
			throw std::runtime_error("motion alternate texture requires its actual generic selected-scene source consumer");
		}
		const MotionTaskControlScope control(owner->Controlling);
		owner->Alternates->Register(uid, texture, rectangle, origin);
	}

	bool MotionPlayerTask::UnregisterAlternateTexture(std::string_view uid) {
		const auto owner = Require();
		owner->RequireMutable();
		if (!owner->Alternates) {
			return false;
		}
		const MotionTaskControlScope control(owner->Controlling);

		return owner->Alternates->Unregister(uid);
	}

	void MotionPlayerTask::ClearAlternateTextures() {
		const auto owner = Require();
		owner->RequireMutable();
		if (owner->Alternates) {
			const MotionTaskControlScope control(owner->Controlling);
			owner->Alternates->Clear();
		}
	}

	void MotionPlayerTask::PostUpdate() {
		const auto owner = Require();
		// Authored boundary: status2 wrappers can be destroyed before the group retires their actual task.
		if (m_Tasks->State(m_Handle).Status == TaskStatus::Retire) {
			throw std::runtime_error("motion PostUpdate rejects pending retirement before wrapper listener delivery");
		}

		if (owner->ScenePlayer) {
			owner->ScenePlayer->PostUpdate();
		} else {
			owner->Player->PostUpdate();
		}
	}

	void MotionPlayerTask::RequestRetire() {
		const auto owner = Require();
		m_Tasks->SetStatus(m_Handle, TaskStatus::Retire);
		if (owner->Animations) {
			owner->Animations->RequestRetire();
		}
	}

} // namespace sgre::scene
