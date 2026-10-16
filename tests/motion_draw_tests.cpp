#include "sgre/scene/motion_draw.hpp"

#include <bit>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "sprite failure omitted required context");

		return;
	}

	throw std::runtime_error("invalid sprite was accepted: " + std::string(context));
}

struct SpriteFixture {
	sgre::scene::PreparedMotionLayer Layer;
	sgre::scene::MotionHierarchyState State;
	sgre::resource::MotionSource Source;
	sgre::resource::MotionIcon Icon;
};

static SpriteFixture MakeFixture() {
	SpriteFixture fixture;
	fixture.State.Attributes.Hidden = false;
	fixture.State.Attributes.Source = "Atlas";
	fixture.State.Attributes.Icon = "Icon";
	fixture.State.Attributes.Colors.fill(0x808080ff);
	fixture.Source.Name = "Atlas";
	fixture.Source.Texture.Width = 4096;
	fixture.Source.Texture.Height = 4096;
	fixture.Icon.Name = "Icon";
	fixture.Icon.Width = 32;
	fixture.Icon.Height = 32;

	return fixture;
}

static void TestOriginalSpritePositionVectors() {
	// Original 0x140520d80 includes origin adjustment, depth projection, and 0x140525fd0 quad generation.
	const std::array<std::array<std::uint32_t, 14>, 2> inputs = {{{0xc00fcb53, 0xbd93796d, 0xc005e8aa, 0x3f335ce9, 0xc28dc900, 0x422aab1e, 0x41eb2ba3, 0xc20b2ece, 0x41be00e7, 0xc09cc552, 0x3fd37798, 0xbfcc993f, 0x000003e1, 0x00000169}, {0x3f74d80c, 0x40200365, 0x3ea8af08, 0x3f2ff246, 0xc0b67501, 0xc1dd5a82, 0xc11abe98, 0xc20530c2, 0xc23524e3, 0x40ae894f, 0x401e4d7d, 0xbfb44953, 0x000002d7, 0x000000de}}};
	const std::array<std::array<std::uint32_t, 8>, 2> expected = {{{0xc31e3fc0, 0xc2d258a2, 0xc51554e7, 0xc5086da4, 0xc3383e87, 0x4313c1b4, 0xc516f4d3, 0xc4f13d87}, {0x42ffefd6, 0x41c46868, 0x444dd25e, 0x438408ca, 0x442ac0ed, 0x43312126, 0x44ac4aa7, 0x43d052d6}}};
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		auto fixture = MakeFixture();
		fixture.State.Transform.XX = std::bit_cast<float>(inputs[index][0]);
		fixture.State.Transform.XY = std::bit_cast<float>(inputs[index][1]);
		fixture.State.Transform.YX = std::bit_cast<float>(inputs[index][2]);
		fixture.State.Transform.YY = std::bit_cast<float>(inputs[index][3]);
		for (std::size_t component = 0; component < 3; ++component) {
			fixture.State.Attributes.Coordinate[component] = std::bit_cast<float>(inputs[index][4 + component]);
		}

		fixture.Icon.OriginX = std::bit_cast<float>(inputs[index][7]);
		fixture.Icon.OriginY = std::bit_cast<float>(inputs[index][8]);
		fixture.State.Attributes.OriginX = std::bit_cast<float>(inputs[index][9]);
		fixture.State.Attributes.OriginY = std::bit_cast<float>(inputs[index][10]);
		fixture.Icon.Width = static_cast<float>(inputs[index][12]);
		fixture.Icon.Height = static_cast<float>(inputs[index][13]);
		const auto command = sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, std::bit_cast<float>(inputs[index][11]), false);
		Require(command.has_value(), "visible original sprite vector was omitted");
		for (std::size_t corner = 0; corner < command->Vertices.size(); ++corner) {
			Require(std::bit_cast<std::uint32_t>(command->Vertices[corner].Position.X) == expected[index][corner * 2] && std::bit_cast<std::uint32_t>(command->Vertices[corner].Position.Y) == expected[index][corner * 2 + 1], "original sprite position instruction vector changed");
		}
	}
}

static void TestOriginalUvAndStripVector() {
	// Original 0x140556c20, source extent 2405×2863 and atlas rectangle (366,635,871,862).
	const std::array<std::uint32_t, 8> expected = {0x3e1bd5d9, 0x3e631e50, 0x3f03ac1f, 0x3e631e50, 0x3e1bd5d9, 0x3f05db57, 0x3f03ac1f, 0x3f05db57};
	auto fixture = MakeFixture();
	fixture.Source.Texture.Width = 2405;
	fixture.Source.Texture.Height = 2863;
	fixture.Icon.Left = 366;
	fixture.Icon.Top = 635;
	fixture.Icon.Width = 871;
	fixture.Icon.Height = 862;
	const auto command = sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false);
	for (std::size_t corner = 0; corner < command->Vertices.size(); ++corner) {
		Require(std::bit_cast<std::uint32_t>(command->Vertices[corner].Uv.X) == expected[corner * 2] && std::bit_cast<std::uint32_t>(command->Vertices[corner].Uv.Y) == expected[corner * 2 + 1], "original sprite UV instruction vector changed");
	}

	Require(command->Indices == std::array<std::uint16_t, 4>({0, 2, 1, 3}), "original sprite triangle strip indices changed");
	Require(command->Source.data() == fixture.Source.Name.data() && command->Icon.data() == fixture.Icon.Name.data(), "sprite command copied borrowed resource names");
}

static void TestOriginalDrawColorVectors() {
	// Original 0x14050ad30, including uniform/corner colors, opacity, blend coupling, and texture flags.
	const std::array<std::array<std::uint32_t, 10>, 5> inputs = {{{0x49054422, 0x76d191f3, 0x04914e84, 0x1df7cada, 0x000000ff, 0x00000000, 0x808080ff, 0x00000001, 0, 0}, {0x78d6b9b6, 0x78d6b9b6, 0x78d6b9b6, 0x78d6b9b6, 0x0000000b, 0x00000010, 0x7c8f094d, 0x0000000a, 1, 0}, {0x808080ff, 0x808080ff, 0x808080ff, 0x808080ff, 0x000000c2, 0x00000013, 0x808080ff, 0x0000000c, 1, 1}, {0x15395623, 0x15395623, 0x15395623, 0x15395623, 0x000000ff, 0x00000103, 0xab0ef93b, 0x00000001, 1, 0}, {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0x00000095, 0x00000000, 0x808080ff, 0x00000004, 1, 1}}};
	const std::array<std::array<std::uint32_t, 5>, 5> expected = {{{0x00000001, 0x49054422, 0x76d191f3, 0x04914e84, 0x1df7cada}, {0x00000008, 0x74ef0d02, 0x74ef0d02, 0x74ef0d02, 0x74ef0d02}, {0x0000000c, 0x808080c2, 0x808080c2, 0x808080c2, 0x808080c2}, {0x00000001, 0x1c06a708, 0x1c06a708, 0x1c06a708, 0x1c06a708}, {0x00000004, 0xffffff95, 0xffffff95, 0xffffff95, 0xffffff95}}};
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		auto fixture = MakeFixture();
		for (std::size_t corner = 0; corner < fixture.State.Attributes.Colors.size(); ++corner) {
			fixture.State.Attributes.Colors[corner] = inputs[index][corner];
		}

		fixture.State.Attributes.Opacity = inputs[index][4];
		fixture.State.Attributes.BlendMode = inputs[index][5];
		fixture.State.ModulationColor = inputs[index][6];
		fixture.Icon.AttributeFlags = inputs[index][7];
		fixture.State.Attributes.UniformColor = inputs[index][8] != 0;
		fixture.State.Attributes.UsesDefaultColor = inputs[index][9] != 0;
		const auto command = sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false);
		Require(command->TextureAttributes == expected[index][0], "original sprite texture flags changed");
		for (std::size_t corner = 0; corner < command->Vertices.size(); ++corner) {
			Require(command->Vertices[corner].Color == expected[index][corner + 1], "original sprite color/opacity instruction vector changed");
		}
	}
}

static void TestSupportAndSafetyGuards() {
	auto fixture = MakeFixture();
	fixture.Layer.MeshTransform = 1;
	Require(sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false).has_value(), "native mesh layer without a patch failed its quad fallback");
	fixture.State.Attributes.Deformation = sgre::resource::MotionBezierPatch{};
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false));
	},
	    "undeformed type-0");
	fixture.State.Attributes.Deformation.reset();
	fixture.Layer.MeshTransform = 2;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false));
	},
	    "undeformed type-0");
	fixture.Layer.MeshTransform = 0;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, true));
	},
	    "undeformed type-0");
	fixture.State.Attributes.UnresolvedMask = 0x80000;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false));
	},
	    "unresolved frame");
	fixture.State.Attributes.UnresolvedMask = 0x40000;
	Require(sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false).has_value(), "native ignored mesh bit rejected an undeformed layer");
	fixture.Icon.Left = 4095;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false));
	},
	    "atlas");
	fixture.Icon.Left = 0;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, std::numeric_limits<float>::quiet_NaN(), false));
	},
	    "projection");
	fixture.State.Attributes.Hidden = true;
	Require(!sgre::scene::BuildMotionSpriteCommand(fixture.Layer, fixture.State, fixture.Source, fixture.Icon, 0, false).has_value(), "hidden sprite produced a draw command");
}

int main() {
	try {
		TestOriginalSpritePositionVectors();
		TestOriginalUvAndStripVector();
		TestOriginalDrawColorVectors();
		TestSupportAndSafetyGuards();
		std::cout << "Native undeformed sprite positions, UVs, strip indices, colors, flags, and safety checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
