#include "sgre/scene/motion_grid_draw.hpp"

#include <array>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "grid failure omitted required context");
		return;
	}
	throw std::runtime_error("unsupported motion grid was accepted");
}
static std::array<std::uint32_t, 5> VertexWords(const sgre::scene::MotionSpriteVertex& vertex) {
	return {std::bit_cast<std::uint32_t>(vertex.Position.X), std::bit_cast<std::uint32_t>(vertex.Position.Y), vertex.Color, std::bit_cast<std::uint32_t>(vertex.Uv.X), std::bit_cast<std::uint32_t>(vertex.Uv.Y)};
}
static void TestOriginalGridCommand() {
	// Original patch/grid/color/UV/draw routines generated these bounded synthetic words.
	sgre::resource::MotionSource source;
	source.Name = "Atlas";
	source.Texture.Format = sgre::resource::PixelFormat::Bc7;
	source.Texture.Width = 2048;
	source.Texture.Height = 1024;
	sgre::resource::MotionIcon icon;
	icon.Name = "Icon";
	icon.Left = 124;
	icon.Top = 45;
	icon.Width = 119;
	icon.Height = 182;
	sgre::scene::MotionHierarchyState state;
	state.Attributes.Hidden = false;
	state.Attributes.Source = source.Name;
	state.Attributes.Icon = icon.Name;
	state.Attributes.UniformColor = false;
	state.Attributes.UsesDefaultColor = false;
	state.Attributes.Deformation = sgre::scene::IdentityMotionBezierPatch();
	state.Attributes.Colors = {0x15172a72, 0x02b1283a, 0x696b4c0b, 0x56a92133};
	sgre::scene::PreparedMotionLayer layer;
	layer.MeshTransform = 1;
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = 2;
	subdivision.Y = 5;
	std::array<sgre::resource::MotionPoint, 18> positions;
	std::array<sgre::scene::MotionSpriteVertex, 18> vertices;
	std::array<std::uint32_t, 60> indices;
	sgre::scene::MotionGridBuffers buffers;
	buffers.Positions = positions;
	buffers.Vertices = vertices;
	buffers.Indices = indices;
	const auto command = sgre::scene::BuildOwnMotionGridCommand(layer, state, source, icon, subdivision, buffers, 0, false);
	Require(command && command->Topology == sgre::scene::MotionDrawTopology::TriangleList, "native grid topology changed");
	Require(command->Vertices.data() == vertices.data() && command->Indices.data() == indices.data() && command->Material.Source.data() == source.Name.data(), "grid command stopped borrowing its prepared owners");
	Require(VertexWords(vertices[0]) == std::array<std::uint32_t, 5>{0x00000000, 0x00000000, 0x15172a72, 0x3d780000, 0x3d340000}, "original grid command vertex golden changed");
	Require(VertexWords(vertices[9]) == std::array<std::uint32_t, 5>{0x00000000, 0x42da6667, 0x47493e34, 0x3d780000, 0x3e1a3333}, "original grid command vertex golden changed");
	Require(VertexWords(vertices[17]) == std::array<std::uint32_t, 5>{0x42ee0000, 0x43360000, 0x56a92133, 0x3df30000, 0x3e630000}, "original grid command vertex golden changed");
	Require(std::array<std::uint32_t, 6>{indices[0], indices[1], indices[2], indices[3], indices[4], indices[5]} == std::array<std::uint32_t, 6>{0, 3, 1, 1, 3, 4}, "native first-cell triangle winding changed");
	Require(std::array<std::uint32_t, 6>{indices[54], indices[55], indices[56], indices[57], indices[58], indices[59]} == std::array<std::uint32_t, 6>{13, 16, 14, 14, 16, 17}, "native final-cell triangle offsets changed");

	const auto own_patch = state.Attributes.Deformation;
	state.Attributes.Deformation.reset();
	positions[9].X = 4;
	positions[9].Y = 8;
	const auto packed = sgre::scene::PackMotionGridCommand(layer, state, source, icon, subdivision, buffers);
	Require(packed && VertexWords(vertices[9]) == std::array<std::uint32_t, 5>{0x40800000, 0x41000000, 0x47493e34, 0x3d780000, 0x3e1a3333}, "packing replaced inherited geometry or changed native color/UV arithmetic");
	Require(positions[9].X == 4 && positions[9].Y == 8, "packing mutated its evaluated input positions");
	positions[9].X = std::numeric_limits<float>::infinity();
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PackMotionGridCommand(layer, state, source, icon, subdivision, buffers));
	},
	    "nonfinite");
	positions[9].X = 4;
	state.Attributes.Deformation = own_patch;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildOwnMotionGridCommand(layer, state, source, icon, subdivision, buffers, 0, true));
	},
	    "resolved local");
	buffers.Indices = std::span<std::uint32_t>(indices).first(59);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildOwnMotionGridCommand(layer, state, source, icon, subdivision, buffers, 0, false));
	},
	    "geometry counts");
	buffers.Indices = indices;
	const auto patch = state.Attributes.Deformation;
	state.Attributes.Deformation.reset();
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::BuildOwnMotionGridCommand(layer, state, source, icon, subdivision, buffers, 0, false));
	},
	    "resolved local");
	state.Attributes.Deformation = patch;
	state.Attributes.Hidden = true;
	Require(!sgre::scene::BuildOwnMotionGridCommand(layer, state, source, icon, subdivision, {}, 0, false), "hidden grid unexpectedly required geometry or produced a command");
	state.Attributes.Hidden = false;
	state.ModulationColor = 0x80808000;
	Require(!sgre::scene::BuildOwnMotionGridCommand(layer, state, source, icon, subdivision, {}, 0, false), "zero modulation-alpha grid produced a command");
	state.ModulationColor = 0x808080ff;
	subdivision.X = 1;
	subdivision.Y = 1;
	buffers.Positions = std::span<sgre::resource::MotionPoint>(positions).first(4);
	buffers.Vertices = std::span<sgre::scene::MotionSpriteVertex>(vertices).first(4);
	buffers.Indices = std::span<std::uint32_t>(indices).first(4);
	const auto shortcut = sgre::scene::BuildOwnMotionGridCommand(layer, state, source, icon, subdivision, buffers, 0, false);
	Require(shortcut->Topology == sgre::scene::MotionDrawTopology::TriangleStrip && indices[0] == 0 && indices[1] == 2 && indices[2] == 1 && indices[3] == 3, "native quad strip shortcut changed");
}
static void TestSubdivisionAndGridBounds() {
	const auto subdivision = sgre::scene::SelectMotionMeshSubdivision(20, 1920, 1080);
	Require(subdivision.X == 13 && subdivision.Y == 9 && sgre::scene::MotionMeshGridVertexCount(subdivision) == 140, "native aspect-ratio subdivision or point counts changed");
	Require(sgre::scene::SelectMotionMeshSubdivision(0, 1920, 1080).X == 2, "native minimum subdivision changed");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::SelectMotionMeshSubdivision(1, 0, 0));
	},
	    "integer arithmetic");
	sgre::scene::MotionMeshSubdivision invalid;
	invalid.X = 4097;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::MotionMeshGridVertexCount(invalid));
	},
	    "axis limit");
	invalid.X = 4096;
	invalid.Y = 4096;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::MotionMeshGridVertexCount(invalid));
	},
	    "vertex limit");
}
int main() {
	try {
		TestOriginalGridCommand();
		TestSubdivisionAndGridBounds();
		std::cout << "Native grid positions, colors, UVs, triangle topology, borrowed buffers, and safety checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
