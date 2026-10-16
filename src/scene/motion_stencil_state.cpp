#include "sgre/scene/motion_stencil_state.hpp"

#include <bit>
#include <stdexcept>

namespace sgre::scene {

	static void RequireComparison(MotionStencilComparison comparison) {
		const auto value = static_cast<std::uint32_t>(comparison);
		if (value < 1 || value > 8) {
			throw std::runtime_error("motion stencil comparison is invalid");
		}
	}

	static void RequireOperation(MotionStencilOperation operation) {
		const auto value = static_cast<std::uint32_t>(operation);
		if (value < 1 || value > 8) {
			throw std::runtime_error("motion stencil operation is invalid");
		}
	}

	static void RequireFace(const MotionStencilFace& face) {
		RequireOperation(face.StencilFail);
		RequireOperation(face.DepthFail);
		RequireOperation(face.Pass);
		RequireComparison(face.Comparison);
	}

	static MotionStencilBinding PrepareStencilBinding(MotionStencilAction action, const MotionStencilBinding& current, std::uint32_t reference) {
		RequireComparison(current.Descriptor.DepthComparison);
		auto binding = current;
		if (action == MotionStencilAction::Finish) {
			RequireFace(binding.Descriptor.Front);
			RequireFace(binding.Descriptor.Back);
			binding.Descriptor.StencilEnabled = false;

			return binding;
		}

		const auto operation = action == MotionStencilAction::Begin ? MotionStencilOperation::IncrementWrap : (action == MotionStencilAction::End ? MotionStencilOperation::DecrementWrap : MotionStencilOperation::Keep);
		MotionStencilFace face;
		face.StencilFail = MotionStencilOperation::Keep;
		face.DepthFail = operation;
		face.Pass = operation;
		face.Comparison = MotionStencilComparison::Equal;

		binding.Descriptor.StencilEnabled = true;
		binding.Descriptor.ReadMask = 0xff;
		binding.Descriptor.WriteMask = 0xff;
		binding.Descriptor.Front = face;
		binding.Descriptor.Back = face;
		binding.Reference = reference;

		return binding;
	}

	MotionStencilTransition PrepareMotionStencilTransition(MotionStencilAction action, const MotionStencilControl& control, const MotionStencilBinding& current, std::uint32_t reference, bool has_stencil_attachment) {
		MotionStencilTransition transition;
		transition.Control = control;
		switch (action) {
		case MotionStencilAction::Prepare:
			if (has_stencil_attachment) {
				transition.ClearStencil = static_cast<std::uint8_t>(reference);
			}

			transition.Control.MaskActive = true;
			transition.Control.ResolvedMask = false;
			transition.Control.MaskRenderValue = std::bit_cast<float>(std::uint32_t{0x3e808081});
			transition.Control.Phase = MotionMaskPhase::Prepared;
			break;
		case MotionStencilAction::Begin:
		case MotionStencilAction::End:
			transition.Binding = PrepareStencilBinding(action, current, reference);
			transition.Control.Phase = MotionMaskPhase::StencilWrite;
			break;
		case MotionStencilAction::Resolve:
			transition.Binding = PrepareStencilBinding(action, current, reference);
			transition.Control.ResolvedMask = true;
			transition.Control.MaskRenderValue = std::bit_cast<float>(std::uint32_t{0x3b808081});
			transition.Control.Phase = MotionMaskPhase::Resolved;
			break;
		case MotionStencilAction::Finish:
			transition.Binding = PrepareStencilBinding(action, current, reference);
			transition.Control.MaskActive = false;
			transition.Control.Phase = MotionMaskPhase::Idle;
			break;
		default:
			throw std::runtime_error("motion stencil action is invalid");
		}

		return transition;
	}

} // namespace sgre::scene
