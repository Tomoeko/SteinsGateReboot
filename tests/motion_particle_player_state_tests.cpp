#include "sgre/scene/motion_particle_player_state.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("particle player accepted an unsupported construction phase");
}

static sgre::scene::TextureCache SourceFreeCache() {
	return sgre::scene::TextureCache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("particle state fixture unexpectedly requested source upload");
	},
	    [](auto) {
		    throw std::runtime_error("particle state fixture unexpectedly released texture");
	    });
}

static sgre::resource::MotionTimeline Timeline(std::string name, float last) {
	sgre::resource::MotionTimeline timeline;
	timeline.Name = std::move(name);
	timeline.LastTime = last;

	return timeline;
}

static std::shared_ptr<const sgre::resource::MotionResource> Resource(bool second) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject common;
	common.Name = "part";
	common.Timelines = {Timeline("birth", second ? 24 : 12), Timeline("stealth", second ? 30 : 18)};
	resource->Objects.push_back(std::move(common));
	sgre::resource::MotionObject distinct;
	distinct.Name = second ? "cross" : "parent";
	distinct.Timelines = {Timeline(second ? "birth" : "main", second ? 16 : 40)};
	resource->Objects.push_back(std::move(distinct));
	if (!second) {
		auto division = Timeline("division", 0);
		division.Kind = sgre::resource::MotionTimelineKind::Division;
		division.Division = {0};
		division.MotionList = {"birth"};
		resource->Objects[0].Timelines.push_back(std::move(division));
	}

	return resource;
}

static sgre::scene::MotionProjectId Register(sgre::scene::MotionResourceManager& manager, bool second) {
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = second ? 2 : 1;
	identity.RootOffset = 44;

	return manager.Register(identity, Resource(second), {});
}

static void TestOrdinaryParticleCapsule() {
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto first = Register(manager, false);
	const auto second = Register(manager, true);
	sgre::scene::MotionPlayerCommands parent(manager);
	parent.SetCharacter("parent");
	parent.Play("main");
	parent.SetTickCount(11);
	parent.SetSpeed(-2);
	parent.ParameterValues().WriteLocal("parent_value", 7, 0);
	sgre::scene::MotionRootScene parent_root;
	parent_root.SetPriority(-3.5f);
	parent_root.SetCoordinate3({11, 12, 13});
	parent_root.SetZoom(4, 5);
	parent_root.SetVisible(true);
	sgre::scene::MotionPlayerRenderControls render;
	render.ColorFilter = 0x12345678;
	render.ColorFilterParameter = 0xabcdef01;
	render.EmissiveColor = 0x87654321;
	render.AmbientFactor = 0.25f;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	sgre::scene::MotionSharedRandom shared(seed);
	sgre::scene::MotionSharedRandom expected_shared(seed);
	sgre::scene::MotionPlayerRandom expected_random(expected_shared.NextUnitFloat());
	std::size_t samples = 0;
	sgre::scene::MotionChildConstructionServices services;
	services.ReadManagerRandom = [&](const auto& actual) {
		Require(&actual == &manager, "particle constructor must use actual immediate parent's manager");
		++samples;

		return shared.NextUnitFloat();
	};
	{
		sgre::scene::MotionParticlePlayerState particle(parent, parent_root, render, services);
		Require(samples == 1 && particle.Random().State() == expected_random.State() && &particle.ImmediateParentCommands() == &parent && &particle.UltimateParentCommands() == &parent, "particle capsule owns exactly one manager-sampled independent generator and real parent identities");
		Require(particle.Root().MatrixMode() == sgre::scene::MotionRootMatrixMode::Recomputed && particle.Root().Priority() == -3.5f && particle.Root().Overrides().Coordinate == std::array<float, 3>{} && particle.Root().Overrides().ZoomX == 1 && !particle.Root().Overrides().Visible, "particle ordinary root copies priority without cloning parent pose or kind3 supplied-matrix policy");
		Require(particle.RenderControls().ColorFilter == render.ColorFilter && particle.RenderControls().ColorFilterParameter == render.ColorFilterParameter && particle.RenderControls().EmissiveColor == render.EmissiveColor && particle.RenderControls().AmbientFactor == render.AmbientFactor, "particle copies all genuine parent render controls before first selection");
		Require(particle.Commands().PreferredProject() == parent.PreferredProject() && !particle.Commands().SelectedProject() && particle.Commands().Names().CurrentCharacter.empty() && particle.Commands().ClockState().Speed == 1 && particle.Commands().ClockState().TickCount == 0 && &particle.Commands().LayerUids() == &parent.LayerUids() && particle.Commands().ParameterValues().Read("parent_value") == 7, "constructor copies preferred root and exact parameter/UID ancestry independently of names/clock");
		particle.SelectInitialTarget({"part", "birth"});
		Require(particle.Commands().SelectedProject()->Project == first && particle.Commands().ClockState().LastTime == 12 && particle.Commands().ClockState().Playing && particle.Root().Overrides().Visible && particle.Commands().Names().CurrentMotion == "birth", "initial particle Play must resolve actual preferred project and only then publish root visible");
		particle.Root().Compose(true);
		Require(particle.Root().Frame().Composed && particle.Root().Frame().Hierarchy.Transform.XX == 1, "ordinary particle root composes without any fabricated parent-supplied pose");
		RequireFailure([&] {
			particle.SelectInitialTarget({"cross", "birth"});
		},
		    "one genuine construction phase");
		particle.Commands().ParameterValues().WriteLocal("parent_value", 9, 0);
		Require(parent.ParameterValues().ReadLocal("parent_value") == 7, "particle local values must not overwrite the actual containing parent map");
		const auto parent_random = particle.Random().State();
		sgre::scene::MotionParticlePlayerState nested(particle, services);
		Require(samples == 2 && &nested.ImmediateParentCommands() == &particle.Commands() && &nested.UltimateParentCommands() == &parent && nested.Commands().ParameterValues().Read("parent_value") == 9 && &nested.Commands().LayerUids() == &parent.LayerUids() && nested.Random().State() != parent_random, "nested emitted player retains exact immediate/ultimate ancestry and distinct manager-sampled stream");
		nested.SelectInitialTarget({"cross", "birth"});
		Require(nested.Commands().SelectedProject()->Project == second && nested.Commands().ClockState().LastTime == 16, "real cross-project particle selection falls through actual preferred-root miss");
		sgre::scene::MotionParticlePlayerState nested_again(nested, services);
		nested_again.SelectInitialTarget({"part", "birth"});
		Require(nested_again.Commands().SelectedProject()->Project == second && nested_again.Commands().ClockState().LastTime == 24 && &nested_again.UltimateParentCommands() == &parent, "next nested particle must inherit the genuinely changed preferred project rather than containing first project");
		Require(manager.ReferenceCount(first) == 1 && manager.ReferenceCount(second) == 1 && parent.ClockState().TickCount == 11 && parent.ClockState().Speed == -2, "particle state construction/selection adds no manager registration pins or parent clock changes");
	}

	Require(manager.ReferenceCount(first) == 1 && manager.ReferenceCount(second) == 1 && parent.ClockState().Playing, "particle capsule retirement preserves enclosing registrations and commands");
	RequireFailure([&] {
		sgre::scene::MotionParticlePlayerState rejected(parent, parent_root, render, {});
	},
	    "serialized random sample");
	services.ReadManagerRandom = [](const auto&) {
		return 1.0f;
	};
	RequireFailure([&] {
		sgre::scene::MotionParticlePlayerState rejected(parent, parent_root, render, services);
	},
	    "unit sample");
}

static void TestFailedAndDeferredTargets() {
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto project = Register(manager, false);
	sgre::scene::MotionPlayerCommands parent(manager);
	parent.SetCharacter("parent");
	parent.Play("main");
	sgre::scene::MotionRootScene root;
	sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionChildConstructionServices services;
	services.ReadManagerRandom = [](const auto&) {
		return 0.5f;
	};
	sgre::scene::MotionParticlePlayerState failed(parent, root, render, services);
	failed.SelectInitialTarget({"part", "missing"});
	Require(!failed.Commands().SelectedProject() && !failed.Commands().PreferredProject() && failed.Commands().CachedTimeline() == nullptr && !failed.Commands().ClockState().Playing && failed.Commands().Names().CurrentCharacter == "part" && failed.Commands().Names().CurrentMotion.empty() && failed.Root().Overrides().Visible, "failed target must remain actual stopped/root-only state for enclosing pre-tick prune");
	sgre::scene::MotionParticlePlayerState deferred(parent, root, render, services);
	deferred.Commands().Play("stealth", sgre::scene::MotionPlayFlags::Stealth);
	deferred.SelectInitialTarget({"part", "birth"});
	Require(deferred.Commands().SelectedProject()->Project == project && deferred.Commands().Names().CurrentMotion == "stealth" && deferred.Commands().Names().RequestedMotion == "birth" && deferred.Commands().Names().DeferredStealthMotion.empty() && deferred.Commands().SelectionSerial() == 2 && deferred.Commands().ClockState().LastTime == 18, "genuine optional D0 target must select stealth after ordinary target and preserve requested name");
	sgre::scene::MotionParticlePlayerState division(parent, root, render, services);
	RequireFailure([&] {
		division.SelectInitialTarget({"part", "division"});
	},
	    "division playback");
}

static void TestParticleInsideActualKind3() {
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto project = Register(manager, false);
	sgre::scene::MotionPlayerCommands parent(manager);
	parent.SetCharacter("parent");
	parent.Play("main");
	sgre::scene::MotionRootScene root;
	sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionChildConstructionServices services;
	services.ReadManagerRandom = [](const auto&) {
		return 0.25f;
	};
	sgre::resource::MotionLayer layer;
	layer.Type = 3;
	sgre::resource::PsbValue inherit;
	inherit.Data = false;
	layer.Properties.emplace_back("motionIndependentLayerInherit", inherit);
	sgre::scene::PreparedMotionLayer prepared;
	prepared.Type = 3;
	sgre::scene::MotionChildConstructionState child(parent, root, render, layer, prepared, services);
	child.Root().SetPriority(7);
	sgre::scene::MotionParticlePlayerState particle(child, services);
	particle.SelectInitialTarget({"part", "birth"});
	Require(&particle.ImmediateParentCommands() == &child.Commands() && &particle.UltimateParentCommands() == &parent && particle.Root().Priority() == 7 && particle.Root().MatrixMode() == sgre::scene::MotionRootMatrixMode::Recomputed && particle.Commands().SelectedProject()->Project == project, "kind3-to-particle constructor preserves real ancestry but never inherits supplied-matrix root policy");
}

int main() {
	try {
		TestOrdinaryParticleCapsule();
		TestFailedAndDeferredTargets();
		TestParticleInsideActualKind3();
		std::cout << "motion particle player state tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
