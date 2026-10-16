#include "sgre/scene/motion_frame_submission.hpp"
#include "sgre/scene/motion_stencil_state.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct FrameFixture {
	std::array<std::array<sgre::resource::MotionPoint, 4>, 3> Positions{};
	std::array<sgre::scene::MotionClipGeometry, 3> Geometries{};
	std::array<sgre::scene::MotionClipCommand, 3> Descriptors{};
	std::array<sgre::scene::MotionPreparedFrameCommand, 3> Commands{};
	std::array<sgre::scene::MotionClipCommand, 2> Members{};
	std::array<sgre::scene::MotionClipNode, 1> Nodes{};
	std::array<sgre::scene::MotionMaskSettings, 1> Settings{};
	std::array<sgre::scene::MotionFrameGroup, 1> Groups{};
	std::array<sgre::scene::MotionFrameCandidateBinding, 3> Candidates{};
	std::array<sgre::resource::MotionPoint, 2> TextureSizes{};
	std::array<sgre::resource::MotionPoint, 64> BatchPositions{};
	std::array<sgre::resource::MotionPoint, 64> BatchUvs{};
	std::array<std::uint32_t, 64> BatchColors{};
	std::array<std::uint32_t, 64> BatchIndices{};
	std::array<sgre::scene::MotionMaskOperation, 32> MaskOperations{};
	std::array<sgre::scene::MotionMaskedBlendPass, 32> BlendPasses{};
	sgre::scene::MotionPreparedFrame Frame;
	sgre::scene::MotionFrameScratch Scratch;

	FrameFixture() {
		for (std::size_t index = 0; index < Commands.size(); ++index) {
			const auto offset = static_cast<float>(index * 6);
			Positions[index][0].X = -2 + offset;
			Positions[index][0].Y = -2;
			Positions[index][1].X = 2 + offset;
			Positions[index][1].Y = -2;
			Positions[index][2].X = -2 + offset;
			Positions[index][2].Y = 2;
			Positions[index][3].X = 2 + offset;
			Positions[index][3].Y = 2;
			Geometries[index].Positions = Positions[index];
			Descriptors[index].Geometry = &Geometries[index];
			Descriptors[index].GeometryCount = 4;
			Commands[index].Clip = &Descriptors[index];
			Commands[index].PixelUvs.X = 4;
			Commands[index].PixelUvs.Y = 8;
			Commands[index].PixelUvs.Width = 16;
			Commands[index].PixelUvs.Height = 8;
			Commands[index].Colors.fill(0xff112233);
		}

		TextureSizes[0].X = 64;
		TextureSizes[0].Y = 32;
		TextureSizes[1].X = 32;
		TextureSizes[1].Y = 16;
		Frame.Queue = std::span<const sgre::scene::MotionPreparedFrameCommand>(Commands).first(2);
		Frame.ClipQueue = std::span<const sgre::scene::MotionClipCommand>(Descriptors).first(2);
		Frame.TextureSizes = TextureSizes;
		Frame.InitialScissor.MinimumX = 0;
		Frame.InitialScissor.MinimumY = 0;
		Frame.InitialScissor.MaximumX = 64;
		Frame.InitialScissor.MaximumY = 32;
		Frame.AppliedRoundedScissor = Frame.InitialScissor;
		Frame.View.Coefficients = {1.0f / 32, 0, 0, 0, 0, -1.0f / 16, 0, 0, 0, 0, 1, 0};
		Frame.Viewport.Width = 64;
		Frame.Viewport.Height = 32;
		Frame.TargetBounds.MinimumX = -32;
		Frame.TargetBounds.MinimumY = -16;
		Frame.TargetBounds.MaximumX = 32;
		Frame.TargetBounds.MaximumY = 16;
		Frame.AxisScale.X = 1;
		Frame.AxisScale.Y = 1;
		Scratch.Batch.Positions = BatchPositions;
		Scratch.Batch.Uvs = BatchUvs;
		Scratch.Batch.Colors = BatchColors;
		Scratch.Batch.Indices = BatchIndices;
		Scratch.MaskOperations = MaskOperations;
		Scratch.BlendPasses = BlendPasses;
	}

	FrameFixture(const FrameFixture&) = delete;
	FrameFixture& operator=(const FrameFixture&) = delete;

	void UseMask(std::uint8_t flags = 1) {
		Nodes[0].Command = Descriptors[2];
		Nodes[0].ModeFlags = flags;
		Members[0] = Descriptors[0];
		Members[1] = Descriptors[1];
		Nodes[0].Members = Members;
		Settings[0].ThresholdExtent = 1;
		Groups[0].Identity = 1;
		Candidates[0].Descriptor = &Nodes[0].Command;
		Candidates[0].Command = &Commands[2];
		Candidates[1].Descriptor = &Members[0];
		Candidates[1].Command = &Commands[0];
		Candidates[2].Descriptor = &Members[1];
		Candidates[2].Command = &Commands[1];
		Frame.Groups = Groups;
		Frame.Nodes = Nodes;
		Frame.MaskSettings = Settings;
		Frame.Candidates = Candidates;
	}
};

struct RecordedPacket {
	std::uint32_t Key = 0;
	std::vector<sgre::resource::MotionPoint> Positions;
	std::vector<sgre::resource::MotionPoint> Uvs;
	std::vector<std::uint32_t> Colors;
	std::vector<std::uint32_t> Indices;
};

struct FrameObservations {
	std::vector<sgre::scene::MotionFrameBlend> Blends;
	std::vector<sgre::scene::MotionFrameFilter> Filters;
	std::vector<sgre::scene::MotionFrameTexture> Textures;
	std::vector<sgre::scene::MotionFrameScissor> Scissors;
	std::vector<RecordedPacket> Packets;
	std::vector<sgre::scene::MotionMaskOperation> Masks;
	std::size_t Begins = 0;
	std::size_t Ends = 0;
	std::size_t Finishes = 0;
	std::size_t Candidates = 0;
	std::size_t Bounds = 0;

	void Record(const sgre::scene::MotionFrameEvent& event) {
		std::visit([this](const auto& value) {
			using Event = std::remove_cvref_t<decltype(value)>;
			if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameBegin>) {
				++Begins;
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameEnd>) {
				++Ends;
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameBlend>) {
				Blends.push_back(value);
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameFilter>) {
				Filters.push_back(value);
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameTexture>) {
				Textures.push_back(value);
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameScissor>) {
				Scissors.push_back(value);
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameMaskFinish>) {
				++Finishes;
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameCandidate>) {
				++Candidates;
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionFrameBlendBounds>) {
				++Bounds;
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionMaskOperation>) {
				Masks.push_back(value);
			} else if constexpr (std::is_same_v<Event, sgre::scene::MotionBatchSubmission>) {
				// Consume borrowed packet spans before the caller's scratch streams are reused.
				RecordedPacket packet;
				packet.Key = value.Key;
				packet.Positions.assign(value.Positions.begin(), value.Positions.end());
				packet.Uvs.assign(value.Uvs.begin(), value.Uvs.end());
				packet.Colors.assign(value.Colors.begin(), value.Colors.end());
				packet.Indices.assign(value.Indices.begin(), value.Indices.end());
				Packets.push_back(packet);
			}
		},
		    event);
	}

	void Submit(FrameFixture& fixture) {
		sgre::scene::SubmitPreparedMotionFrame(fixture.Frame, fixture.Scratch, [this](const sgre::scene::MotionFrameEvent& event) {
			Record(event);
		});
	}
};

static void TestCoalescingAndPacketData() {
	FrameFixture fixture;
	fixture.Commands[1].BlendParameter = -0.0f;
	FrameObservations observation;
	observation.Submit(fixture);
	Require(observation.Begins == 1 && observation.Ends == 1 && observation.Textures.size() == 1 && observation.Blends.size() == 1 && observation.Filters.size() == 1, "initial frame state or unchanged-state cache differs");
	Require(observation.Packets.size() == 1 && observation.Packets[0].Positions.size() == 8 && observation.Packets[0].Indices.size() == 10, "equal states must coalesce two ordinary quads into one native indexed strip");
	Require(observation.Packets[0].Colors[0] == 0x33ff1122 && observation.Packets[0].Uvs[0].X == 0.0625f && observation.Packets[0].Uvs[3].Y == 0.5f, "ordinary packet color rotation or pixel UV conversion differs");
	const std::vector<std::uint32_t> expected_indices{0, 2, 1, 3, 3, 4, 4, 6, 5, 7};
	Require(observation.Packets[0].Indices == expected_indices, "indexed strip must retain internal degenerates and remove its outer duplicates");
	Require(observation.Masks.size() == 1 && observation.Masks[0].Kind == sgre::scene::MotionMaskOperationKind::Configure && observation.Masks[0].ThresholdMinimum == 1 && observation.Masks[0].ThresholdMaximum == 0, "frame must restore mask control");
	Require(observation.Scissors.size() == 1 && !observation.Scissors[0].Update.Submission, "unchanged final scissor must submit raw state without a device rectangle");
}

static void TestKeyTextureAndFilterState() {
	FrameFixture fixture;
	fixture.Commands[1].Key = 1;
	FrameObservations keyed;
	keyed.Submit(fixture);
	Require(keyed.Packets.size() == 2 && keyed.Packets[0].Key == 0 && keyed.Packets[1].Key == 1 && keyed.Textures.size() == 1, "packet key change must flush without reselecting unchanged texture");
	fixture.Commands[1].Key = 0;
	fixture.Commands[1].Texture = 1;
	fixture.Commands[1].Filter = 2;
	fixture.Commands[1].FilterColor = 0xff00aabb;
	fixture.Commands[1].BlendMode = 1;
	fixture.Commands[1].BlendParameter = 0.25f;
	FrameObservations changed;
	changed.Submit(fixture);
	Require(changed.Textures.size() == 2 && changed.Textures[1].Index == 1 && changed.Packets.size() == 2, "texture state change must flush the prior packet");
	Require(changed.Filters.size() == 2 && changed.Filters[1].Id == 2 && changed.Filters[1].Color == 0xbbff00aa, "filter color rotation or source cache differs");
	Require(changed.Blends.size() == 2 && changed.Blends[1].Mode == 1 && changed.Blends[1].Parameter == 0.25f && changed.Packets[1].Uvs[0].X == 0.125f, "blend state change or texture-specific UV dimensions differ");
}

static void TestClipIdentityAndOffscreenSuppression() {
	FrameFixture fixture;
	sgre::scene::MotionBounds first;
	first.MinimumX = -5;
	first.MinimumY = -5;
	first.MaximumX = 5;
	first.MaximumY = 5;
	const auto equal_value = first;
	fixture.Commands[0].IndividualClip = &first;
	fixture.Commands[1].IndividualClip = &equal_value;
	FrameObservations distinct;
	distinct.Submit(fixture);
	Require(distinct.Scissors.size() == 3 && distinct.Packets.size() == 2 && distinct.Scissors[0].Update.Submission && !distinct.Scissors[1].Update.Submission && distinct.Scissors[2].Update.Submission, "distinct equal clip identities must flush/reissue raw clip while device cache suppresses the duplicate rectangle");
	fixture.Commands[1].IndividualClip = &first;
	FrameObservations shared;
	shared.Submit(fixture);
	Require(shared.Scissors.size() == 2 && shared.Packets.size() == 1, "a shared clip identity must preserve coalescing");
	first.MinimumX = 1000;
	first.MaximumX = 1005;
	FrameObservations offscreen;
	offscreen.Submit(fixture);
	Require(offscreen.Packets.empty() && offscreen.Textures.empty() && offscreen.Scissors.size() == 1 && offscreen.Ends == 1, "an invalid individual scissor must suppress ordinary data/state selection and retain final restore");
}

static void TestMaskMembersWhitePacketAndTextureReset() {
	FrameFixture fixture;
	fixture.Descriptors[0].GeometryCount = 0;
	fixture.Descriptors[1].ClipGroup = 1;
	fixture.Descriptors[2].Hidden = true;
	fixture.UseMask(5);
	fixture.Nodes[0].Members = std::span<const sgre::scene::MotionClipCommand>(fixture.Members).first(1);
	FrameObservations observation;
	observation.Submit(fixture);
	Require(observation.Candidates == 1 && observation.Packets.size() == 2 && observation.Packets[0].Key == 4 && observation.Packets[1].Key == 0, "visible mask member with zero ordinary geometry count must still emit its own mask packet");
	for (const auto color : observation.Packets[0].Colors) {
		Require(color == 0xffffffff, "mask member must use opaque white rather than ordinary corner color");
	}

	Require(observation.Textures.size() == 2 && observation.Textures[0].Index == 0 && observation.Textures[1].Index == 0, "mask resolve must invalidate the texture cache before ordinary submission");
	Require(observation.Finishes == 1 && observation.Masks[0].Kind == sgre::scene::MotionMaskOperationKind::Prepare && observation.Masks[3].Kind == sgre::scene::MotionMaskOperationKind::Resolve && observation.Masks[3].Reference == 1, "mask pass lifecycle or final finish differs");
}

static void TestDisjointMaskAndOrdinaryClip() {
	FrameFixture fixture;
	fixture.Descriptors[0].GeometryCount = 0;
	fixture.Descriptors[1].ClipGroup = 1;
	fixture.Positions[1] = fixture.Positions[0];
	sgre::scene::MotionBounds individual;
	individual.MinimumX = -20;
	individual.MinimumY = -10;
	individual.MaximumX = -15;
	individual.MaximumY = -5;
	fixture.Commands[0].IndividualClip = &individual;
	fixture.Commands[1].IndividualClip = &individual;
	fixture.UseMask(5);
	fixture.Nodes[0].Members = std::span<const sgre::scene::MotionClipCommand>(fixture.Members).first(1);
	fixture.Frame.MaskRegionClipping = true;
	FrameObservations observation;
	observation.Submit(fixture);
	Require(observation.Packets.size() == 1 && observation.Packets[0].Key == 4 && observation.Textures.size() == 1, "disjoint valid group/individual clips retain mask candidate append while suppressing ordinary draw");
	Require(observation.Scissors.front().Update.Raw.MinimumX == 28 && observation.Scissors.front().Update.Raw.MinimumY == 12, "automatic group scissor must pad transformed bounds by two");
}

static void TestActualCopiedDarkenEvents() {
	FrameFixture fixture;
	fixture.Commands[0].BlendMode = 0x118;
	fixture.Commands[1].BlendMode = 0x118;
	fixture.Commands[1].Key = 4;
	std::size_t bounds = 0;
	std::size_t packets = 0;
	std::uint32_t current_blend = 0;
	bool prepared_copy = false;
	sgre::scene::SubmitPreparedMotionFrame(fixture.Frame, fixture.Scratch, [&](const sgre::scene::MotionFrameEvent& event) {
		if (const auto* blend = std::get_if<sgre::scene::MotionFrameBlend>(&event)) {
			current_blend = blend->Mode;
		} else if (const auto* region = std::get_if<sgre::scene::MotionFrameBlendBounds>(&event)) {
			Require(current_blend == 0x118 && !prepared_copy && region->Bounds.MinimumX < region->Bounds.MaximumX && region->Bounds.MinimumY < region->Bounds.MaximumY, "copied darken must emit the exact nonempty bounds after selecting its real mode");
			prepared_copy = true;
			++bounds;
		} else if (const auto* packet = std::get_if<sgre::scene::MotionBatchSubmission>(&event)) {
			Require(prepared_copy && packet->Key == (packets == 0 ? 0u : 4u) && packet->Positions.size() == 4, "copied darken must flush each source row after its real copy event and preserve actual attribute key");
			prepared_copy = false;
			++packets;
		}
	});
	Require(bounds == 2 && packets == 2 && !prepared_copy, "copied darken must not coalesce rows across native per-row copy bounds");
}

static void TestSpecialBlendFramePackets() {
	FrameFixture fixture;
	fixture.Frame.Queue = std::span<const sgre::scene::MotionPreparedFrameCommand>(fixture.Commands).first(1);
	fixture.Frame.ClipQueue = std::span<const sgre::scene::MotionClipCommand>(fixture.Descriptors).first(1);
	fixture.Commands[0].BlendMode = 0x10d;
	fixture.Commands[0].BlendParameter = 0.25f;
	FrameObservations observation;
	observation.Submit(fixture);
	Require(observation.Packets.size() == 6 && observation.Bounds == 6 && observation.Blends.size() == 8 && fixture.Geometries[0].Calculated, "special blend must select its passes, measure bounds and flush each quad packet");
	const std::array<float, 6> selectors{4, 9, 3, 8, 0, 5};
	for (std::size_t index = 0; index < selectors.size(); ++index) {
		Require(observation.Blends[index + 2].Mode == 0x10d && observation.Blends[index + 2].Parameter == selectors[index] && observation.Packets[index].Positions.size() == 4, "special blend packet parameter/order differs");
	}
}

static void TestPreparedStencilSink() {
	for (const auto flags : std::array<std::uint8_t, 2>{1, 2}) {
		FrameFixture fixture;
		fixture.Descriptors[1].ClipGroup = 1;
		fixture.UseMask(flags);
		sgre::scene::MotionStencilControl control;
		sgre::scene::MotionStencilBinding binding;
		binding.Descriptor.DepthEnabled = true;
		binding.Descriptor.DepthWriteEnabled = false;
		binding.Descriptor.DepthComparison = sgre::scene::MotionStencilComparison::LessEqual;
		binding.Reference = 99;
		std::size_t mask_packets = 0;
		std::size_t resolved_packets = 0;
		std::uint32_t clear_reference = 999;
		sgre::scene::SubmitPreparedMotionFrame(fixture.Frame, fixture.Scratch, [&](const sgre::scene::MotionFrameEvent& event) {
			std::optional<sgre::scene::MotionStencilAction> action;
			std::uint32_t reference = 0;
			if (const auto* operation = std::get_if<sgre::scene::MotionMaskOperation>(&event)) {
				reference = operation->Reference;
				switch (operation->Kind) {
				case sgre::scene::MotionMaskOperationKind::Prepare:
					action = sgre::scene::MotionStencilAction::Prepare;
					break;
				case sgre::scene::MotionMaskOperationKind::Begin:
					action = sgre::scene::MotionStencilAction::Begin;
					break;
				case sgre::scene::MotionMaskOperationKind::End:
					action = sgre::scene::MotionStencilAction::End;
					break;
				case sgre::scene::MotionMaskOperationKind::Resolve:
					action = sgre::scene::MotionStencilAction::Resolve;
					break;
				default:
					break;
				}
			} else if (std::holds_alternative<sgre::scene::MotionFrameMaskFinish>(event)) {
				action = sgre::scene::MotionStencilAction::Finish;
			}

			if (action) {
				const auto transition = sgre::scene::PrepareMotionStencilTransition(*action, control, binding, reference, true);
				control = transition.Control;
				if (transition.ClearStencil) {
					clear_reference = *transition.ClearStencil;
				}
				if (transition.Binding) {
					binding = *transition.Binding;
				}
			}

			if (const auto* packet = std::get_if<sgre::scene::MotionBatchSubmission>(&event)) {
				Require(binding.Descriptor.DepthEnabled && !binding.Descriptor.DepthWriteEnabled && binding.Descriptor.DepthComparison == sgre::scene::MotionStencilComparison::LessEqual, "frame stencil sink must preserve its existing depth policy");
				if (packet->Key == 4) {
					++mask_packets;
					const auto expected = flags == 1 ? sgre::scene::MotionStencilOperation::IncrementWrap : sgre::scene::MotionStencilOperation::DecrementWrap;
					Require(control.Phase == sgre::scene::MotionMaskPhase::StencilWrite && binding.Descriptor.Front.Pass == expected && binding.Reference == (flags == 1 ? 0u : 1u), "frame mask packet must observe the prepared stencil write state");
				} else if (control.Phase == sgre::scene::MotionMaskPhase::Resolved) {
					++resolved_packets;
					Require(binding.Descriptor.StencilEnabled && binding.Reference == 1 && binding.Descriptor.Front.Pass == sgre::scene::MotionStencilOperation::Keep, "resolved ordinary packet must use the final equal/keep stencil reference");
				}
			}
		});
		Require(mask_packets == 1 && resolved_packets == 1 && clear_reference == (flags == 1 ? 0u : 1u), "frame phase records must route to the native stencil helper contract");
		Require(!binding.Descriptor.StencilEnabled && binding.Reference == 1 && !control.MaskActive && control.Phase == sgre::scene::MotionMaskPhase::Idle, "frame finish must disable stencil while preserving the final reference");
	}
}

static void TestPreflightAndCallbackFailure() {
	FrameFixture fixture;
	fixture.Descriptors[1].ClipGroup = 1;
	fixture.UseMask();
	fixture.Frame.Candidates = {};
	FrameObservations invalid;
	bool rejected = false;
	try {
		invalid.Submit(fixture);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && invalid.Begins == 0, "an unprepared mask candidate must fail before frame begin");
	fixture.Descriptors[1].ClipGroup = 0;
	fixture.Frame.Groups = {};
	bool callback_failed = false;
	try {
		sgre::scene::SubmitPreparedMotionFrame(fixture.Frame, fixture.Scratch, [](const sgre::scene::MotionFrameEvent& event) {
			if (std::holds_alternative<sgre::scene::MotionFrameTexture>(event)) {
				throw std::runtime_error("independent synthetic sink failure");
			}
		});
	} catch (const std::runtime_error&) {
		callback_failed = true;
	}

	Require(callback_failed, "portable sink diagnostic must propagate to its C++ caller");
	FrameObservations repeated;
	repeated.Submit(fixture);
	Require(repeated.Packets.size() == 1 && repeated.Packets[0].Positions.size() == 8 && repeated.Ends == 1, "caller-owned scratch must remain reusable after failed frame owner unwinds");
}

static void TestActualGridFrame() {
	FrameFixture fixture;
	std::array<sgre::resource::MotionPoint, 9> positions{};
	for (std::size_t row = 0; row < 3; ++row) {
		for (std::size_t column = 0; column < 3; ++column) {
			positions[row * 3 + column].X = static_cast<float>(column * 2) - 2;
			positions[row * 3 + column].Y = static_cast<float>(row * 2) - 2;
		}
	}
	fixture.Geometries[0].Positions = positions;
	fixture.Descriptors[0].GeometryCount = 9;
	fixture.Commands[0].Subdivision.X = 2;
	fixture.Commands[0].Subdivision.Y = 2;
	FrameObservations observations;
	sgre::scene::SubmitPreparedMotionFrame(fixture.Frame, fixture.Scratch, [&](const auto& event) {
		observations.Record(event);
	});
	Require(observations.Packets.size() == 1 && observations.Packets[0].Positions.size() == 13 && observations.Packets[0].Indices.size() == 20, "full frame failed to coalesce actual grid and quad through the same native mode2 batch");
	const std::array<std::uint32_t, 20> expected{0, 3, 1, 4, 2, 5, 5, 3, 3, 6, 4, 7, 5, 8, 8, 9, 9, 11, 10, 12};
	Require(std::equal(expected.begin(), expected.end(), observations.Packets[0].Indices.begin()), "full grid frame changed native inter-row or inter-command index order");
	fixture.UseMask(5);
	fixture.Descriptors[0].ClipGroup = 1;
	fixture.Descriptors[1].ClipGroup = 1;
	observations = FrameObservations{};
	sgre::scene::SubmitPreparedMotionFrame(fixture.Frame, fixture.Scratch, [&](const auto& event) {
		observations.Record(event);
	});
	Require(observations.Candidates == 2 && observations.Packets.size() >= 2 && observations.Packets.front().Positions.size() == 13, "actual composite mask candidates did not retain their complete grid topology");
	Require(std::all_of(observations.Packets.front().Colors.begin(), observations.Packets.front().Colors.end(), [](auto color) {
		return color == 0xffffffff;
	}),
	    "grid mask must use the native white color path for every generated vertex");
}

static void TestAggregateGridWorkPreflight() {
	FrameFixture fixture;
	std::vector<sgre::resource::MotionPoint> positions(2002);
	fixture.Geometries[0].Positions = positions;
	std::vector<sgre::scene::MotionClipCommand> descriptors(512, fixture.Descriptors[0]);
	std::vector<sgre::scene::MotionPreparedFrameCommand> commands(512, fixture.Commands[0]);
	for (std::size_t index = 0; index < commands.size(); ++index) {
		commands[index].Clip = &descriptors[index];
		commands[index].Subdivision.X = 1000;
		commands[index].Subdivision.Y = 1;
	}
	fixture.Frame.Queue = commands;
	fixture.Frame.ClipQueue = descriptors;
	FrameObservations observations;
	bool rejected = false;
	try {
		observations.Submit(fixture);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && observations.Begins == 0, "aggregate grid vertex/index work must reject before frame begin and output mutation");
}

static void TestSparseActualLocalSlots() {
	FrameFixture fixture;
	fixture.TextureSizes[0] = {};
	for (auto& command : fixture.Commands) {
		command.Texture = 1;
	}
	FrameObservations output;
	output.Submit(fixture);
	Require(output.Textures.size() == 1 && output.Textures[0].Index == 1 && output.Packets.size() == 1 && output.Packets[0].Uvs[0].X == .125f && output.Packets[0].Uvs[0].Y == .5f, "unused empty shared slot changed the later real source index or UV dimensions");
	for (const auto malformed : std::array<sgre::resource::MotionPoint, 4>{{{}, {32, 0}, {0, 16}, {std::numeric_limits<float>::quiet_NaN(), 16}}}) {
		fixture.TextureSizes[1] = malformed;
		FrameObservations rejected;
		bool failed = false;
		try {
			rejected.Submit(fixture);
		} catch (const std::runtime_error& error) {
			failed = std::string_view(error.what()).find("positive finite") != std::string_view::npos;
		}
		Require(failed && rejected.Begins == 0, "used empty/partial/NaN local dimensions must reject before any frame event");
	}
}

int main() {
	try {
		TestSparseActualLocalSlots();
		TestAggregateGridWorkPreflight();
		TestActualGridFrame();
		TestCoalescingAndPacketData();
		TestKeyTextureAndFilterState();
		TestClipIdentityAndOffscreenSuppression();
		TestMaskMembersWhitePacketAndTextureReset();
		TestDisjointMaskAndOrdinaryClip();
		TestActualCopiedDarkenEvents();
		TestSpecialBlendFramePackets();
		TestPreparedStencilSink();
		TestPreflightAndCallbackFailure();
		std::cout << "motion frame submission tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
