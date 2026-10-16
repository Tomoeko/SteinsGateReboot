#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include "sgre/scene/motion_clone_resource.hpp"

#include "sgre/scene/motion_timeline_variables.hpp"

#include "sgre/scene/motion_layer_parameters.hpp"
#include "sgre/scene/motion_parameter_values.hpp"

#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_particle_layer_state.hpp"
#include "sgre/scene/motion_shape_owner.hpp"
#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::scene {

	static constexpr std::size_t kMaximumSelectedLayers = 65536;
	static constexpr std::size_t kMaximumSelectedFrames = 1000000;
	static constexpr std::size_t kMaximumSelectedDepth = 64;

	struct MotionSelectedLayerNodes::Owner {
		MotionLayerIdentifier Identifier;
		bool IdentifierLive = false;
		bool UidRegistered = false;
		std::unique_ptr<MotionChildConstructionState> Child;
		std::unique_ptr<MotionParticleLayerState> Particles;
		std::unique_ptr<MotionChildFrameSlots> Slots;
		std::unique_ptr<MotionSelectedLayerNodes> Selected;
		std::unique_ptr<MotionStencilMaskOwner> Stencil;
		std::unique_ptr<MotionLayerMeshLocalOwner> MeshLocal;
		std::unique_ptr<MotionLayerShapeOwner> Shape;
	};

	static bool EmptySelectedArrayMetadata(const resource::PsbValue& value) {
		if (const auto* array = std::get_if<resource::PsbArray>(&value.Data)) {
			return array->empty();
		}

		return std::holds_alternative<std::monostate>(value.Data);
	}

	static const resource::PsbValue* FindSelectedProperty(const resource::MotionLayer& layer, std::string_view name) {
		for (const auto& field : layer.Properties) {
			if (field.first == name) {
				return &field.second;
			}
		}

		return nullptr;
	}

	static std::uint32_t ReadSelectedInteger(const resource::MotionLayer& layer, std::string_view name) {
		const auto* property = FindSelectedProperty(layer, name);
		const auto* integer = property == nullptr ? nullptr : std::get_if<std::int64_t>(&property->Data);
		if (integer == nullptr || *integer < std::numeric_limits<std::int32_t>::min() || *integer > std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("motion selected node requires bounded 32-bit property " + std::string(name));
		}

		return static_cast<std::uint32_t>(*integer);
	}

	static bool ReadSelectedBoolean(const resource::PsbValue& property, std::string_view name) {
		if (const auto* boolean = std::get_if<bool>(&property.Data)) {
			return *boolean;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&property.Data)) {
			return *integer != 0;
		}
		if (const auto* real = std::get_if<double>(&property.Data)) {
			return *real != 0;
		}

		throw std::runtime_error("motion selected node requires numeric boolean property " + std::string(name));
	}

	static bool ReadRequiredSelectedBoolean(const resource::MotionLayer& layer, std::string_view name) {
		const auto* property = FindSelectedProperty(layer, name);
		if (property == nullptr) {
			throw std::runtime_error("motion selected node requires present numeric boolean property " + std::string(name));
		}

		if (const auto* real = std::get_if<double>(&property->Data); name == "joinTarget" && real != nullptr && !std::isfinite(*real)) {
			throw std::runtime_error("motion selected node requires finite numeric boolean property joinTarget");
		}

		return ReadSelectedBoolean(*property, name);
	}

	static void RequireFalseSelectedProperty(const resource::MotionLayer& layer, std::string_view name) {
		if (ReadRequiredSelectedBoolean(layer, name)) {
			throw std::runtime_error("motion selected node requires recovered false " + std::string(name) + " owner");
		}
	}

	static void ValidateSelectedLayer(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, float priority) {
		if (layer.Type != 0 && layer.Type != 1 && layer.Type != 2 && layer.Type != 3 && layer.Type != 4 && layer.Type != 7 && layer.Type != 12) {
			throw std::runtime_error("motion selected node kind " + std::to_string(layer.Type) + " label " + layer.Label + " requires its genuine unrecovered owner");
		}
		if (layer.Label != prepared.Label || layer.Type != prepared.Type || layer.InheritMask != prepared.InheritMask || layer.Coordinate != prepared.CoordinatePlane || layer.Coordinate > 1 || layer.TransformOrder.size() != prepared.TransformOrder.size()) {
			throw std::runtime_error("motion selected node identity differs from its preparation");
		}
		for (std::size_t index = 0; index < layer.TransformOrder.size(); ++index) {
			if (layer.TransformOrder[index] > 3 || layer.TransformOrder[index] != static_cast<std::uint32_t>(prepared.TransformOrder[index])) {
				throw std::runtime_error("motion selected node transform order differs from its preparation");
			}
		}
		if (layer.Label.size() > kMaximumMotionCommandNameBytes || layer.Label.find('\0') != std::string::npos) {
			throw std::runtime_error("motion selected node label exceeds its native string domain");
		}
		const bool supported_stencil = prepared.StencilType == 0 || layer.Type == 12 || (layer.Type == 0 && prepared.StencilType <= 3) || (layer.Type == 3 && prepared.StencilType == 5);
		if (ReadSelectedInteger(layer, "meshTransform") != prepared.MeshTransform || (prepared.MeshTransform != 0 && !(layer.Type == 0 && prepared.MeshTransform == 1)) || !supported_stencil) {
			throw std::runtime_error("motion selected node mesh/stencil requires its genuine unrecovered owner");
		}
		if (prepared.MeshTransform == 1) {
			if (ReadSelectedInteger(layer, "meshSyncChildMask") != prepared.MeshSyncChildMask || ReadSelectedInteger(layer, "meshDivision") != prepared.MeshDivision || ReadRequiredSelectedBoolean(layer, "meshCombine") != prepared.MeshCombine || FindSelectedProperty(layer, "meshCombinator") != nullptr || prepared.MeshCombinator) {
				throw std::runtime_error("motion selected mesh settings differ from the actual ordinary bicubic owner");
			}
		}
		const auto* modifier = FindSelectedProperty(layer, "hasColorModifier");
		const auto rotation = FindSelectedProperty(layer, "rotateTransition") == nullptr ? 0 : ReadSelectedInteger(layer, "rotateTransition");
		if ((modifier != nullptr && ReadSelectedBoolean(*modifier, "hasColorModifier")) != prepared.HasColorModifier || rotation > 1 || rotation != static_cast<std::uint32_t>(prepared.Rotation)) {
			throw std::runtime_error("motion selected node modifier/rotation differs from its bounded preparation");
		}
		if (const auto* stencil = FindSelectedProperty(layer, "stencilType"); (stencil == nullptr && prepared.StencilType != 0) || (stencil != nullptr && ReadSelectedInteger(layer, "stencilType") != prepared.StencilType)) {
			throw std::runtime_error("motion selected node stencil requires its genuine unrecovered owner");
		}
		const auto* parameter = FindSelectedProperty(layer, "parameterize");
		if (parameter == nullptr) {
			throw std::runtime_error("motion selected node parameter requires its genuine unrecovered owner");
		}
		if (const auto* uid = FindSelectedProperty(layer, "uid")) {
			const auto* text = std::get_if<std::string>(&uid->Data);
			if (text == nullptr || text->size() > kMaximumMotionCommandNameBytes) {
				throw std::runtime_error("motion selected node UID requires a bounded native string");
			}
		}
		// Native514a04 records eligibility. Ordinary child selection does not consume it; JOIN capture remains a separate required producer.
		static_cast<void>(ReadRequiredSelectedBoolean(layer, "joinTarget"));
		RequireFalseSelectedProperty(layer, "groundCorrection");
		if (layer.Frames.size() != prepared.Frames.size() || layer.Frames.size() > kMaximumMotionChildSlotFrames) {
			throw std::runtime_error("motion selected node frame count differs from its bounded preparation");
		}
		for (std::size_t index = 0; index < layer.Frames.size(); ++index) {
			const auto type = layer.Frames[index].Type;
			if (!std::isfinite(layer.Frames[index].Time) || layer.Frames[index].Time != prepared.Frames[index].Time || (type != 0 && type != 2 && type != 3) || prepared.Frames[index].Hidden != (type == 0) || prepared.Frames[index].Tween != (type == 3)) {
				throw std::runtime_error("motion selected node frame metadata requires a matching recovered preparation");
			}
		}
		if (layer.Type == 0) {
			static_cast<void>(ReadSelectedInteger(layer, "objTriPriority"));
		}
		if (layer.Type == 3) {
			static_cast<void>(PrepareMotionChildRootConfiguration(layer, prepared, priority));
		}
	}

	MotionSelectedLayerNodes::MotionSelectedLayerNodes(const MotionPlayerCommands& commands, const MotionRootScene& root, const MotionPlayerRenderControls& render, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots)
	    : MotionSelectedLayerNodes(commands, root, render, nullptr, nullptr, timeline, preparation, identifiers, std::move(construction), std::move(slots), 0) {
	}

	MotionSelectedLayerNodes::MotionSelectedLayerNodes(MotionChildConstructionState& owner, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots)
	    : MotionSelectedLayerNodes(owner.Commands(), owner.Root(), owner.RenderControls(), &owner, nullptr, timeline, preparation, identifiers, std::move(construction), std::move(slots), 0) {
	}

	MotionSelectedLayerNodes::MotionSelectedLayerNodes(MotionParticlePlayerState& owner, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots)
	    : MotionSelectedLayerNodes(owner.Commands(), owner.Root(), owner.RenderControls(), nullptr, &owner, timeline, preparation, identifiers, std::move(construction), std::move(slots), 0) {
	}

	MotionSelectedLayerNodes::MotionSelectedLayerNodes(const MotionPlayerCommands& commands, const MotionRootScene& root, const MotionPlayerRenderControls& render, MotionChildConstructionState* owner, MotionParticlePlayerState* particle, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots, std::size_t owner_depth)
	    : m_Commands(commands), m_Root(root), m_Render(render), m_EnclosingChild(owner), m_EnclosingParticle(particle), m_Timeline(timeline), m_Preparation(preparation), m_Identifiers(identifiers), m_Construction(std::move(construction)), m_Slots(std::move(slots)), m_OwnerDepth(owner_depth) {
		m_CommandSelectionSerial = commands.SelectionSerial();
		if (commands.CachedTimeline() != &timeline || !commands.SelectedProject() || commands.SelectedProject()->Timeline != &timeline) {
			throw std::runtime_error("motion selected nodes require the enclosing player's actual selected timeline");
		}
		if (timeline.Kind != resource::MotionTimelineKind::Frames || !std::isfinite(timeline.LastTime) || timeline.LastTime < 0 || timeline.LastTime != preparation.LastTime || timeline.LoopTime != preparation.LoopTime || preparation.Layers.size() > kMaximumSelectedLayers || preparation.Layers.size() != preparation.Parents.size() || owner_depth >= kMaximumSelectedDepth) {
			throw std::runtime_error("motion selected nodes require bounded matching ordinary timeline preparation");
		}
		if ((!EmptySelectedArrayMetadata(timeline.Variables) && (commands.TimelineVariables() == nullptr || !commands.TimelineVariables()->Uses(commands, timeline))) || !std::holds_alternative<std::monostate>(timeline.Stream.Data)) {
			throw std::runtime_error("motion selected nodes require actual variable/parameter/packed owners");
		}

		m_Parameters = std::make_unique<MotionTimelineParameters>(commands.ParameterValues(), timeline);
		if (!std::holds_alternative<std::monostate>(timeline.Parameterization.Data)) {
			const auto* registered = commands.ParameterValues().CurrentTimelineParameters(timeline);
			if (registered != m_Parameters.get() || registered->TimelineParameter() == nullptr) {
				throw std::runtime_error("motion selected whole-timeline parameter requires its exact live registered controller");
			}
		}
		m_ParameterBindings = std::make_unique<MotionLayerParameterBindings>(*m_Parameters);

		std::size_t index = 0;
		std::size_t frames = 0;
		ValidateLayers(timeline.Layers, std::nullopt, 0, index, frames);
		if (index != preparation.Layers.size()) {
			throw std::runtime_error("motion selected hierarchy differs from its flattened preparation");
		}
		m_ExternalSources.resize(index);
		m_Nodes.reserve(index);
		m_Owners.reserve(index);
		m_ChildOrdinals.reserve(index);
		m_ParticleOrdinals.reserve(index);
		m_ShapeOrdinals.reserve(index);

		try {
			index = 0;
			ConstructLayers(timeline.Layers, std::nullopt, index);
			m_Commands.ParameterValues().BindSelected(*this);
			FinalizeNodes();
			m_Published = true;
			for (auto& actual_owner : m_Owners) {
				if (actual_owner->Stencil) {
					actual_owner->Stencil->BindMaskLayers(*this);
				}
			}
		} catch (...) {
			Retire();
			throw;
		}
	}

	MotionSelectedLayerNodes::MotionSelectedLayerNodes(const MotionPlayerCommands& commands, const MotionRootScene& root, const MotionPlayerRenderControls& render, const MotionSelectedLayerNodes& source, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots)
	    : MotionSelectedLayerNodes(commands, root, render, nullptr, nullptr, source, identifiers, std::move(construction), std::move(slots)) {
	}

	MotionSelectedLayerNodes::MotionSelectedLayerNodes(MotionChildConstructionState& owner, const MotionSelectedLayerNodes& source, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots)
	    : MotionSelectedLayerNodes(owner.Commands(), owner.Root(), owner.RenderControls(), &owner, nullptr, source, identifiers, std::move(construction), std::move(slots)) {
	}

	MotionSelectedLayerNodes::MotionSelectedLayerNodes(MotionParticlePlayerState& owner, const MotionSelectedLayerNodes& source, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots)
	    : MotionSelectedLayerNodes(owner.Commands(), owner.Root(), owner.RenderControls(), nullptr, &owner, source, identifiers, std::move(construction), std::move(slots)) {
	}

	MotionSelectedLayerNodes::MotionSelectedLayerNodes(const MotionPlayerCommands& commands, const MotionRootScene& root, const MotionPlayerRenderControls& render, MotionChildConstructionState* owner, MotionParticlePlayerState* particle, const MotionSelectedLayerNodes& source, MotionLayerIdentifierPool& identifiers, MotionChildConstructionServices construction, MotionChildFrameSlotServices slots)
	    : m_Commands(commands), m_Root(root), m_Render(render), m_EnclosingChild(owner), m_EnclosingParticle(particle), m_CommandSelectionSerial(commands.SelectionSerial()), m_Timeline(source.m_Timeline), m_Preparation(source.m_Preparation), m_Identifiers(identifiers), m_Construction(std::move(construction)), m_Slots(std::move(slots)), m_OwnerDepth(source.m_OwnerDepth) {
		if (&commands == &source.m_Commands || !source.m_Published || source.m_Mutating || source.Nodes().size() != source.m_Nodes.size() || source.m_Nodes.size() > kMaximumSelectedLayers || source.m_Owners.size() != source.m_Nodes.size() || !source.m_Parameters || commands.CachedTimeline() != &m_Timeline || !commands.SelectedProject() || commands.SelectedProject()->Timeline != &m_Timeline || commands.SelectionSerial() != source.m_CommandSelectionSerial || m_OwnerDepth >= kMaximumSelectedDepth || commands.LayerUids().SubscriptionCount() != 0) {
			throw std::runtime_error("motion selected clone requires independent exact cloned commands and its idle live source graph");
		}
		RequireMotionCloneSelection(commands, source.m_Commands);
		for (const auto& external : source.m_ExternalSources) {
			if (external.Binding) {
				throw std::runtime_error("motion selected clone requires the actual external texture wrapper/descriptor clone owner");
			}
		}
		if (!m_Slots.EnqueueAction) {
			// Native copy queues no actions. The new Context installs its own real update queue before traversal.
			m_Slots.EnqueueAction = [](auto, auto) {
				throw std::runtime_error("motion cloned action queue requires its new actual scene context");
			};
		}
		m_Parameters = std::make_unique<MotionTimelineParameters>(commands.ParameterValues(), *source.m_Parameters);
		m_ParameterBindings = std::make_unique<MotionLayerParameterBindings>(*m_Parameters);
		m_Nodes = source.m_Nodes;
		// Native510c00 clears every copied node+F8; external listeners/texture cells belong to the source wrapper.
		m_ExternalSources.resize(m_Nodes.size());
		m_Owners.reserve(m_Nodes.size());
		for (auto& node : m_Nodes) {
			node.Identifier = 0;
			m_Owners.push_back(std::make_unique<Owner>());
		}
		m_ChildOrdinals = source.m_ChildOrdinals;
		m_ParticleOrdinals = source.m_ParticleOrdinals;
		m_ShapeOrdinals = source.m_ShapeOrdinals;
		m_SpriteCount = source.m_SpriteCount;
		m_KindMask = source.m_KindMask;
		m_DrawRowCount = source.m_DrawRowCount;
		try {
			CloneOwners(source);
			m_Commands.ParameterValues().BindSelected(*this);
			m_Published = true;
			for (auto& actual : m_Owners) {
				if (actual->Stencil) {
					actual->Stencil->BindMaskLayers(*this);
				}
			}
		} catch (...) {
			m_FinalizingUidOrdinal.reset();
			m_Mutating = false;
			Retire();
			throw;
		}
	}

	void MotionSelectedLayerNodes::CloneOwners(const MotionSelectedLayerNodes& source) {
		const auto& ultimate = m_EnclosingChild != nullptr ? m_EnclosingChild->UltimateParentCommands() : (m_EnclosingParticle != nullptr ? m_EnclosingParticle->UltimateParentCommands() : m_Commands);
		for (std::size_t index = 0; index < m_Nodes.size(); ++index) {
			auto& node = m_Nodes[index];
			auto& actual = *m_Owners[index];
			const auto& old = *source.m_Owners[index];
			if (old.Particles) {
				for (const auto& particle : old.Particles->m_Particles) {
					if (!particle || !particle->Player || !particle->Selected || particle->Player->m_UpdateOwner == nullptr || particle->Selected->m_EnclosingParticle != particle->Player.get() || particle->Selected->m_ContainingSelected != &source || &particle->Player->ImmediateParentCommands() != &source.m_Commands || !particle->Player->m_PreparationOwner.Owns(source.m_Commands.Manager(), particle->Selected->m_Timeline, &particle->Selected->m_Preparation)) {
						throw std::runtime_error("motion retained particle clone requires its actual completed selected player, containing owner and preparation lease");
					}
				}
			}
			if (!old.IdentifierLive || node.Raw == nullptr || node.Prepared != &m_Preparation.Layers[index]) {
				throw std::runtime_error("motion selected clone encountered a retired or mismatched source node");
			}
			actual.Identifier = m_Identifiers.Acquire();
			actual.IdentifierLive = true;
			node.Identifier = actual.Identifier.Value();
			if (old.UidRegistered) {
				// Native50582d registers this new ID before recursive kind3/4 construction; the new root has no inherited listeners.
				m_Mutating = true;
				m_FinalizingUidOrdinal = node.NativeOrdinal;
				m_Commands.LayerUids().Register(*this, node.NativeOrdinal);
				m_FinalizingUidOrdinal.reset();
				m_Mutating = false;
				actual.UidRegistered = true;
			}
			const auto layer = static_cast<std::uint32_t>(index);
			if (old.MeshLocal) {
				actual.MeshLocal = std::make_unique<MotionLayerMeshLocalOwner>(*this, layer, *old.MeshLocal);
			}
			if (old.Shape) {
				actual.Shape = std::make_unique<MotionLayerShapeOwner>(*this, layer, *old.Shape);
			}
			if (old.Stencil) {
				actual.Stencil = std::make_unique<MotionStencilMaskOwner>(*old.Stencil);
			}
			if (old.Child) {
				actual.Child = std::make_unique<MotionChildConstructionState>(*old.Child, m_Commands, ultimate);
				MotionChildFrameSlotServices queue;
				queue.EnqueueAction = [this](auto label, auto action) {
					QueueAction(label, action);
				};
				if (!old.Slots) {
					throw std::runtime_error("motion cloned child lacks its actual physical slot owner");
				}
				actual.Slots = std::make_unique<MotionChildFrameSlots>(*old.Slots, *actual.Child, std::move(queue), *m_ParameterBindings);
				if (old.Selected) {
					actual.Selected = std::make_unique<MotionSelectedLayerNodes>(*actual.Child, *old.Selected, m_Identifiers, m_Construction, MotionChildFrameSlotServices{});
					actual.Selected->m_ContainingSelected = this;
				}
			}
			if (old.Particles) {
				actual.Particles = std::make_unique<MotionParticleLayerState>(*old.Particles, *node.Prepared);
				// Native505d9e recursively copies every retained player in birth order, including stopped children awaiting pruning.
				for (const auto& source_particle : old.Particles->m_Particles) {
					auto particle = std::make_unique<MotionParticleInstance>();
					particle->Player = std::make_unique<MotionParticlePlayerState>(*source_particle->Player, m_Commands, ultimate);
					// Native510e36 retains the outside-screen entry latch; copying is not a new birth.
					particle->Screen = source_particle->Screen;
					particle->Selected = std::make_unique<MotionSelectedLayerNodes>(*particle->Player, *source_particle->Selected, m_Identifiers, m_Construction, MotionChildFrameSlotServices{});
					particle->Selected->m_ContainingSelected = this;
					actual.Particles->m_Particles.push_back(std::move(particle));
				}
			}
		}
	}

	MotionSelectedLayerNodes::~MotionSelectedLayerNodes() noexcept {
		try {
			Retire();
		} catch (...) {
			// Pool lifetime and exclusive tree ownership are destructor invariants, not recoverable input errors.
			std::terminate();
		}
	}

	void MotionSelectedLayerNodes::ValidateLayers(const std::vector<resource::MotionLayer>& layers, std::optional<std::uint32_t> parent, std::size_t depth, std::size_t& index, std::size_t& frames) const {
		if (layers.empty()) {
			return;
		}

		if (depth >= kMaximumSelectedDepth) {
			throw std::runtime_error("motion selected hierarchy exceeds its depth bound");
		}
		for (const auto& layer : layers) {
			if (index >= m_Preparation.Layers.size() || m_Preparation.Parents[index] != parent) {
				throw std::runtime_error("motion selected hierarchy parent differs from its preparation");
			}
			ValidateSelectedLayer(layer, m_Preparation.Layers[index], m_Root.Priority());
			if ((layer.Type == 3 || layer.Type == 4) && (!m_Construction.ReadManagerRandom || !m_Slots.EnqueueAction)) {
				throw std::runtime_error("motion selected children require actual manager sampling and action queue services");
			}
			if (layer.Frames.size() > kMaximumSelectedFrames - frames) {
				throw std::runtime_error("motion selected hierarchy exceeds its total frame bound");
			}
			frames += layer.Frames.size();
			const auto current = static_cast<std::uint32_t>(index++);
			ValidateLayers(layer.Children, current, depth + 1, index, frames);
		}
	}

	void MotionSelectedLayerNodes::ConstructLayers(const std::vector<resource::MotionLayer>& layers, std::optional<std::uint32_t> parent, std::size_t& index) {
		for (const auto& layer : layers) {
			const auto current = static_cast<std::uint32_t>(index++);
			MotionSelectedLayerNode node;
			node.NativeOrdinal = current + 1;
			node.Parent = parent;
			node.RelativeParent = static_cast<std::int32_t>(parent ? *parent + 1 : 0) - static_cast<std::int32_t>(node.NativeOrdinal);
			node.Raw = &layer;
			node.Prepared = &m_Preparation.Layers[current];
			node.JoinTarget = ReadRequiredSelectedBoolean(layer, "joinTarget");
			auto owned = std::make_unique<Owner>();
			owned->Identifier = m_Identifiers.Acquire();
			owned->IdentifierLive = true;
			node.Identifier = owned->Identifier.Value();
			m_Owners.push_back(std::move(owned));
			m_Nodes.push_back(node);
			auto& actual_owner = *m_Owners.back();
			if (layer.Type == 3) {
				// Structural ancestors do not become the new player's immediate parent.
				if (m_EnclosingChild != nullptr) {
					actual_owner.Child = std::make_unique<MotionChildConstructionState>(*m_EnclosingChild, layer, *node.Prepared, m_Construction);
				} else if (m_EnclosingParticle != nullptr) {
					actual_owner.Child = std::make_unique<MotionChildConstructionState>(*m_EnclosingParticle, layer, *node.Prepared, m_Construction);
				} else {
					actual_owner.Child = std::make_unique<MotionChildConstructionState>(m_Commands, m_Root, m_Render, layer, *node.Prepared, m_Construction);
				}
				MotionChildFrameSlotServices slots;
				slots.EnqueueAction = [this](auto label, auto action) {
					QueueAction(label, action);
				};
				actual_owner.Slots = std::make_unique<MotionChildFrameSlots>(layer, *node.Prepared, m_Timeline.LastTime, *actual_owner.Child, std::move(slots), *m_ParameterBindings, current);
				m_ChildOrdinals.push_back(node.NativeOrdinal);
			}
			if (layer.Type == 4) {
				if (!m_Construction.ReadDocumentRoot || !m_Commands.SelectedProject()) {
					throw std::runtime_error("motion kind4 requires its actual registered document owner");
				}
				actual_owner.Particles = std::make_unique<MotionParticleLayerState>(layer, *node.Prepared, m_Construction.ReadDocumentRoot(m_Commands.SelectedProject()->Project));
				m_ParticleOrdinals.push_back(node.NativeOrdinal);
			}
			if (layer.Type == 1) {
				actual_owner.Shape = std::make_unique<MotionLayerShapeOwner>(*this, current);
				m_ShapeOrdinals.push_back(node.NativeOrdinal);
			}
			if (layer.Type == 0) {
				m_Nodes.back().ObjectTrianglePriority = ReadSelectedInteger(layer, "objTriPriority");
				++m_SpriteCount;
			}
			if (node.Prepared->MeshTransform == 1) {
				actual_owner.MeshLocal = std::make_unique<MotionLayerMeshLocalOwner>(*this, current);
			}
			if (layer.Type == 12) {
				actual_owner.Stencil = std::make_unique<MotionStencilMaskOwner>(layer, *node.Prepared, m_Commands.Manager().Resource(m_Commands.SelectedProject()->Project));
			}
			m_KindMask |= std::uint32_t{1} << layer.Type;

			ConstructLayers(layer.Children, current, index);
		}
	}

	void MotionSelectedLayerNodes::FinalizeNodes() {
		m_Mutating = true;
		try {
			for (std::size_t index = 0; index < m_Nodes.size(); ++index) {
				auto& node = m_Nodes[index];
				const auto* value = FindSelectedProperty(*node.Raw, "uid");
				const auto* text = value == nullptr ? nullptr : std::get_if<std::string>(&value->Data);
				if (text != nullptr && !text->empty() && text->front() != '\0') {
					m_FinalizingUidOrdinal = node.NativeOrdinal;
					m_Commands.LayerUids().Register(*this, node.NativeOrdinal);
					m_FinalizingUidOrdinal.reset();
					m_Owners[index]->UidRegistered = true;
				}

				// Native50db39 notifies this node before50dbc4 assigns its row; previous nodes are already finalized.
				if (node.Prepared->Type == 0 || node.Prepared->Type == 3 || node.Prepared->Type == 12) {
					node.DrawRow = m_DrawRowCount++;
				}
			}
			m_Mutating = false;
		} catch (...) {
			m_FinalizingUidOrdinal.reset();
			m_Mutating = false;
			throw;
		}
	}

	std::span<const MotionSelectedLayerNode> MotionSelectedLayerNodes::Nodes() const noexcept {
		return m_Published && m_CommandSelectionSerial == m_Commands.SelectionSerial() ? std::span<const MotionSelectedLayerNode>(m_Nodes) : std::span<const MotionSelectedLayerNode>();
	}

	void MotionSelectedLayerNodes::QueueAction(std::string_view label, std::string_view action) {
		if (m_Mutating) {
			throw std::runtime_error("motion selected action-queue service must not reenter tree mutation");
		}
		m_Mutating = true;
		try {
			m_Slots.EnqueueAction(label, action);
			m_Mutating = false;
		} catch (...) {
			m_Mutating = false;
			throw;
		}
	}

	std::span<const std::uint32_t> MotionSelectedLayerNodes::ChildOrdinals() const noexcept {
		return m_Published ? std::span<const std::uint32_t>(m_ChildOrdinals) : std::span<const std::uint32_t>();
	}

	std::span<const std::uint32_t> MotionSelectedLayerNodes::ShapeOrdinals() const noexcept {
		return m_Published ? std::span<const std::uint32_t>(m_ShapeOrdinals) : std::span<const std::uint32_t>();
	}

	std::span<const std::uint32_t> MotionSelectedLayerNodes::ParticleOrdinals() const noexcept {
		return m_Published ? std::span<const std::uint32_t>(m_ParticleOrdinals) : std::span<const std::uint32_t>();
	}

	MotionParticleLayerState& MotionSelectedLayerNodes::Particles(std::uint32_t layer) {
		if (!m_Published || m_Mutating || layer >= m_Owners.size() || !m_Owners[layer]->Particles) {
			throw std::runtime_error("motion selected layer has no actual exclusive kind4 emitter");
		}
		return *m_Owners[layer]->Particles;
	}

	const MotionParticleLayerState& MotionSelectedLayerNodes::Particles(std::uint32_t layer) const {
		if (!m_Published || m_Mutating || layer >= m_Owners.size() || !m_Owners[layer]->Particles) {
			throw std::runtime_error("motion selected layer has no actual exclusive kind4 emitter");
		}
		return *m_Owners[layer]->Particles;
	}

	bool MotionSelectedLayerNodes::OwnsParticleSelection(std::uint32_t layer, const MotionSelectedLayerNodes& selected) const {
		const auto& particles = Particles(layer);
		for (const auto& particle : particles.m_Particles) {
			if (particle->Selected.get() == &selected) {
				return selected.m_EnclosingParticle == particle->Player.get() && selected.m_ContainingSelected == this;
			}
		}
		return false;
	}

	bool MotionSelectedLayerNodes::OwnsParticleSelection(std::uint32_t layer, std::size_t occurrence, const MotionSelectedLayerNodes& selected) const {
		const auto& particles = Particles(layer);
		if (occurrence >= particles.m_Particles.size()) {
			return false;
		}
		const auto& particle = *particles.m_Particles[occurrence];
		return particle.Selected.get() == &selected && selected.m_EnclosingParticle == particle.Player.get() && selected.m_ContainingSelected == this;
	}

	MotionLayerShapeOwner* MotionSelectedLayerNodes::ShapeOwner(std::uint32_t layer) const noexcept {
		if (layer >= m_Owners.size() || !m_Owners[layer]->IdentifierLive) {
			return nullptr;
		}
		return m_Owners[layer]->Shape.get();
	}

	MotionLayerShapeOwner& MotionSelectedLayerNodes::Shape(std::uint32_t layer) {
		auto* shape = ShapeOwner(layer);
		if (shape == nullptr) {
			throw std::runtime_error("motion selected layer has no live exclusive type1 shape owner");
		}
		return *shape;
	}

	const MotionLayerShapeOwner& MotionSelectedLayerNodes::Shape(std::uint32_t layer) const {
		const auto* shape = ShapeOwner(layer);
		if (shape == nullptr) {
			throw std::runtime_error("motion selected layer has no live exclusive type1 shape owner");
		}
		return *shape;
	}

	std::uint32_t MotionSelectedLayerNodes::SpriteCount() const noexcept {
		return m_Published ? m_SpriteCount : 0;
	}

	std::uint32_t MotionSelectedLayerNodes::KindMask() const noexcept {
		return m_Published ? m_KindMask : 0;
	}

	std::uint32_t MotionSelectedLayerNodes::DrawRowCount() const noexcept {
		return m_Published ? m_DrawRowCount : 0;
	}

	const MotionPlayerParameterValues& MotionSelectedLayerNodes::DescriptorChildValues(std::uint32_t layer) const {
		// Controller rebinding follows complete node construction and precedes UID callbacks/publication.
		if (layer >= m_Owners.size() || m_Owners[layer]->Child == nullptr) {
			throw std::runtime_error("motion descriptor lacks its actual constructed child-player owner");
		}

		return m_Owners[layer]->Child->Commands().ParameterValues();
	}

	MotionSelectedLayerNodes::Owner& MotionSelectedLayerNodes::ReadChildOwner(std::uint32_t layer) {
		if (!m_Published || layer >= m_Owners.size() || m_Owners[layer]->Child == nullptr) {
			throw std::runtime_error("motion selected layer has no live exclusive child owner");
		}

		return *m_Owners[layer];
	}

	const MotionSelectedLayerNodes::Owner& MotionSelectedLayerNodes::ReadChildOwner(std::uint32_t layer) const {
		if (!m_Published || layer >= m_Owners.size() || m_Owners[layer]->Child == nullptr) {
			throw std::runtime_error("motion selected layer has no live exclusive child owner");
		}

		return *m_Owners[layer];
	}

	MotionLayerMeshLocalOwner& MotionSelectedLayerNodes::MutableUidMesh(std::uint32_t layer) const {
		const bool during_added = m_FinalizingUidOrdinal && layer < *m_FinalizingUidOrdinal;
		if ((m_Mutating && !during_added) || (!m_Published && !during_added) || layer >= m_Owners.size() || !m_Owners[layer]->IdentifierLive || m_Owners[layer]->MeshLocal == nullptr || m_Nodes[layer].Prepared != &m_Preparation.Layers[layer]) {
			throw std::runtime_error("motion mesh UID write requires its actual constructed exclusive mesh owner");
		}
		const auto& selection = m_Commands.SelectedProject();
		if (!selection || selection->Timeline != &m_Timeline || m_CommandSelectionSerial != m_Commands.SelectionSerial() || !m_Commands.Manager().Contains(selection->Project)) {
			throw std::runtime_error("motion mesh UID write requires its exact current registered selection");
		}

		return *m_Owners[layer]->MeshLocal;
	}

	MotionLayerMeshLocalOwner& MotionSelectedLayerNodes::MeshLocal(std::uint32_t layer) {
		if (!m_Published || layer >= m_Owners.size() || !m_Owners[layer]->MeshLocal) {
			throw std::runtime_error("motion selected layer has no live exclusive local mesh owner");
		}

		return *m_Owners[layer]->MeshLocal;
	}

	const MotionLayerMeshLocalOwner& MotionSelectedLayerNodes::MeshLocal(std::uint32_t layer) const {
		if (!m_Published || layer >= m_Owners.size() || !m_Owners[layer]->MeshLocal) {
			throw std::runtime_error("motion selected layer has no live exclusive local mesh owner");
		}

		return *m_Owners[layer]->MeshLocal;
	}

	MotionStencilMaskOwner& MotionSelectedLayerNodes::StencilMask(std::uint32_t layer) {
		if (!m_Published || layer >= m_Owners.size() || !m_Owners[layer]->Stencil) {
			throw std::runtime_error("motion selected layer has no live exclusive kind12 stencil owner");
		}

		return *m_Owners[layer]->Stencil;
	}

	const MotionStencilMaskOwner& MotionSelectedLayerNodes::StencilMask(std::uint32_t layer) const {
		if (!m_Published || layer >= m_Owners.size() || !m_Owners[layer]->Stencil) {
			throw std::runtime_error("motion selected layer has no live exclusive kind12 stencil owner");
		}

		return *m_Owners[layer]->Stencil;
	}

	MotionChildConstructionState& MotionSelectedLayerNodes::Child(std::uint32_t layer) {
		return *ReadChildOwner(layer).Child;
	}

	const MotionChildConstructionState& MotionSelectedLayerNodes::Child(std::uint32_t layer) const {
		return *ReadChildOwner(layer).Child;
	}

	MotionChildFrameSlots& MotionSelectedLayerNodes::ChildSlots(std::uint32_t layer) {
		return *ReadChildOwner(layer).Slots;
	}

	const MotionChildFrameSlots& MotionSelectedLayerNodes::ChildSlots(std::uint32_t layer) const {
		return *ReadChildOwner(layer).Slots;
	}

	void MotionSelectedLayerNodes::UnbindJoinedDescriptors() const noexcept {
		// Raw descriptor targets may address a player about to move or retire with a duplicate JOIN path.
		m_Commands.ParameterValues().UnbindSelected(*this);
	}

	void MotionSelectedLayerNodes::RebindJoinedDescriptors() const {
		if (m_Mutating || !m_Published) {
			throw std::runtime_error("motion JOIN descriptor rebinding requires its completed actual selected owner");
		}
		m_Commands.ParameterValues().BindSelected(*this);
	}

	MotionSelectedLayerNodes::JoinedChildOwners MotionSelectedLayerNodes::ExtractJoinedChild(std::uint32_t layer) {
		auto& owner = ReadChildOwner(layer);
		if (m_Mutating) {
			throw std::runtime_error("motion JOIN child extraction requires its idle actual selected owner");
		}
		// Native5276e0 moves only the player payload. Incoming node slot controls remain separate.
		owner.Child->m_InheritedClip = nullptr;
		owner.Child->m_InheritedStencil = nullptr;
		owner.Child->m_DeformationTransferOwner = nullptr;
		owner.Child->m_InheritedDeformation = nullptr;
		owner.Child->m_DeformationTransferSerial = 0;
		if (owner.Selected) {
			owner.Selected->m_ContainingSelected = nullptr;
		}
		return {std::move(owner.Child), std::move(owner.Selected)};
	}

	void MotionSelectedLayerNodes::InstallJoinedChild(std::uint32_t layer, JoinedChildOwners& owners, MotionChildFrameSlotServices action_service) {
		auto& incoming = ReadChildOwner(layer);
		if (m_Mutating || incoming.Selected || !owners.first || &owners.first->ImmediateParentCommands() != &m_Commands || &owners.first->Commands().Manager() != &m_Commands.Manager() || (owners.second && (owners.second->m_EnclosingChild != owners.first.get() || !action_service.EnqueueAction))) {
			throw std::runtime_error("motion JOIN child installation requires the actual retained player, selected nodes and action owner");
		}
		// Native527450 destroys the fresh incoming player after its real construction/slot preparation.
		// These two controls belong to that incoming node; retain them when rebinding this C++ facade.
		owners.first->m_Controls = incoming.Child->m_Controls;
		incoming.Slots->m_Child = owners.first.get();
		incoming.Selected.reset();
		incoming.Child.reset();
		incoming.Child = std::move(owners.first);
		incoming.Selected = std::move(owners.second);
		if (incoming.Selected) {
			incoming.Selected->m_ContainingSelected = this;
			incoming.Selected->m_OwnerDepth = m_OwnerDepth + 1;
			incoming.Selected->m_Slots = std::move(action_service);
		}
	}

	void MotionSelectedLayerNodes::RetireChildNodes(std::uint32_t layer) {
		if (m_Mutating) {
			throw std::runtime_error("motion selected child retirement must not reenter tree mutation");
		}
		auto& owner = ReadChildOwner(layer);
		m_Mutating = true;
		// Native50fd90 keeps the player/root; this exclusive facade owns only its retired nonroot nodes.
		owner.Selected.reset();
		m_Mutating = false;
	}

	void MotionSelectedLayerNodes::RebuildChildNodes(std::uint32_t layer, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, MotionChildFrameSlotServices child_slots) {
		if (m_Mutating) {
			throw std::runtime_error("motion selected tree mutation must not reenter");
		}
		auto& owner = ReadChildOwner(layer);
		m_Mutating = true;
		try {
			owner.Selected.reset();
			if (m_OwnerDepth + 1 >= kMaximumSelectedDepth) {
				throw std::runtime_error("motion selected child tree exceeds its owner depth bound");
			}
			owner.Selected = std::make_unique<MotionSelectedLayerNodes>(*owner.Child, timeline, preparation, m_Identifiers, m_Construction, std::move(child_slots));
			owner.Selected->m_OwnerDepth = m_OwnerDepth + 1;
			owner.Selected->m_ContainingSelected = this;
			m_Mutating = false;
		} catch (...) {
			m_Mutating = false;
			throw;
		}
	}

	MotionSelectedLayerNodes* MotionSelectedLayerNodes::ChildNodes(std::uint32_t layer) {
		return ReadChildOwner(layer).Selected.get();
	}

	void MotionSelectedLayerNodes::ResetChildMotion(std::uint32_t layer) {
		if (m_Mutating) {
			throw std::runtime_error("motion selected tree mutation must not reenter");
		}
		auto& owner = ReadChildOwner(layer);
		m_Mutating = true;
		try {
			owner.Child->Commands().ResetChildMotion([&] {
				owner.Selected.reset();
			});
			m_Mutating = false;
		} catch (...) {
			m_Mutating = false;
			throw;
		}
	}

	void MotionSelectedLayerNodes::Retire() {
		Retire(false);
	}

	std::unique_ptr<MotionTimelineParameters> MotionSelectedLayerNodes::ClearLayers() {
		Retire(true);
		return std::move(m_Parameters);
	}

	void MotionSelectedLayerNodes::Retire(bool preserve_parameters) {
		if (m_Mutating) {
			throw std::runtime_error("motion selected tree retirement must not reenter");
		}
		m_Commands.ParameterValues().UnbindSelected(*this);
		m_Published = false;
		m_Mutating = true;
		try {
			std::uint32_t native_ordinal = 0;
			for (auto& owner : m_Owners) {
				++native_ordinal;
				if (owner->UidRegistered) {
					m_Commands.LayerUids().Unregister(*this, native_ordinal);
					owner->UidRegistered = false;
				}
				if (owner->IdentifierLive) {
					m_Identifiers.Retire(owner->Identifier);
					owner->IdentifierLive = false;
				}
				owner->Shape.reset();
				owner->MeshLocal.reset();
				owner->Stencil.reset();
				owner->Particles.reset();
				owner->Selected.reset();
				owner->Slots.reset();
				owner->Child.reset();
			}
			m_ParameterBindings.reset();
			if (!preserve_parameters) {
				m_Parameters.reset();
			}
			m_Owners.clear();
			m_Nodes.clear();
			m_ChildOrdinals.clear();
			m_ParticleOrdinals.clear();
			m_ShapeOrdinals.clear();
			m_SpriteCount = 0;
			m_KindMask = 0;
			m_DrawRowCount = 0;
			m_Mutating = false;
		} catch (...) {
			m_Mutating = false;
			throw;
		}
	}

} // namespace sgre::scene
