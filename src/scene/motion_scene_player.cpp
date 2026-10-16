#include "sgre/scene/motion_scene_player.hpp"

#include "sgre/scene/motion_join_state.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"

#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {
	class MotionScenePlayer::Implementation {
	public:
		struct Selection {
			const resource::MotionTimeline* Timeline = nullptr;
			MotionPreparationLease PreparationOwner;
			std::unique_ptr<MotionTimelineParameters> RetainedParameters;
			std::unique_ptr<MotionSelectedLayerNodes> Nodes;
			std::unique_ptr<MotionSceneEvaluation> Scene;
			bool Cleared = false;
		};

		Implementation(MotionManagerRuntime& runtime, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, std::function<void(const MotionPlayerEvent&)> listener, MotionPlayerRandom* actual_random)
		    : Runtime(runtime), Commands(commands), Root(root), Render(render), Random(actual_random), Listener(std::move(listener)), UpdateOwner(commands, UpdateServices()) {
			if (&runtime.Manager() != &commands.Manager() || !runtime.HasSceneServices() || !Listener) {
				throw std::runtime_error("motion scene player requires actual matching manager/catalog/asset and listener owners");
			}
			UpdateOwner.m_HasParticlePhase = true;
		}

		MotionManagerRuntime& Runtime;
		MotionPlayerCommands& Commands;
		MotionRootScene& Root;
		const MotionPlayerRenderControls& Render;
		MotionPlayerRandom* Random = nullptr;
		std::function<void(const MotionPlayerEvent&)> Listener;
		MotionPlayerUpdate UpdateOwner;
		std::unique_ptr<Selection> Current;
		std::vector<MotionJoinedLayer> JoinedLayers;
		bool Operating = false;

		void RequireUnqueued() const {
			if (Current && Current->Scene && Runtime.SceneContext().IsBorrowing(*Current->Scene)) {
				throw std::runtime_error("motion scene mutation requires consumption of its actual manager draw publication");
			}
		}

		bool CanMutate() const noexcept {
			try {
				RequireUnqueued();
				return !Operating && (!Current || !Current->Scene || Current->Scene->CanClose());
			} catch (...) {
				return false;
			}
		}

		void RequireMutable() const {
			if (!CanMutate()) {
				throw std::runtime_error("motion scene player requires its idle owner outside queued scene borrows");
			}
		}

		void RequirePhase() const {
			// Clear retains the timeline and parameter cells after its selected resource is unregistered.
			// Only a live selected tree needs those source registrations during the root-only phase.
			if (Runtime.IsClosed() || (Current && !Current->Cleared && !Runtime.ResourceBorrowsLive()) || (Current ? Commands.CachedTimeline() != Current->Timeline : Commands.CachedTimeline() != nullptr)) {
				throw std::runtime_error("motion scene phase requires its actual installed live selection and registered resources");
			}
		}

		MotionPlayerUpdateServices UpdateServices() {
			MotionPlayerUpdateServices services;
			services.ReadFrameDelta = [this] {
				RequirePhase();
				return Runtime.ReadFrameDelta();
			};
			services.PrepareFrames = [this](auto traversal, const auto& frame) {
				if (Current && !Current->Cleared) {
					Current->Scene->PrepareFrames(traversal, frame);
				}
			};
			services.ComposeLayers = [this](const auto& frame) {
				if (Current && !Current->Cleared) {
					Current->Scene->Compose(frame);
				} else {
					Root.AdvanceVelocity(frame.Clock.DeltaTicks);
					Root.Compose(frame.Clock.TickAssigned);
					if (Current && Current->RetainedParameters) {
						Current->RetainedParameters->CompleteClearedFrame();
					}
				}
			};
			services.PublishDrawPackets = [this](const auto& frame) {
				if (Current && !Current->Cleared) {
					Current->Scene->Publish(frame);
				}
			};
			services.Draw = [this] {
				if (Current && !Current->Cleared) {
					Current->Scene->Draw();
				}
			};
			services.DeliverEvent = [this](const auto& event) {
				RequirePhase();
				Listener(event);
			};
			return services;
		}

		void CaptureJoin() {
			RequireMutable();
			RequirePhase();
			if (!Current || Current->Cleared) {
				JoinedLayers.clear();
				return;
			}
			JoinedLayers = Current->Scene->CaptureJoin();
		}

		void Rebuild(const ResolvedMotionTimeline& selection) {
			RequireMutable();
			if (!Commands.SelectedProject() || Commands.SelectedProject()->Timeline != selection.Timeline || Commands.CachedTimeline() != selection.Timeline || selection.Timeline == nullptr) {
				throw std::runtime_error("motion scene installation requires the real command selection before constructing its selected owner");
			}
			const auto prepared = Runtime.PrepareScene(selection);
			UpdateOwner.PrepareCurrentTimeline(*selection.Timeline, prepared.Owner);
			Operating = true;
			try {
				// Keep the concrete evaluator implementation alive around any owning child JOIN capsules.
				std::unique_ptr<MotionSceneEvaluation> evaluator;
				if (Current) {
					evaluator = std::move(Current->Scene);
					if (evaluator) {
						evaluator->RetireRootForRebuild();
					}
				}
				Current.reset();
				auto installed = std::make_unique<Selection>();
				installed->Timeline = selection.Timeline;
				installed->PreparationOwner = prepared.Owner;
				MotionChildConstructionServices construction;
				construction.ReadManagerRandom = [this](const auto& manager) {
					if (&manager != &Runtime.Manager()) {
						throw std::runtime_error("motion scene constructor sampled a foreign random/manager owner");
					}
					return Runtime.NextUnitFloat();
				};
				construction.ReadDocumentRoot = [this](auto project) -> const resource::PsbValue& {
					const auto& asset = Runtime.ReadRegisteredAsset(project);
					if (!asset.Document) {
						throw std::runtime_error("motion particle constructor requires the actual registered PSB document");
					}
					return asset.Document->Root;
				};
				MotionChildFrameSlotServices slots;
				slots.EnqueueAction = [this](auto layer, auto action) {
					UpdateOwner.EnqueueAction(layer, action);
				};
				installed->Nodes = std::make_unique<MotionSelectedLayerNodes>(Commands, Root, Render, *selection.Timeline, prepared.Timeline, Runtime.Identifiers(), construction, slots);
				MotionSceneEvaluationServices services;
				services.PlayerRandom = Random;
				services.ReadRegisteredAsset = [this](auto project) -> const LoadedMotionAsset& {
					return Runtime.ReadRegisteredAsset(project);
				};
				services.PrepareOwnedChildTimeline = [this](const auto& child) {
					return Runtime.PrepareScene(child);
				};
				services.RequireUnqueued = [this] {
					RequireUnqueued();
				};
				services.AppendDraw = [this](const auto& packets, auto rows) {
					if (!Current || !Current->Scene || &packets != &Current->Scene->Packets() || rows.data() != Current->Scene->GlobalRows().data()) {
						throw std::runtime_error("motion task Draw did not lend its actual published scene rows");
					}
					Runtime.SceneContext().Append(*Current->Scene);
				};
				services.DeliverEvent = [this](const auto& event) {
					Listener(event);
				};
				if (evaluator) {
					evaluator->RebuildRoot(*installed->Nodes, Commands, Root, Render);
					installed->Scene = std::move(evaluator);
				} else {
					installed->Scene = std::make_unique<MotionSceneEvaluation>(*installed->Nodes, Commands, Root, Render, Runtime.SceneAssets(), Runtime, UpdateOwner, std::move(services));
				}
				installed->Scene->RestoreJoin(std::move(JoinedLayers));
				Current = std::move(installed);
				Operating = false;
			} catch (...) {
				Operating = false;
				throw;
			}
		}

		void CopyForClone(const Implementation& source) {
			source.RequireMutable();
			source.RequirePhase();
			if (Current || !JoinedLayers.empty() || !source.JoinedLayers.empty() || &Commands == &source.Commands) {
				throw std::runtime_error("motion scene clone requires a fresh destination and an idle source without owning JOIN snapshots");
			}
			UpdateOwner.CopyForClone(source.UpdateOwner, Runtime);
			if (!source.Current) {
				return;
			}
			const auto& original = *source.Current;
			const auto prepared = Runtime.ClonePreparation(original.PreparationOwner);
			if (Commands.CachedTimeline() != original.Timeline || !Commands.SelectedProject() || Commands.SelectedProject()->Timeline != original.Timeline || !prepared.Owner.Owns(Runtime.Manager(), *original.Timeline, &prepared.Timeline)) {
				throw std::runtime_error("motion scene clone lost its actual selected immutable timeline/preparation");
			}
			auto copied = std::make_unique<Selection>();
			copied->Timeline = original.Timeline;
			copied->PreparationOwner = prepared.Owner;
			copied->Cleared = original.Cleared;
			if (original.Cleared) {
				if (!original.RetainedParameters || original.Nodes) {
					throw std::runtime_error("motion cleared clone requires its real retained parameter owner and retired selected tree");
				}
				copied->RetainedParameters = std::make_unique<MotionTimelineParameters>(Commands.ParameterValues(), *original.RetainedParameters);
				Current = std::move(copied);
				return;
			}
			if (!original.Nodes || !original.Scene) {
				throw std::runtime_error("motion scene clone requires the real current selected tree and phase owners");
			}
			MotionChildConstructionServices construction;
			construction.ReadManagerRandom = [this](const auto& manager) {
				if (&manager != &Runtime.Manager()) {
					throw std::runtime_error("motion clone child sampled a foreign manager");
				}
				return Runtime.NextUnitFloat();
			};
			construction.ReadDocumentRoot = [this](auto project) -> const resource::PsbValue& {
				const auto& asset = Runtime.ReadRegisteredAsset(project);
				if (!asset.Document) {
					throw std::runtime_error("motion cloned particle requires the actual registered PSB document");
				}
				return asset.Document->Root;
			};
			MotionChildFrameSlotServices slots;
			slots.EnqueueAction = [this](auto layer, auto action) {
				UpdateOwner.EnqueueAction(layer, action);
			};
			copied->Nodes = std::make_unique<MotionSelectedLayerNodes>(Commands, Root, Render, *original.Nodes, Runtime.Identifiers(), std::move(construction), std::move(slots));
			MotionSceneEvaluationServices services;
			services.PlayerRandom = Random;
			services.ReadRegisteredAsset = [this](auto project) -> const LoadedMotionAsset& {
				return Runtime.ReadRegisteredAsset(project);
			};
			services.PrepareOwnedChildTimeline = [this](const auto& child) {
				return Runtime.PrepareScene(child);
			};
			services.RequireUnqueued = [this] {
				RequireUnqueued();
			};
			services.AppendDraw = [this](const auto& packets, auto rows) {
				if (!Current || !Current->Scene || &packets != &Current->Scene->Packets() || rows.data() != Current->Scene->GlobalRows().data()) {
					throw std::runtime_error("motion clone Draw must lend its actual independent published scene rows");
				}
				Runtime.SceneContext().Append(*Current->Scene);
			};
			services.DeliverEvent = [this](const auto& event) {
				Listener(event);
			};
			copied->Scene = std::make_unique<MotionSceneEvaluation>(*copied->Nodes, Commands, Root, Render, Runtime.SceneAssets(), Runtime, UpdateOwner, std::move(services), *original.Scene);
			Current = std::move(copied);
		}

		template <typename Phase>
		void Run(Phase&& phase) {
			RequireMutable();
			RequirePhase();
			Operating = true;
			try {
				phase();
				Operating = false;
			} catch (...) {
				Operating = false;
				throw;
			}
		}
	};

	MotionScenePlayer::MotionScenePlayer(MotionManagerRuntime& runtime, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, std::function<void(const MotionPlayerEvent&)> listener, MotionPlayerRandom* actual_random)
	    : m_Implementation(std::make_unique<Implementation>(runtime, commands, root, render, std::move(listener), actual_random)) {
	}

	MotionScenePlayer::MotionScenePlayer(MotionManagerRuntime& runtime, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, std::function<void(const MotionPlayerEvent&)> listener, MotionPlayerRandom* actual_random, const MotionScenePlayer& source)
	    : MotionScenePlayer(runtime, commands, root, render, std::move(listener), actual_random) {
		m_Implementation->CopyForClone(*source.m_Implementation);
	}

	MotionScenePlayer::~MotionScenePlayer() noexcept {
		if (!CanMutate()) {
			std::terminate();
		}
	}

	void MotionScenePlayer::CaptureJoin() {
		m_Implementation->CaptureJoin();
	}

	void MotionScenePlayer::Clear() {
		auto& state = *m_Implementation;
		state.Run([&] {
			if (state.Current && !state.Current->Cleared) {
				// Keep the evaluator implementation around any owning JOIN snapshots; only its current tree retires.
				state.Current->Scene->RetireRootForRebuild();
				state.Current->RetainedParameters = state.Current->Nodes->ClearLayers();
				state.Current->Nodes.reset();
				state.Current->Cleared = true;
			}
			state.Commands.m_SelectedLayersCleared = true;
		});
	}

	void MotionScenePlayer::Rebuild(const ResolvedMotionTimeline& selection) {
		m_Implementation->Rebuild(selection);
	}

	void MotionScenePlayer::Update() {
		auto& state = *m_Implementation;
		state.Run([&] {
			if (state.Current && !state.Current->Cleared) {
				state.Current->Scene->Update();
			} else {
				state.UpdateOwner.Update();
			}
		});
	}

	void MotionScenePlayer::Progress(float elapsed_ticks) {
		auto& state = *m_Implementation;
		state.Run([&] {
			if (state.Current && !state.Current->Cleared) {
				state.Current->Scene->Progress(elapsed_ticks);
			} else {
				state.UpdateOwner.Progress(elapsed_ticks);
			}
		});
	}

	void MotionScenePlayer::PostUpdate() {
		auto& state = *m_Implementation;
		state.Run([&] {
			state.UpdateOwner.PostUpdate();
		});
	}

	void MotionScenePlayer::Draw() {
		auto& state = *m_Implementation;
		state.Run([&] {
			state.UpdateOwner.Draw();
		});
	}

	bool MotionScenePlayer::CanMutate() const noexcept {
		return m_Implementation->CanMutate();
	}

	bool MotionScenePlayer::ResourcesLive() const {
		const auto& state = *m_Implementation;
		return !state.Runtime.IsClosed() && (!state.Current || state.Current->Cleared || state.Runtime.ResourceBorrowsLive()) && (state.Current || state.Commands.CachedTimeline() == nullptr);
	}

	const MotionSceneEvaluation* MotionScenePlayer::Scene() const noexcept {
		const auto& current = m_Implementation->Current;
		return current && !current->Cleared ? current->Scene.get() : nullptr;
	}
} // namespace sgre::scene
