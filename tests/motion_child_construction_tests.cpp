#include "sgre/scene/motion_child_construction.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported child construction state was accepted");
}

static std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

static sgre::scene::TextureCache SourceFreeCache() {
	return sgre::scene::TextureCache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("source-free child fixture attempted an upload");
	},
	    [](auto) {
		    throw std::runtime_error("source-free child fixture attempted a texture release");
	    });
}

static sgre::resource::MotionLayer Raw(bool independent) {
	sgre::resource::MotionLayer layer;
	layer.Type = 3;
	layer.Coordinate = 1;
	sgre::resource::PsbValue flag;
	flag.Data = independent;
	layer.Properties.emplace_back("motionIndependentLayerInherit", std::move(flag));

	return layer;
}

static sgre::scene::PreparedMotionLayer Prepared() {
	sgre::scene::PreparedMotionLayer layer;
	layer.Type = 3;
	layer.CoordinatePlane = 1;
	layer.TransformOrder = {sgre::scene::MotionTransformOperation::Scale, sgre::scene::MotionTransformOperation::Rotate, sgre::scene::MotionTransformOperation::Shear, sgre::scene::MotionTransformOperation::Flip};

	return layer;
}

static sgre::scene::MotionProjectId Register(sgre::scene::MotionResourceManager& manager) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "idle";
	timeline.LastTime = 20;
	sgre::resource::MotionObject character;
	character.Name = "a";
	character.Timelines.push_back(std::move(timeline));
	resource->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	identity.RootOffset = 128;

	return manager.Register(identity, resource, {});
}

static void RequireZeroControl(const sgre::scene::MotionChildFrameControl& control) {
	Require(control.Flags == sgre::scene::MotionPlayFlags::None && control.DirectionMode == 0 && Bits(control.DirectionOffset) == 0 && !control.CompleteDirection && control.DirectionTarget.empty() && Bits(control.TimeOffset) == 0 && !control.Specified, "the actual two initial decoded controls must be zeroed independently");
}

static void TestActualOwnedConstruction() {
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto registration = Register(manager);
	sgre::scene::MotionPlayerCommands parent(manager);
	parent.SetCharacter("a");
	parent.Play("idle");
	parent.SetTickCount(11);
	parent.SetSpeed(-2);
	sgre::scene::MotionRootScene root;
	root.SetPriority(-3.5f);
	root.SetVisible(true);
	root.SetCoordinate3({11, 12, 13});
	sgre::scene::MotionPlayerRenderControls render;
	render.ColorFilter = 0xffffffee;
	render.ColorFilterParameter = 43;
	render.EmissiveColor = 0x87654321;
	render.AmbientFactor = 0.25f;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	sgre::scene::MotionSharedRandom shared(seed);
	std::size_t reads = 0;
	sgre::scene::MotionChildConstructionServices services;
	services.ReadManagerRandom = [&](const auto& sampled_manager) {
		Require(&sampled_manager == &manager, "child sampling must borrow its actual parent manager");
		++reads;

		return shared.NextUnitFloat();
	};
	const auto layer = Raw(true);
	const auto prepared = Prepared();
	const auto expected_sample = std::bit_cast<float>(std::uint32_t{0x3f5090fe});
	sgre::scene::MotionPlayerRandom expected_random(expected_sample);
	{
		auto child = std::make_unique<sgre::scene::MotionChildConstructionState>(parent, root, render, layer, prepared, services);
		Require(reads == 1 && &child->Commands().Manager() == &manager && &child->ImmediateParentCommands() == &parent && &child->UltimateParentCommands() == &parent, "child must sample once and borrow its real parent and manager without substituting a task/root owner");
		Require(child->Commands().PreferredProject() == parent.PreferredProject() && !child->Commands().SelectedProject() && child->Commands().CachedTimeline() == nullptr && child->Commands().Names().CurrentCharacter.empty() && child->Commands().Names().CurrentMotion.empty(), "constructor copies preferred project independently of names and cached timeline");
		Require(Bits(child->Commands().ClockState().Speed) == Bits(1) && Bits(child->Commands().ClockState().TickCount) == 0 && child->Commands().ClockState().InitialCompositionPending, "child clock must use real constructor defaults independently of playing parent");
		Require(child->Root().MatrixMode() == sgre::scene::MotionRootMatrixMode::SuppliedByParent && Bits(child->Root().Priority()) == Bits(-3.5f) && child->Root().TransformOrder() == prepared.TransformOrder && child->Root().Frame().Hierarchy.CoordinatePlane == 1 && child->IndependentLayerInherit(), "child root must couple actual plane/order/priority/418 and supplied-matrix configuration");
		Require(!child->Root().Overrides().Visible && child->Root().Overrides().Coordinate == std::array<float, 3>{}, "child constructor must not clone the parent root's visible/coordinate overrides");
		Require(child->RenderControls().ColorFilter == 0xffffffee && child->RenderControls().ColorFilterParameter == 43 && child->RenderControls().EmissiveColor == 0x87654321 && Bits(child->RenderControls().AmbientFactor) == Bits(0.25f), "child copies color-filter mode/parameter, emissive color and ambient factor as separate values");
		Require(child->Random().State() == expected_random.State(), "child owns the actual independent xorshift state initialized from one shared manager sample");
		RequireZeroControl(child->Control(0));
		RequireZeroControl(child->Control(1));
		std::string actual_target = "target";
		sgre::scene::MotionChildFrameControl update;
		update.DirectionMode = 4;
		update.DirectionTarget = actual_target;
		update.CompleteDirection = true;
		update.Specified = true;
		child->ApplyControlUpdate(1, update);
		child->ApplyControlUpdate(1, std::nullopt);
		Require(child->Control(1).DirectionTarget.data() == actual_target.data() && child->Control(1).Specified && child->Control(1).DirectionMode == 4, "reused control slot must borrow its actual immutable target and retain it on a missing update");
		RequireZeroControl(child->Control(0));
		RequireFailure([&] {
			static_cast<void>(child->Control(2));
		},
		    "slot");
		RequireFailure([&] {
			child->ApplyControlUpdate(2, std::nullopt);
		},
		    "slot");
		RequireFailure([&] {
			child->Root().Compose(true);
		},
		    "pose");
		child->Commands().SetCharacter("a");
		child->Commands().Play("idle");
		child->Root().SetPriority(7);
		auto nested = std::make_unique<sgre::scene::MotionChildConstructionState>(*child, layer, prepared, services);
		Require(reads == 2 && &nested->ImmediateParentCommands() == &child->Commands() && &nested->UltimateParentCommands() == &parent && Bits(nested->Root().Priority()) == Bits(7), "nested child borrows its real immediate parent and preserves ultimate parent identity");
		Require(nested->Commands().PreferredProject() == child->Commands().PreferredProject() && nested->Commands().CachedTimeline() == nullptr && nested->Commands().Names().CurrentMotion.empty(), "nested constructor copies preferred project only");
		Require(nested->Random().State() != child->Random().State(), "each child consumes one shared sample and owns an independent player stream");
		const auto nested_random = nested->Random().State();
		static_cast<void>(child->Random().NextUnitFloat());
		Require(nested->Random().State() == nested_random, "player random generation must not mutate sibling/descendant streams");
		Require(manager.ReferenceCount(registration) == 1, "child construction must not invent a resource registration pin");
		nested.reset();
		Require(parent.ClockState().Playing && child->Commands().ClockState().Playing && manager.ReferenceCount(registration) == 1, "exclusive constructor-state retirement must preserve live parent state and manager registration");
	}

	Require(manager.ReferenceCount(registration) == 1 && parent.ClockState().Playing, "constructor-state destruction does not unregister its borrowed parent resources");
	RequireFailure([&] {
		sgre::scene::MotionChildConstructionState rejected(parent, root, render, layer, prepared, {});
	},
	    "sampling");
	services.ReadManagerRandom = [](const auto&) {
		return -1.0f;
	};
	RequireFailure([&] {
		sgre::scene::MotionChildConstructionState rejected(parent, root, render, layer, prepared, services);
	},
	    "unit");
}

class Reader {
public:
	explicit Reader(std::istream& stream) : m_Stream(stream) {
	}

	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		if (value == std::char_traits<char>::eof()) {
			throw std::runtime_error("short child constructor fixture");
		}

		return static_cast<std::uint8_t>(value);
	}

	std::uint32_t Word() {
		std::uint32_t value = 0;
		for (std::uint32_t shift = 0; shift != 32; shift += 8) {
			value |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return value;
	}

private:
	std::istream& m_Stream;
};

static void ReplayNative(std::string_view filename) {
	std::ifstream stream(std::string(filename), std::ios::binary);
	Reader reader(stream);
	Require(reader.Word() == 0x314f434d, "wrong child constructor fixture magic");
	const auto count = reader.Word();
	Require(count == 40, "child constructor fixture requires40 native children");
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands parent(manager);
	for (std::uint32_t record = 0; record < count; ++record) {
		auto prepared = Prepared();
		prepared.CoordinatePlane = reader.Word();
		for (auto& operation : prepared.TransformOrder) {
			operation = static_cast<sgre::scene::MotionTransformOperation>(reader.Word());
		}

		sgre::scene::MotionRootScene root;
		root.SetPriority(std::bit_cast<float>(reader.Word()));
		const bool independent = reader.Byte() != 0;
		auto layer = Raw(independent);
		layer.Coordinate = prepared.CoordinatePlane;
		sgre::scene::MotionPlayerRenderControls render;
		render.ColorFilter = reader.Word();
		render.ColorFilterParameter = reader.Word();
		render.EmissiveColor = reader.Word();
		render.AmbientFactor = std::bit_cast<float>(reader.Word());
		const auto sample = std::bit_cast<float>(reader.Word());
		std::size_t reads = 0;
		sgre::scene::MotionChildConstructionServices services;
		services.ReadManagerRandom = [&](const auto& sampled_manager) {
			Require(&sampled_manager == &manager, "native replay used a different manager identity");
			++reads;

			return sample;
		};
		sgre::scene::MotionChildConstructionState child(parent, root, render, layer, prepared, services);
		Require(reads == 1 && child.Root().Frame().Hierarchy.CoordinatePlane == reader.Word(), "native copied plane differs");
		for (const auto operation : child.Root().TransformOrder()) {
			Require(static_cast<std::uint32_t>(operation) == reader.Word(), "native transform-order copy differs");
		}

		Require(Bits(child.Root().Priority()) == reader.Word() && child.IndependentLayerInherit() == (reader.Byte() != 0) && reader.Byte() == 1, "native priority/independent/supplied-matrix copy differs");
		Require(child.RenderControls().ColorFilter == reader.Word() && child.RenderControls().ColorFilterParameter == reader.Word() && child.RenderControls().EmissiveColor == reader.Word() && Bits(child.RenderControls().AmbientFactor) == reader.Word(), "native color-filter/emissive/ambient copies differ");
		for (const auto word : child.Random().State()) {
			Require(word == reader.Word(), "native independent random constructor word differs");
		}

		RequireZeroControl(child.Control(0));
		RequireZeroControl(child.Control(1));
	}

	Require(stream.get() == std::char_traits<char>::eof(), "trailing child constructor fixture bytes");
	std::cout << "native child constructor40 PASS\n";
}

int main(int argc, char** argv) {
	try {
		TestActualOwnedConstruction();
		if (argc == 2) {
			ReplayNative(argv[1]);
		} else if (argc != 1) {
			throw std::runtime_error("usage: motion_child_construction_tests [native-fixture]");
		}

		std::cout << "motion child construction state PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
