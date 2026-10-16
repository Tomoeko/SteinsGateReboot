#include "sgre/scene/motion_stencil_state.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::scene::MotionStencilBinding PreparedBinding() {
	sgre::scene::MotionStencilBinding binding;
	binding.Descriptor.DepthEnabled = true;
	binding.Descriptor.DepthWriteEnabled = false;
	binding.Descriptor.DepthComparison = sgre::scene::MotionStencilComparison::GreaterEqual;
	binding.Descriptor.StencilEnabled = false;
	binding.Descriptor.ReadMask = 0x3a;
	binding.Descriptor.WriteMask = 0xc5;
	binding.Descriptor.Front.StencilFail = sgre::scene::MotionStencilOperation::Zero;
	binding.Descriptor.Front.DepthFail = sgre::scene::MotionStencilOperation::Invert;
	binding.Descriptor.Front.Pass = sgre::scene::MotionStencilOperation::IncrementSaturate;
	binding.Descriptor.Front.Comparison = sgre::scene::MotionStencilComparison::Less;
	binding.Descriptor.Back.StencilFail = sgre::scene::MotionStencilOperation::Replace;
	binding.Descriptor.Back.DepthFail = sgre::scene::MotionStencilOperation::DecrementSaturate;
	binding.Descriptor.Back.Pass = sgre::scene::MotionStencilOperation::Keep;
	binding.Descriptor.Back.Comparison = sgre::scene::MotionStencilComparison::NotEqual;
	binding.Reference = 0x123456ab;

	return binding;
}

static void TestStencilPrepare() {
	const auto current = PreparedBinding();
	sgre::scene::MotionStencilControl control;
	control.ResolvedMask = true;
	control.MaskRenderValue = 17;
	const auto transition = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Prepare, control, current, 0xfedcba01, true);
	Require(transition.ClearStencil == 1 && !transition.Binding, "prepare must clear only the low reference byte without changing the binding");
	Require(transition.Control.MaskActive && !transition.Control.ResolvedMask && transition.Control.Phase == sgre::scene::MotionMaskPhase::Prepared, "prepare control fields differ");
	Require(std::bit_cast<std::uint32_t>(transition.Control.MaskRenderValue) == 0x3e808081, "prepare render scalar differs");
	const auto absent = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Prepare, control, current, 255, false);
	Require(!absent.ClearStencil && absent.Control.MaskActive, "missing attachment must omit clearing while preserving the native prepare phase");
	Require(current == PreparedBinding() && control.ResolvedMask && control.MaskRenderValue == 17, "prepare modified caller state");
}

static void TestStencilMaskFaces() {
	const auto current = PreparedBinding();
	sgre::scene::MotionStencilControl control;
	control.ResolvedMask = true;
	control.MaskRenderValue = -0.0f;
	const auto begin = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Begin, control, current, 0xdeadbeef, false);
	Require(begin.Binding.has_value() && !begin.ClearStencil, "begin binding request is missing");
	const auto& descriptor = begin.Binding->Descriptor;
	Require(descriptor.DepthEnabled && !descriptor.DepthWriteEnabled && descriptor.DepthComparison == sgre::scene::MotionStencilComparison::GreaterEqual, "begin changed existing depth policy");
	Require(descriptor.StencilEnabled && descriptor.ReadMask == 255 && descriptor.WriteMask == 255, "begin masks differ");
	Require(descriptor.Front == descriptor.Back && descriptor.Front.StencilFail == sgre::scene::MotionStencilOperation::Keep && descriptor.Front.DepthFail == sgre::scene::MotionStencilOperation::IncrementWrap && descriptor.Front.Pass == sgre::scene::MotionStencilOperation::IncrementWrap && descriptor.Front.Comparison == sgre::scene::MotionStencilComparison::Equal, "begin must use equal comparison and wrapping increments on either depth outcome");
	Require(begin.Binding->Reference == 0xdeadbeef && begin.Control.ResolvedMask && std::bit_cast<std::uint32_t>(begin.Control.MaskRenderValue) == 0x80000000, "begin reference or retained control state differs");
	const auto end = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::End, control, *begin.Binding, 0x100, false);
	Require(end.Binding->Descriptor.Front.DepthFail == sgre::scene::MotionStencilOperation::DecrementWrap && end.Binding->Descriptor.Front.Pass == sgre::scene::MotionStencilOperation::DecrementWrap && end.Binding->Descriptor.Front == end.Binding->Descriptor.Back, "end must use wrapping decrements on either depth outcome");
	Require(end.Binding->Reference == 0x100 && end.Control.Phase == sgre::scene::MotionMaskPhase::StencilWrite, "end reference must remain the full integer");
}

static void TestStencilResolveAndFinish() {
	auto current = PreparedBinding();
	sgre::scene::MotionStencilControl control;
	control.MaskActive = true;
	const auto resolve = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Resolve, control, current, 47, true);
	Require(resolve.Binding && resolve.Binding->Descriptor.Front.DepthFail == sgre::scene::MotionStencilOperation::Keep && resolve.Binding->Descriptor.Front.Pass == sgre::scene::MotionStencilOperation::Keep, "resolve must retain stencil values");
	Require(resolve.Control.ResolvedMask && resolve.Control.MaskActive && resolve.Control.Phase == sgre::scene::MotionMaskPhase::Resolved && std::bit_cast<std::uint32_t>(resolve.Control.MaskRenderValue) == 0x3b808081, "resolve control fields differ");
	current.Descriptor.StencilEnabled = true;
	const auto finish = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Finish, resolve.Control, current, 999, true);
	auto expected = current;
	expected.Descriptor.StencilEnabled = false;
	Require(finish.Binding == expected, "finish must only disable stencil and retain the existing reference and face policy");
	Require(!finish.Control.MaskActive && finish.Control.ResolvedMask && finish.Control.Phase == sgre::scene::MotionMaskPhase::Idle && std::bit_cast<std::uint32_t>(finish.Control.MaskRenderValue) == 0x3b808081, "finish must retain resolve scalar and flag");
}

static void TestUncommittedBinding() {
	const auto current = PreparedBinding();
	sgre::scene::MotionStencilControl control;
	const auto transition = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Begin, control, current, 12, true);
	Require(current.Reference == 0x123456ab && !current.Descriptor.StencilEnabled, "a prepared request must not commit a device binding");
	Require(transition.Control.Phase == sgre::scene::MotionMaskPhase::StencilWrite, "native control transition must exist even if platform creation later fails");
}

static void TestInvalidEnumsAndIgnoredDescriptor() {
	auto current = PreparedBinding();
	current.Descriptor.DepthComparison = static_cast<sgre::scene::MotionStencilComparison>(0);
	sgre::scene::MotionStencilControl control;
	const auto prepare = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Prepare, control, current, 1, true);
	Require(prepare.ClearStencil == 1, "prepare must not inspect an unused bound descriptor");
	bool rejected = false;
	try {
		static_cast<void>(sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Begin, control, current, 1, true));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "invalid depth comparison must reject before preparing a binding");
	current = PreparedBinding();
	current.Descriptor.Front.Pass = static_cast<sgre::scene::MotionStencilOperation>(9);
	rejected = false;
	try {
		static_cast<void>(sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Finish, control, current, 1, false));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "finish must reject invalid retained face policy");
	const auto begin = sgre::scene::PrepareMotionStencilTransition(sgre::scene::MotionStencilAction::Begin, control, current, 1, false);
	Require(begin.Binding->Descriptor.Front.Pass == sgre::scene::MotionStencilOperation::IncrementWrap, "begin overwrites the unused previous face policy");
	rejected = false;
	try {
		static_cast<void>(sgre::scene::PrepareMotionStencilTransition(static_cast<sgre::scene::MotionStencilAction>(5), control, current, 1, false));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "invalid action must reject");
}

int main() {
	try {
		TestStencilPrepare();
		TestStencilMaskFaces();
		TestStencilResolveAndFinish();
		TestUncommittedBinding();
		TestInvalidEnumsAndIgnoredDescriptor();
		std::cout << "motion stencil state tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
