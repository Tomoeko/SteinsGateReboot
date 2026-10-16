#include "sgre/scene/motion_selected_sprite_player.hpp"

#include "sgre/scene/motion_local_frames.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	struct MotionSpriteOperationScope {
		bool& Active;

		explicit MotionSpriteOperationScope(bool& active)
		    : Active(active) {
			if (active) {
				throw std::runtime_error("motion selected sprite owner cannot reenter an active phase");
			}

			active = true;
		}

		~MotionSpriteOperationScope() {
			Active = false;
		}
	};

	struct MotionSelectedSpritePlayer::Selection {
		struct Geometry {
			std::array<resource::MotionPoint, 4> Positions{};
			MotionClipGeometry Cache;
			bool Ready = false;
		};
		MotionPreparationLease PreparationOwner;
		const resource::MotionResource& Resource;
		const resource::MotionTimeline& Timeline;
		const PreparedMotionTimeline& Preparation;
		MotionProjectId Project;
		MotionProjectIdentity Identity;
		MotionSelectedLayerNodes Nodes;
		MotionPriorityCursor Priority;
		std::vector<std::unique_ptr<MotionStaticSourceBinding>> Sources;
		std::vector<Geometry> Geometries;
		MotionLocalFrames Frames;
		MotionLayerComposition Composition;
		float DepthProjection = 0;

		static MotionChildFrameSlotServices MakeSlotServices(MotionSelectedSpritePlayer& owner) {
			MotionChildFrameSlotServices services;
			services.EnqueueAction = [&owner](std::string_view layer, std::string_view action) {
				owner.m_Update.EnqueueAction(layer, action);
			};

			return services;
		}

		static MotionLocalFrameServices MakeFrameServices(MotionSelectedSpritePlayer& owner, Selection& selection) {
			MotionLocalFrameServices services;
			services.BindSource = [&selection](std::uint32_t layer, const PreparedMotionFrame& current, const PreparedMotionFrame&) {
				if (layer >= selection.Sources.size() || !selection.Sources[layer]) {
					throw std::runtime_error("motion sprite source binding requires its actual selected sprite node");
				}

				static_cast<void>(selection.Sources[layer]->Bind(current));
			};
			services.EnqueueAction = [&owner](std::string_view layer, std::string_view action) {
				owner.m_Update.EnqueueAction(layer, action);
			};

			return services;
		}

		Selection(MotionSelectedSpritePlayer& owner, const resource::MotionResource& resource, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, const PreparedMotionSourceRegistry& sources, const ResolvedMotionTimeline& selected, MotionPreparationLease preparation_owner)
		    : PreparationOwner(std::move(preparation_owner)), Resource(resource), Timeline(timeline), Preparation(preparation), Project(selected.Project), Identity(selected.Identity), Nodes(owner.m_Commands, owner.m_Root, owner.m_Render, timeline, preparation, owner.m_Identifiers, owner.m_Services.Construction, MakeSlotServices(owner)), Priority(preparation, owner.m_Commands.m_PriorityFrameIndex), Geometries(preparation.Layers.size()), Frames(timeline, preparation, resource, MakeFrameServices(owner, *this)), Composition(preparation, owner.m_Root, !owner.m_Services.IndependentLayerInherit) {
			if (preparation.Layers.size() > 65536 || Nodes.ChildOrdinals().size() != 0) {
				throw std::runtime_error("motion sprite selected owner requires bounded real sprite/hierarchy nodes; kind3 needs its enclosing child producer");
			}

			Sources.reserve(preparation.Layers.size());
			for (const auto& layer : preparation.Layers) {
				if (layer.Type == 0) {
					Sources.push_back(std::make_unique<MotionStaticSourceBinding>(sources, Identity, owner.m_Services.Textures));
				} else if (layer.Type == 2) {
					Sources.push_back(nullptr);
				} else {
					throw std::runtime_error("motion sprite selected owner requires the actual producer for this non-sprite/hierarchy kind");
				}
			}
		}
	};

	MotionPlayerUpdateServices MotionSelectedSpritePlayer::MakeUpdateServices() {
		MotionPlayerUpdateServices services;
		services.ReadFrameDelta = [this] {
			RequirePhase();

			return m_Services.ReadFrameDelta();
		};
		services.PrepareFrames = [this](MotionFrameTraversal traversal, const MotionPlayerFrameContext& frame) {
			PrepareFrames(traversal, frame);
		};
		services.ComposeLayers = [this](const MotionPlayerFrameContext& frame) {
			Compose(frame);
		};
		services.PublishDrawPackets = [this](const MotionPlayerFrameContext& frame) {
			Publish(frame);
		};
		services.Draw = [this] {
			AppendDrawRows();
		};
		services.DeliverEvent = [this](const MotionPlayerEvent& event) {
			Deliver(event);
		};

		return services;
	}

	MotionSelectedSpritePlayer::MotionSelectedSpritePlayer(MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, const resource::MotionResource& resource, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, const PreparedMotionSourceRegistry& sources, MotionLayerIdentifierPool& identifiers, MotionSpriteDrawContext& context, MotionSelectedSpriteServices services)
	    : MotionSelectedSpritePlayer(commands, root, render, identifiers, context, std::move(services)) {
		Rebuild(resource, timeline, preparation, sources);
	}

	MotionSelectedSpritePlayer::MotionSelectedSpritePlayer(MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, MotionLayerIdentifierPool& identifiers, MotionSpriteDrawContext& context, MotionSelectedSpriteServices services)
	    : m_Commands(commands), m_Root(root), m_Render(render), m_Identifiers(identifiers), m_Context(context), m_Services(std::move(services)), m_Update(commands, MakeUpdateServices()) {
		if (!m_Services.ReadFrameDelta || !m_Services.ReadDepthProjection || !m_Services.Construction.ReadManagerRandom || !m_Services.Textures.FindTexture || !m_Services.DeliverEvent) {
			throw std::runtime_error("motion selected sprite owner requires actual manager timing/projection/random, texture and listener services");
		}

		m_PreparedProjects.reserve(256);
		// Native full-new owns an empty real draw queue, including Draw before the first Update.
		m_Published = commands.CachedTimeline() == nullptr;
	}

	MotionSelectedSpritePlayer::~MotionSelectedSpritePlayer() noexcept {
		// A queued context holds actual borrowed rows. Destroying their owner before flush/clear is a programmer lifetime error.
		if (m_Context.IsBorrowing(*this) || m_Operating) {
			std::terminate();
		}

		m_Selected.reset();
	}

	void MotionSelectedSpritePlayer::RequireMutable() const {
		if (!CanMutate()) {
			throw std::runtime_error("motion sprite mutation requires an idle owner and consumption of its actual queued draw rows");
		}
	}

	bool MotionSelectedSpritePlayer::ResourcesLive() const {
		return (m_Selected || (m_LayersCleared && m_Commands.CachedTimeline() == m_ClearedTimeline) || m_Commands.CachedTimeline() == nullptr) && (!m_Services.ResourceBorrowsLive || m_Services.ResourceBorrowsLive()) && std::all_of(m_PreparedProjects.begin(), m_PreparedProjects.end(), [&](MotionProjectId project) {
			return m_Commands.Manager().Contains(project);
		});
	}

	bool MotionSelectedSpritePlayer::AllPlaying() const {
		if (m_Commands.ClockState().Playing) {
			return true;
		}
		if (!m_Selected) {
			return false;
		}
		const auto found = m_Selected->Nodes.FindAllPlaying(m_Commands);
		if (!found) {
			throw std::runtime_error("motion sprite playing query lost its actual physical player");
		}
		return *found;
	}

	bool MotionSelectedSpritePlayer::CanMutate() const noexcept {
		return !m_Operating && !m_Context.IsBorrowing(*this);
	}

	void MotionSelectedSpritePlayer::RequirePhase() const {
		if (!m_Selected && (m_Commands.CachedTimeline() == nullptr || (m_LayersCleared && m_Commands.CachedTimeline() == m_ClearedTimeline)) && ResourcesLive()) {
			return;
		}

		RequireSelected();
	}

	void MotionSelectedSpritePlayer::RequireSelected() const {
		if (!m_Selected || !ResourcesLive() || m_Commands.CachedTimeline() != &m_Selected->Timeline) {
			throw std::runtime_error("motion sprite phase requires its actual live selected resource/tree; a new real selection requires rebuilding");
		}
	}

	void MotionSelectedSpritePlayer::Rebuild(const resource::MotionResource& resource, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, const PreparedMotionSourceRegistry& sources, MotionPreparationLease owner) {
		RequireMutable();
		const MotionSpriteOperationScope operation(m_Operating);
		m_Published = false;
		m_PublishedRows.clear();
		const auto& selected = m_Commands.SelectedProject();
		if (!selected || selected->Timeline != &timeline || m_Commands.CachedTimeline() != &timeline || !m_Commands.Manager().Contains(selected->Project)) {
			throw std::runtime_error("motion sprite construction requires the command selector's actual live timeline and project");
		}

		if (resource.Objects.size() > 1000000) {
			throw std::runtime_error("motion sprite resource owner exceeds its bounded character count");
		}

		if (m_Root.MatrixMode() != MotionRootMatrixMode::Recomputed) {
			throw std::runtime_error("motion sprite selected owner requires its standalone root; exclusive child/inherited owner coupling remains unsupported");
		}

		bool character_owned = false;
		bool timeline_owned = false;
		for (const auto& character : resource.Objects) {
			if (&character != selected->Character) {
				continue;
			}

			character_owned = true;
			if (character.Timelines.size() > 1000000) {
				throw std::runtime_error("motion sprite character owner exceeds its bounded timeline count");
			}

			for (const auto& candidate : character.Timelines) {
				timeline_owned = timeline_owned || &candidate == &timeline;
			}
		}
		if (!character_owned || !timeline_owned) {
			throw std::runtime_error("motion sprite curve/resource owner differs from the actual selected registration");
		}

		if (preparation.Layers.size() > 65536) {
			throw std::runtime_error("motion sprite selection exceeds its bounded actual node count");
		}
		for (const auto& layer : preparation.Layers) {
			if (layer.Type != 0 && layer.Type != 2) {
				throw std::runtime_error("motion sprite selection requires its actual non-sprite producer; kind3 admission remains closed");
			}
		}

		if (!owner.IsEmpty() && !owner.Owns(m_Commands.Manager(), timeline, &preparation)) {
			throw std::runtime_error("motion sprite preparation lease differs from its actual selected timeline");
		}
		m_Update.PrepareCurrentTimeline(timeline, owner);
		m_LayersCleared = false;
		m_ClearedTimeline = nullptr;
		if (std::find(m_PreparedProjects.begin(), m_PreparedProjects.end(), selected->Project) == m_PreparedProjects.end()) {
			if (m_PreparedProjects.size() == m_PreparedProjects.capacity()) {
				throw std::runtime_error("motion sprite prepared project borrows exceed their bounded tag-cache scope");
			}

			m_PreparedProjects.push_back(selected->Project);
		}
		for (auto& row : m_Rows) {
			row.Clip.Geometry = nullptr;
		}
		m_Selected.reset();
		m_RetainedParameters.reset();
		m_Selected = std::make_unique<Selection>(*this, resource, timeline, preparation, sources, *selected, std::move(owner));
		m_Rows.resize(m_Selected->Nodes.DrawRowCount());
		m_PublishedRows.reserve(m_Rows.size());
		for (auto& row : m_Rows) {
			row.Command.Clip = &row.Clip;
		}
	}

	void MotionSelectedSpritePlayer::Retire() {
		RequireMutable();
		const MotionSpriteOperationScope operation(m_Operating);
		m_Published = false;
		m_PublishedRows.clear();
		for (auto& row : m_Rows) {
			row.Clip.Geometry = nullptr;
		}
		m_Selected.reset();
	}

	void MotionSelectedSpritePlayer::Clear() {
		RequireMutable();
		RequirePhase();
		const MotionSpriteOperationScope operation(m_Operating);
		m_Published = false;
		m_PublishedRows.clear();
		for (auto& row : m_Rows) {
			row.Clip.Geometry = nullptr;
		}
		if (m_Selected) {
			m_RetainedParameters = m_Selected->Nodes.ClearLayers();
			m_Selected.reset();
		}
		m_ClearedTimeline = m_Commands.CachedTimeline();
		m_LayersCleared = true;
		m_Commands.m_SelectedLayersCleared = true;
		m_Published = true;
	}

	void MotionSelectedSpritePlayer::PrepareFrames(MotionFrameTraversal traversal, const MotionPlayerFrameContext& frame) {
		RequirePhase();
		if (!m_Selected) {
			return;
		}
		if (frame.Timeline != &m_Selected->Timeline) {
			throw std::runtime_error("motion sprite frame traversal does not match its actual selected owner");
		}

		switch (traversal) {
		case MotionFrameTraversal::ParameterOnly:
			break;
		case MotionFrameTraversal::Seek:
			m_Selected->Priority.Seek(frame.Clock.EvaluationTime);
			break;
		case MotionFrameTraversal::Forward:
			m_Selected->Priority.Forward(frame.Clock.EvaluationTime);
			break;
		case MotionFrameTraversal::Reverse:
			m_Selected->Priority.Reverse(frame.Clock.EvaluationTime);
			break;
		}

		m_Selected->Frames.Select(traversal, frame.Clock.EvaluationTime);
	}

	void MotionSelectedSpritePlayer::Compose(const MotionPlayerFrameContext& frame) {
		RequirePhase();
		m_Root.AdvanceVelocity(frame.Clock.DeltaTicks);
		if (!m_Selected) {
			m_Published = false;
			m_PublishedRows.clear();
			m_Root.Compose(frame.Clock.TickAssigned);
			if (m_RetainedParameters) {
				m_RetainedParameters->CompleteClearedFrame();
			}

			return;
		}
		m_Published = false;
		m_PublishedRows.clear();
		m_Selected->DepthProjection = m_Services.ReadDepthProjection();
		if (!std::isfinite(m_Selected->DepthProjection)) {
			throw std::runtime_error("motion sprite geometry requires a finite actual projection coefficient");
		}

		m_Selected->Composition.Compose(m_Selected->Frames, frame.Clock.TickAssigned);
		const auto layers = m_Selected->Composition.Layers();
		for (std::size_t index = 0; index < layers.size(); ++index) {
			if (!m_Selected->Sources[index] || !layers[index].Active || !m_Selected->Composition.Changed(static_cast<std::uint32_t>(index))) {
				continue;
			}

			const auto sprite = m_Selected->Sources[index]->BuildSprite(m_Selected->Preparation.Layers[index], layers[index].Hierarchy, m_Selected->DepthProjection);
			if (!sprite) {
				continue;
			}

			auto& geometry = m_Selected->Geometries[index];
			for (std::size_t corner = 0; corner < geometry.Positions.size(); ++corner) {
				geometry.Positions[corner] = sprite->Sprite.Vertices[corner].Position;
			}
			geometry.Cache.Positions = geometry.Positions;
			geometry.Cache.CachedBounds = MotionBounds{};
			geometry.Cache.Calculated = false;
			geometry.Ready = true;
		}

		// Only this bounded complete sprite/hierarchy geometry family may acknowledge the selected dirty phase.
		m_Selected->Frames.CompleteComposition();
	}

	void MotionSelectedSpritePlayer::Publish(const MotionPlayerFrameContext& frame) {
		RequirePhase();
		if (!m_Selected) {
			m_PublishedRows.clear();
			m_Published = true;

			return;
		}
		m_Published = false;
		m_PublishedRows.clear();
		if (frame.Clock.TickAssigned) {
			m_Published = true;

			return;
		}

		const auto layers = m_Selected->Composition.Layers();
		const auto nodes = m_Selected->Nodes.Nodes();
		if (layers.size() != nodes.size()) {
			throw std::runtime_error("motion sprite publication requires complete actual node/composition storage");
		}

		for (const auto index : m_Selected->Priority.DrawOrder()) {
			const auto& node = nodes[index];
			if (!node.DrawRow || !layers[index].Active || !m_Selected->Sources[index]) {
				continue;
			}

			const auto& source = m_Selected->Sources[index]->State();
			if (!source.Ready) {
				continue;
			}
			if (!source.Binding || source.Binding->Project != m_Selected->Identity) {
				throw std::runtime_error("motion sprite cross-project source binding requires its actual resource lifetime owner");
			}

			const auto sprite = m_Selected->Sources[index]->BuildSprite(*node.Prepared, layers[index].Hierarchy, m_Selected->DepthProjection);
			auto& geometry = m_Selected->Geometries[index];
			if (!sprite || !geometry.Ready || *node.DrawRow >= m_Rows.size() || sprite->Slot.Index > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
				throw std::runtime_error("motion sprite publication requires real prepared geometry, source slot and persistent row");
			}

			auto& row = m_Rows[*node.DrawRow];
			row.Order.Primary = layers[index].Hierarchy.Attributes.Coordinate[2];
			row.Order.Secondary = m_Root.Priority();
			row.NodeIdentifier = node.Identifier;
			row.CoordinatePlane = node.Prepared->CoordinatePlane;
			row.TrianglePriority = *node.ObjectTrianglePriority;
			row.EmissiveColor = m_Render.EmissiveColor;
			row.AmbientFactor = m_Render.AmbientFactor;
			row.Slot = sprite->Slot;
			row.Texture = sprite->Texture;
			row.Clip.Geometry = &geometry.Cache;
			row.Clip.Hidden = false;
			// Native row+44 is evaluated opacity; zero suppresses ordinary rendering without removing the row from its queue.
			row.Clip.GeometryCount = layers[index].Hierarchy.Attributes.Opacity;
			row.Clip.ClipGroup = 0;
			row.Command.Clip = &row.Clip;
			row.Command.IndividualClip = nullptr;
			row.Command.Texture = static_cast<std::int32_t>(sprite->Slot.Index);
			row.Command.BlendMode = sprite->Sprite.BlendMode;
			row.Command.BlendParameter = 0;
			row.Command.Filter = m_Render.ColorFilter;
			row.Command.FilterColor = m_Render.ColorFilterParameter;
			row.Command.Colors = {};
			for (std::size_t corner = 0; corner < row.Command.Colors.size(); ++corner) {
				row.Command.Colors[corner] = sprite->Sprite.Vertices[corner].Color;
			}
			const auto& icon = *source.Binding->Icon;
			row.Command.PixelUvs.X = std::trunc(icon.Left);
			row.Command.PixelUvs.Y = std::trunc(icon.Top);
			row.Command.PixelUvs.Width = std::trunc(icon.Width) * icon.Resolution;
			row.Command.PixelUvs.Height = std::trunc(icon.Height) * icon.Resolution;
			row.Command.Key = sprite->Sprite.TextureAttributes;
			m_PublishedRows.push_back(&row);
		}

		m_Published = true;
	}

	void MotionSelectedSpritePlayer::AppendDrawRows() {
		RequirePhase();
		if (!m_Published) {
			throw std::runtime_error("motion sprite draw requires a successfully published actual frame");
		}

		m_Context.Append(*this, m_PublishedRows);
	}

	void MotionSelectedSpritePlayer::Deliver(const MotionPlayerEvent& event) {
		RequirePhase();
		m_Services.DeliverEvent(event);
		// This detects removal before any following borrowed event is visited; it supplies no speculative native pin.
		if (!ResourcesLive()) {
			throw std::runtime_error("motion sprite resource unregister during borrowed event delivery is outside the recovered lifetime domain");
		}
	}

	void MotionSelectedSpritePlayer::Update() {
		RequireMutable();
		RequirePhase();
		const MotionSpriteOperationScope operation(m_Operating);
		try {
			m_Update.Update();
		} catch (...) {
			m_Published = false;
			m_PublishedRows.clear();
			throw;
		}
	}

	void MotionSelectedSpritePlayer::Progress(float elapsed_ticks) {
		RequireMutable();
		RequirePhase();
		const MotionSpriteOperationScope operation(m_Operating);
		try {
			m_Update.Progress(elapsed_ticks);
		} catch (...) {
			m_Published = false;
			m_PublishedRows.clear();
			throw;
		}
	}

	void MotionSelectedSpritePlayer::PostUpdate() {
		RequireMutable();
		RequirePhase();
		const MotionSpriteOperationScope operation(m_Operating);
		try {
			m_Update.PostUpdate();
		} catch (...) {
			m_Published = false;
			m_PublishedRows.clear();
			throw;
		}
	}

	void MotionSelectedSpritePlayer::Draw() {
		if (m_Operating) {
			throw std::runtime_error("motion sprite draw cannot reenter an active phase");
		}

		const MotionSpriteOperationScope operation(m_Operating);
		m_Update.Draw();
	}

	std::span<const MotionSpriteDrawRow> MotionSelectedSpritePlayer::Rows() const noexcept {
		return m_Published ? std::span<const MotionSpriteDrawRow>(m_Rows) : std::span<const MotionSpriteDrawRow>{};
	}

	std::span<MotionSpriteDrawRow* const> MotionSelectedSpritePlayer::PublishedRows() const noexcept {
		return m_Published ? std::span<MotionSpriteDrawRow* const>(m_PublishedRows) : std::span<MotionSpriteDrawRow* const>{};
	}

	const MotionSelectedLayerNodes& MotionSelectedSpritePlayer::Nodes() const {
		RequireSelected();

		return m_Selected->Nodes;
	}

	const MotionLocalFrameState& MotionSelectedSpritePlayer::LocalState(std::uint32_t layer) const {
		RequireSelected();

		return m_Selected->Frames.State(layer);
	}

} // namespace sgre::scene
