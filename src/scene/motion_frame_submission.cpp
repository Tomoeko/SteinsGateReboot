#include "sgre/scene/motion_frame_submission.hpp"

#include <bit>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaximumFrameWork = 1048576;
	static constexpr std::size_t kMaximumFrameEntries = 65536;

	static void ConsumeWork(std::size_t& remaining, std::size_t amount) {
		if (amount > remaining) {
			throw std::runtime_error("motion frame exceeds its prepared work bound");
		}

		remaining -= amount;
	}

	static void RequireFiniteBounds(const MotionBounds& bounds) {
		if (!std::isfinite(bounds.MinimumX) || !std::isfinite(bounds.MinimumY) || !std::isfinite(bounds.MaximumX) || !std::isfinite(bounds.MaximumY)) {
			throw std::runtime_error("motion frame requires finite prepared rectangles");
		}
	}

	static std::size_t ChainWork(const MotionPreparedFrame& input, const MotionFrameGroup& group) {
		std::size_t remaining = kMaximumFrameWork;
		std::size_t visited = 0;
		std::optional<std::uint32_t> index = group.FirstNode;
		while (index) {
			if (*index >= input.Nodes.size() || ++visited > input.Nodes.size()) {
				throw std::runtime_error("motion frame clip chain contains an invalid parent or cycle");
			}

			const auto& node = input.Nodes[*index];
			ConsumeWork(remaining, 4);
			ConsumeWork(remaining, node.Members.size());
			index = node.Parent;
		}

		return kMaximumFrameWork - remaining;
	}

	static bool ValidBounds(const MotionBounds& bounds) noexcept {
		return bounds.MaximumX >= bounds.MinimumX && bounds.MaximumY >= bounds.MinimumY;
	}

	static MotionBounds IntersectBounds(const MotionBounds& left, const MotionBounds& right) noexcept {
		MotionBounds result;
		result.MinimumX = left.MinimumX > right.MinimumX ? left.MinimumX : right.MinimumX;
		result.MinimumY = left.MinimumY > right.MinimumY ? left.MinimumY : right.MinimumY;
		result.MaximumX = left.MaximumX < right.MaximumX ? left.MaximumX : right.MaximumX;
		result.MaximumY = left.MaximumY < right.MaximumY ? left.MaximumY : right.MaximumY;

		return result;
	}

	static const MotionFrameGroup& FindGroup(const MotionPreparedFrame& input, std::uint64_t identity, std::size_t& remaining) {
		for (const auto& group : input.Groups) {
			if (remaining == 0) {
				throw std::runtime_error("motion frame exceeds its prepared work bound");
			}

			--remaining;
			if (group.Identity == identity) {
				return group;
			}
		}

		throw std::runtime_error("motion frame references an unprepared clip group");
	}

	static const MotionPreparedFrameCommand& FindCandidate(const MotionPreparedFrame& input, const MotionClipCommand* descriptor, std::size_t& remaining) {
		for (const auto& candidate : input.Candidates) {
			if (remaining == 0) {
				throw std::runtime_error("motion frame exceeds its prepared candidate work bound");
			}

			--remaining;
			if (candidate.Descriptor == descriptor && candidate.Command) {
				return *candidate.Command;
			}
		}

		throw std::runtime_error("motion frame mask candidate has no prepared command binding");
	}

	static resource::MotionPoint FrameTextureSize(const MotionPreparedFrame& input, std::int32_t index) {
		if (index >= 0) {
			if (static_cast<std::size_t>(index) >= input.TextureSizes.size()) {
				throw std::runtime_error("motion frame texture index exceeds its actual local source table");
			}

			const auto size = input.TextureSizes[static_cast<std::size_t>(index)];
			if (!std::isfinite(size.X) || !std::isfinite(size.Y) || size.X <= 0 || size.Y <= 0) {
				throw std::runtime_error("motion frame referenced local texture requires positive finite prepared dimensions");
			}

			return size;
		}
		const auto shared_index = ~std::bit_cast<std::uint32_t>(index);
		if (shared_index >= input.ExternalTextures.size()) {
			throw std::runtime_error("motion frame negative texture index exceeds its actual shared table");
		}
		const auto& binding = input.ExternalTextures[shared_index];
		if (binding.Table == nullptr || binding.Reference == nullptr || binding.Slot.Index != shared_index) {
			throw std::runtime_error("motion frame negative texture index points to an unoccupied actual shared slot");
		}
		const auto current = binding.Table->Get(binding.Slot);
		if (current.External != binding.Reference || std::bit_cast<std::uint32_t>(current.Dimensions.X) != std::bit_cast<std::uint32_t>(binding.Dimensions.X) || std::bit_cast<std::uint32_t>(current.Dimensions.Y) != std::bit_cast<std::uint32_t>(binding.Dimensions.Y)) {
			throw std::runtime_error("motion frame external descriptor or first-use dimensions changed before submission");
		}
		if (!std::isfinite(binding.Dimensions.X) || !std::isfinite(binding.Dimensions.Y) || binding.Dimensions.X <= 0 || binding.Dimensions.Y <= 0) {
			throw std::runtime_error("motion frame external UV arithmetic requires finite positive shared dimensions");
		}

		return binding.Dimensions;
	}

	static void ValidateCommand(const MotionPreparedFrameCommand& command, const MotionPreparedFrame& input, std::size_t& geometry_work) {
		if (!command.Clip || !command.Clip->Geometry || command.Clip->Geometry->Positions.size() != MotionMeshGridVertexCount(command.Subdivision)) {
			throw std::runtime_error("motion frame command differs from its actual bounded row-major subdivision geometry");
		}

		ConsumeWork(geometry_work, command.Clip->Geometry->Positions.size());
		const auto strip_indices = static_cast<std::size_t>(command.Subdivision.Y) * (2 * (static_cast<std::size_t>(command.Subdivision.X) + 1) + 2);
		ConsumeWork(geometry_work, strip_indices);

		if (!std::isfinite(command.BlendParameter) || ((command.BlendMode & ~std::uint32_t{0x10}) > 5 && (command.BlendMode & 0xff0f) != 0x10d && command.BlendMode != 0x110 && command.BlendMode != 0x112 && command.BlendMode != 0x118)) {
			throw std::runtime_error("motion frame command has an unprepared texture or unsupported blend mode");
		}

		if (!std::isfinite(command.PixelUvs.X) || !std::isfinite(command.PixelUvs.Y) || !std::isfinite(command.PixelUvs.Width) || !std::isfinite(command.PixelUvs.Height)) {
			throw std::runtime_error("motion frame requires finite prepared pixel UVs");
		}

		const auto texture_size = FrameTextureSize(input, command.Texture);
		const auto inverse_x = 1.0f / texture_size.X;
		const auto inverse_y = 1.0f / texture_size.Y;
		if (!std::isfinite(inverse_x) || !std::isfinite(inverse_y) || !std::isfinite(command.PixelUvs.X * inverse_x + command.PixelUvs.Width * inverse_x) || !std::isfinite(command.PixelUvs.Y * inverse_y + command.PixelUvs.Height * inverse_y)) {
			throw std::runtime_error("motion frame UV arithmetic exceeds its finite scope");
		}

		if (command.IndividualClip) {
			RequireFiniteBounds(*command.IndividualClip);
			static_cast<void>(TransformMotionScissorBounds(*command.IndividualClip, input.View, input.Model, input.Viewport));
		}

		RequireFiniteBounds(command.Clip->Geometry->CachedBounds);
		for (const auto& point : command.Clip->Geometry->Positions) {
			if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
				throw std::runtime_error("motion frame requires finite prepared grid positions");
			}
		}
	}

	static void ValidateFrame(const MotionPreparedFrame& input, const MotionFrameScratch& scratch) {
		if (input.Queue.size() != input.ClipQueue.size() || input.Queue.size() > kMaximumFrameEntries || input.Groups.size() > kMaximumFrameEntries || input.Nodes.size() > kMaximumFrameEntries || input.TextureSizes.size() > kMaximumFrameEntries || input.ExternalTextures.size() > kMaximumFrameEntries || input.Candidates.size() > kMaximumFrameWork || input.MaskSettings.size() != input.Nodes.size()) {
			throw std::runtime_error("motion frame prepared storage is inconsistent or exceeds bounds");
		}

		RequireFiniteBounds(input.InitialScissor);
		if (!ValidBounds(input.InitialScissor) || input.Viewport.Width <= 0 || input.Viewport.Height <= 0) {
			throw std::runtime_error("motion frame requires a valid initial scissor and positive physical viewport");
		}

		for (const auto coefficient : input.View.Coefficients) {
			if (!std::isfinite(coefficient)) {
				throw std::runtime_error("motion frame view matrix must be finite");
			}
		}

		for (const auto coefficient : input.Model.Coefficients) {
			if (!std::isfinite(coefficient)) {
				throw std::runtime_error("motion frame model matrix must be finite");
			}
		}

		for (const auto& size : input.TextureSizes) {
			if (!std::isfinite(size.X) || !std::isfinite(size.Y) || size.X < 0 || size.Y < 0 || (size.X == 0) != (size.Y == 0)) {
				throw std::runtime_error("motion frame local slot dimensions must be an empty zero pair or positive finite dimensions");
			}
		}

		std::size_t geometry_work = kMaximumFrameWork;
		for (std::size_t index = 0; index < input.Queue.size(); ++index) {
			const auto& command = input.Queue[index];
			ValidateCommand(command, input, geometry_work);
			const auto& clip = input.ClipQueue[index];
			if (command.Clip->Geometry != clip.Geometry || command.Clip->Hidden != clip.Hidden || command.Clip->GeometryCount != clip.GeometryCount || command.Clip->ClipGroup != clip.ClipGroup) {
				throw std::runtime_error("motion frame queue descriptor does not match its prepared clip view");
			}
		}

		std::size_t candidate_remaining = kMaximumFrameWork;
		for (std::size_t index = 0; index < input.Candidates.size(); ++index) {
			const auto& binding = input.Candidates[index];
			ConsumeWork(candidate_remaining, index);
			for (std::size_t previous = 0; previous < index; ++previous) {
				if (input.Candidates[previous].Descriptor == binding.Descriptor) {
					throw std::runtime_error("motion frame candidate descriptor binding is duplicated");
				}
			}
			if (!binding.Descriptor || !binding.Command || !binding.Command->Clip || binding.Descriptor->Geometry != binding.Command->Clip->Geometry || binding.Descriptor->Hidden != binding.Command->Clip->Hidden) {
				throw std::runtime_error("motion frame has an invalid borrowed mask candidate binding");
			}

			ValidateCommand(*binding.Command, input, geometry_work);
		}

		std::size_t remaining = kMaximumFrameWork;
		for (std::size_t index = 0; index < input.Groups.size(); ++index) {
			const auto& group = input.Groups[index];
			if (group.Identity == 0 || group.FirstNode >= input.Nodes.size()) {
				throw std::runtime_error("motion frame has an invalid clip group identity");
			}

			ConsumeWork(remaining, index);
			for (std::size_t previous = 0; previous < index; ++previous) {
				if (input.Groups[previous].Identity == group.Identity) {
					throw std::runtime_error("motion frame clip group identity is duplicated");
				}
			}

			ConsumeWork(remaining, ChainWork(input, group));
			const auto count = PrepareMotionMaskPlan(input.Nodes, input.MaskSettings, group.FirstNode, scratch.MaskOperations);
			ConsumeWork(remaining, count);
			for (const auto& operation : std::span<const MotionMaskOperation>(scratch.MaskOperations).first(count)) {
				if (operation.Kind == MotionMaskOperationKind::VisitCommand) {
					static_cast<void>(FindCandidate(input, operation.Command, remaining));
				}
			}
		}

		for (const auto& command : input.Queue) {
			if (command.Clip->ClipGroup) {
				static_cast<void>(FindGroup(input, command.Clip->ClipGroup, remaining));
			}

			if ((command.BlendMode & 0xff0f) == 0x10d) {
				MotionMaskedBlendInput blend;
				blend.TargetBounds = input.TargetBounds;
				blend.AxisScale = input.AxisScale;
				blend.Amount = command.BlendParameter;
				const auto count = PrepareMotionMaskedBlendPasses(blend, scratch.BlendPasses);
				ConsumeWork(remaining, count);
			}
		}
	}

	class MotionFrameSubmission {
	public:
		MotionFrameSubmission(const MotionPreparedFrame& input, const MotionFrameScratch& scratch, const std::function<void(const MotionFrameEvent&)>& output)
		    : m_Input(input), m_Scratch(scratch), m_Output(output), m_Batch(MotionBatchMode::IndexedTriangleStrip, scratch.Batch), m_AppliedScissor(input.AppliedRoundedScissor), m_IndividualBounds(input.InitialScissor) {
		}

		void Submit() {
			m_Output(MotionFrameBegin{});
			SetBlend(0, 0);
			SetFilter(0, 0);
			std::uint64_t previous_group = 0;
			for (std::size_t index = 0; index < m_Input.Queue.size(); ++index) {
				const auto& command = m_Input.Queue[index];
				if (command.Clip->Hidden || command.Clip->GeometryCount == 0) {
					continue;
				}

				const auto group = command.Clip->ClipGroup;
				if (group != previous_group) {
					if (group) {
						PrepareGroup(group, index);
					} else {
						m_GroupBounds = MotionBounds{};
						m_DrawOrdinary = ApplyCombinedScissor();

						Flush();
						m_Output(MotionFrameMaskFinish{});
					}

					previous_group = group;
				}

				UpdateIndividualClip(command);
				if (m_IndividualValid && m_DrawOrdinary) {
					DrawOrdinary(command);
				}
			}

			if (previous_group) {
				Flush();
				m_Output(MotionFrameMaskFinish{});
			}

			MotionMaskOperation reset;
			reset.Kind = MotionMaskOperationKind::Configure;
			reset.ThresholdMinimum = 1;
			Flush();
			m_Output(reset);
			SetScissor(m_Input.InitialScissor);
			Flush();
			m_Output(MotionFrameEnd{});
		}

	private:
		void Flush() {
			m_Batch.Flush([this](const MotionBatchSubmission& submission) {
				m_Output(submission);
			});
		}

		void SetBlend(std::uint32_t mode, float parameter) {
			Flush();
			MotionFrameBlend event;
			event.Mode = mode;
			event.Parameter = parameter;
			m_Output(event);
		}

		void SetFilter(std::uint32_t filter, std::uint32_t color) {
			Flush();
			MotionFrameFilter event;
			event.Id = filter;
			event.Color = color;
			m_Output(event);
		}

		void SetScissor(const MotionBounds& bounds) {
			Flush();
			MotionFrameScissor event;
			event.Update = PrepareMotionScissorUpdate(bounds, m_AppliedScissor);
			m_Output(event);
			m_AppliedScissor = event.Update.Rounded;
		}

		bool ApplyCombinedScissor() {
			const bool group_valid = ValidBounds(m_GroupBounds);
			MotionBounds bounds;
			if (m_IndividualValid) {
				bounds = group_valid ? IntersectBounds(m_IndividualBounds, m_GroupBounds) : m_IndividualBounds;
			} else if (group_valid) {
				bounds = m_GroupBounds;
			} else {
				return false;
			}

			if (!ValidBounds(bounds)) {
				return false;
			}

			SetScissor(bounds);

			return true;
		}

		void UpdateIndividualClip(const MotionPreparedFrameCommand& command) {
			if (m_IndividualClip == command.IndividualClip) {
				return;
			}

			m_IndividualClip = command.IndividualClip;
			if (m_IndividualClip) {
				const auto transformed = TransformMotionScissorBounds(*m_IndividualClip, m_Input.View, m_Input.Model, m_Input.Viewport);
				m_IndividualBounds = IntersectBounds(transformed, m_Input.InitialScissor);
			} else {
				m_IndividualBounds = m_Input.InitialScissor;
			}

			m_IndividualValid = ValidBounds(m_IndividualBounds);
			m_DrawOrdinary = ApplyCombinedScissor();
		}

		void SelectTexture(const MotionPreparedFrameCommand& command) {
			if (command.Texture == m_Texture) {
				return;
			}

			Flush();
			MotionFrameTexture event;
			event.Index = command.Texture;
			m_Output(event);
			m_Texture = command.Texture;
			const auto size = FrameTextureSize(m_Input, *m_Texture);
			m_InverseTextureSize.X = 1.0f / size.X;
			m_InverseTextureSize.Y = 1.0f / size.Y;
		}

		void Append(const MotionPreparedFrameCommand& command, bool mask) {
			MotionGridBatchInput grid;
			grid.Positions = command.Clip->Geometry->Positions;
			grid.Subdivision = command.Subdivision;
			for (std::size_t index = 0; index < grid.Colors.size(); ++index) {
				grid.Colors[index] = mask ? 0xffffffff : std::rotr(command.Colors[index], 8);
			}

			grid.PixelUvs = command.PixelUvs;
			grid.InverseTextureSize = m_InverseTextureSize;
			grid.Key = mask ? 4 : command.Key;
			m_Batch.AppendGrid(grid, [this](const MotionBatchSubmission& submission) {
				m_Output(submission);
			});
		}

		void DrawOrdinary(const MotionPreparedFrameCommand& command) {
			SelectTexture(command);
			if (m_Filter != command.Filter || m_FilterColor != command.FilterColor) {
				m_Filter = command.Filter;
				m_FilterColor = command.FilterColor;
				SetFilter(m_Filter, std::rotr(m_FilterColor, 8));
			}

			if (m_BlendMode != command.BlendMode || m_BlendParameter != command.BlendParameter) {
				m_BlendMode = command.BlendMode;
				m_BlendParameter = command.BlendParameter;
				SetBlend(m_BlendMode, m_BlendParameter);
			}

			if ((m_BlendMode & 0xff0f) == 0x10d) {
				MotionMaskedBlendInput blend;
				blend.TargetBounds = m_Input.TargetBounds;
				blend.AxisScale = m_Input.AxisScale;
				blend.Amount = m_BlendParameter;
				const auto count = PrepareMotionMaskedBlendPasses(blend, m_Scratch.BlendPasses);
				ConsumeWork(m_RemainingWork, count);
				for (const auto& pass : std::span<const MotionMaskedBlendPass>(m_Scratch.BlendPasses).first(count)) {
					SetBlend(m_BlendMode, pass.BlendParameter);
					MotionFrameBlendBounds event;
					event.Bounds = MeasureMotionGeometryBounds(*command.Clip->Geometry);
					Flush();
					m_Output(event);
					Append(command, false);
				}
			} else {
				if ((m_BlendMode & 0xf00) == 0x100) {
					// Native55670c..5567ae requests a copy of these actual bounds before every family100 quad.
					MotionFrameBlendBounds event;
					event.Bounds = MeasureMotionGeometryBounds(*command.Clip->Geometry);
					Flush();
					m_Output(event);
				}
				Append(command, false);
			}
		}

		MotionBounds GroupBounds(const MotionFrameGroup& group, std::size_t start) {
			if (!m_Input.MaskRegionClipping) {
				return MotionBounds{};
			}

			const auto run = MeasureMotionClipRun(m_Input.ClipQueue, start, group.Identity);
			const auto chain = MeasureMotionClipChain(m_Input.Nodes, group.FirstNode);
			const auto combined = IntersectBounds(run.Bounds, chain.Bounds);
			if (!chain.HasBounds || !ValidBounds(combined)) {
				return MotionBounds{};
			}

			auto bounds = TransformMotionScissorBounds(combined, m_Input.View, m_Input.Model, m_Input.Viewport);
			bounds.MinimumX = std::floor(bounds.MinimumX) - 2.0f;
			bounds.MinimumY = std::floor(bounds.MinimumY) - 2.0f;
			bounds.MaximumX = std::ceil(bounds.MaximumX) + 2.0f;
			bounds.MaximumY = std::ceil(bounds.MaximumY) + 2.0f;

			return bounds;
		}

		void PrepareGroup(std::uint64_t identity, std::size_t start) {
			const auto& group = FindGroup(m_Input, identity, m_RemainingWork);
			ConsumeWork(m_RemainingWork, ChainWork(m_Input, group));
			if (m_Input.MaskRegionClipping) {
				ConsumeWork(m_RemainingWork, m_Input.ClipQueue.size() - start);
			}

			m_GroupBounds = GroupBounds(group, start);
			const auto initial_bounds = IntersectBounds(m_GroupBounds, m_Input.InitialScissor);
			SetScissor(ValidBounds(m_GroupBounds) && ValidBounds(initial_bounds) ? initial_bounds : m_Input.InitialScissor);
			m_BlendMode = 0;
			m_BlendParameter = 0;
			m_Filter = 0;
			m_FilterColor = 0;
			SetBlend(0, 0);
			SetFilter(0, 0);
			const auto operation_count = PrepareMotionMaskPlan(m_Input.Nodes, m_Input.MaskSettings, group.FirstNode, m_Scratch.MaskOperations);
			ConsumeWork(m_RemainingWork, operation_count);
			for (const auto& operation : std::span<const MotionMaskOperation>(m_Scratch.MaskOperations).first(operation_count)) {
				if (operation.Kind == MotionMaskOperationKind::VisitCommand) {
					const auto& command = FindCandidate(m_Input, operation.Command, m_RemainingWork);
					MotionFrameCandidate event;
					event.Command = &command;
					m_Output(event);
					SelectTexture(command);
					UpdateIndividualClip(command);
					if (m_IndividualValid) {
						Append(command, true);
					}
				} else {
					Flush();
					m_Output(operation);
				}
			}

			m_IndividualValid = ValidBounds(m_IndividualBounds);
			m_DrawOrdinary = ApplyCombinedScissor();

			m_Texture.reset();
		}

		const MotionPreparedFrame& m_Input;
		const MotionFrameScratch& m_Scratch;
		const std::function<void(const MotionFrameEvent&)>& m_Output;
		MotionQuadBatch m_Batch;
		MotionBounds m_AppliedScissor;
		MotionBounds m_GroupBounds;
		MotionBounds m_IndividualBounds;
		const MotionBounds* m_IndividualClip = nullptr;
		bool m_IndividualValid = true;
		resource::MotionPoint m_InverseTextureSize;
		std::optional<std::int32_t> m_Texture;
		std::uint32_t m_BlendMode = 0;
		float m_BlendParameter = 0;
		std::uint32_t m_Filter = 0;
		std::uint32_t m_FilterColor = 0;
		bool m_DrawOrdinary = true;
		std::size_t m_RemainingWork = kMaximumFrameWork;
	};

	void SubmitPreparedMotionFrame(const MotionPreparedFrame& input, const MotionFrameScratch& scratch, const std::function<void(const MotionFrameEvent&)>& output) {
		if (!output) {
			throw std::runtime_error("motion frame requires a prepared output callback");
		}

		ValidateFrame(input, scratch);
		MotionFrameSubmission submission(input, scratch, output);
		submission.Submit();
	}

} // namespace sgre::scene
