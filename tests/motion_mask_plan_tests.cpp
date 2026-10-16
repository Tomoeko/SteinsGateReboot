#include "sgre/scene/motion_mask_plan.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

using sgre::scene::MotionClipCommand;
using sgre::scene::MotionClipNode;
using sgre::scene::MotionMaskOperation;
using sgre::scene::MotionMaskOperationKind;
using sgre::scene::MotionMaskSettings;
using sgre::scene::PrepareMotionMaskPlan;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected mask-plan failure");
}

static void TestPassAndReferenceOrder() {
	std::array<MotionClipNode, 3> nodes;
	nodes[0].ModeFlags = 2;
	nodes[0].Parent = 1;
	nodes[1].ModeFlags = 1;
	nodes[1].Parent = 2;
	nodes[2].ModeFlags = 1;
	std::array<MotionMaskSettings, 3> settings;
	settings[0].Operation = 255;
	settings[0].ThresholdMinimum = 0.75f;
	settings[0].ThresholdExtent = 0.625f;
	std::array<MotionMaskOperation, 16> output;
	const auto count = PrepareMotionMaskPlan(nodes, settings, 0, output);
	Require(count == 11 && output[0].Kind == MotionMaskOperationKind::Prepare && output[0].Reference == 1, "mode2 ancestor did not initialize mask reference1");
	Require(output[1].Kind == MotionMaskOperationKind::Configure && output[2].Kind == MotionMaskOperationKind::Begin && output[2].Reference == 1 && output[3].Command == &nodes[1].Command, "first-pass traversal did not skip the leaf's mode2 then visit its mode1 parent");
	Require(output[5].Kind == MotionMaskOperationKind::Begin && output[5].Reference == 2 && output[6].Command == &nodes[2].Command, "mode1 reference increments did not follow native leaf-to-parent order");
	Require(output[7].Kind == MotionMaskOperationKind::Configure && output[7].Operation == 255 && output[7].ThresholdMinimum == 0.75f && output[7].ThresholdMaximum == -0.375f && output[8].Kind == MotionMaskOperationKind::End && output[8].Reference == 3 && output[9].Command == &nodes[0].Command, "second-pass complement, byte operation or final reference changed");
	Require(output[10].Kind == MotionMaskOperationKind::Resolve && output[10].Reference == 3, "mask resolve lost the final reference");
}

static void TestVisibleMemberCandidates() {
	std::array<MotionClipCommand, 3> members;
	members[0].GeometryCount = 0;
	members[1].Hidden = true;
	members[2].GeometryCount = 4;
	std::array<MotionClipNode, 1> nodes;
	nodes[0].ModeFlags = 5;
	nodes[0].Command.Hidden = true;
	nodes[0].Members = members;
	const std::array<MotionMaskSettings, 1> settings{};
	std::array<MotionMaskOperation, 8> output;
	const auto count = PrepareMotionMaskPlan(nodes, settings, 0, output);
	Require(count == 6 && output[0].Reference == 0 && output[2].Kind == MotionMaskOperationKind::Begin && output[2].Reference == 0, "first-only mask plan selected the wrong initial reference");
	Require(output[3].Kind == MotionMaskOperationKind::VisitCommand && output[3].Command == &members[0] && output[4].Command == &members[2], "member masking skipped zero-count candidates, followed hidden members, or skipped an otherwise hidden union node");
	nodes[0].ModeFlags = 1;
	Require(PrepareMotionMaskPlan(nodes, settings, 0, output) == 4, "hidden direct node lost its configure/begin/resolve phases");
	nodes[0].Command.Hidden = false;
	nodes[0].Command.GeometryCount = 0;
	Require(PrepareMotionMaskPlan(nodes, settings, 0, output) == 5 && output[3].Command == &nodes[0].Command, "direct mask candidate incorrectly used the queue's empty-geometry exclusion");
}

static void TestIgnoredModesAndPreparedBounds() {
	std::array<MotionClipNode, 2> nodes;
	nodes[0].ModeFlags = 0;
	nodes[0].Parent = 1;
	nodes[1].ModeFlags = 3;
	std::array<MotionMaskSettings, 2> settings;
	settings[0].ThresholdMinimum = std::numeric_limits<float>::quiet_NaN();
	settings[1].ThresholdExtent = std::numeric_limits<float>::infinity();
	std::array<MotionMaskOperation, 8> output;
	Require(PrepareMotionMaskPlan(nodes, settings, 0, output) == 2 && output[0].Kind == MotionMaskOperationKind::Prepare && output[1].Kind == MotionMaskOperationKind::Resolve, "unused modes read their threshold fields or emitted candidates");
	Require(PrepareMotionMaskPlan({}, {}, {}, {}) == 0, "missing clip group emitted mask phases");
	nodes[1].ModeFlags = 1;
	output[0].Kind = MotionMaskOperationKind::VisitCommand;
	RequireFailure([&] {
		static_cast<void>(PrepareMotionMaskPlan(nodes, settings, 0, output));
	},
	    "finite threshold");
	Require(output[0].Kind == MotionMaskOperationKind::VisitCommand, "rejected thresholds partially mutated the prepared plan");
	settings[1].ThresholdExtent = 0;
	RequireFailure([&] {
		static_cast<void>(PrepareMotionMaskPlan(nodes, settings, 0, std::span<MotionMaskOperation>(output).first(1)));
	},
	    "output capacity");
	Require(output[0].Kind == MotionMaskOperationKind::VisitCommand, "insufficient output capacity partially changed the plan");
	nodes[1].Parent = 0;
	RequireFailure([&] {
		static_cast<void>(PrepareMotionMaskPlan(nodes, settings, 0, output));
	},
	    "parent cycle");
	nodes[1].Parent = 2;
	RequireFailure([&] {
		static_cast<void>(PrepareMotionMaskPlan(nodes, settings, 0, output));
	},
	    "parent index");
	RequireFailure([&] {
		static_cast<void>(PrepareMotionMaskPlan(nodes, std::span<const MotionMaskSettings>(settings).first(1), 0, output));
	},
	    "matching node/settings");
}

int main() {
	try {
		TestPassAndReferenceOrder();
		TestVisibleMemberCandidates();
		TestIgnoredModesAndPreparedBounds();
		std::cout << "motion mask plan fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
