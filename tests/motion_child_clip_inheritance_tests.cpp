#include "sgre/scene/motion_child_construction.hpp"

#include "sgre/scene/motion_layer_clip_rectangles.hpp"
#include "sgre/scene/motion_player_update.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

	throw std::runtime_error("invalid child clip owner was accepted");
}

static sgre::scene::PreparedMotionTimeline Preparation() {
	sgre::scene::PreparedMotionTimeline result;
	result.Parents = {std::nullopt, 0, 1, 2};
	result.Layers.resize(4);
	const std::array<std::uint32_t, 4> kinds{7, 2, 7, 3};
	for (std::size_t index = 0; index < kinds.size(); ++index) {
		result.Layers[index].Type = kinds[index];
		result.Layers[index].Frames.resize(2);
	}
	result.Layers[1].InheritMask = 0x400000;

	return result;
}

class Child {
public:
	explicit Child(const sgre::scene::PreparedMotionLayer& prepared) : m_Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		                                                                   throw std::runtime_error("clip-only fixture attempted texture upload");
	                                                                   },
	                                                                       [](auto) {
		                                                                       throw std::runtime_error("clip-only fixture attempted texture release");
	                                                                       }),
	                                                                   m_Manager(m_Cache), m_Parent(m_Manager), m_Shared(m_Seed) {
		m_Raw.Type = 3;
		m_Raw.Coordinate = prepared.CoordinatePlane;
		for (const auto operation : prepared.TransformOrder) {
			m_Raw.TransformOrder.push_back(static_cast<std::uint32_t>(operation));
		}
		sgre::resource::PsbValue independent;
		independent.Data = false;
		m_Raw.Properties.emplace_back("motionIndependentLayerInherit", std::move(independent));
		sgre::scene::MotionChildConstructionServices construction;
		construction.ReadManagerRandom = [&](const auto& manager) {
			Require(&manager == &m_Manager, "child clip fixture must use its genuine manager borrow");

			return m_Shared.NextUnitFloat();
		};
		m_State = std::make_unique<sgre::scene::MotionChildConstructionState>(m_Parent, m_ParentRoot, m_Render, m_Raw, prepared, construction);
		sgre::scene::MotionChildRootPose pose;
		pose.Overrides = m_State->Root().Overrides();
		m_State->Root().BindChildPose(pose);
		sgre::scene::MotionPlayerUpdateServices services;
		services.ReadFrameDelta = []() -> float {
			throw std::runtime_error("child clip update must not read task/manager delta");
		};
		services.PrepareFrames = [](auto, const auto&) {
			throw std::runtime_error("unselected root-only child attempted selected frame preparation");
		};
		services.ComposeLayers = [&](const auto& context) {
			m_State->Root().Compose(context.Clock.TickAssigned);
		};
		services.PublishDrawPackets = [](const auto&) {
			throw std::runtime_error("child clip update attempted publication");
		};
		services.Draw = [] {
			throw std::runtime_error("child clip update attempted draw");
		};
		services.DeliverEvent = [](const auto&) {
			throw std::runtime_error("child clip update attempted detached delivery");
		};
		m_Update = std::make_unique<sgre::scene::MotionPlayerUpdate>(m_State->Commands(), std::move(services));
	}

	[[nodiscard]] sgre::scene::MotionChildConstructionState& State() {
		return *m_State;
	}

	void Compose() {
		m_Update->UpdateChild(0);
	}

private:
	sgre::scene::TextureCache m_Cache;
	sgre::scene::MotionResourceManager m_Manager;
	sgre::scene::MotionPlayerCommands m_Parent;
	sgre::scene::MotionRootScene m_ParentRoot;
	sgre::scene::MotionPlayerRenderControls m_Render;
	sgre::resource::MotionLayer m_Raw;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> m_Seed{};
	sgre::scene::MotionSharedRandom m_Shared;
	std::unique_ptr<sgre::scene::MotionChildConstructionState> m_State;
	std::unique_ptr<sgre::scene::MotionPlayerUpdate> m_Update;
};

static void TestActualOwnedBorrowAndDescendantUse() {
	const auto prepared = Preparation();
	sgre::scene::MotionLayerClipRectangles clips(prepared);
	Child child(prepared.Layers[3]);
	Require(child.State().InheritedClip() == nullptr, "native child root clip must initialize absent");
	RequireFailure([&] {
		clips.BindChildClip(3, child.State());
	},
	    "composed kind3");
	std::array<sgre::scene::MotionComposedLayer, 4> nodes;
	for (auto& node : nodes) {
		node.Active = true;
	}
	const std::array<std::size_t, 4> indices{};
	clips.Compose(nodes, indices, nullptr);
	child.Compose();
	const auto was_dirty = child.State().Root().Overrides().Dirty;
	clips.BindChildClip(3, child.State());
	Require(child.State().InheritedClip() == &clips.OwnedRectangle(2) && child.State().Root().Overrides().Dirty == was_dirty, "native clip borrow must preserve exact owned rectangle identity without dirtying root pose");
	child.Compose();
	Require(child.State().InheritedClip() == &clips.OwnedRectangle(2), "actual child composition must preserve inherited clip borrow");
	sgre::scene::PreparedMotionTimeline descendants;
	descendants.Layers.resize(1);
	descendants.Layers[0].Type = 2;
	descendants.Parents = {std::nullopt};
	sgre::scene::MotionLayerClipRectangles child_clips(descendants);
	const std::array<sgre::scene::MotionComposedLayer, 1> child_nodes{};
	const std::array<std::size_t, 1> child_indices{};
	child_clips.Compose(child_nodes, child_indices, child.State().InheritedClip());
	Require(child_clips.Clips()[0] == &clips.OwnedRectangle(2), "actual child clip phase must use the genuine enclosing root rectangle");
	const auto* stable = child.State().InheritedClip();
	nodes[2].Hierarchy.Attributes.Coordinate = {3, 4, 5};
	clips.Compose(nodes, indices, nullptr);
	Require(child.State().InheritedClip() == stable && stable->Left == -13 && stable->Top == -7, "native inherited borrow must observe updates to the same stable enclosing rectangle");
	nodes[0].Active = false;
	nodes[2].Active = false;
	clips.Compose(nodes, indices, nullptr);
	clips.BindChildClip(3, child.State());
	Require(child.State().InheritedClip() == nullptr, "native absent clip must replace a preceding nonnull borrow");
	RequireFailure([&] {
		clips.BindChildClip(0, child.State());
	},
	    "kind3");
	RequireFailure([&] {
		clips.BindChildClip(4, child.State());
	},
	    "kind3");
	auto mismatch = prepared.Layers[3];
	mismatch.CoordinatePlane = 1;
	Child other(mismatch);
	RequireFailure([&] {
		clips.BindChildClip(3, other.State());
	},
	    "constructor configuration");
}

class Reader {
public:
	explicit Reader(const std::string& path) {
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		Require(static_cast<bool>(stream), "cannot open child clip native fixture");
		const auto size = stream.tellg();
		Require(size >= 16 && size <= 65536, "child clip fixture exceeds its bounded length");
		m_Bytes.resize(static_cast<std::size_t>(size));
		stream.seekg(0);
		stream.read(m_Bytes.data(), static_cast<std::streamsize>(m_Bytes.size()));
		Require(static_cast<bool>(stream) && std::string_view(m_Bytes.data(), 4) == "MCLI", "child clip native fixture read/magic failed");
		m_Offset = 4;
	}

	[[nodiscard]] std::uint8_t Byte() {
		Require(m_Offset < m_Bytes.size(), "child clip fixture is truncated");

		return static_cast<std::uint8_t>(m_Bytes[m_Offset++]);
	}

	[[nodiscard]] std::uint32_t Word() {
		std::uint32_t word = 0;
		for (std::size_t shift = 0; shift < 32; shift += 8) {
			word |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return word;
	}

	[[nodiscard]] float Float() {
		return std::bit_cast<float>(Word());
	}

	[[nodiscard]] bool Complete() const {
		return m_Offset == m_Bytes.size();
	}

	[[nodiscard]] std::size_t Position() const {
		return m_Offset;
	}

	void SetPosition(std::size_t position) {
		Require(position <= m_Bytes.size(), "child clip fixture position exceeds its bounded bytes");
		m_Offset = position;
	}

private:
	std::vector<char> m_Bytes;
	std::size_t m_Offset = 0;
};

static void CompareClip(Reader& reader, const sgre::scene::MotionClipRectangle* actual, const sgre::scene::MotionLayerClipRectangles& owner, const sgre::scene::MotionClipRectangle& enclosing) {
	const auto ordinal = reader.Word();
	const sgre::scene::MotionClipRectangle* expected = nullptr;
	if (ordinal == 0) {
		expected = &enclosing;
	} else if (ordinal != 0xffffffff) {
		Require(ordinal == 1 || ordinal == 3, "native clip ordinal requires an actual owned kind7 rectangle");
		expected = &owner.OwnedRectangle(ordinal - 1);
	}

	Require(actual == expected, "native child clip borrow identity differs");
	const std::array<float, 4> values = actual != nullptr ? std::array{actual->Left, actual->Top, actual->Right, actual->Bottom} : std::array<float, 4>{};
	for (const auto value : values) {
		Require(std::bit_cast<std::uint32_t>(value) == reader.Word(), "native inherited clip edge word differs");
	}
}

static void Replay(const std::string& path) {
	Reader reader(path);
	Require(reader.Word() == 32 && reader.Word() == 4 && reader.Word() == 4, "native child clip fixture dimensions changed");
	for (std::size_t scenario = 0; scenario < 32; ++scenario) {
		auto prepared = Preparation();
		for (std::size_t index = 0; index < 4; ++index) {
			const auto parent = reader.Word();
			Require(parent <= index, "native clip parent must precede its child");
			prepared.Parents[index] = parent == 0 ? std::nullopt : std::optional<std::uint32_t>(parent - 1);
			prepared.Layers[index].Frames.resize(8);
		}

		// Each native current-slot observation becomes a distinct immutable fixture frame before owner construction.
		const auto first_step = reader.Position();
		for (std::size_t step = 0; step < 4; ++step) {
			static_cast<void>(reader.Byte());
			for (std::size_t edge = 0; edge < 4; ++edge) {
				static_cast<void>(reader.Float());
			}
			for (std::size_t index = 0; index < 4; ++index) {
				static_cast<void>(reader.Byte());
				const auto slot = reader.Byte();
				Require(slot < 2, "native clip slot exceeds its actual decoded pair");
				for (std::size_t component = 0; component < 7; ++component) {
					static_cast<void>(reader.Float());
				}

				prepared.Layers[index].Frames[step * 2 + slot].Attributes.OriginX = reader.Float();
				prepared.Layers[index].Frames[step * 2 + slot].Attributes.OriginY = reader.Float();
			}
			for (std::size_t word = 0; word < 10; ++word) {
				static_cast<void>(reader.Word());
			}

			static_cast<void>(reader.Byte());
			static_cast<void>(reader.Byte());
		}

		reader.SetPosition(first_step);
		sgre::scene::MotionLayerClipRectangles clips(prepared);
		Child child(prepared.Layers[3]);
		sgre::scene::MotionClipRectangle enclosing;
		for (std::size_t step = 0; step < 4; ++step) {
			const auto present = reader.Byte() != 0;
			enclosing.Left = reader.Float();
			enclosing.Top = reader.Float();
			enclosing.Right = reader.Float();
			enclosing.Bottom = reader.Float();
			std::array<sgre::scene::MotionComposedLayer, 4> nodes;
			std::array<std::size_t, 4> indices{};
			for (std::size_t index = 0; index < 4; ++index) {
				nodes[index].Active = reader.Byte() != 0;
				const auto slot = reader.Byte();
				Require(slot < 2, "native clip slot exceeds genuine prepared frame pair");
				indices[index] = step * 2 + slot;
				for (auto& coordinate : nodes[index].Hierarchy.Attributes.Coordinate) {
					coordinate = reader.Float();
				}

				nodes[index].Hierarchy.Transform.XX = reader.Float();
				nodes[index].Hierarchy.Transform.XY = reader.Float();
				nodes[index].Hierarchy.Transform.YX = reader.Float();
				nodes[index].Hierarchy.Transform.YY = reader.Float();
				const auto origin_x = reader.Float();
				const auto origin_y = reader.Float();
				Require(std::bit_cast<std::uint32_t>(prepared.Layers[index].Frames[indices[index]].Attributes.OriginX) == std::bit_cast<std::uint32_t>(origin_x) && std::bit_cast<std::uint32_t>(prepared.Layers[index].Frames[indices[index]].Attributes.OriginY) == std::bit_cast<std::uint32_t>(origin_y), "native current origin differs from its immutable prepared fixture frame");
			}

			clips.Compose(nodes, indices, present ? &enclosing : nullptr);
			CompareClip(reader, clips.Clips()[3], clips, enclosing);
			sgre::scene::MotionChildRootPose pose;
			pose.Overrides = child.State().Root().Overrides();
			child.State().Root().BindChildPose(pose);
			const auto before = child.State().Root().Overrides().Dirty;
			clips.BindChildClip(3, child.State());
			CompareClip(reader, child.State().InheritedClip(), clips, enclosing);
			Require(before == (reader.Byte() != 0) && child.State().Root().Overrides().Dirty == (reader.Byte() != 0), "native clip transfer root-dirty preservation differs");
			const auto* borrowed = child.State().InheritedClip();
			child.Compose();
			Require(child.State().InheritedClip() == borrowed && !child.State().Root().Overrides().Dirty, "actual child numeric composition changed clip identity or failed to consume root dirty");
		}
	}

	Require(reader.Complete(), "child clip native fixture has trailing bytes");
}

int main(int argc, char** argv) {
	try {
		TestActualOwnedBorrowAndDescendantUse();
		if (argc == 2) {
			Replay(argv[1]);
		}

		std::cout << "Motion actual child clip borrow and 128 native inheritance comparisons passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
