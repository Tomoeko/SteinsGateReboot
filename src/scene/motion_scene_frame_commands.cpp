#include "sgre/scene/motion_scene_frame_commands.hpp"

#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_render_queue.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	class MotionSceneFrameCommands::Implementation {
	public:
		struct OrderedRow {
			MotionRenderOrder Order;
			std::size_t Packet = 0;
		};

		static std::size_t RequireCapacity(std::size_t value, std::size_t maximum) {
			if (value == 0 || value > maximum) {
				throw std::runtime_error("motion scene commands require bounded row/candidate storage");
			}

			return value;
		}

		Implementation(std::size_t rows, std::size_t candidates) : MaximumRows(RequireCapacity(rows, 65536)), MaximumCandidates(RequireCapacity(candidates, 1048576)), Order(MaximumRows) {
			Scenes.reserve(rows);
			PacketRows.reserve(rows);
			PacketScenes.reserve(rows);
			PacketNodes.reserve(rows);
			MaskSettings.reserve(rows);
			PacketCommands.reserve(rows);
			QueueCommands.reserve(rows);
			Clips.reserve(rows);
			Ordered.reserve(rows);
			IndividualIdentities.reserve(rows);
			IndividualBounds.reserve(rows);
			Groups.reserve(rows);
			GroupPrepared.reserve(rows);
			CandidatePrepared.reserve(rows);
			MaterialPrepared.reserve(rows);
			Candidates.reserve(candidates);
			ExternalTextures.reserve(65536);
		}

		void ConsumeWork(std::size_t amount) {
			if (amount > RemainingWork) {
				throw std::runtime_error("motion scene command preparation exceeds its bounded work");
			}
			RemainingWork -= amount;
		}

		const MotionBounds* IndividualBoundsFor(const MotionClipRectangle* clip) {
			if (clip == nullptr) {
				return nullptr;
			}
			for (std::size_t index = 0; index < IndividualIdentities.size(); ++index) {
				ConsumeWork(1);
				if (IndividualIdentities[index] == clip) {
					return &IndividualBounds[index];
				}
			}
			if (IndividualBounds.size() == MaximumRows) {
				throw std::runtime_error("motion scene individual clip identities exceed prepared storage");
			}
			MotionBounds bounds;
			bounds.MinimumX = clip->Left;
			bounds.MinimumY = clip->Top;
			bounds.MaximumX = clip->Right;
			bounds.MaximumY = clip->Bottom;
			IndividualIdentities.push_back(clip);
			IndividualBounds.push_back(bounds);

			return &IndividualBounds.back();
		}

		void PrepareMaterial(std::size_t index, const MotionClipNode& packet) {
			if (MaterialPrepared[index]) {
				return;
			}
			const auto actual = PacketScenes[index]->DrawState(*PacketRows[index]);
			auto& command = PacketCommands[index];
			command = MotionPreparedFrameCommand{};
			command.Clip = &packet.Command;
			if (packet.Command.Hidden || packet.Command.Geometry == nullptr) {
				throw std::runtime_error("motion scene material requested a hidden or absent actual geometry");
			}
			if (actual.Source == nullptr || actual.Numeric == nullptr || actual.Render == nullptr) {
				throw std::runtime_error("motion scene commands require their actual supported geometry/source/numeric/render owners");
			}
			if (packet.Command.Geometry->Positions.size() != MotionMeshGridVertexCount(actual.Subdivision)) {
				throw std::runtime_error("motion scene geometry differs from its actual selected subdivision product");
			}
			command.Subdivision = actual.Subdivision;
			const auto& source = actual.Source->State();
			if (!source.Ready || source.Blank || (!source.External && (!source.Binding || !source.Texture || source.Binding->Icon == nullptr || source.Texture->Slot.Index > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())))) {
				throw std::runtime_error("motion scene material has no exact ready source and registered texture slot");
			}
			if (source.External && (!source.External->Installed || source.External->Reference == nullptr)) {
				throw std::runtime_error("motion scene external source no longer has its actual installed listener");
			}
			if ((actual.Numeric->Hierarchy.Attributes.UnresolvedMask & 0x40000000u) != 0) {
				throw std::runtime_error("motion scene blend parameter requires its genuine prepared bp cell");
			}
			const auto material = source.External ? EvaluateMotionExternalMaterial(actual.Numeric->Hierarchy) : EvaluateMotionSpriteMaterial(actual.Numeric->Hierarchy, *source.Binding);
			if (!material) {
				throw std::runtime_error("motion scene visible descriptor lacks its recovered material");
			}
			command.IndividualClip = IndividualBoundsFor(actual.IndividualClip);
			if (source.External) {
				const auto& external = *source.External;
				if (external.Table == nullptr || external.Slot.Index >= 65536) {
					throw std::runtime_error("motion scene external descriptor requires its actual bounded resource-manager table");
				}
				const auto shared = external.Table->Get(external.Slot);
				if (shared.External != external.Reference) {
					throw std::runtime_error("motion scene external descriptor differs from its actual selected source registration");
				}
				if (ExternalTextures.size() <= external.Slot.Index) {
					ExternalTextures.resize(static_cast<std::size_t>(external.Slot.Index) + 1);
				}
				auto& published = ExternalTextures[external.Slot.Index];
				if (published.Reference != nullptr && (published.Table != external.Table || published.Slot != external.Slot || published.Reference != external.Reference)) {
					throw std::runtime_error("motion scene aggregation mixed distinct external source-manager slot identities");
				}
				published.Table = external.Table;
				published.Slot = external.Slot;
				published.Reference = external.Reference;
				published.Dimensions = shared.Dimensions;
			}
			command.Texture = source.External ? source.External->Slot.EncodedIndex() : static_cast<std::int32_t>(source.Texture->Slot.Index);
			command.BlendMode = material->BlendMode;
			// Native514575 clears decoded +2c; only bit40000000 reads bp at5130ca. Nondefault cells remain gated above.
			command.BlendParameter = 0;
			command.Filter = actual.Render->ColorFilter;
			command.FilterColor = actual.Render->ColorFilterParameter;
			command.Colors = material->Colors;
			if (source.External) {
				command.PixelUvs.X = static_cast<float>(source.External->Left);
				command.PixelUvs.Y = static_cast<float>(source.External->Top);
				command.PixelUvs.Width = static_cast<float>(source.External->Width);
				command.PixelUvs.Height = static_cast<float>(source.External->Height);
			} else {
				const auto& icon = *source.Binding->Icon;
				command.PixelUvs.X = std::trunc(icon.Left);
				command.PixelUvs.Y = std::trunc(icon.Top);
				command.PixelUvs.Width = std::trunc(icon.Width) * icon.Resolution;
				command.PixelUvs.Height = std::trunc(icon.Height) * icon.Resolution;
			}
			command.Key = material->TextureAttributes;
			MaterialPrepared[index] = 1;
		}

		void AddCandidate(const MotionClipCommand& descriptor, std::span<const MotionClipNode> packets) {
			if (descriptor.Hidden) {
				return;
			}
			if (descriptor.Geometry == nullptr || Candidates.size() == MaximumCandidates) {
				throw std::runtime_error("motion scene mask candidate lacks actual geometry or exceeds prepared storage");
			}
			std::optional<std::size_t> owner;
			for (std::size_t index = 0; index < packets.size(); ++index) {
				ConsumeWork(1);
				if (packets[index].Command.Geometry == descriptor.Geometry && !packets[index].Command.Hidden) {
					if (owner) {
						throw std::runtime_error("motion scene mask geometry has ambiguous actual row ownership");
					}
					owner = index;
				}
			}
			if (!owner) {
				throw std::runtime_error("motion scene mask descriptor has no actual packet-row material owner");
			}
			PrepareMaterial(*owner, packets[*owner]);
			MotionFrameCandidateBinding binding;
			binding.Descriptor = &descriptor;
			binding.Command = &PacketCommands[*owner];
			Candidates.push_back(binding);
		}

		void PrepareCandidates(std::span<const MotionClipNode> packets) {
			for (const auto& group : Groups) {
				std::optional<std::uint32_t> current = group.FirstNode;
				std::size_t visited = 0;
				while (current) {
					ConsumeWork(1);
					if (*current >= packets.size() || ++visited > packets.size()) {
						throw std::runtime_error("motion scene mask ancestry contains an invalid parent or cycle");
					}
					const auto& packet = packets[*current];
					const auto mode = packet.ModeFlags & 3u;
					if (!CandidatePrepared[*current] && (mode == 1 || mode == 2)) {
						if ((packet.ModeFlags & 4u) == 0) {
							AddCandidate(packet.Command, packets);
						} else {
							for (const auto& member : packet.Members) {
								ConsumeWork(1);
								AddCandidate(member, packets);
							}
						}
						CandidatePrepared[*current] = 1;
					}
					current = packet.Parent;
				}
			}
		}

		void AppendOrdered(std::span<const MotionRenderOrderReference> sorted, std::span<const MotionClipNode> packets) {
			for (const auto reference : sorted) {
				ConsumeWork(1);
				const auto ordinal = reference.get().Ordinal;
				if (ordinal >= Ordered.size()) {
					throw std::runtime_error("motion scene sorted ordinal escaped its actual append occurrence");
				}
				const auto index = Ordered[ordinal].Packet;
				const auto& packet = packets[index];
				// Native555610 excludes hidden/zero-opacity rows only after the complete actual queue is sorted.
				if (packet.Command.Hidden || packet.Command.GeometryCount == 0) {
					continue;
				}
				auto clip = packet.Command;
				if (packet.Parent) {
					if (*packet.Parent >= packets.size()) {
						throw std::runtime_error("motion scene row ancestor exceeds its actual packet graph");
					}
					clip.ClipGroup = static_cast<std::uint64_t>(*packet.Parent) + 1;
					if (!GroupPrepared[*packet.Parent]) {
						MotionFrameGroup group;
						// Identity represents exact ancestor equality within this borrowed packet generation.
						group.Identity = clip.ClipGroup;
						group.FirstNode = *packet.Parent;
						Groups.push_back(group);
						GroupPrepared[*packet.Parent] = 1;
					}
				} else {
					clip.ClipGroup = 0;
				}
				PrepareMaterial(index, packet);
				Clips.push_back(clip);
				auto command = PacketCommands[index];
				command.Clip = &Clips.back();
				QueueCommands.push_back(command);
			}
		}

		void Reset() {
			Order.Clear();
			Scenes.clear();
			PacketRows.clear();
			PacketScenes.clear();
			PacketNodes.clear();
			MaskSettings.clear();
			PacketCommands.clear();
			QueueCommands.clear();
			Clips.clear();
			Ordered.clear();
			IndividualIdentities.clear();
			IndividualBounds.clear();
			Groups.clear();
			Candidates.clear();
			ExternalTextures.clear();
		}

		std::size_t MaximumRows = 0;
		std::size_t MaximumCandidates = 0;
		std::size_t RemainingWork = 0;
		std::vector<const MotionSceneEvaluation*> Scenes;
		std::vector<const MotionStencilRowState*> PacketRows;
		std::vector<const MotionSceneEvaluation*> PacketScenes;
		std::vector<MotionClipNode> PacketNodes;
		std::vector<MotionMaskSettings> MaskSettings;
		MotionRenderQueue Order;
		std::vector<MotionPreparedFrameCommand> PacketCommands;
		std::vector<MotionPreparedFrameCommand> QueueCommands;
		std::vector<MotionClipCommand> Clips;
		std::vector<OrderedRow> Ordered;
		std::vector<const MotionClipRectangle*> IndividualIdentities;
		std::vector<MotionBounds> IndividualBounds;
		std::vector<MotionFrameGroup> Groups;
		std::vector<std::uint8_t> GroupPrepared;
		std::vector<std::uint8_t> CandidatePrepared;
		std::vector<std::uint8_t> MaterialPrepared;
		std::vector<MotionFrameCandidateBinding> Candidates;
		std::vector<MotionFrameExternalTexture> ExternalTextures;
		bool Preparing = false;
	};

	MotionSceneFrameCommands::MotionSceneFrameCommands(std::size_t maximum_rows, std::size_t maximum_candidates) : m_Implementation(std::make_unique<Implementation>(maximum_rows, maximum_candidates)) {
	}

	MotionSceneFrameCommands::~MotionSceneFrameCommands() = default;

	MotionPreparedFrame MotionSceneFrameCommands::Prepare(const MotionSceneEvaluation& scene, const MotionPreparedFrame& globals) {
		const MotionSceneEvaluation* actual = &scene;
		return Prepare(std::span<const MotionSceneEvaluation* const>(&actual, 1), globals);
	}

	MotionPreparedFrame MotionSceneFrameCommands::Prepare(std::span<const MotionSceneEvaluation* const> scenes, const MotionPreparedFrame& globals) {
		auto& state = *m_Implementation;
		if (state.Preparing || !state.Scenes.empty() || scenes.size() > state.MaximumRows || !globals.Queue.empty() || !globals.ClipQueue.empty() || !globals.Groups.empty() || !globals.Nodes.empty() || !globals.MaskSettings.empty() || !globals.Candidates.empty()) {
			throw std::runtime_error("motion scene command preparation requires an unborrowed owner and actual globals without substitute scene spans");
		}
		state.Preparing = true;
		try {
			state.RemainingWork = 1048576;
			for (const auto* scene : scenes) {
				if (scene == nullptr || std::find(state.Scenes.begin(), state.Scenes.end(), scene) != state.Scenes.end()) {
					throw std::runtime_error("motion source-manager queue requires distinct actual task scene occurrences");
				}
				state.Scenes.push_back(scene);
				const auto packets = scene->Packets().Nodes();
				const auto rows = scene->PacketRows();
				const auto settings = scene->Packets().Settings();
				const auto global_rows = scene->GlobalRows();
				if (packets.size() != rows.size() || settings.size() != packets.size() || packets.size() > state.MaximumRows - state.PacketNodes.size() || global_rows.size() > state.MaximumRows - state.Ordered.size()) {
					throw std::runtime_error("motion scene commands require bounded exact packet-indexed persistent rows");
				}
				const auto base = state.PacketNodes.size();
				for (std::size_t index = 0; index < rows.size(); ++index) {
					state.ConsumeWork(1);
					if (rows[index] == nullptr || scene->Packets().Index(*rows[index]) != index) {
						throw std::runtime_error("motion scene command source differs from its exact packet-indexed row");
					}
					auto packet = packets[index];
					if (packet.Parent) {
						if (*packet.Parent >= packets.size()) {
							throw std::runtime_error("motion scene packet parent escapes its actual selected task graph");
						}
						packet.Parent = static_cast<std::uint32_t>(base + *packet.Parent);
					}
					state.PacketNodes.push_back(packet);
					state.MaskSettings.push_back(settings[index]);
					state.PacketRows.push_back(rows[index]);
					state.PacketScenes.push_back(scene);
				}
				for (const auto* row : global_rows) {
					state.ConsumeWork(1);
					if (row == nullptr) {
						throw std::runtime_error("motion scene global queue contains no actual persistent row");
					}
					const auto actual = scene->DrawState(*row);
					if (actual.Numeric == nullptr) {
						throw std::runtime_error("motion scene global order lacks its actual numeric owner");
					}
					Implementation::OrderedRow entry;
					entry.Packet = base + scene->Packets().Index(*row);
					entry.Order.Primary = actual.Numeric->Hierarchy.Attributes.Coordinate[2];
					entry.Order.Secondary = actual.RootPriority;
					entry.Order.CommandIdentity = row->Node().Identifier;
					state.Ordered.push_back(entry);
					state.Order.Append(state.Ordered.back().Order);
				}
			}
			const auto packets = std::span<const MotionClipNode>(state.PacketNodes);
			state.PacketCommands.resize(packets.size());
			state.GroupPrepared.assign(packets.size(), 0);
			state.CandidatePrepared.assign(packets.size(), 0);
			state.MaterialPrepared.assign(packets.size(), 0);
			state.Order.Flush([&](auto sorted) {
				state.AppendOrdered(sorted, packets);
			});
			state.PrepareCandidates(packets);
			auto frame = globals;
			frame.Queue = state.QueueCommands;
			frame.ClipQueue = state.Clips;
			frame.Groups = state.Groups;
			frame.Nodes = packets;
			frame.MaskSettings = state.MaskSettings;
			frame.Candidates = state.Candidates;
			frame.ExternalTextures = state.ExternalTextures;
			state.Preparing = false;
			return frame;
		} catch (...) {
			state.Reset();
			state.Preparing = false;
			throw;
		}
	}

	bool MotionSceneFrameCommands::IsBorrowing(const MotionSceneEvaluation& scene) const noexcept {
		const auto& scenes = m_Implementation->Scenes;
		return std::find(scenes.begin(), scenes.end(), &scene) != scenes.end();
	}

	void MotionSceneFrameCommands::Clear() {
		if (m_Implementation->Preparing) {
			throw std::runtime_error("motion scene command owner cannot clear during actual preparation");
		}
		m_Implementation->Reset();
	}

} // namespace sgre::scene
