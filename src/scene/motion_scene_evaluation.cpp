#include "sgre/scene/motion_scene_evaluation.hpp"

#include "sgre/scene/motion_child_frame_selection.hpp"
#include "sgre/scene/motion_child_root.hpp"
#include "sgre/scene/motion_child_target.hpp"
#include "sgre/scene/motion_layer_clip_rectangles.hpp"
#include "sgre/scene/motion_layer_stencil_ancestry.hpp"
#include "sgre/scene/motion_particle_birth.hpp"
#include "sgre/scene/motion_particle_layer_state.hpp"
#include "sgre/scene/motion_timeline_variables.hpp"

#include <algorithm>
#include <bit>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sgre::scene {

	struct MotionScenePhaseScope {
		bool& Active;
		explicit MotionScenePhaseScope(bool& active)
		    : Active(active) {
			if (Active) {
				throw std::runtime_error("actual motion scene phase cannot reenter");
			}
			Active = true;
		}
		~MotionScenePhaseScope() {
			Active = false;
		}
	};

	class MotionSceneEvaluation::Implementation {
	public:
		struct Context;
		struct NestedJoin;

		struct JoinedChild {
			std::string Path;
			MotionSelectedLayerNodes::JoinedChildOwners Owners;
			std::unique_ptr<NestedJoin> Pending;
			std::unique_ptr<Context> Phases;
		};

		struct NestedJoin {
			// Dereference Player only while its exact weak native lifetime is live.
			MotionChildConstructionState* Player = nullptr;
			std::optional<MotionChildBorrow> Lifetime;
			bool CaptureInstalled = false;
			std::vector<MotionJoinedLayer> Layers;
			std::vector<JoinedChild> Children;
		};

		struct Context {
			Context(Implementation& scene, MotionSelectedLayerNodes& selected, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, MotionPlayerUpdate* actual_update, Context* containing, MotionChildConstructionState* child, MotionParticlePlayerState* particle, std::size_t depth, const Context* copy_source = nullptr, NestedJoin* pending_join = nullptr)
			    : Scene(scene), Selected(selected), Commands(commands), Root(root), Render(render), Containing(containing), Child(child), Particle(particle), Depth(depth), Timeline(commands.CachedTimeline()), SelectionSerial(commands.SelectionSerial()), Identity(scene.NextContextIdentity()), PendingJoin(pending_join) {
				if (depth > scene.Services.MaxDepth || ((containing == nullptr) != (child == nullptr && particle == nullptr)) || (child != nullptr && particle != nullptr) || (particle != nullptr && particle->m_UpdateOwner != nullptr) || Timeline == nullptr || selected.Nodes().size() > 65536 || &commands.Manager() != &scene.Runtime.Manager() || scene.ContextCount == scene.Services.MaxContexts) {
					throw std::runtime_error("motion scene requires bounded exact selected/containing/manager owners");
				}
				const auto& selection = commands.SelectedProject();
				if (!selection || selection->Timeline != Timeline) {
					throw std::runtime_error("motion scene requires the command selector's actual registered timeline");
				}
				Asset = &scene.ReadAsset(selection->Project);
				if (Asset->Model.get() != &scene.Runtime.Manager().Resource(selection->Project)) {
					throw std::runtime_error("motion scene local resource differs from its actual selected registration");
				}

				Sources = std::make_unique<MotionSelectedLayerSources>(selected, scene.Catalog, [&scene](auto project) -> const LoadedMotionAsset& {
					return scene.ReadAsset(project);
				});
				MotionLocalFrameServices local_services = Sources->FrameServices([this](auto layer, auto action) {
					if (Update == nullptr) {
						throw std::runtime_error("motion scene action queue is unavailable during constructor preparation");
					}
					Update->EnqueueAction(layer, action);
				});
				Local = std::make_unique<MotionLocalFrames>(selected, *Asset->Model, std::move(local_services));
				if (copy_source != nullptr) {
					Ancestry = std::make_unique<MotionSelectedMeshAncestry>(selected, *Sources, scene.Runtime, containing == nullptr ? nullptr : containing->Ancestry.get(), *copy_source->Ancestry);
				} else {
					Ancestry = std::make_unique<MotionSelectedMeshAncestry>(selected, *Sources, scene.Runtime, containing == nullptr ? nullptr : containing->Ancestry.get());
				}
				Composition = std::make_unique<MotionLayerComposition>(selected, root, child == nullptr || !child->IndependentLayerInherit());
				Vertices = std::make_unique<MotionSelectedMeshVertices>(*Ancestry);
				Stencil = std::make_unique<MotionLayerStencilAncestry>(selected);
				Clips = std::make_unique<MotionLayerClipRectangles>(Local->Preparation());
				Rows = std::make_unique<MotionStencilRowPolicies>(selected, *Stencil, containing == nullptr ? nullptr : containing->Rows.get());
				Priority = std::make_unique<MotionPriorityCursor>(Local->Preparation());
				CurrentIndices.resize(selected.Nodes().size());
				CurrentSources.resize(selected.Nodes().size());
				Children.resize(selected.Nodes().size());
				ChildJoins.resize(selected.Nodes().size());
				UnselectedUpdates.resize(selected.Nodes().size());
				ChildRows.resize(selected.Nodes().size());
				ParticleContexts.resize(selected.Nodes().size());
				ParticleRows.resize(selected.Nodes().size());
				if (containing == nullptr) {
					if (actual_update == nullptr) {
						throw std::runtime_error("motion scene root requires its actual containing task update owner");
					}
					Update = actual_update;
				} else {
					MotionPlayerUpdateServices services;
					services.ReadFrameDelta = [&scene] {
						return scene.Runtime.ReadFrameDelta();
					};
					services.PrepareFrames = [this](auto traversal, const auto& frame) {
						Select(traversal, frame);
					};
					services.ComposeLayers = [this](const auto& frame) {
						Compose(frame);
					};
					services.PublishDrawPackets = [&scene](const auto& frame) {
						scene.Publish(frame);
					};
					services.Draw = [&scene] {
						scene.Draw();
					};
					services.DeliverEvent = scene.Services.DeliverEvent;
					OwnedUpdate = std::make_unique<MotionPlayerUpdate>(commands, std::move(services));
					Update = OwnedUpdate.get();
				}
				Update->m_HasParticlePhase = true;
				if (Child != nullptr) {
					Update->PrepareCurrentTimeline(*Timeline, Child->m_PreparationOwner);
				} else if (Particle != nullptr) {
					Update->PrepareCurrentTimeline(*Timeline, Particle->m_PreparationOwner);
				} else {
					Update->PrepareTimeline(*Timeline);
				}
				if (Particle != nullptr) {
					Particle->m_UpdateOwner = Update;
				}
				++scene.ContextCount;
			}

			~Context() noexcept {
				for (auto& pending : ChildJoins) {
					if (pending) {
						Scene.RetireNestedJoin(*pending);
					}
				}
				if (Particle != nullptr && Particle->m_UpdateOwner == Update) {
					Particle->m_UpdateOwner = nullptr;
				}
				--Scene.ContextCount;
			}

			void RequireSelected() const {
				if (Commands.CachedTimeline() != Timeline || Commands.SelectionSerial() != SelectionSerial || Selected.Nodes().size() != CurrentIndices.size()) {
					throw std::runtime_error("motion scene phase requires its exact live selected generation and source registrations");
				}
				Sources->RequireRegistrations();
			}

			void Select(MotionFrameTraversal traversal, const MotionPlayerFrameContext& frame) {
				RequireSelected();
				if (frame.Timeline != Timeline) {
					throw std::runtime_error("motion scene local traversal differs from its actual timeline");
				}
				switch (traversal) {
				case MotionFrameTraversal::Seek:
					Priority->Seek(frame.Clock.EvaluationTime);
					break;
				case MotionFrameTraversal::Forward:
					Priority->Forward(frame.Clock.EvaluationTime);
					break;
				case MotionFrameTraversal::Reverse:
					Priority->Reverse(frame.Clock.EvaluationTime);
					break;
				case MotionFrameTraversal::ParameterOnly:
					break;
				}
				Local->Select(traversal, frame.Clock.EvaluationTime);
				auto& joined_layers = PendingJoin == nullptr ? Scene.JoinedLayers : PendingJoin->Layers;
				auto& joined_children = PendingJoin == nullptr ? Scene.JoinedChildren : PendingJoin->Children;
				if ((Containing == nullptr || PendingJoin != nullptr) && !joined_layers.empty()) {
					const bool replacing_children = !joined_children.empty();
					if (replacing_children) {
						Selected.UnbindJoinedDescriptors();
					}
					for (const auto& node : Selected.Nodes()) {
						if (!node.JoinTarget) {
							continue;
						}
						const auto path = Scene.LayerPath(Selected, node.NativeOrdinal - 1);
						const auto found = std::find_if(joined_layers.begin(), joined_layers.end(), [&](const auto& saved) {
							return saved.Path == path && saved.Kind == node.Prepared->Type;
						});
						if (found != joined_layers.end()) {
							if (found->Kind == 3) {
								const auto layer = node.NativeOrdinal - 1;
								const auto owning = std::find_if(joined_children.begin(), joined_children.end(), [&](const auto& child) {
									return child.Path == path;
								});
								if (owning == joined_children.end()) {
									throw std::runtime_error("motion JOIN kind3 numeric state lacks its actual owning player capsule");
								}
								auto* context = owning->Phases.get();
								MotionChildFrameSlotServices queue;
								if (context != nullptr) {
									queue.EnqueueAction = [context](auto label, auto action) {
										context->Update->EnqueueAction(label, action);
									};
								}
								Selected.InstallJoinedChild(layer, owning->Owners, std::move(queue));
								if (ChildJoins[layer]) {
									Scene.RetireNestedJoin(*ChildJoins[layer]);
								}
								ChildJoins[layer] = std::move(owning->Pending);
								auto& retained = Selected.Child(layer);
								Ancestry->m_States[layer].m_Child = &retained;
								if (context != nullptr) {
									context->Containing = this;
									context->Ancestry->m_Containing = Ancestry.get();
									context->Ancestry->m_RootAncestor = nullptr;
									context->Ancestry->m_ContainingSerial = 0;
									context->Ancestry->m_Composed = false;
									context->Rows->m_ContainingRows = Rows.get();
									context->Rows->m_ContainingLayer = layer;
									context->Rows->m_ContainingPreparationSerial = 0;
									Children[layer] = std::move(owning->Phases);
								}
								joined_children.erase(owning);
							}
							Local->RestoreJoin(node.NativeOrdinal - 1, *found);
							joined_layers.erase(found);
						}
					}
					// Native51e720 retires both consumed and unmatched outgoing records after initial seek.
					joined_layers.clear();
					Scene.RetireJoinedChildren(joined_children);
					if (replacing_children) {
						// Native511640 calls512490 after51e720 restores and retires its actual owning snapshots.
						Selected.RebindJoinedDescriptors();
					}
				}
			}

			void Compose(const MotionPlayerFrameContext& frame) {
				RequireSelected();
				if (frame.Timeline != Timeline) {
					throw std::runtime_error("motion scene composition differs from its actual timeline");
				}
				if (ClonedRootTransferPending) {
					const MotionSelectedMeshAncestry* transfer_owner = nullptr;
					std::uint64_t transfer_serial = 0;
					if (Child != nullptr) {
						transfer_owner = Child->m_DeformationTransferOwner;
						transfer_serial = Child->m_DeformationTransferSerial;
					} else if (Particle != nullptr) {
						transfer_owner = Particle->m_DeformationTransferOwner;
						transfer_serial = Particle->m_DeformationTransferSerial;
					}
					if (Containing == nullptr || transfer_owner != Containing->Ancestry.get() || transfer_serial != Containing->Ancestry->m_AncestrySerial || transfer_serial == ClonedContainingSerial) {
						throw std::runtime_error("motion copied child root requires a fresh genuine parent transfer before its next numeric composition");
					}
					ClonedRootTransferPending = false;
				}
				Root.AdvanceVelocity(frame.Clock.DeltaTicks);
				Composition->Compose(*Local, frame.Clock.TickAssigned);
				for (const auto& node : Selected.Nodes()) {
					const auto layer = node.NativeOrdinal - 1;
					CurrentIndices[layer] = Local->State(layer).CurrentIndex;
					CurrentSources[layer] = node.Prepared->Type == 0 || node.Prepared->Type == 12 ? &Sources->Source(layer) : nullptr;
				}
				Ancestry->Compose(*Composition);
				Stencil->Compose(Composition->Layers(), CurrentIndices, CurrentSources, frame.Clock.TickCount, Child != nullptr ? Child->InheritedStencil() : Particle != nullptr ? Particle->m_InheritedStencil
				                                                                                                                                                                  : nullptr);
				Clips->Compose(Composition->Layers(), CurrentIndices, Child != nullptr ? Child->InheritedClip() : Particle != nullptr ? Particle->m_InheritedClip
				                                                                                                                      : nullptr);
				for (const auto ordinal : Selected.ShapeOrdinals()) {
					Selected.Shape(ordinal - 1).Compose(*Composition, *Local, *Vertices);
				}
				if (!Scene.GraphDirty) {
					Rows->PrepareRecursiveFrame(Composition->Layers());
				}
				const auto ordinals = Selected.ChildOrdinals();
				for (auto index = ordinals.rbegin(); index != ordinals.rend(); ++index) {
					ComposeChild(*index - 1, frame.Clock.DeltaTicks);
				}
				for (const auto ordinal : Selected.ParticleOrdinals()) {
					ComposeParticles(ordinal - 1, frame.Clock.DeltaTicks);
				}
				Vertices->Compose();
				Local->CompleteComposition();
			}

			void CreateInitialUnselectedUpdate(std::uint32_t layer) {
				auto& owner = UnselectedUpdates.at(layer);
				auto& child = Selected.Child(layer);
				if (!Selected.ChildSlots(layer).HasInitialUnselectedTarget() || Children.at(layer) || Selected.ChildNodes(layer) != nullptr) {
					throw std::runtime_error("motion unselected child update requires its actual failed first command and separate root only");
				}
				if (!owner) {
					MotionPlayerUpdateServices services;
					services.ReadFrameDelta = [this] {
						return Scene.Runtime.ReadFrameDelta();
					};
					services.PrepareFrames = [](auto, const auto&) {
						throw std::runtime_error("motion unselected child cannot traverse an uninstalled timeline");
					};
					services.ComposeLayers = [this, layer](const auto& frame) {
						auto& actual = Selected.Child(layer);
						if (frame.Timeline != nullptr || !Selected.ChildSlots(layer).HasInitialUnselectedTarget() || Selected.ChildNodes(layer) != nullptr) {
							throw std::runtime_error("motion unselected child composition lost its actual root-only command target");
						}
						// Native524910 calls51ffd0 then50c550 even with no selected atom: compose the actual transferred root.
						actual.Root().AdvanceVelocity(frame.Clock.DeltaTicks);
						actual.Root().Compose(frame.Clock.TickAssigned);
					};
					services.PublishDrawPackets = [this](const auto& frame) {
						Scene.Publish(frame);
					};
					services.Draw = [this] {
						Scene.Draw();
					};
					services.DeliverEvent = Scene.Services.DeliverEvent;
					owner = std::make_unique<MotionPlayerUpdate>(child.Commands(), std::move(services));
				}
			}

			void UpdateInitialUnselectedChild(std::uint32_t layer, float elapsed) {
				CreateInitialUnselectedUpdate(layer);
				auto& owner = UnselectedUpdates.at(layer);
				owner->UpdateChild(elapsed);
				Local->CompleteInitialUnselectedChild(layer);
				Update->PrependChildEvents(*owner);
			}

			NestedJoin& PrepareChildJoin(std::uint32_t layer) {
				auto& pending = ChildJoins.at(layer);
				auto& child = Selected.Child(layer);
				if (!pending) {
					pending = std::make_unique<NestedJoin>();
				}
				const auto borrow = child.Borrow();
				if (pending->Lifetime && pending->Lifetime->IsLive() && !pending->Lifetime->SameTarget(borrow)) {
					throw std::runtime_error("motion child JOIN map differs from its actual containing player");
				}
				pending->Player = &child;
				pending->Lifetime = borrow;
				if (pending->CaptureInstalled) {
					return *pending;
				}
				child.Commands().m_CaptureJoin = [this, layer, borrow] {
					Scene.Services.RequireUnqueued();
					auto& outgoing = Children.at(layer);
					auto& saved = *ChildJoins.at(layer);
					if (!borrow.IsLive() || !Selected.Child(layer).Borrow().SameTarget(borrow) || saved.Player != &Selected.Child(layer) || !saved.Lifetime || !saved.Lifetime->SameTarget(borrow)) {
						throw std::runtime_error("motion child JOIN capture lost its exact actual player");
					}
					if (const auto* variables = saved.Player->Commands().TimelineVariables(); variables != nullptr && !variables->Empty()) {
						throw std::runtime_error("motion child JOIN variable records require their actual native snapshot owner");
					}
					if (!outgoing) {
						if (const auto* selected = Selected.ChildNodes(layer); selected != nullptr && !selected->Nodes().empty()) {
							throw std::runtime_error("motion child JOIN capture requires its completed outgoing selected context");
						}
						saved.Layers.clear();
						Scene.RetireJoinedChildren(saved.Children);
						return;
					}
					if (outgoing->Child != saved.Player || &outgoing->Commands != &saved.Player->Commands()) {
						throw std::runtime_error("motion child JOIN capture context differs from its actual player");
					}
					saved.Layers = Scene.CaptureJoin(*outgoing, saved.Children);
				};
				pending->CaptureInstalled = true;
				return *pending;
			}

			void ComposeChild(std::uint32_t layer, float elapsed) {
				auto& stored = Children.at(layer);
				auto& child = Selected.Child(layer);
				auto& pending = PrepareChildJoin(layer);
				const auto action = SelectMotionChildFrameCommands(Selected.ChildSlots(layer), *Composition, [&] {
					Scene.InvalidateGraph();
					stored.reset();
					Selected.RetireChildNodes(layer);
				});
				if (action == MotionChildFrameSelection::Reset) {
					return;
				}
				if (action == MotionChildFrameSelection::BindRoot) {
					const MotionChildDirectionScene direction{*Timeline, Local->Preparation(), *Asset->Model, Root, Composition->Layers(), child.Commands().ClockState()};
					const auto& slots = Selected.ChildSlots(layer);
					const MotionChildDirectionSelection selection{layer, Local->State(layer).CurrentIndex, slots.CurrentControl(), slots.FollowingControl(), Local->Position(layer)};
					BindMotionChildRoot(child.Root(), direction, selection, std::bit_cast<std::int32_t>(child.RenderControls().ColorFilter));
				}
				Ancestry->BindChildDeformation(layer, child);
				Stencil->BindChildStencil(layer, child);
				Clips->BindChildClip(layer, child);
				if (!stored && Selected.ChildSlots(layer).HasInitialUnselectedTarget()) {
					UpdateInitialUnselectedChild(layer, elapsed);
					return;
				}
				if (!stored) {
					UnselectedUpdates[layer].reset();
					const auto* selected_child = Selected.ChildNodes(layer);
					const auto& names = child.Commands().Names();
					if ((selected_child == nullptr || selected_child->Nodes().empty()) && names.CurrentMotion.empty() && names.RequestedMotion.empty()) {
						// No selected timeline work or events exist after real reset; retain only the transferred root borrows.
						return;
					}
					const auto& selection = child.Commands().SelectedProject();
					if (!selection || selection->Timeline != child.Commands().CachedTimeline()) {
						throw std::runtime_error("present motion child lacks its actual selected timeline");
					}
					std::optional<MotionRuntimeSelection> owned;
					const PreparedMotionTimeline* preparation = nullptr;
					if (Scene.Services.PrepareOwnedChildTimeline) {
						owned.emplace(Scene.Services.PrepareOwnedChildTimeline(*selection));
						if (!owned->Owner.UsesRuntime(Scene.Runtime) || !owned->Owner.Owns(Scene.Runtime.Manager(), *selection->Timeline, &owned->Timeline)) {
							throw std::runtime_error("motion child preparation lease differs from its actual selected timeline owner");
						}
						preparation = &owned->Timeline;
					} else {
						preparation = &Scene.Services.PrepareChildTimeline(*selection);
					}
					MotionChildFrameSlotServices slots;
					slots.EnqueueAction = [&stored](auto label, auto event) {
						if (!stored || stored->Update == nullptr) {
							throw std::runtime_error("motion child action queue is unavailable during selected construction");
						}
						stored->Update->EnqueueAction(label, event);
					};
					Scene.InvalidateGraph();
					Selected.RebuildChildNodes(layer, *selection->Timeline, *preparation, std::move(slots));
					child.m_PreparationOwner = owned ? owned->Owner : MotionPreparationLease{};
					stored = std::make_unique<Context>(Scene, *Selected.ChildNodes(layer), child.Commands(), child.Root(), child.RenderControls(), nullptr, this, &child, nullptr, Depth + 1, nullptr, &pending);
				}
				stored->Update->UpdateChild(elapsed);
				Update->PrependChildEvents(*stored->Update);
			}

			MotionPlayerRandom& PlayerRandom() const {
				if (Child != nullptr) {
					return Child->Random();
				}
				if (Particle != nullptr) {
					return Particle->Random();
				}
				if (Scene.Services.PlayerRandom == nullptr) {
					throw std::runtime_error("motion kind4 emission requires its actual containing task random owner");
				}
				return *Scene.Services.PlayerRandom;
			}

			void InstallParticleSelection(MotionParticleInstance& instance, const ResolvedMotionTimeline& selection, MotionPlayFlags flags) {
				if (flags != MotionPlayFlags::None || !Scene.Services.PrepareOwnedChildTimeline || instance.Selected || !instance.Player || !instance.Player->Commands().SelectedProject() || instance.Player->Commands().SelectedProject()->Timeline != selection.Timeline) {
					throw std::runtime_error("motion emitted selection requires its genuine first Play and actual preparation lease");
				}
				const auto prepared = Scene.Services.PrepareOwnedChildTimeline(selection);
				if (!prepared.Owner.UsesRuntime(Scene.Runtime) || !prepared.Owner.Owns(Scene.Runtime.Manager(), *selection.Timeline, &prepared.Timeline)) {
					throw std::runtime_error("motion emitted preparation differs from its actual selected registration");
				}
				instance.Player->m_PreparationOwner = prepared.Owner;
				MotionChildFrameSlotServices queue;
				queue.EnqueueAction = [actual = &instance](auto label, auto action) {
					if (!actual->Player || actual->Player->m_UpdateOwner == nullptr) {
						throw std::runtime_error("motion particle actions require its completed actual update owner");
					}
					actual->Player->m_UpdateOwner->EnqueueAction(label, action);
				};
				instance.Selected = std::make_unique<MotionSelectedLayerNodes>(*instance.Player, *selection.Timeline, prepared.Timeline, Scene.Runtime.Identifiers(), Selected.m_Construction, std::move(queue));
				instance.Selected->m_ContainingSelected = &Selected;
				instance.Selected->m_OwnerDepth = Selected.m_OwnerDepth + 1;
			}

			void SpawnParticle(std::uint32_t layer, float elapsed) {
				auto& emitter = Selected.Particles(layer);
				const auto& definition = emitter.Definition();
				if (definition.MotionList.empty()) {
					return;
				}
				auto& random = PlayerRandom();
				const auto sample = random.NextUnitFloat();
				const auto target_index = static_cast<std::size_t>(static_cast<float>(definition.MotionList.size()) * sample);
				if (target_index >= definition.MotionList.size() || Depth == Scene.Services.MaxDepth) {
					throw std::runtime_error("motion emitted target/depth exceeds its actual bounded owner graph");
				}
				Scene.InvalidateGraph();
				auto instance = std::make_unique<MotionParticleInstance>();
				if (Child != nullptr) {
					instance->Player = std::make_unique<MotionParticlePlayerState>(*Child, Selected.m_Construction);
				} else if (Particle != nullptr) {
					instance->Player = std::make_unique<MotionParticlePlayerState>(*Particle, Selected.m_Construction);
				} else {
					instance->Player = std::make_unique<MotionParticlePlayerState>(Commands, Root, Render, Selected.m_Construction);
				}
				auto* actual = instance.get();
				instance->Player->Commands().m_InstallSelection = [this, actual](const auto& selection, auto flags) {
					InstallParticleSelection(*actual, selection, flags);
				};
				instance->Player->SelectInitialTarget(definition.MotionList[target_index]);
				const auto& numeric = Composition->Layers()[layer].Hierarchy;
				const auto& attributes = numeric.Attributes;
				const auto& current = Selected.Nodes()[layer].Prepared->Frames[Local->State(layer).CurrentIndex].Attributes;
				MotionParticleBirthSource source;
				source.Transform = numeric.Transform;
				source.Coordinate = attributes.Coordinate;
				source.Displacement = Composition->Layers()[layer].Displacement;
				source.DeltaTicks = elapsed;
				source.Origin = {current.OriginX, current.OriginY};
				source.PlayerRootTransform = Root.Frame().Hierarchy.Transform;
				source.CoordinatePlane = numeric.CoordinatePlane;
				source.Opacity = attributes.Opacity;
				source.IndependentLayerInherit = Child != nullptr && Child->IndependentLayerInherit();
				source.FlipX = attributes.FlipX;
				source.FlipY = attributes.FlipY;
				const auto birth = EvaluateMotionParticleBirth(definition, emitter.Frames().Numeric(), source, random);
				auto& root = instance->Player->Root();
				root.SetCoordinate3(birth.Coordinate);
				root.SetVelocity(birth.Velocity);
				root.SetAngleDegrees(birth.AngleDegrees);
				root.SetZoom(birth.Zoom, birth.Zoom);
				root.SetOpacity(birth.Opacity);
				root.SetFlip(birth.FlipX, birth.FlipY);
				root.SetVelocityAttenuation(birth.Attenuation);
				emitter.m_Particles.push_back(std::move(instance));
				ParticleContexts[layer].push_back(nullptr);
				if (MotionParticleCountExceedsMaximum(emitter.m_Particles.size(), definition.MaximumParticles)) {
					RetireParticle(layer, 0);
				}
			}

			void RetireParticle(std::uint32_t layer, std::size_t index) {
				auto& particles = Selected.Particles(layer).m_Particles;
				auto& contexts = ParticleContexts[layer];
				if (index >= particles.size() || contexts.size() != particles.size()) {
					throw std::runtime_error("motion particle retirement requires its exact selected/context vector pair");
				}
				Scene.InvalidateGraph();
				contexts[index].reset();
				particles[index].reset();
				contexts.erase(contexts.begin() + static_cast<std::ptrdiff_t>(index));
				particles.erase(particles.begin() + static_cast<std::ptrdiff_t>(index));
			}

			void ComposeParticles(std::uint32_t layer, float elapsed) {
				auto& emitter = Selected.Particles(layer);
				const auto& definition = emitter.Definition();
				if (definition.InheritOpacity != 1 || definition.InheritVelocity != 0) {
					throw std::runtime_error("motion particle live update requires its measured retirement/inheritance producer");
				}
				const auto& frame = Local->State(layer);
				const auto& numeric = emitter.Frames().Numeric();
				MotionParticleEmissionInput input;
				input.Active = Composition->Layers()[layer].Active;
				input.FrameChanged = frame.FrameDirty;
				input.Trigger = emitter.Frames().Control(frame.CurrentSlot).Trigger;
				input.FrequencyMin = numeric.FrequencyMin;
				input.FrequencyMax = numeric.FrequencyMax;
				input.DeltaTicks = elapsed;
				const auto attempts = emitter.m_Emission.Advance(input, PlayerRandom());
				for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
					SpawnParticle(layer, elapsed);
				}
				for (std::size_t index = 0; index < emitter.m_Particles.size();) {
					auto& instance = *emitter.m_Particles[index];
					if (!instance.Player->Commands().ClockState().Playing) {
						RetireParticle(layer, index);
						continue;
					}
					if (!instance.Selected || instance.Player->Commands().CachedTimeline() == nullptr) {
						throw std::runtime_error("motion playing particle requires its actual successful selected tree");
					}
					const auto& coordinate = instance.Player->Root().Overrides().Coordinate;
					if (instance.Screen.ShouldRetire(definition, {coordinate[0], coordinate[1]}, instance.Player->Commands().CachedTimeline()->Bounds)) {
						RetireParticle(layer, index);
						continue;
					}
					auto& state = *instance.Player;
					Ancestry->BindParticleDeformation(layer, state, *instance.Selected);
					Stencil->BindParticleStencil(layer, state, *instance.Selected);
					Clips->BindParticleClip(layer, state);
					const auto& hierarchy = Composition->Layers()[layer].Hierarchy;
					const auto modulation = ComposeMotionColor(hierarchy.ModulationColor, hierarchy.Attributes.Colors[0]);
					if (state.RenderControls().ColorFilter != modulation) {
						state.Root().TransferModulationColor(modulation);
					}
					auto& context = ParticleContexts[layer][index];
					if (!context) {
						context = std::make_unique<Context>(Scene, *instance.Selected, state.Commands(), state.Root(), state.RenderControls(), nullptr, this, nullptr, &state, Depth + 1);
					}
					context->Update->UpdateChild(elapsed);
					Update->PrependChildEvents(*context->Update);
					++index;
				}
			}

			void InvalidateRows() {
				Rows->InvalidateChildRows();
				for (auto& child : Children) {
					if (child) {
						child->InvalidateRows();
					}
				}
				for (auto& layer : ParticleContexts) {
					for (auto& particle : layer) {
						if (particle) {
							particle->InvalidateRows();
						}
					}
				}
			}

			void RefreshRows() {
				for (std::size_t layer = 0; layer != Children.size(); ++layer) {
					if (Children[layer]) {
						Children[layer]->RefreshRows();
						ChildRows[layer] = Children[layer]->Rows.get();
					} else {
						ChildRows[layer] = nullptr;
					}
				}
				for (std::size_t layer = 0; layer < ParticleContexts.size(); ++layer) {
					auto& rows = ParticleRows[layer];
					rows.clear();
					rows.reserve(ParticleContexts[layer].size());
					for (auto& particle : ParticleContexts[layer]) {
						if (!particle) {
							throw std::runtime_error("motion graph refresh requires every surviving particle update context");
						}
						particle->RefreshRows();
						rows.push_back(particle->Rows.get());
					}
				}
				Rows->RefreshChildRows(ChildRows, ParticleRows);
			}

			void CollectOwners(std::vector<MotionMeshMaskOwner>& owners) {
				if (owners.size() == Scene.Services.MaxContexts) {
					throw std::runtime_error("motion packet graph exceeds its actual context bound");
				}
				owners.emplace_back(*Rows, *Vertices);
				for (auto& child : Children) {
					if (child) {
						child->CollectOwners(owners);
					}
				}
				for (auto& layer : ParticleContexts) {
					for (auto& particle : layer) {
						if (particle) {
							particle->CollectOwners(owners);
						}
					}
				}
			}

			void RegisterRows() {
				for (std::size_t index = 0; index != Rows->Size(); ++index) {
					if (!Scene.RowContexts.emplace(&Rows->Row(index), this).second) {
						throw std::runtime_error("motion scene packet graph duplicates a persistent row owner");
					}
				}
				for (auto& child : Children) {
					if (child) {
						child->RegisterRows();
					}
				}
				for (auto& layer : ParticleContexts) {
					for (auto& particle : layer) {
						if (particle) {
							particle->RegisterRows();
						}
					}
				}
			}

			void PrepareRows() {
				Rows->PrepareRecursiveFrame(Composition->Layers());
				Vertices->Compose();
				for (auto& child : Children) {
					if (child) {
						child->PrepareRows();
					}
				}
				for (auto& layer : ParticleContexts) {
					for (auto& particle : layer) {
						if (particle) {
							particle->PrepareRows();
						}
					}
				}
			}

			void PublishRows(bool copied_snapshot = false) {
				for (auto& child : Children) {
					if (child) {
						child->PublishRows(copied_snapshot);
					}
				}
				for (auto& layer : ParticleContexts) {
					for (auto& particle : layer) {
						if (particle) {
							particle->PublishRows(copied_snapshot);
						}
					}
				}
				if (ClonedPublicationPending && Commands.ClockState().TickAssigned) {
					// Native50AD30 returns before touching this copied player's rows while its assigned tick is pending.
					Rows->m_CollectedRows.clear();
					Rows->m_GlobalRows.clear();
					Rows->m_PublishedNumeric = Composition->Layers();
					Rows->m_Published = true;
					return;
				}

				Rows->Publish(Composition->Layers(), CurrentIndices, CurrentSources, Commands.ClockState().TickCount, *Priority, copied_snapshot);
				ClonedPublicationPending = false;
			}

			Implementation& Scene;
			MotionSelectedLayerNodes& Selected;
			MotionPlayerCommands& Commands;
			MotionRootScene& Root;
			const MotionPlayerRenderControls& Render;
			Context* Containing;
			MotionChildConstructionState* Child;
			MotionParticlePlayerState* Particle;
			std::size_t Depth;
			const resource::MotionTimeline* Timeline;
			std::uint64_t SelectionSerial;
			std::uint64_t Identity;
			NestedJoin* PendingJoin = nullptr;
			const LoadedMotionAsset* Asset = nullptr;
			std::unique_ptr<MotionSelectedLayerSources> Sources;
			std::unique_ptr<MotionLocalFrames> Local;
			std::unique_ptr<MotionSelectedMeshAncestry> Ancestry;
			std::unique_ptr<MotionLayerComposition> Composition;
			std::unique_ptr<MotionSelectedMeshVertices> Vertices;
			std::unique_ptr<MotionLayerStencilAncestry> Stencil;
			std::unique_ptr<MotionLayerClipRectangles> Clips;
			std::unique_ptr<MotionPriorityCursor> Priority;
			std::unique_ptr<MotionStencilRowPolicies> Rows;
			std::vector<std::size_t> CurrentIndices;
			std::vector<const MotionStaticSourceBinding*> CurrentSources;
			std::unique_ptr<MotionPlayerUpdate> OwnedUpdate;
			MotionPlayerUpdate* Update = nullptr;
			// Separate from ordinary SetTickCount: that source still draws its previous completed collection.
			bool ClonedPublicationPending = false;
			bool ClonedRootTransferPending = false;
			std::uint64_t ClonedContainingSerial = 0;
			std::vector<std::unique_ptr<Context>> Children;
			// Actual player JOIN maps survive replacement of that player's child Context.
			std::vector<std::unique_ptr<NestedJoin>> ChildJoins;
			// These own only actual command/root update phases; they have no selected timeline, rows or synthetic preparation.
			std::vector<std::unique_ptr<MotionPlayerUpdate>> UnselectedUpdates;
			std::vector<MotionStencilRowPolicies*> ChildRows;
			std::vector<std::vector<std::unique_ptr<Context>>> ParticleContexts;
			std::vector<std::vector<MotionStencilRowPolicies*>> ParticleRows;
		};

		struct ClonedContext {
			const Context* Source = nullptr;
			Context* Destination = nullptr;
		};

		void CloneContextTree(Context& destination, const Context& source, std::vector<ClonedContext>& contexts) {
			if (&destination.Selected.m_Preparation != &source.Selected.m_Preparation || destination.Selected.Nodes().size() != source.Selected.Nodes().size() || contexts.size() == Services.MaxContexts) {
				throw std::runtime_error("motion context clone requires exact immutable frames and bounded independent selected owners");
			}
			for (const auto& pending : source.ChildJoins) {
				if (pending && (!pending->Layers.empty() || !pending->Children.empty())) {
					throw std::runtime_error("motion clone cannot safely copy a native pending child JOIN map");
				}
			}
			contexts.push_back({&source, &destination});
			destination.ClonedPublicationPending = source.Commands.ClockState().TickAssigned;
			CopyLocalValues(destination, source);
			CopySourceValues(destination, source);
			CopyAncestryValues(destination, source, contexts);
			destination.Selected.m_Slots.EnqueueAction = [&destination](auto layer, auto action) {
				destination.Update->EnqueueAction(layer, action);
			};
			for (std::size_t layer = 0; layer < source.Children.size(); ++layer) {
				if (source.Selected.Nodes()[layer].Prepared->Type == 3) {
					auto& child = destination.Selected.Child(static_cast<std::uint32_t>(layer));
					if (!child.m_PreparationOwner.IsEmpty()) {
						// A dormant cleared child still retains immutable cache metadata in its new runtime.
						child.m_PreparationOwner = Runtime.ClonePreparation(child.m_PreparationOwner).Owner;
					}
				}
				if (source.Children[layer]) {
					auto& child = destination.Selected.Child(static_cast<std::uint32_t>(layer));
					auto* selected = destination.Selected.ChildNodes(static_cast<std::uint32_t>(layer));
					if (selected == nullptr) {
						throw std::runtime_error("motion clone lost an actual selected child owner");
					}
					const auto& original = *source.Children[layer];
					auto& pending = destination.PrepareChildJoin(static_cast<std::uint32_t>(layer));
					child.m_DeformationTransferOwner = destination.Ancestry.get();
					child.m_DeformationTransferLayer = static_cast<std::uint32_t>(layer);
					child.m_DeformationTransferSerial = destination.Ancestry->m_AncestrySerial;
					auto& context = destination.Children[layer];
					context = std::make_unique<Context>(*this, *selected, child.Commands(), child.Root(), child.RenderControls(), nullptr, &destination, &child, nullptr, destination.Depth + 1, &original, &pending);
					// Native root copies retain dormant source link words. They are never lent as live typed borrows.
					context->ClonedRootTransferPending = true;
					context->ClonedContainingSerial = destination.Ancestry->m_AncestrySerial;
					CloneContextTree(*context, *source.Children[layer], contexts);
				} else if (source.UnselectedUpdates[layer]) {
					// This owner has no timeline/cursor; create only its real root phase without advancing it.
					destination.CreateInitialUnselectedUpdate(static_cast<std::uint32_t>(layer));
					destination.UnselectedUpdates[layer]->CopyForClone(*source.UnselectedUpdates[layer], Runtime);
				}
				const auto& original = source.ParticleContexts[layer];
				if (source.Selected.Nodes()[layer].Prepared->Type == 4 && source.Selected.Particles(static_cast<std::uint32_t>(layer)).Size() != original.size()) {
					throw std::runtime_error("motion retained particle clone requires its exact completed selected/context vector pair");
				}
				auto& copied = destination.ParticleContexts[layer];
				copied.resize(original.size());
				if (!original.empty() && destination.Selected.Particles(static_cast<std::uint32_t>(layer)).Size() != original.size()) {
					throw std::runtime_error("motion clone lost an actual particle occurrence");
				}
				for (std::size_t occurrence = 0; occurrence < original.size(); ++occurrence) {
					const auto& source_instance = *source.Selected.Particles(static_cast<std::uint32_t>(layer)).m_Particles[occurrence];
					if (!original[occurrence] || original[occurrence]->Particle != source_instance.Player.get() || &original[occurrence]->Selected != source_instance.Selected.get() || original[occurrence]->Update != source_instance.Player->m_UpdateOwner) {
						throw std::runtime_error("motion retained particle clone requires each completed context to belong to its actual player and selected tree");
					}
					auto& instance = *destination.Selected.Particles(static_cast<std::uint32_t>(layer)).m_Particles[occurrence];
					if (!instance.Selected || !instance.Player) {
						throw std::runtime_error("motion clone requires each retained particle's independent player and selected tree");
					}
					auto& player = *instance.Player;
					if (!player.m_PreparationOwner.IsEmpty()) {
						player.m_PreparationOwner = Runtime.ClonePreparation(player.m_PreparationOwner).Owner;
					}
					player.Commands().m_InstallSelection = [&destination, actual = &instance](const auto& selection, auto flags) {
						destination.InstallParticleSelection(*actual, selection, flags);
					};
					// The copied root keeps only a dormant receipt until this new parent performs the next real transfer.
					player.m_DeformationTransferOwner = destination.Ancestry.get();
					player.m_DeformationTransferLayer = static_cast<std::uint32_t>(layer);
					player.m_DeformationTransferSerial = destination.Ancestry->m_AncestrySerial;
					copied[occurrence] = std::make_unique<Context>(*this, *instance.Selected, player.Commands(), player.Root(), player.RenderControls(), nullptr, &destination, nullptr, &player, destination.Depth + 1, original[occurrence].get());
					copied[occurrence]->ClonedRootTransferPending = true;
					copied[occurrence]->ClonedContainingSerial = destination.Ancestry->m_AncestrySerial;
					CloneContextTree(*copied[occurrence], *original[occurrence], contexts);
				}
			}
			destination.Update->CopyForClone(*source.Update, Runtime);
		}

		static std::string_view CloneSampleText(std::string_view text, const MotionLocalFrames::Layer& source, const MotionLocalFrames::Layer& destination) {
			if (source.JoinedDecoded && destination.JoinedDecoded) {
				const auto& old = source.JoinedDecoded->Attributes;
				const auto& copied = destination.JoinedDecoded->Attributes;
				if (text.data() == old.Source.data() && text.size() <= old.Source.size()) {
					return std::string_view(copied.Source).substr(0, text.size());
				}
				if (text.data() == old.Icon.data() && text.size() <= old.Icon.size()) {
					return std::string_view(copied.Icon).substr(0, text.size());
				}
			}
			return text;
		}

		static void CopyLocalValues(Context& destination, const Context& source) {
			auto& local = *destination.Local;
			const auto& old = *source.Local;
			if (old.m_Traversing || local.m_Layers.size() != old.m_Layers.size()) {
				throw std::runtime_error("motion local clone requires its idle exact physical slot owner");
			}
			local.m_Layers = old.m_Layers;
			local.m_Samples = old.m_Samples;
			for (std::size_t index = 0; index < local.m_Layers.size(); ++index) {
				auto& layer = local.m_Layers[index];
				if (layer.Evaluation.Sample) {
					layer.Evaluation.Sample->Source = CloneSampleText(layer.Evaluation.Sample->Source, old.m_Layers[index], layer);
					layer.Evaluation.Sample->Icon = CloneSampleText(layer.Evaluation.Sample->Icon, old.m_Layers[index], layer);
				}
				local.m_Samples[index].Source = CloneSampleText(local.m_Samples[index].Source, old.m_Layers[index], layer);
				local.m_Samples[index].Icon = CloneSampleText(local.m_Samples[index].Icon, old.m_Layers[index], layer);
			}
			local.m_FrameCount = old.m_FrameCount;
			local.m_Published = old.m_Published;
			local.m_Selected = old.m_Selected;
			local.m_Position = old.m_Position;
			local.m_SelectionSerial = old.m_SelectionSerial;
			local.m_ParameterValueSerial = old.m_ParameterValueSerial;
			local.m_FinishedSelectionSerial = old.m_FinishedSelectionSerial;
			local.m_ContainingPreparationSerial = old.m_ContainingPreparationSerial;
			destination.CurrentIndices = source.CurrentIndices;
			const auto& original = *source.Composition;
			auto& composition = *destination.Composition;
			composition.m_LocalFrames = original.m_LocalFrames == nullptr ? nullptr : destination.Local.get();
			composition.m_Overrides = original.m_Overrides;
			composition.m_Layers = original.m_Layers;
			for (std::size_t index = 0; index < composition.m_Layers.size(); ++index) {
				auto& attributes = composition.m_Layers[index].Hierarchy.Attributes;
				attributes.Source = CloneSampleText(attributes.Source, old.m_Layers[index], local.m_Layers[index]);
				attributes.Icon = CloneSampleText(attributes.Icon, old.m_Layers[index], local.m_Layers[index]);
			}
			composition.m_OverrideDirty = original.m_OverrideDirty;
			composition.m_Changes = original.m_Changes;
			composition.m_Composed = original.m_Composed;
			composition.m_ConditionalTrace = original.m_ConditionalTrace;
			composition.m_ComposedSelectionSerial = original.m_ComposedSelectionSerial;
			composition.m_CompositionSerial = original.m_CompositionSerial;
			destination.Priority->m_State = source.Priority->m_State;
		}

		static void CopySourceValues(Context& destination, const Context& source) {
			for (std::size_t index = 0; index < source.Sources->m_Entries.size(); ++index) {
				const auto& old = source.Sources->m_Entries[index];
				auto& entry = destination.Sources->m_Entries[index];
				entry.Frame = old.Frame;
				if (!old.Binding) {
					continue;
				}
				if (!entry.Binding || old.Binding->m_Binding || old.Binding->m_State.External) {
					throw std::runtime_error("motion source clone requires idle ordinary cached bindings; external wrapper cloning remains unrecovered");
				}
				auto& binding = *entry.Binding;
				binding.m_State = old.Binding->m_State;
				binding.m_CurrentFrame = old.Binding->m_CurrentFrame;
				binding.m_ExternalRevision = old.Binding->m_ExternalRevision;
				if (binding.m_State.Binding) {
					const auto actual = destination.Sources->m_Registry->Resolve(binding.m_State.Source, binding.m_State.Icon, destination.Sources->m_Identity);
					if (!actual || actual->Source != binding.m_State.Binding->Source || actual->Icon != binding.m_State.Binding->Icon || actual->Project != binding.m_State.Binding->Project) {
						throw std::runtime_error("motion clone destination source resolution differs from the copied actual registered icon");
					}
					const auto texture = destination.Sources->FindTexture(*actual);
					if (!texture) {
						throw std::runtime_error("motion cloned ready source requires its actual destination registered texture");
					}
					binding.m_State.Binding = actual;
					binding.m_State.Texture = texture;
				}
				destination.CurrentSources[index] = source.CurrentSources[index] == nullptr ? nullptr : &binding;
			}
		}

		static const MotionSelectedDeformationState* CloneDeformation(const MotionSelectedDeformationState* original, std::span<const ClonedContext> contexts) {
			if (original == nullptr) {
				return nullptr;
			}
			for (const auto& context : contexts) {
				for (std::size_t index = 0; index < context.Source->Ancestry->m_States.size(); ++index) {
					if (&context.Source->Ancestry->m_States[index] == original) {
						return &context.Destination->Ancestry->m_States[index];
					}
				}
			}
			throw std::runtime_error("motion clone deformation edge is outside its actual copied graph");
		}

		static const MotionStencilRowState* CloneRow(const MotionStencilRowState* original, std::span<const ClonedContext> contexts) {
			if (original == nullptr) {
				return nullptr;
			}
			for (const auto& context : contexts) {
				for (std::size_t index = 0; index < context.Source->Rows->m_Rows.size(); ++index) {
					if (&context.Source->Rows->m_Rows[index] == original) {
						return &context.Destination->Rows->m_Rows[index];
					}
				}
			}
			throw std::runtime_error("motion clone row edge is outside its actual copied graph");
		}

		static void CopyAncestryValues(Context& destination, const Context& source, std::span<const ClonedContext> contexts) {
			auto& ancestry = *destination.Ancestry;
			const auto& old_ancestry = *source.Ancestry;
			ancestry.m_RootAncestor = nullptr;
			ancestry.m_Composition = old_ancestry.m_Composition == nullptr ? nullptr : destination.Composition.get();
			ancestry.m_CompositionSerial = old_ancestry.m_CompositionSerial;
			ancestry.m_ContainingSerial = old_ancestry.m_ContainingSerial;
			ancestry.m_AncestrySerial = old_ancestry.m_AncestrySerial;
			ancestry.m_Composed = old_ancestry.m_Composed;
			for (std::size_t index = 0; index < ancestry.m_States.size(); ++index) {
				auto& state = ancestry.m_States[index];
				const auto& old = old_ancestry.m_States[index];
				state.m_Parent = CloneDeformation(old.m_Parent, contexts);
				// Native5056FC clears live ancestry links while retaining completed geometry.
				state.m_Ancestor = nullptr;
				state.m_Anchor = old.m_Anchor;
				state.m_Source = old.m_Source;
				if (state.m_Source && state.m_Source->BlankSource != nullptr) {
					state.m_Source->BlankSource = &destination.Sources->Source(static_cast<std::uint32_t>(index));
				}
				state.m_OwnLattice = old.m_OwnLattice;
				state.m_CombinedLattice = old.m_CombinedLattice;
				state.m_World = old.m_World;
				state.m_Active = old.m_Active;
				state.m_Export = old.m_Export;
				state.m_Stop = old.m_Stop;
				state.m_Changed = old.m_Changed;
			}
		}

		static void CopyDerivedValues(Context& destination, const Context& source, std::span<const ClonedContext> contexts) {
			CopyAncestryValues(destination, source, contexts);
			destination.Stencil->m_Composed = source.Stencil->m_Composed;
			for (std::size_t index = 0; index < destination.Stencil->m_States.size(); ++index) {
				destination.Stencil->m_States[index].m_Active = source.Stencil->m_States[index].m_Active;
				destination.Stencil->m_States[index].m_Ancestor = nullptr;
			}
			destination.Clips->m_Rectangles = source.Clips->m_Rectangles;
			destination.Clips->m_Composed = source.Clips->m_Composed;
			for (std::size_t index = 0; index < destination.Clips->m_Clips.size(); ++index) {
				destination.Clips->m_Clips[index] = nullptr;
			}
			auto& vertices = *destination.Vertices;
			const auto& old_vertices = *source.Vertices;
			for (std::size_t index = 0; index < vertices.m_Entries.size(); ++index) {
				auto& entry = vertices.m_Entries[index];
				const auto& old = old_vertices.m_Entries[index];
				entry.Positions = old.Positions;
				entry.Geometry = old.Geometry;
				entry.Geometry.Positions = entry.Positions;
				entry.Subdivision = old.Subdivision;
				entry.Division = old.Division;
				entry.Allocated = old.Allocated;
			}
			vertices.m_RowPreparationSerial = old_vertices.m_RowPreparationSerial;
			vertices.m_VertexSerial = old_vertices.m_VertexSerial;
			vertices.m_PreparationSerial = old_vertices.m_PreparationSerial;
			vertices.m_Composed = old_vertices.m_Composed;
			// Completed packet inputs survive the native row-list early return; geometry is not recomposed.
			destination.Rows->m_CollectedThisFrame = source.Scene.Published && source.Rows->m_CollectedThisFrame;
			for (const auto ordinal : destination.Selected.ShapeOrdinals()) {
				auto& shape = destination.Selected.Shape(ordinal - 1);
				const auto& original = source.Selected.Shape(ordinal - 1);
				shape.m_Composition = original.m_Composition == nullptr ? nullptr : destination.Composition.get();
			}
			destination.Rows->m_CollectedRows.clear();
			for (const auto* row : source.Rows->m_CollectedRows) {
				destination.Rows->m_CollectedRows.push_back(CloneRow(row, contexts));
			}
			destination.Rows->m_GlobalRows.clear();
			for (const auto* row : source.Rows->m_GlobalRows) {
				destination.Rows->m_GlobalRows.push_back(CloneRow(row, contexts));
			}
			for (std::size_t index = 0; index < destination.Rows->m_Rows.size(); ++index) {
				auto& row = destination.Rows->m_Rows[index];
				const auto& old = source.Rows->m_Rows[index];
				// Native5057xx clears these live row edges before any copied player's50AD30.
				row.m_Ancestor = nullptr;
				row.m_Members.clear();
				row.m_StencilType = old.m_StencilType;
				row.m_Scale = old.m_Scale;
				row.m_Offset = old.m_Offset;
				row.m_ControlEnabled = old.m_ControlEnabled;
				row.m_Published = old.m_Published;
			}
		}

		void CopyCompletedState(const Implementation& source) {
			source.Services.RequireUnqueued();
			if (!source.Root || source.Operating || source.Updating || !source.JoinedLayers.empty() || !source.JoinedChildren.empty()) {
				throw std::runtime_error("motion scene clone requires an idle actual selected graph; owning JOIN snapshots remain unrecovered");
			}
			std::vector<ClonedContext> contexts;
			contexts.reserve(Services.MaxContexts);
			CloneContextTree(*Root, *source.Root, contexts);
			for (const auto& context : contexts) {
				CopyDerivedValues(*context.Destination, *context.Source, contexts);
			}
			if (!source.Published) {
				return;
			}
			// Native504cf0 publishes50ad30 from copied completed state, without advancing or recomposing numeric/geometry.
			Root->RefreshRows();
			Root->CollectOwners(Owners);
			Packets = std::make_unique<MotionSelectedMeshMaskPackets>(Owners);
			for (const auto& context : contexts) {
				auto& actual = *context.Destination;
				actual.Rows->PrepareRecursiveFrame(actual.Composition->Layers());
				actual.Vertices->m_RowPreparationSerial = actual.Rows->m_PreparationSerial;
			}
			Root->PublishRows(true);
			std::size_t rows = 0;
			for (const auto& owner : Owners) {
				rows += owner.Rows.Size();
			}
			RowContexts.reserve(rows);
			Root->RegisterRows();
			PacketRowOwners.assign(rows, nullptr);
			for (const auto& owner : Owners) {
				for (std::size_t index = 0; index < owner.Rows.Size(); ++index) {
					const auto& row = owner.Rows.Row(index);
					PacketRowOwners.at(Packets->Index(row)) = &row;
				}
			}
			Packets->Build();
			GraphDirty = false;
			Published = true;
		}

		static void RetireJoinedChild(JoinedChild& child) noexcept {
			child.Phases.reset();
			if (child.Pending) {
				RetireNestedJoin(*child.Pending);
				child.Pending.reset();
			}
			child.Owners.second.reset();
			child.Owners.first.reset();
		}

		static void RetireJoinedChildren(std::vector<JoinedChild>& children) noexcept {
			// Native50fc00 retires unmatched player payloads in the path map's ascending key order.
			for (auto& child : children) {
				RetireJoinedChild(child);
			}
			children.clear();
		}

		static void RetireNestedJoin(NestedJoin& pending) noexcept {
			if (pending.Player != nullptr && pending.Lifetime && pending.Lifetime->IsLive()) {
				pending.Player->Commands().m_CaptureJoin = {};
			}
			pending.CaptureInstalled = false;
			pending.Layers.clear();
			RetireJoinedChildren(pending.Children);
		}

		void RetireJoinedChildren() noexcept {
			RetireJoinedChildren(JoinedChildren);
		}

		~Implementation() noexcept {
			RetireJoinedChildren();
		}

		Implementation(MotionSelectedLayerNodes& selected, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, MotionAssetCatalog& catalog, MotionManagerRuntime& runtime, MotionPlayerUpdate& actual_update, MotionSceneEvaluationServices services)
		    : Catalog(catalog), Runtime(runtime), ActualUpdate(actual_update), Services(std::move(services)) {
			if (!Services.ReadRegisteredAsset || (!Services.PrepareChildTimeline && !Services.PrepareOwnedChildTimeline) || (Services.PrepareChildTimeline && Services.PrepareOwnedChildTimeline) || !Services.RequireUnqueued || !Services.AppendDraw || !Services.DeliverEvent || Services.MaxContexts == 0 || Services.MaxContexts > 65536 || Services.MaxDepth == 0 || Services.MaxDepth > 64) {
				throw std::runtime_error("motion scene requires actual asset/preparation/queue/output/listener owners and bounded traversal");
			}
			Services.RequireUnqueued();
			Root = std::make_unique<Context>(*this, selected, commands, root, render, &actual_update, nullptr, nullptr, nullptr, 0);
			Owners.reserve(Services.MaxContexts);
			Catalogue.reserve(Services.MaxContexts);
		}

		void RequireRoot() const {
			if (!Root) {
				throw std::runtime_error("motion scene root is unavailable during actual owning selection replacement");
			}
			Root->RequireSelected();
		}

		static std::string LayerPath(const MotionSelectedLayerNodes& selected, std::uint32_t layer) {
			const auto nodes = selected.Nodes();
			if (layer >= nodes.size()) {
				throw std::runtime_error("motion JOIN path requires its actual selected node");
			}
			std::string path;
			std::size_t depth = 0;
			for (;;) {
				const auto& node = nodes[layer];
				if (++depth > 128 || node.Prepared->Label.size() > kMaximumMotionCommandNameBytes - path.size() || path.size() + node.Prepared->Label.size() == kMaximumMotionCommandNameBytes) {
					throw std::runtime_error("motion JOIN path exceeds its bounded native string domain");
				}
				path.insert(0, "/" + node.Prepared->Label);
				if (!node.Parent) {
					return path;
				}
				layer = *node.Parent;
			}
		}

		std::vector<MotionJoinedLayer> CaptureJoin(Context& actual, std::vector<JoinedChild>& joined_children) {
			actual.RequireSelected();
			const auto nodes = actual.Selected.Nodes();
			// Preflight every required owning variant before forcing any local cache.
			for (const auto& node : nodes) {
				if (!node.JoinTarget || node.Prepared->Type == 1) {
					continue;
				}
				if ((node.Prepared->Type != 0 && node.Prepared->Type != 2 && node.Prepared->Type != 3 && node.Prepared->Type != 7 && node.Prepared->Type != 12) || node.Prepared->MeshTransform != 0) {
					throw std::runtime_error("motion JOIN requires the actual mesh/other-kind snapshot transfer owner");
				}
				if (node.Prepared->Type == 3) {
					const auto layer = node.NativeOrdinal - 1;
					const auto* selected = actual.Selected.ChildNodes(layer);
					const auto* context = actual.Children.at(layer).get();
					const auto& child = actual.Selected.Child(layer);
					const bool retired_empty_facade = selected != nullptr && selected->Nodes().empty() && context == nullptr;
					if ((!retired_empty_facade && (selected == nullptr) != (context == nullptr)) || (context != nullptr && (&context->Selected != selected || context->Child != &child || &context->Commands != &child.Commands() || &context->Root != &child.Root()))) {
						throw std::runtime_error("motion JOIN child capture requires its actual completed selected phase owner");
					}
				}
			}
			std::vector<MotionJoinedLayer> result;
			result.reserve(nodes.size());
			for (const auto& node : nodes) {
				const auto layer = node.NativeOrdinal - 1;
				static_cast<void>(actual.Local->Evaluate(layer, true));
				if (!node.JoinTarget || node.Prepared->Type == 1) {
					continue;
				}
				MotionJoinedLayer saved;
				saved.Path = LayerPath(actual.Selected, layer);
				saved.Kind = node.Prepared->Type;
				saved.Mask = actual.Local->CurrentMask(layer);
				saved.Numeric = *actual.Local->Evaluation(layer).Sample;
				// Source/icon are not JOIN-restored and may not outlive the outgoing preparation.
				saved.Numeric.Source = {};
				saved.Numeric.Icon = {};
				const auto same = std::find_if(result.begin(), result.end(), [&](const auto& value) {
					return value.Path == saved.Path;
				});
				if (same == result.end()) {
					result.push_back(std::move(saved));
				} else {
					*same = std::move(saved);
				}
			}
			std::vector<JoinedChild> captured;
			captured.reserve(result.size());
			// Allocate every potential owning key, even when a later same-path nonplayer replaces it.
			for (const auto& node : nodes) {
				if (!node.JoinTarget || node.Prepared->Type != 3) {
					continue;
				}
				const auto path = LayerPath(actual.Selected, node.NativeOrdinal - 1);
				if (std::none_of(captured.begin(), captured.end(), [&](const auto& child) {
					    return child.Path == path;
				    })) {
					JoinedChild child;
					child.Path = path;
					captured.push_back(std::move(child));
				}
			}
			std::sort(captured.begin(), captured.end(), [](const auto& first, const auto& second) {
				return first.Path < second.Path;
			});
			struct ChildTransfer {
				std::uint32_t Layer = 0;
				std::size_t Capsule = 0;
				bool IsPlayer = false;
			};
			std::vector<ChildTransfer> transfers;
			transfers.reserve(nodes.size());
			for (const auto& node : nodes) {
				if (!node.JoinTarget || node.Prepared->Type == 1) {
					continue;
				}
				const auto path = LayerPath(actual.Selected, node.NativeOrdinal - 1);
				const auto found = std::find_if(captured.begin(), captured.end(), [&](const auto& child) {
					return child.Path == path;
				});
				if (found != captured.end()) {
					transfers.push_back({node.NativeOrdinal - 1, static_cast<std::size_t>(found - captured.begin()), node.Prepared->Type == 3});
				}
			}
			if (!transfers.empty()) {
				InvalidateGraph();
				actual.Selected.UnbindJoinedDescriptors();
				for (const auto& transfer : transfers) {
					JoinedChild incoming;
					if (transfer.IsPlayer) {
						incoming.Owners = actual.Selected.ExtractJoinedChild(transfer.Layer);
						// A real reset leaves only this retired facade; native5276E0 still moves its live root-only player.
						if (!actual.Children.at(transfer.Layer) && incoming.Owners.second && incoming.Owners.second->Nodes().empty()) {
							incoming.Owners.second.reset();
						}
						actual.Ancestry->m_States[transfer.Layer].m_Child = nullptr;
						incoming.Pending = std::move(actual.ChildJoins.at(transfer.Layer));
						incoming.Owners.first->Commands().m_CaptureJoin = {};
						if (incoming.Pending) {
							incoming.Pending->CaptureInstalled = false;
						}
						incoming.Phases = std::move(actual.Children.at(transfer.Layer));
						if (incoming.Phases) {
							incoming.Phases->Containing = nullptr;
							incoming.Phases->Ancestry->m_Containing = nullptr;
							incoming.Phases->Ancestry->m_RootAncestor = nullptr;
							incoming.Phases->Rows->m_ContainingRows = nullptr;
						}
					}
					auto& target = captured[transfer.Capsule];
					// Native captures the new payload before replacing/retiring the previous same-path value.
					RetireJoinedChild(target);
					target.Owners = std::move(incoming.Owners);
					target.Pending = std::move(incoming.Pending);
					target.Phases = std::move(incoming.Phases);
				}
			}
			std::erase_if(captured, [](const auto& child) {
				return !child.Owners.first;
			});
			RetireJoinedChildren(joined_children);
			joined_children = std::move(captured);
			return result;
		}

		std::vector<MotionJoinedLayer> CaptureJoin() {
			RequireRoot();
			return CaptureJoin(*Root, JoinedChildren);
		}

		std::uint64_t NextContextIdentity() {
			if (ContextIdentity == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("motion scene context identity is exhausted");
			}
			return ++ContextIdentity;
		}

		struct CatalogueEntry {
			Context* Actual = nullptr;
			// An empty native kind3 player still owns its genuine Commands/Root and contributes one root slot.
			const MotionPlayerCommands* Commands = nullptr;
			const MotionRootScene* Root = nullptr;
		};

		void AppendCatalogueChildren(Context& context) const {
			const auto first = Catalogue.size();
			for (const auto ordinal : context.Selected.ChildOrdinals()) {
				const auto& child = context.Children.at(ordinal - 1);
				if (Catalogue.size() == Services.MaxContexts) {
					throw std::runtime_error("motion layer getter catalogue exceeds actual player bound");
				}
				auto& actual_player = context.Selected.Child(ordinal - 1);
				if (!child) {
					const auto* actual_nodes = context.Selected.ChildNodes(ordinal - 1);
					if (actual_nodes != nullptr && !actual_nodes->Nodes().empty()) {
						throw std::runtime_error("motion getter catalogue requires the completed context for its actual selected child nodes");
					}
				}
				if (child) {
					child->RequireSelected();
					if (&child->Commands != &actual_player.Commands() || &child->Root != &actual_player.Root()) {
						throw std::runtime_error("motion getter catalogue differs from its exact actual kind3 player");
					}
				}
				Catalogue.push_back(CatalogueEntry{child.get(), &actual_player.Commands(), &actual_player.Root()});
			}
			// Native518cc0 appends all immediate players before visiting each one's descendants.
			const auto end = Catalogue.size();
			for (auto index = first; index != end; ++index) {
				if (Catalogue[index].Actual != nullptr) {
					AppendCatalogueChildren(*Catalogue[index].Actual);
				}
			}
		}

		void BuildCatalogue(const MotionPlayerCommands* actual_player = nullptr) const {
			RequireRoot();
			Catalogue.clear();
			Catalogue.push_back(CatalogueEntry{Root.get(), &Root->Commands, &Root->Root});
			AppendCatalogueChildren(*Root);
			if (actual_player != nullptr && actual_player != &Root->Commands) {
				const auto found = std::find_if(Catalogue.begin(), Catalogue.end(), [actual_player](const auto& entry) {
					return entry.Commands == actual_player;
				});
				if (found == Catalogue.end()) {
					throw std::runtime_error("motion layer getter scope is not an actual current child player");
				}

				// Native516B90 starts its catalogue at the receiver plain player, including a root-only child.
				const auto first = *found;
				Catalogue.clear();
				Catalogue.push_back(first);
				if (first.Actual != nullptr) {
					AppendCatalogueChildren(*first.Actual);
				}
			}
		}

		std::optional<MotionSceneLayerReference> FindLayer(std::string_view name, const MotionPlayerCommands* actual_player) const {
			BuildCatalogue(actual_player);
			std::uint32_t base = 0;
			for (const auto& entry : Catalogue) {
				const auto* context = entry.Actual;
				const auto nodes = context == nullptr ? std::span<const MotionSelectedLayerNode>() : context->Selected.Nodes();
				// Native516b90 skips map lookup at a root-only player, but always counts its root for later offsets.
				const auto ordinal = nodes.empty() ? std::nullopt : ResolveMotionChildTargetIndex(*context->Timeline, name, nodes.size());
				if (ordinal) {
					if (*ordinal > std::numeric_limits<std::uint32_t>::max() - base) {
						throw std::runtime_error("motion layer getter global ordinal exceeds its native word");
					}
					MotionSceneLayerReference result;
					result.Owner = this;
					result.RootSelectionSerial = Root->SelectionSerial;
					result.GraphGeneration = Generation;
					result.ContextIdentity = context->Identity;
					result.ContextSelectionSerial = context->SelectionSerial;
					result.NativeOrdinal = *ordinal;
					result.GlobalOrdinal = base + *ordinal;
					result.NodeIdentifier = *ordinal == 0 ? 0 : nodes[*ordinal - 1].Identifier;
					return result;
				}
				if (nodes.size() + 1 > std::numeric_limits<std::uint32_t>::max() - base) {
					throw std::runtime_error("motion layer getter global ordinal exceeds its native word");
				}
				base += static_cast<std::uint32_t>(nodes.size() + 1);
			}
			return std::nullopt;
		}

		const MotionSelectedLayerNode& ResolveLayer(const MotionSceneLayerReference& reference) const {
			RequireRoot();
			if (reference.Owner != this || reference.RootSelectionSerial != Root->SelectionSerial) {
				throw std::runtime_error("motion layer getter belongs to a retired actual root selection");
			}
			BuildCatalogue();
			for (const auto& entry : Catalogue) {
				const auto* context = entry.Actual;
				if (context == nullptr || context->Identity != reference.ContextIdentity) {
					continue;
				}
				if (reference.NativeOrdinal == 0) {
					throw std::runtime_error("motion layer getter separate-root type/shape payload remains unrecovered");
				}
				const auto nodes = context->Selected.Nodes();
				if (context->SelectionSerial != reference.ContextSelectionSerial || reference.NativeOrdinal > nodes.size() || nodes[reference.NativeOrdinal - 1].Identifier != reference.NodeIdentifier) {
					throw std::runtime_error("motion layer getter target differs from its actual selected identity");
				}
				return nodes[reference.NativeOrdinal - 1];
			}
			throw std::runtime_error("motion layer getter target context has retired");
		}

		bool PlayerContains(const Context& context, resource::MotionPoint point) const {
			context.RequireSelected();
			// Native510780 searches the actual player catalogue, including emitted players. Its particle catalogue producer remains separate.
			for (const auto ordinal : context.Selected.ParticleOrdinals()) {
				if (context.Selected.Particles(ordinal - 1).Size() != 0) {
					throw std::runtime_error("motion child hit test requires its actual emitted-player shape catalogue");
				}
			}
			for (const auto ordinal : context.Selected.ShapeOrdinals()) {
				if (context.Selected.Shape(ordinal - 1).Snapshot().Contains(point)) {
					return true;
				}
			}
			for (const auto ordinal : context.Selected.ChildOrdinals()) {
				const auto& child = context.Children.at(ordinal - 1);
				if (child) {
					if (PlayerContains(*child, point)) {
						return true;
					}
				} else if (const auto* nodes = context.Selected.ChildNodes(ordinal - 1); nodes != nullptr && !nodes->Nodes().empty()) {
					throw std::runtime_error("motion child hit test requires its actual completed selected context");
				}
			}

			return false;
		}

		bool LayerContains(const Context& context, std::uint32_t layer, resource::MotionPoint point) const {
			struct PendingLayer {
				std::uint32_t Layer = 0;
				bool Recursive = false;
			};
			const auto nodes = context.Selected.Nodes();
			std::vector<PendingLayer> pending{{layer, false}};
			while (!pending.empty()) {
				const auto query = pending.back();
				pending.pop_back();
				const auto type = nodes[query.Layer].Prepared->Type;
				if (type == 1 && context.Selected.Shape(query.Layer).Snapshot().Contains(point)) {
					return true;
				}
				if (type == 3) {
					const auto& child = context.Children.at(query.Layer);
					if (child) {
						if (PlayerContains(*child, point)) {
							return true;
						}
					} else if (const auto* selected = context.Selected.ChildNodes(query.Layer); selected != nullptr && !selected->Nodes().empty()) {
						throw std::runtime_error("motion layer hit test requires its actual completed child context");
					}
				}
				// Native2741F0 always enters folders; other nodes descend only within an existing recursive search.
				if (type == 2 || query.Recursive) {
					for (std::size_t index = nodes.size(); index != 0; --index) {
						if (nodes[index - 1].Parent == query.Layer) {
							pending.push_back({static_cast<std::uint32_t>(index - 1), true});
						}
					}
				}
			}

			return false;
		}

		const LoadedMotionAsset& ReadAsset(MotionProjectId project) const {
			const auto& asset = Services.ReadRegisteredAsset(project);
			if (!Runtime.Manager().Contains(project) || Runtime.Manager().Identity(project) != asset.Identity || !Catalog.ContainsPrepared(asset) || asset.Model.get() != &Runtime.Manager().Resource(project)) {
				throw std::runtime_error("motion scene source borrow differs from its actual registered catalog/manager owner");
			}
			return asset;
		}

		void InvalidateGraph() {
			Services.RequireUnqueued();
			if (Generation == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("motion scene graph generation is exhausted");
			}
			Packets.reset();
			Owners.clear();
			RowContexts.clear();
			PacketRowOwners.clear();
			if (Root) {
				Root->InvalidateRows();
			}
			Published = false;
			GraphDirty = true;
			++Generation;
		}

		void Publish(const MotionPlayerFrameContext& frame) {
			RequireRoot();
			if (frame.Timeline != Root->Timeline) {
				throw std::runtime_error("motion scene publication requires its actual root task phase");
			}
			Published = false;
			const bool rebuilding = GraphDirty;
			if (rebuilding) {
				Root->RefreshRows();
				Owners.clear();
				Root->CollectOwners(Owners);
				Packets = std::make_unique<MotionSelectedMeshMaskPackets>(Owners);
				Root->PrepareRows();
			}
			Root->PublishRows();
			if (rebuilding) {
				std::size_t rows = 0;
				for (const auto& owner : Owners) {
					rows += owner.Rows.Size();
				}
				RowContexts.reserve(rows);
				Root->RegisterRows();
				PacketRowOwners.assign(rows, nullptr);
				for (const auto& owner : Owners) {
					for (std::size_t index = 0; index != owner.Rows.Size(); ++index) {
						const auto& row = owner.Rows.Row(index);
						const auto packet_index = Packets->Index(row);
						if (packet_index >= PacketRowOwners.size() || PacketRowOwners[packet_index] != nullptr) {
							throw std::runtime_error("motion scene packet row map is not an exact unique bound index");
						}
						PacketRowOwners[packet_index] = &row;
					}
				}
				if (std::any_of(PacketRowOwners.begin(), PacketRowOwners.end(), [](const auto* row) {
					    return row == nullptr;
				    })) {
					throw std::runtime_error("motion scene packet row map omits an actual persistent row");
				}
			}
			Packets->Build();
			GraphDirty = false;
			Published = true;
		}

		void Draw() {
			RequireRoot();
			if (!Published || !Packets) {
				throw std::runtime_error("motion scene drawing requires genuinely completed packet/row publication");
			}
			if (Root->ClonedPublicationPending) {
				return;
			}

			Services.AppendDraw(*Packets, Root->Rows->GlobalRows());
		}

		MotionAssetCatalog& Catalog;
		MotionManagerRuntime& Runtime;
		MotionPlayerUpdate& ActualUpdate;
		MotionSceneEvaluationServices Services;
		std::vector<MotionJoinedLayer> JoinedLayers;
		std::size_t ContextCount = 0;
		std::uint64_t ContextIdentity = 0;
		mutable std::vector<CatalogueEntry> Catalogue;
		std::uint64_t Generation = 1;
		bool GraphDirty = true;
		bool Published = false;
		bool Operating = false;
		bool Updating = false;
		std::unique_ptr<Context> Root;
		// Owning snapshots survive outgoing root destruction inside this stable evaluator implementation.
		std::vector<JoinedChild> JoinedChildren;
		std::vector<MotionMeshMaskOwner> Owners;
		std::unordered_map<const MotionStencilRowState*, Context*> RowContexts;
		std::vector<const MotionStencilRowState*> PacketRowOwners;
		// Packet bindings retire before every context/row/vertex borrower during destruction.
		std::unique_ptr<MotionSelectedMeshMaskPackets> Packets;
	};

	MotionSceneEvaluation::MotionSceneEvaluation(MotionSelectedLayerNodes& selected, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, MotionAssetCatalog& catalog, MotionManagerRuntime& runtime, MotionPlayerUpdate& actual_root_update, MotionSceneEvaluationServices services)
	    : m_Implementation(std::make_unique<Implementation>(selected, commands, root, render, catalog, runtime, actual_root_update, std::move(services))) {
	}

	MotionSceneEvaluation::MotionSceneEvaluation(MotionSelectedLayerNodes& selected, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, MotionAssetCatalog& catalog, MotionManagerRuntime& runtime, MotionPlayerUpdate& actual_root_update, MotionSceneEvaluationServices services, const MotionSceneEvaluation& source)
	    : MotionSceneEvaluation(selected, commands, root, render, catalog, runtime, actual_root_update, std::move(services)) {
		m_Implementation->CopyCompletedState(*source.m_Implementation);
	}

	MotionSceneEvaluation::~MotionSceneEvaluation() noexcept {
		if (!CanClose()) {
			std::terminate();
		}
	}

	std::vector<MotionJoinedLayer> MotionSceneEvaluation::CaptureJoin() {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		if (state.Updating) {
			throw std::runtime_error("motion JOIN capture cannot reenter an actual task update");
		}
		const MotionScenePhaseScope operation(state.Operating);
		return state.CaptureJoin();
	}

	void MotionSceneEvaluation::RetireRootForRebuild() {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		if (state.Updating) {
			throw std::runtime_error("motion root retirement cannot interrupt an actual update");
		}
		const MotionScenePhaseScope operation(state.Operating);
		if (state.Root) {
			state.InvalidateGraph();
			state.Root.reset();
		}
		state.Catalogue.clear();
	}

	void MotionSceneEvaluation::RebuildRoot(MotionSelectedLayerNodes& selected, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render) {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		if (state.Root || state.Updating) {
			throw std::runtime_error("motion root rebuild requires the retired old root and actual live resource owners");
		}
		const MotionScenePhaseScope operation(state.Operating);
		// The new context validates its exact selected resource and complete current source/alias snapshot.
		state.Root = std::make_unique<Implementation::Context>(state, selected, commands, root, render, &state.ActualUpdate, nullptr, nullptr, nullptr, 0);
		state.GraphDirty = true;
		state.Published = false;
	}

	void MotionSceneEvaluation::RestoreJoin(std::vector<MotionJoinedLayer> layers) {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		if (state.Operating || state.Updating || state.Published || (!layers.empty() && !state.JoinedLayers.empty())) {
			throw std::runtime_error("motion JOIN restore requires the newly installed idle selected owner");
		}
		// Native51c0a0 exits before clearing pending snapshots when the new clock is still TickAssigned.
		if (!layers.empty()) {
			state.JoinedLayers = std::move(layers);
		}
	}

	void MotionSceneEvaluation::PrepareFrames(MotionFrameTraversal traversal, const MotionPlayerFrameContext& frame) {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		const MotionScenePhaseScope operation(state.Operating);
		state.Published = false;
		state.RequireRoot();
		state.Root->Select(traversal, frame);
	}

	void MotionSceneEvaluation::Compose(const MotionPlayerFrameContext& frame) {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		const MotionScenePhaseScope operation(state.Operating);
		state.Published = false;
		state.RequireRoot();
		state.Root->Compose(frame);
	}

	void MotionSceneEvaluation::Publish(const MotionPlayerFrameContext& frame) {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		const MotionScenePhaseScope operation(state.Operating);
		try {
			state.Publish(frame);
		} catch (...) {
			const auto failure = std::current_exception();
			state.InvalidateGraph();
			std::rethrow_exception(failure);
		}
	}

	void MotionSceneEvaluation::Draw() {
		auto& state = *m_Implementation;
		const MotionScenePhaseScope operation(state.Operating);
		state.Draw();
	}

	void MotionSceneEvaluation::Update() {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		state.RequireRoot();
		const MotionScenePhaseScope update(state.Updating);
		try {
			state.ActualUpdate.Update();
			state.RequireRoot();
		} catch (...) {
			state.Published = false;
			throw;
		}
	}

	void MotionSceneEvaluation::Progress(float elapsed_ticks) {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		state.RequireRoot();
		const MotionScenePhaseScope update(state.Updating);
		try {
			state.ActualUpdate.Progress(elapsed_ticks);
			state.RequireRoot();
		} catch (...) {
			state.Published = false;
			throw;
		}
	}

	void MotionSceneEvaluation::PostUpdate() {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		state.RequireRoot();
		const MotionScenePhaseScope update(state.Updating);
		try {
			state.ActualUpdate.PostUpdate();
			state.RequireRoot();
		} catch (...) {
			state.Published = false;
			throw;
		}
	}

	bool MotionSceneEvaluation::CanClose() const noexcept {
		const auto& state = *m_Implementation;
		if (state.Operating || state.Updating) {
			return false;
		}
		try {
			state.Services.RequireUnqueued();
		} catch (...) {
			return false;
		}
		return true;
	}

	bool MotionSceneEvaluation::UsesRuntime(const MotionManagerRuntime& runtime) const noexcept {
		return &m_Implementation->Runtime == &runtime;
	}

	const MotionSelectedMeshMaskPackets& MotionSceneEvaluation::Packets() const {
		const auto& state = *m_Implementation;
		state.RequireRoot();
		if (!state.Published || !state.Packets) {
			throw std::runtime_error("motion scene packet borrow requires actual completed publication");
		}
		return *state.Packets;
	}

	std::span<const MotionStencilRowState* const> MotionSceneEvaluation::GlobalRows() const {
		static_cast<void>(Packets());
		return m_Implementation->Root->Rows->GlobalRows();
	}

	std::span<const MotionStencilRowState* const> MotionSceneEvaluation::PacketRows() const {
		const auto& packets = Packets();
		const auto& rows = m_Implementation->PacketRowOwners;
		if (packets.Nodes().size() != rows.size()) {
			throw std::runtime_error("motion scene packet row borrow differs from its current actual packet graph");
		}

		return rows;
	}

	MotionSceneDrawState MotionSceneEvaluation::DrawState(const MotionStencilRowState& row) const {
		static_cast<void>(Packets());
		const auto& state = *m_Implementation;
		const auto found = state.RowContexts.find(&row);
		if (found == state.RowContexts.end()) {
			throw std::runtime_error("motion scene draw row is outside its actual current packet graph");
		}
		const auto& context = *found->second;
		const auto& node = row.Node();
		const auto layer = node.NativeOrdinal - 1;
		if (layer >= context.Selected.Nodes().size() || &context.Selected.Nodes()[layer] != &node) {
			throw std::runtime_error("motion scene draw metadata differs from its exact selected row");
		}
		MotionSceneDrawState result;
		result.Source = context.CurrentSources[layer];
		result.Numeric = &context.Composition->Layers()[layer];
		result.IndividualClip = context.Clips->Clips()[layer];
		result.Render = &context.Render;
		result.RequestingProject = context.Sources->RequestingProject();
		result.RootPriority = state.Root->Root.Priority();
		if (context.Vertices->Allocated(layer)) {
			result.Subdivision = context.Vertices->Subdivision(layer);
		}
		return result;
	}

	std::optional<MotionSceneLayerReference> MotionSceneEvaluation::FindLayer(std::string_view name) const {
		return m_Implementation->FindLayer(name, nullptr);
	}

	std::optional<MotionSceneLayerReference> MotionSceneEvaluation::FindLayer(std::string_view name, const MotionPlayerCommands& actual_player) const {
		return m_Implementation->FindLayer(name, &actual_player);
	}

	std::uint32_t MotionSceneEvaluation::LayerType(const MotionSceneLayerReference& reference) const {
		return m_Implementation->ResolveLayer(reference).Prepared->Type;
	}

	std::vector<MotionSceneLayerReference> MotionSceneEvaluation::LayerChildren(const MotionSceneLayerReference& reference) const {
		const auto& state = *m_Implementation;
		static_cast<void>(state.ResolveLayer(reference));
		for (const auto& entry : state.Catalogue) {
			const auto* context = entry.Actual;
			if (context == nullptr || context->Identity != reference.ContextIdentity) {
				continue;
			}
			std::vector<MotionSceneLayerReference> children;
			const auto nodes = context->Selected.Nodes();
			for (const auto& node : nodes) {
				// Native5162f0 scans +38 relative-parent links in the receiver's physical player only.
				if (!node.Parent || *node.Parent != reference.NativeOrdinal - 1) {
					continue;
				}
				auto child = reference;
				child.NativeOrdinal = node.NativeOrdinal;
				child.GlobalOrdinal = reference.GlobalOrdinal - reference.NativeOrdinal + node.NativeOrdinal;
				child.NodeIdentifier = node.Identifier;
				children.push_back(child);
			}
			return children;
		}
		throw std::runtime_error("motion children getter context is no longer current");
	}

	const MotionChildConstructionState& MotionSceneEvaluation::LayerChild(const MotionSceneLayerReference& reference) const {
		auto& state = *m_Implementation;
		const auto& node = state.ResolveLayer(reference);
		if (node.Prepared->Type != 3) {
			throw std::runtime_error("motion layer child borrow requires an actual kind3 player");
		}
		for (const auto& context : state.Catalogue) {
			if (context.Actual != nullptr && context.Actual->Identity == reference.ContextIdentity) {
				return context.Actual->Selected.Child(reference.NativeOrdinal - 1);
			}
		}
		throw std::runtime_error("motion child borrow target context has retired");
	}

	std::array<float, 3> MotionSceneEvaluation::LayerCoordinate(const MotionSceneLayerReference& reference) const {
		const auto& state = *m_Implementation;
		static_cast<void>(state.ResolveLayer(reference));
		for (const auto& entry : state.Catalogue) {
			const auto* context = entry.Actual;
			if (context != nullptr && context->Identity == reference.ContextIdentity) {
				return context->Composition->RetainedCoordinate(reference.NativeOrdinal - 1);
			}
		}
		throw std::runtime_error("motion coordinate getter context is no longer current");
	}

	MotionLayerVisibility MotionSceneEvaluation::LayerVisibility(const MotionSceneLayerReference& reference) const {
		const auto& state = *m_Implementation;
		static_cast<void>(state.ResolveLayer(reference));
		for (const auto& entry : state.Catalogue) {
			const auto* context = entry.Actual;
			if (context != nullptr && context->Identity == reference.ContextIdentity) {
				return context->Composition->RetainedVisibility(reference.NativeOrdinal - 1);
			}
		}
		throw std::runtime_error("motion visibility getter context is no longer current");
	}

	const MotionComposedLayer& MotionSceneEvaluation::LayerNumeric(const MotionSceneLayerReference& reference) const {
		const auto& state = *m_Implementation;
		static_cast<void>(state.ResolveLayer(reference));
		for (const auto& entry : state.Catalogue) {
			const auto* context = entry.Actual;
			if (context != nullptr && context->Identity == reference.ContextIdentity) {
				return context->Composition->RetainedLayer(reference.NativeOrdinal - 1);
			}
		}
		throw std::runtime_error("motion numeric getter context is no longer current");
	}

	MotionLayerOverrides MotionSceneEvaluation::LayerOverrides(const MotionSceneLayerReference& reference) const {
		const auto& state = *m_Implementation;
		static_cast<void>(state.ResolveLayer(reference));
		for (const auto& entry : state.Catalogue) {
			const auto* context = entry.Actual;
			if (context != nullptr && context->Identity == reference.ContextIdentity) {
				return context->Composition->Overrides(reference.NativeOrdinal - 1);
			}
		}
		throw std::runtime_error("motion override getter context is no longer current");
	}

	void MotionSceneEvaluation::SetLayerOverrides(const MotionSceneLayerReference& reference, const MotionLayerOverrides& overrides, bool force_dirty) {
		auto& state = *m_Implementation;
		state.Services.RequireUnqueued();
		static_cast<void>(state.ResolveLayer(reference));
		for (const auto& entry : state.Catalogue) {
			auto* context = entry.Actual;
			if (context != nullptr && context->Identity == reference.ContextIdentity) {
				context->Composition->SetOverrides(reference.NativeOrdinal - 1, overrides, force_dirty);
				return;
			}
		}
		throw std::runtime_error("motion override setter context is no longer current");
	}

	MotionShapeSnapshot MotionSceneEvaluation::Shape(const MotionSceneLayerReference& reference) const {
		const auto& state = *m_Implementation;
		const auto& node = state.ResolveLayer(reference);
		if (node.Prepared->Type != 1) {
			throw std::runtime_error("motion shape getter requires the actual type1 selected node");
		}
		for (const auto& entry : state.Catalogue) {
			const auto* context = entry.Actual;
			if (context != nullptr && context->Identity == reference.ContextIdentity) {
				return context->Selected.Shape(reference.NativeOrdinal - 1).Snapshot();
			}
		}
		throw std::runtime_error("motion shape getter context is no longer current");
	}

	bool MotionSceneEvaluation::Contains(const MotionSceneLayerReference& reference, resource::MotionPoint point) const {
		const auto& state = *m_Implementation;
		static_cast<void>(state.ResolveLayer(reference));
		for (const auto& entry : state.Catalogue) {
			const auto* context = entry.Actual;
			if (context != nullptr && context->Identity == reference.ContextIdentity) {
				return state.LayerContains(*context, reference.NativeOrdinal - 1, point);
			}
		}
		throw std::runtime_error("motion layer hit test context is no longer current");
	}

	std::uint64_t MotionSceneEvaluation::GraphGeneration() const noexcept {
		return m_Implementation->Generation;
	}

} // namespace sgre::scene
