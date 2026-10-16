#pragma once

#include <cstdint>
#include <optional>

namespace sgre::scene {

	enum class MotionStencilComparison : std::uint32_t {
		Never = 1,
		Less = 2,
		Equal = 3,
		LessEqual = 4,
		Greater = 5,
		NotEqual = 6,
		GreaterEqual = 7,
		Always = 8
	};

	enum class MotionStencilOperation : std::uint32_t {
		Keep = 1,
		Zero = 2,
		Replace = 3,
		IncrementSaturate = 4,
		DecrementSaturate = 5,
		Invert = 6,
		IncrementWrap = 7,
		DecrementWrap = 8
	};

	struct MotionStencilFace {
		MotionStencilOperation StencilFail = MotionStencilOperation::Keep;
		MotionStencilOperation DepthFail = MotionStencilOperation::Keep;
		MotionStencilOperation Pass = MotionStencilOperation::Keep;
		MotionStencilComparison Comparison = MotionStencilComparison::Always;
		bool operator==(const MotionStencilFace&) const = default;
	};

	struct MotionDepthStencilDescriptor {
		bool DepthEnabled = false;
		bool DepthWriteEnabled = false;
		MotionStencilComparison DepthComparison = MotionStencilComparison::Always;
		bool StencilEnabled = false;
		std::uint8_t ReadMask = 0xff;
		std::uint8_t WriteMask = 0xff;
		MotionStencilFace Front;
		MotionStencilFace Back;
		bool operator==(const MotionDepthStencilDescriptor&) const = default;
	};

	struct MotionStencilBinding {
		MotionDepthStencilDescriptor Descriptor;
		std::uint32_t Reference = 0;
		bool operator==(const MotionStencilBinding&) const = default;
	};

	enum class MotionStencilAction {
		Prepare,
		Begin,
		End,
		Resolve,
		Finish
	};

	enum class MotionMaskPhase : std::uint32_t {
		Idle = 0,
		Prepared = 1,
		StencilWrite = 2,
		TextureFirst = 3,
		TextureCombine = 4,
		TextureSubtract = 5,
		Resolved = 6
	};

	struct MotionStencilControl {
		bool MaskActive = false;
		bool ResolvedMask = false;
		// Preserves the native render-state scalar; its shader consumer remains open.
		float MaskRenderValue = 0;
		MotionMaskPhase Phase = MotionMaskPhase::Idle;
	};

	struct MotionStencilTransition {
		MotionStencilControl Control;
		std::optional<std::uint8_t> ClearStencil;
		// This is a request. Commit the bound descriptor/reference only after creation succeeds.
		std::optional<MotionStencilBinding> Binding;
	};

	// Recovers the native stencil path only. A fallback texture-mask path is not substituted.
	[[nodiscard]] MotionStencilTransition PrepareMotionStencilTransition(MotionStencilAction action, const MotionStencilControl& control, const MotionStencilBinding& current, std::uint32_t reference, bool has_stencil_attachment);

} // namespace sgre::scene
