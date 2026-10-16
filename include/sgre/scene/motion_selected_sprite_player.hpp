#pragma once

#include "sgre/scene/motion_frame_submission.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_priority_cursor.hpp"
#include "sgre/scene/motion_render_queue.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_static_source.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace sgre::scene {

	class MotionSelectedSpritePlayer;

	struct MotionSpriteDrawRow {
		// The enclosing player retains these row values across same-size selected-tree rebuilds.
		MotionRenderOrder Order;
		MotionPreparedFrameCommand Command;
		MotionClipCommand Clip;
		MotionTextureSlot Slot;
		CachedTextureHandle Texture;
		std::uint32_t NodeIdentifier = 0;
		std::uint32_t CoordinatePlane = 0;
		std::uint32_t TrianglePriority = 0;
		std::uint32_t EmissiveColor = 0;
		float AmbientFactor = 0;
	};

	class MotionSpriteDrawContext {
	public:
		// This is the real context queue borrow; player rows/resources remain live and unchanged through flush/clear.
		explicit MotionSpriteDrawContext(std::size_t capacity = 1024);
		MotionSpriteDrawContext(const MotionSpriteDrawContext&) = delete;
		MotionSpriteDrawContext& operator=(const MotionSpriteDrawContext&) = delete;
		void Append(MotionSelectedSpritePlayer& owner, std::span<MotionSpriteDrawRow* const> rows);
		[[nodiscard]] bool IsBorrowing(const MotionSelectedSpritePlayer& owner) const noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] bool CanClear() const noexcept;
		void Clear();
		// The actual frame driver supplies viewport/scissor/matrices/texture dimensions, exclusive scratch and a real output sink.
		// Selected sprite scope has no inherited clip/mask producer; those fields in globals must be empty.
		void Flush(const MotionPreparedFrame& globals, const MotionFrameScratch& scratch, const std::function<void(const MotionFrameEvent&)>& output);

	private:
		struct Entry {
			MotionSelectedSpritePlayer* Owner = nullptr;
			MotionSpriteDrawRow* Row = nullptr;
		};
		MotionRenderQueue m_Queue;
		std::vector<Entry> m_Entries;
		std::vector<MotionPreparedFrameCommand> m_OrderedCommands;
		std::vector<MotionClipCommand> m_OrderedClips;
		bool m_Submitting = false;
	};

	struct MotionSelectedSpriteServices {
		// Actual native player+418; constructor defaultfalse normalizes against its root.
		bool IndependentLayerInherit = false;
		std::function<float()> ReadFrameDelta;
		// Native geometry reads the current actual resource-owner projection coefficient at interface +48.
		std::function<float()> ReadDepthProjection;
		MotionChildConstructionServices Construction;
		MotionStaticSourceServices Textures;
		std::function<void(const MotionPlayerEvent&)> DeliverEvent;
		// Optional additional observational lifetime check; the containing manager supplies all source/tag backing identities.
		std::function<bool()> ResourceBorrowsLive;
	};

	class MotionSelectedSpritePlayer {
	public:
		// Commands/root/render, manager, all prepared immutable raw/curve/source owners and draw context outlive this owner.
		// Recomputed standalone roots only; exclusive child/inherited native owner coupling remains unsupported.
		// This couples an already genuinely selected ordinary sprite/hierarchy timeline, not a substitute Play or script/task constructor.
		MotionSelectedSpritePlayer(MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, const resource::MotionResource& resource, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, const PreparedMotionSourceRegistry& sources, MotionLayerIdentifierPool& identifiers, MotionSpriteDrawContext& context, MotionSelectedSpriteServices services);
		// Genuine full-new initial state has a root and update owner before any timeline selection.
		MotionSelectedSpritePlayer(MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, MotionLayerIdentifierPool& identifiers, MotionSpriteDrawContext& context, MotionSelectedSpriteServices services);
		~MotionSelectedSpritePlayer() noexcept;
		MotionSelectedSpritePlayer(const MotionSelectedSpritePlayer&) = delete;
		MotionSelectedSpritePlayer& operator=(const MotionSelectedSpritePlayer&) = delete;
		// The real command selector precedes rebuilding. Old selected IDs/geometry retire before constructing new selected nodes.
		void Rebuild(const resource::MotionResource& resource, const resource::MotionTimeline& timeline, const PreparedMotionTimeline& preparation, const PreparedMotionSourceRegistry& sources, MotionPreparationLease owner = {});
		void Retire();
		void Clear();
		void Update();
		void Progress(float elapsed_ticks);
		void PostUpdate();
		void Draw();
		[[nodiscard]] std::span<const MotionSpriteDrawRow> Rows() const noexcept;
		[[nodiscard]] std::span<MotionSpriteDrawRow* const> PublishedRows() const noexcept;
		[[nodiscard]] const MotionSelectedLayerNodes& Nodes() const;
		[[nodiscard]] const MotionLocalFrameState& LocalState(std::uint32_t layer) const;
		[[nodiscard]] bool ResourcesLive() const;
		[[nodiscard]] bool CanMutate() const noexcept;
		[[nodiscard]] bool AllPlaying() const;

	private:
		struct Selection;
		[[nodiscard]] MotionPlayerUpdateServices MakeUpdateServices();
		void RequireMutable() const;
		void RequireSelected() const;
		void RequirePhase() const;
		void PrepareFrames(MotionFrameTraversal traversal, const MotionPlayerFrameContext& frame);
		void Compose(const MotionPlayerFrameContext& frame);
		void Publish(const MotionPlayerFrameContext& frame);
		void AppendDrawRows();
		void Deliver(const MotionPlayerEvent& event);
		MotionPlayerCommands& m_Commands;
		MotionRootScene& m_Root;
		const MotionPlayerRenderControls& m_Render;
		MotionLayerIdentifierPool& m_Identifiers;
		MotionSpriteDrawContext& m_Context;
		MotionSelectedSpriteServices m_Services;
		MotionPlayerUpdate m_Update;
		std::unique_ptr<Selection> m_Selected;
		std::unique_ptr<MotionTimelineParameters> m_RetainedParameters;
		// Nonowning safety identities prevent reuse of expired tag-cache backing; these do not retain registrations.
		std::vector<MotionProjectId> m_PreparedProjects;
		// Native +3d0 owns rows independently of selected nodes; +4b0 owns only the current row-pointer queue.
		std::vector<MotionSpriteDrawRow> m_Rows;
		std::vector<MotionSpriteDrawRow*> m_PublishedRows;
		const resource::MotionTimeline* m_ClearedTimeline = nullptr;
		bool m_LayersCleared = false;
		bool m_Published = false;
		bool m_Operating = false;
	};

} // namespace sgre::scene
