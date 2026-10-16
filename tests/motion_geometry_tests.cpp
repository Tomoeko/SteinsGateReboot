#include "sgre/scene/motion_geometry.hpp"

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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "geometry failure lost its context");

		return;
	}

	throw std::runtime_error("invalid motion geometry was accepted");
}

struct NativeGeometryNode {
	std::array<std::uint32_t, 7> Metadata{};
	std::array<std::uint32_t, 11> Pose{};
	std::array<std::array<std::uint32_t, 8>, 4> Controls{};
};

struct NativeGeometryFixture {
	std::array<NativeGeometryNode, 3> Nodes{};
	std::uint32_t SubdivisionX = 0;
	std::uint32_t SubdivisionY = 0;
	std::array<std::array<std::uint32_t, 2>, 3> Expected{};
	bool InheritedChild = false;
	float Ratio = 1;
};

static void TestOriginalCompleteGeometry() {
	// Original 0x140520d80 executed complete scenes with preallocated native descriptors and caches.
	// Fixtures cover combined/stopped patches, a different-size reference window, and inherited bilinear geometry.
	const std::array<NativeGeometryFixture, 4> fixtures = {{
	    {
	        {{
	            {
	                {0x02000000, 0x00000008, 0x00000000, 0x00000001, 0x00000005, 0x000000ab, 0x0000017c},
	                {0xc213c1cc, 0x41f89872, 0x3f39f641, 0xc18d429b, 0xc12d06ed, 0xc16f2b3b, 0xc0819550, 0x3f7be75b, 0x3b542717, 0xbd232e9d, 0x3f75825a},
	                {{
	                    {0x3e40da0b, 0xbdb6d261, 0x3ed4829c, 0xbdfaeb4f, 0x3f3e9d6b, 0xbe0e0803, 0x3f817bb5, 0x3df18d68},
	                    {0xbce7b595, 0x3ec386a1, 0x3e936ee1, 0x3e1176bc, 0x3efaaeb3, 0x3efe7383, 0x3f4de268, 0x3ebf0b21},
	                    {0xbcba702e, 0x3f55cc48, 0x3ed5b852, 0x3f1531a6, 0x3f1c5f46, 0x3f57e9cc, 0x3f6403cc, 0x3ef28917},
	                    {0x3e1a9228, 0x3f86f8ef, 0x3ecc4382, 0x3f97d76f, 0x3f588119, 0x3f4fbb5c, 0x3f63f0b8, 0x3f7f347a},
	                }},
	            },
	            {
	                {0x02000000, 0x00000000, 0x00000001, 0x00000001, 0x00000006, 0x00000228, 0x000001e4},
	                {0x42205acc, 0x4217e469, 0xc2166d92, 0xc10e57d3, 0x40b7ad84, 0xc1033120, 0x419eba46, 0x3f5a0ea3, 0x3ce6124b, 0xbcd66842, 0x3f4ee52d},
	                {{
	                    {0x3e1268f5, 0xbca14c27, 0x3f0196cd, 0x3e3a156a, 0x3f5c7c59, 0x3dd37a90, 0x3f78dcd4, 0xbe08d8f0},
	                    {0xbc8b6b9f, 0x3e37be40, 0x3e71c374, 0x3eb151b3, 0x3f5b3699, 0x3ec3754a, 0x3f577a56, 0x3f04be87},
	                    {0xbe275b37, 0x3f0b2175, 0x3ec31765, 0x3efb7ac3, 0x3f521fdc, 0x3f09297d, 0x3f95f3b4, 0x3f5d3b82},
	                    {0xbdd3da89, 0x3f886df2, 0x3e9e9f9a, 0x3f664997, 0x3f5abeca, 0x3f83f869, 0x3f614a88, 0x3f5003e9},
	                }},
	            },
	            {
	                {0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000007, 0x00000158, 0x000000f1},
	                {0xc228f776, 0xc2340257, 0xc175f201, 0x413c6011, 0x417b7c22, 0x4128ed19, 0xc199dea9, 0x3f966af9, 0xbd20f67b, 0x3baf3557, 0x3f7e4c80},
	                {{
	                    {0x3d76ca07, 0xbde26189, 0x3e405ba8, 0xbe4a8cbd, 0x3f282c91, 0xbde41a34, 0x3f85e1d2, 0xbd84db0e},
	                    {0x3dcf7e18, 0x3ec2106f, 0x3e92daf2, 0x3e987b47, 0x3f5d52f2, 0x3e50df20, 0x3f81e970, 0x3ee0831d},
	                    {0x3da6165a, 0x3f5d5851, 0x3ede4586, 0x3ef1562d, 0x3f29c250, 0x3f10a49c, 0x3f92dcd1, 0x3ef07b2f},
	                    {0xbdaac0fe, 0x3f8caeac, 0x3ed0456d, 0x3f910dc6, 0x3f2c98ba, 0x3f946705, 0x3f6f0268, 0x3f903e30},
	                }},
	            },
	        }},
	        5,
	        4,
	        {{{0x43727b7c, 0xc14c2dd4}, {0x43b08c6b, 0x431163de}, {0x43b2eeb5, 0x438442cb}}},
	    },
	    {
	        {{
	            {
	                {0x02000000, 0x00000008, 0x00000000, 0x00000001, 0x00000009, 0x00000225, 0x00000194},
	                {0xc02d390a, 0xc0845961, 0x41d1838c, 0x4183a3f1, 0xc1350cb1, 0x41329cf6, 0xc19d14ff, 0x3f78e8c1, 0xbb80ae3d, 0xbd3343fa, 0x3f6769a0},
	                {{
	                    {0xbbc8987c, 0x3c2a7e58, 0x3eae17b6, 0x3d72ae99, 0x3f240374, 0x3cd071f8, 0x3f9744ac, 0x3d63fd4d},
	                    {0x3e2ac54d, 0x3ed7f57c, 0x3e8e6508, 0x3e1e4560, 0x3f4e9bde, 0x3e849ff4, 0x3f8fdf6d, 0x3e4069dc},
	                    {0x3d8abaa0, 0x3f420fc2, 0x3eaf781b, 0x3f49b69c, 0x3f1b8f5e, 0x3efe30c2, 0x3f590f33, 0x3f50061d},
	                    {0xbda04671, 0x3f8822a7, 0x3ebadbd9, 0x3f8ce58e, 0x3f223227, 0x3f9383d6, 0x3f828a96, 0x3f875de2},
	                }},
	            },
	            {
	                {0x00000000, 0x00000008, 0x00000001, 0x00000001, 0x00000006, 0x0000014a, 0x0000024d},
	                {0xc2388ea5, 0x40b6d25e, 0x414335ad, 0x4201a1e5, 0x418a8ed8, 0x40d2fd4e, 0x418fc952, 0x3f919fa6, 0x3bba5beb, 0xbcc869a4, 0x3f53261f},
	                {{
	                    {0x3db85e78, 0x3cfaa8a8, 0x3e716f6d, 0xbe22999b, 0x3f0af3b5, 0x3e06b693, 0x3f5795e2, 0xbdf0e26d},
	                    {0x3e1f6f0f, 0x3e89a261, 0x3e28d863, 0x3eb2e5ad, 0x3f4aef47, 0x3e24bee5, 0x3f7c0f99, 0x3eece560},
	                    {0xbddcb8c5, 0x3f5b162e, 0x3ead2fa1, 0x3f3aef3b, 0x3f3d2d1b, 0x3f12a5d6, 0x3f635fde, 0x3f2849ef},
	                    {0x3e0d7305, 0x3f67b366, 0x3ecc2a85, 0x3f57690d, 0x3f25524b, 0x3f87657a, 0x3f7567d2, 0x3f82e228},
	                }},
	            },
	            {
	                {0x02000000, 0x00000008, 0x00000001, 0x00000000, 0x00000005, 0x000001ab, 0x000000e3},
	                {0xc2184e00, 0xc22a740b, 0x4041d2d4, 0xc2285652, 0xc0da5cdf, 0x41763749, 0x4183b3f8, 0x3f84fb6f, 0x3d3b5b35, 0xbbc96fb8, 0x3f8091ad},
	                {},
	            },
	        }},
	        4,
	        3,
	        {{{0x42ecefa5, 0x42d4d001}, {0x42d8733b, 0x4380592d}, {0x43d94df0, 0x43d52804}}},
	    },
	    {
	        {{
	            {
	                {0x02000000, 0x00000008, 0x00000000, 0x00000001, 0x00000006, 0x0000007d, 0x00000108},
	                {0x4221f20a, 0x4246818d, 0x42206018, 0xc0d4f503, 0x4131ed41, 0xc0f5d0ec, 0xc1258a64, 0x3f897188, 0xbc9f7165, 0x3cc18935, 0x3f598637},
	                {{
	                    {0x3b9417e4, 0xbe0fd500, 0x3eaa3478, 0xbe06a4c3, 0x3f0be12a, 0x3a4b9bd2, 0x3f9600a6, 0xbdc67472},
	                    {0xbe10d9b7, 0x3f028d1c, 0x3f07c7a9, 0x3eb68aeb, 0x3ef04136, 0x3f035f0a, 0x3f6b787a, 0x3e8bcdb7},
	                    {0x3e0493c4, 0x3f149e61, 0x3e0a402c, 0x3f333228, 0x3f2f3bfe, 0x3f3fc639, 0x3f6b8cfc, 0x3f1b3ded},
	                    {0xbe340007, 0x3f7c7092, 0x3ea344eb, 0x3f7d3104, 0x3f2a8096, 0x3f742d64, 0x3f826e27, 0x3f97fc6a},
	                }},
	            },
	            {
	                {0x02000000, 0x00000000, 0x00000001, 0x00000001, 0x00000005, 0x00000292, 0x0000022f},
	                {0xc22d3d23, 0x4240b7cc, 0xc0f29205, 0x3fdc298b, 0xc16be143, 0x40efc23a, 0xc1898825, 0x3f64f2e6, 0x3cfd83a7, 0x3c166ded, 0x3f830b5c},
	                {{
	                    {0x3cf8c0e8, 0xbdf94587, 0x3e3063ef, 0x3e2ecbac, 0x3f509487, 0x3e3814db, 0x3f59e0e1, 0x3c257e95},
	                    {0x3e0b573c, 0x3eb4025d, 0x3f0565f4, 0x3ebd4ce8, 0x3f3e5355, 0x3e75410f, 0x3f6ee51f, 0x3ea7de93},
	                    {0xbde8ce29, 0x3f2656d4, 0x3e9d83d1, 0x3efcec21, 0x3ef2725b, 0x3f4e048b, 0x3f915b19, 0x3f3f6797},
	                    {0xbe0a72cf, 0x3f98426b, 0x3e99c829, 0x3f98608f, 0x3f551013, 0x3f6b0839, 0x3f4e4f63, 0x3f5ca37b},
	                }},
	            },
	            {
	                {0x02000000, 0x00000000, 0x00000000, 0x00000000, 0x00000006, 0x0000008f, 0x000002ab},
	                {0xc1984d4c, 0x4130db2f, 0x4184da9c, 0xc11f901c, 0xbfdce10e, 0x3fffe246, 0xc154a528, 0x3f4d110f, 0xbc437f34, 0xbd38d5de, 0x3f5be644},
	                {},
	            },
	        }},
	        2,
	        6,
	        {{{0xc085a38e, 0xc02901e7}, {0x42a0c262, 0x43e8b649}, {0x457ebc66, 0x45780229}}},
	    },
	    {
	        {{
	            {
	                {0x02000000, 0x00000008, 0x00000000, 0x00000001, 0x00000008, 0x0000012d, 0x00000173},
	                {0x4112c91f, 0x421a568b, 0x42079715, 0x41e837fc, 0xbfef3ab7, 0x41937b13, 0xc16d9558, 0x3f771937, 0xbc3d0f59, 0xbbc316b1, 0x3f91c290},
	                {{
	                    {0xbdcb7614, 0xbe031917, 0x3ed2188c, 0xbbe23645, 0x3f114ed7, 0xbddefc94, 0x3f613082, 0xbe3d23e9},
	                    {0x3de136a2, 0x3ec78c9b, 0x3f02cdf3, 0x3e280fe4, 0x3f25f37b, 0x3eac690b, 0x3f801b14, 0x3ec967ff},
	                    {0x3dd65084, 0x3f2160e5, 0x3ec7c39d, 0x3f3ae865, 0x3f4457a2, 0x3f0085a0, 0x3f595a1f, 0x3f58eaae},
	                    {0xbd83d624, 0x3f6a40d2, 0x3ea6190d, 0x3f653956, 0x3f4dc5cb, 0x3f5d7519, 0x3f90abdb, 0x3f8a00af},
	                }},
	            },
	            {
	                {0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000005, 0x000000f4, 0x00000101},
	                {0x420c0b59, 0xc1b3c000, 0xc0d0e211, 0x410a8966, 0xc066d996, 0xc0c28275, 0xc18fa294, 0x3f98e983, 0x3b94f8e6, 0xba959424, 0x3f8cfa5e},
	                {{
	                    {0x3e3532d3, 0xbe2cd05a, 0x3eab0085, 0x3e0f0434, 0x3f10a5d8, 0x3e49b104, 0x3f9479c0, 0xbe2c40d4},
	                    {0x3cb41386, 0x3ed3ece5, 0x3e7263e1, 0x3ede29b6, 0x3f11fcff, 0x3efe8726, 0x3f71ca9d, 0x3e405a93},
	                    {0xbe2855b7, 0x3f1a1036, 0x3e92de62, 0x3f2e4897, 0x3f195ee4, 0x3f1b5b38, 0x3f6d94f8, 0x3f19450b},
	                    {0x3db8d8c5, 0x3f761466, 0x3ecd9f51, 0x3f8a67c0, 0x3f233193, 0x3f972d9b, 0x3f8b0d12, 0x3f9945d4},
	                }},
	            },
	            {
	                {0x02000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000182, 0x00000198},
	                {0x41f09dc1, 0x4091bf17, 0x41b1269e, 0x4246767d, 0xc0cacf04, 0x3f23cf3c, 0xc10cbf63, 0x3f857429, 0xbd21eaf5, 0xbd4ca967, 0x3f8d1ad6},
	                {},
	            },
	        }},
	        2,
	        4,
	        {{{0xc2840573, 0xc3a276c9}, {0x4303940d, 0x431c8f43}, {0x443723a0, 0x43d3e5ae}}},
	        true,
	        0.75f,
	    },

	}};
	for (const auto& fixture : fixtures) {
		std::array<sgre::scene::MotionDeformationNode, 3> nodes;
		std::array<sgre::scene::MotionGeometryFrame, 3> frames;
		std::array<sgre::scene::MotionDeformationLink, 3> links;
		std::array<sgre::scene::MotionGeometryCache, 3> cache;
		for (std::uint32_t index = 0; index < nodes.size(); ++index) {
			const auto& input = fixture.Nodes[index];
			auto& node = nodes[index];
			if (index != 0) {
				node.Parent = index - 1;
			}

			node.InheritMask = input.Metadata[0];
			node.MeshSyncChildMask = input.Metadata[1];
			node.MeshDivision = input.Metadata[4];
			node.MeshCombine = input.Metadata[2] != 0;
			node.HasPatch = input.Metadata[3] != 0;
			node.MeshTransform = fixture.InheritedChild && index == 2 ? 0u : 1u;
			node.Hidden = false;
			node.SourceReady = true;
			sgre::resource::MotionIcon icon;
			icon.Width = static_cast<float>(input.Metadata[5]);
			icon.Height = static_cast<float>(input.Metadata[6]);
			icon.OriginX = std::bit_cast<float>(input.Pose[0]);
			icon.OriginY = std::bit_cast<float>(input.Pose[1]);
			sgre::scene::MotionHierarchyState state;
			state.Attributes.OriginX = std::bit_cast<float>(input.Pose[2]);
			state.Attributes.OriginY = std::bit_cast<float>(input.Pose[3]);
			for (std::size_t component = 0; component < 3; ++component) {
				state.Attributes.Coordinate[component] = std::bit_cast<float>(input.Pose[4 + component]);
			}

			state.Transform.XX = std::bit_cast<float>(input.Pose[7]);
			state.Transform.XY = std::bit_cast<float>(input.Pose[8]);
			state.Transform.YX = std::bit_cast<float>(input.Pose[9]);
			state.Transform.YY = std::bit_cast<float>(input.Pose[10]);
			if (node.HasPatch) {
				sgre::resource::MotionBezierPatch patch;
				for (std::size_t row = 0; row < 4; ++row) {
					for (std::size_t column = 0; column < 4; ++column) {
						patch.Controls[row * 4 + column].X = std::bit_cast<float>(input.Controls[row][column * 2]);
						patch.Controls[row * 4 + column].Y = std::bit_cast<float>(input.Controls[row][column * 2 + 1]);
					}
				}

				state.Attributes.Deformation = patch;
			}

			sgre::scene::PreparedMotionLayer layer;
			layer.MeshTransform = node.MeshTransform;
			frames[index] = sgre::scene::PrepareMotionGeometryFrame(layer, state, icon, 0.25f);
		}

		const sgre::scene::PreparedMotionGeometry geometry(nodes, frames, links, cache);
		sgre::scene::MotionMeshSubdivision subdivision;
		subdivision.X = fixture.SubdivisionX;
		subdivision.Y = fixture.SubdivisionY;
		std::array<sgre::scene::MotionMeshDivisionCache, 3> division_cache;
		const auto selected_subdivision = geometry.SelectSubdivision(2, fixture.Ratio, division_cache);
		Require(selected_subdivision.X == subdivision.X && selected_subdivision.Y == subdivision.Y, "native scene division selection changed");
		std::array<sgre::resource::MotionPoint, 64> storage;
		const auto positions = std::span(storage).first(sgre::scene::MotionMeshGridVertexCount(subdivision));
		geometry.EvaluateVertices(2, subdivision, positions);
		const std::array<std::size_t, 3> selected = {0, positions.size() / 2, positions.size() - 1};
		for (std::size_t index = 0; index < selected.size(); ++index) {
			const auto& position = positions[selected[index]];
			Require(std::bit_cast<std::uint32_t>(position.X) == fixture.Expected[index][0] && std::bit_cast<std::uint32_t>(position.Y) == fixture.Expected[index][1], "complete original geometry instruction vector changed");
		}

		Require(cache[0].Warp.has_value() && links[0].ExportToChildren, "prepared geometry lost its owning ancestor warp");
	}
}

static void TestGeometryGuardsAndQuad() {
	std::array<sgre::scene::MotionDeformationNode, 1> nodes;
	nodes[0].Hidden = false;
	nodes[0].SourceReady = true;
	std::array<sgre::scene::MotionGeometryFrame, 1> frames;
	frames[0].Quad = {{{1, 2}, {3, 2}, {1, 4}, {3, 4}}};
	std::array<sgre::scene::MotionDeformationLink, 1> links;
	std::array<sgre::scene::MotionGeometryCache, 1> cache;
	const sgre::scene::PreparedMotionGeometry geometry(nodes, frames, links, cache);
	std::array<sgre::resource::MotionPoint, 4> positions;
	geometry.EvaluateVertices(0, {}, positions);
	Require(positions[0].X == 1 && positions[0].Y == 2 && positions[3].X == 3 && positions[3].Y == 4, "undeformed native geometry did not preserve its exact corners");
	RequireFailure([&]() {
		geometry.EvaluateVertices(1, {}, positions);
	},
	    "index");
	RequireFailure([&]() {
		geometry.EvaluateVertices(0, {}, std::span(positions).first(3));
	},
	    "output");
	std::array<sgre::resource::MotionPoint, 9> grid;
	sgre::scene::MotionMeshSubdivision subdivision;
	subdivision.X = subdivision.Y = 2;
	RequireFailure([&]() {
		geometry.EvaluateVertices(0, subdivision, grid);
	},
	    "quad subdivision");
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PreparedMotionGeometry(nodes, {}, links, cache));
	},
	    "counts");
	nodes[0].HasPatch = true;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PreparedMotionGeometry(nodes, frames, links, cache));
	},
	    "mesh state");
	nodes[0].HasPatch = false;
	frames[0].Anchor.X = std::numeric_limits<float>::infinity();
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::PreparedMotionGeometry(nodes, frames, links, cache));
	},
	    "nonfinite");
	frames[0].Anchor.X = 0;
	nodes[0].Hidden = true;
	const sgre::scene::PreparedMotionGeometry hidden(nodes, frames, links, cache);
	RequireFailure([&]() {
		hidden.EvaluateVertices(0, {}, positions);
	},
	    "hidden");
}

int main() {
	try {
		TestOriginalCompleteGeometry();
		TestGeometryGuardsAndQuad();
		std::cout << "Complete native combined/inherited geometry, reference windows, boundary offsets, and guards passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
