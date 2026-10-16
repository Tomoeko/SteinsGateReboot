#include "sgre/scene/motion_layer_clip_rectangles.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

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

	throw std::runtime_error("unsupported clip operation was accepted");
}

static sgre::scene::PreparedMotionTimeline Preparation() {
	sgre::scene::PreparedMotionTimeline prepared;
	prepared.Layers.resize(4);
	prepared.Parents = {std::nullopt, 0, 1, 2};
	const std::array<std::uint32_t, 4> types{7, 2, 7, 3};
	for (std::size_t index = 0; index < types.size(); ++index) {
		prepared.Layers[index].Type = types[index];
		prepared.Layers[index].Frames.resize(2);
	}
	prepared.Layers[1].InheritMask = 0x400000;

	return prepared;
}

static std::array<sgre::scene::MotionComposedLayer, 4> Nodes() {
	std::array<sgre::scene::MotionComposedLayer, 4> nodes;
	for (auto& node : nodes) {
		node.Active = true;
		node.Hierarchy.Attributes.Coordinate = {10, 20, 5};
	}

	return nodes;
}

static void TestActualOwnedAndBorrowedClips() {
	const auto prepared = Preparation();
	auto nodes = Nodes();
	const std::array<std::size_t, 4> indices{};
	sgre::scene::MotionLayerClipRectangles clips(prepared);
	Require(clips.Clips().empty() && clips.OwnedRectangle(0).Left == 0 && clips.OwnedRectangle(2).Bottom == 0, "selected clip owner initializes kind7 buffers but publishes no links");
	clips.Compose(nodes, indices, nullptr);
	const auto* first = &clips.OwnedRectangle(0);
	const auto* second = &clips.OwnedRectangle(2);
	Require(first->Left == -6 && first->Top == 9 && first->Right == 26 && first->Bottom == 41, "native clip covers selected diagonal16-unit endpoints with depth added to vertical edges");
	Require(clips.Clips()[0] == first && clips.Clips()[1] == first && clips.Clips()[2] == second && clips.Clips()[3] == second, "active kind7 owns its buffer while ordinary/child nodes inherit exact immediate-parent identities");
	sgre::scene::MotionClipRectangle enclosing;
	enclosing.Left = 0;
	enclosing.Top = 12;
	enclosing.Right = 15;
	enclosing.Bottom = 30;
	clips.Compose(nodes, indices, &enclosing);
	Require(clips.OwnedRectangle(0).Left == 0 && clips.OwnedRectangle(0).Top == 12 && clips.OwnedRectangle(0).Right == 15 && clips.OwnedRectangle(0).Bottom == 30 && &clips.OwnedRectangle(0) == first && &clips.OwnedRectangle(2) == second, "native rectangle intersection updates existing stable owners");
	const auto retained = clips.OwnedRectangle(0);
	nodes[0].Active = false;
	nodes[0].Visible = true;
	clips.Compose(nodes, indices, &enclosing);
	Require(clips.Clips()[0] == &enclosing && clips.Clips()[1] == &enclosing && clips.OwnedRectangle(0).Left == retained.Left && clips.OwnedRectangle(0).Bottom == retained.Bottom, "inactive kind7 borrows parent while retaining its own previous buffer independently of Visible");
	nodes[2].Active = false;
	clips.Compose(nodes, indices, nullptr);
	Require(clips.Clips()[0] == nullptr && clips.Clips()[1] == nullptr && clips.Clips()[2] == nullptr && clips.Clips()[3] == nullptr, "inactive clip ancestry preserves genuine absent identity without a synthetic rectangle");
}

static void TestSelectedOriginsDiagonalAndSseEquality() {
	auto prepared = Preparation();
	prepared.Layers[0].Frames[0].Attributes.OriginX = 2;
	prepared.Layers[0].Frames[0].Attributes.OriginY = 3;
	prepared.Layers[0].Frames[1].Attributes.OriginX = 1;
	prepared.Layers[0].Frames[1].Attributes.OriginY = 1;
	auto nodes = Nodes();
	nodes[0].Hierarchy.Transform.XX = 1;
	nodes[0].Hierarchy.Transform.XY = -1;
	nodes[0].Hierarchy.Transform.YX = 1;
	nodes[0].Hierarchy.Transform.YY = 1;
	std::array<std::size_t, 4> indices{};
	sgre::scene::MotionLayerClipRectangles clips(prepared);
	clips.Compose(nodes, indices, nullptr);
	Require(clips.OwnedRectangle(0).Left == 11 && clips.OwnedRectangle(0).Right == 11 && clips.OwnedRectangle(0).Top == -12 && clips.OwnedRectangle(0).Bottom == 52, "native clip uses two opposite diagonal endpoints rather than substituting four-corner bounds");
	indices[0] = 1;
	clips.Compose(nodes, indices, nullptr);
	Require(clips.OwnedRectangle(0).Left == 10 && clips.OwnedRectangle(0).Top == -9, "clip uses current selected frame origin without stateless interpolation");
	sgre::scene::MotionClipRectangle enclosing;
	enclosing.Left = -0.0f;
	enclosing.Top = -0.0f;
	enclosing.Right = -0.0f;
	enclosing.Bottom = -0.0f;
	nodes[0].Hierarchy.Attributes.Coordinate = {0, 0, 0};
	nodes[0].Hierarchy.Transform.XX = 0;
	nodes[0].Hierarchy.Transform.XY = 0;
	nodes[0].Hierarchy.Transform.YX = 0;
	nodes[0].Hierarchy.Transform.YY = 0;
	prepared.Layers[0].Frames[1].Attributes.OriginX = 0;
	prepared.Layers[0].Frames[1].Attributes.OriginY = 0;
	clips.Compose(nodes, indices, &enclosing);
	Require(std::bit_cast<std::uint32_t>(clips.OwnedRectangle(0).Left) == 0x80000000 && std::bit_cast<std::uint32_t>(clips.OwnedRectangle(0).Right) == 0x80000000, "native intersection retains second operand signed-zero words");
	enclosing.Left = 40;
	enclosing.Right = -40;
	clips.Compose(nodes, indices, &enclosing);
	Require(clips.OwnedRectangle(0).Left == 40 && clips.OwnedRectangle(0).Right == -40, "native empty intersection keeps inverted edges without repair");
}

static void TestBoundedOwnersAndFailurePublication() {
	auto prepared = Preparation();
	auto nodes = Nodes();
	std::array<std::size_t, 4> indices{};
	sgre::scene::MotionLayerClipRectangles clips(prepared);
	clips.Compose(nodes, indices, nullptr);
	indices[2] = 2;
	RequireFailure([&] {
		clips.Compose(nodes, indices, nullptr);
	},
	    "current slot");
	Require(clips.Clips().empty(), "partial failed clip phase invalidates all published links");
	indices[2] = 0;
	nodes[0].Hierarchy.Transform.XX = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		clips.Compose(nodes, indices, nullptr);
	},
	    "finite current origin");
	Require(clips.Clips().empty(), "invalid numeric domains cannot publish clip links");
	RequireFailure([&] {
		clips.Compose(nodes, std::span<const std::size_t>(indices).first(3), nullptr);
	},
	    "every actual current");
	RequireFailure([&] {
		static_cast<void>(clips.OwnedRectangle(1));
	},
	    "kind7 node");
	prepared.Layers[0].StencilType = 1;
	RequireFailure([&] {
		sgre::scene::MotionLayerClipRectangles rejected(prepared);
	},
	    "without mesh/stencil");
	prepared.Layers[0].StencilType = 0;
	prepared.Layers[0].MeshTransform = 1;
	RequireFailure([&] {
		sgre::scene::MotionLayerClipRectangles rejected(prepared);
	},
	    "without mesh/stencil");
	prepared.Layers[0].MeshTransform = 0;
	prepared.Parents[1] = 2;
	RequireFailure([&] {
		sgre::scene::MotionLayerClipRectangles rejected(prepared);
	},
	    "preceding parents");
	prepared = Preparation();
	prepared.Layers[2].Frames.clear();
	RequireFailure([&] {
		sgre::scene::MotionLayerClipRectangles rejected(prepared);
	},
	    "selected frame pair");
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || 4 > bytes.size() - offset) {
		throw std::runtime_error("truncated native clip rectangle word");
	}

	std::uint32_t result = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		result |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return result;
}

static float ReadNumber(std::span<const std::uint8_t> bytes, std::size_t offset) {
	return std::bit_cast<float>(ReadWord(bytes, offset));
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native clip rectangle record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void RequireRectangle(const sgre::scene::MotionClipRectangle& rectangle, std::span<const std::uint8_t> bytes) {
	const std::array<float, 4> values{rectangle.Left, rectangle.Top, rectangle.Right, rectangle.Bottom};
	for (std::size_t index = 0; index < values.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(values[index]) == ReadWord(bytes, index * 4), "clip rectangle float32 word differs from actual native phase");
	}
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native clip fixture");
	const auto header = ReadRecord<16>(input);
	Require(header[0] == 'M' && header[1] == 'C' && header[2] == 'L' && header[3] == 'R' && ReadWord(header, 12) == 4, "invalid native clip signature/layer count");
	const auto cases = ReadWord(header, 4);
	const auto steps = ReadWord(header, 8);
	Require(cases > 0 && cases <= 32 && steps > 0 && steps <= 8, "native clip work exceeds its bound");
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		auto prepared = Preparation();
		for (std::size_t index = 0; index < 4; ++index) {
			const auto property = ReadRecord<5>(input);
			prepared.Layers[index].Type = property[0];
			const auto parent = ReadWord(property, 1);
			prepared.Parents[index] = parent == 0 ? std::nullopt : std::optional<std::uint32_t>(parent - 1);
		}

		sgre::scene::MotionLayerClipRectangles clips(prepared);
		for (std::uint32_t step = 0; step < steps; ++step) {
			try {
				const auto root = ReadRecord<17>(input);
				sgre::scene::MotionClipRectangle root_clip;
				root_clip.Left = ReadNumber(root, 1);
				root_clip.Top = ReadNumber(root, 5);
				root_clip.Right = ReadNumber(root, 9);
				root_clip.Bottom = ReadNumber(root, 13);
				std::array<sgre::scene::MotionComposedLayer, 4> nodes;
				std::array<std::size_t, 4> indices{};
				std::array<std::array<std::uint8_t, 74>, 4> records{};
				for (std::size_t index = 0; index < nodes.size(); ++index) {
					auto& record = records[index];
					record = ReadRecord<74>(input);
					Require(record[0] <= 1 && record[1] <= 1, "native clip node flags exceed their domain");
					indices[index] = record[1];
					auto& node = nodes[index];
					node.Active = record[0] != 0;
					node.Hierarchy.Attributes.Coordinate = {ReadNumber(record, 2), ReadNumber(record, 6), ReadNumber(record, 10)};
					node.Hierarchy.Transform.XX = ReadNumber(record, 14);
					node.Hierarchy.Transform.XY = ReadNumber(record, 18);
					node.Hierarchy.Transform.YX = ReadNumber(record, 22);
					node.Hierarchy.Transform.YY = ReadNumber(record, 26);
					prepared.Layers[index].Frames[indices[index]].Attributes.OriginX = ReadNumber(record, 30);
					prepared.Layers[index].Frames[indices[index]].Attributes.OriginY = ReadNumber(record, 34);
				}

				clips.Compose(nodes, indices, root[0] != 0 ? &root_clip : nullptr);
				Require(clips.Clips().size() == nodes.size(), "native clip phase must publish the actual node span");
				for (std::size_t index = 0; index < nodes.size(); ++index) {
					const auto& record = records[index];
					if (prepared.Layers[index].Type == 7) {
						RequireRectangle(clips.OwnedRectangle(index), std::span<const std::uint8_t>(record).subspan(38, 16));
					}

					const auto ordinal = std::bit_cast<std::int32_t>(ReadWord(record, 54));
					const sgre::scene::MotionClipRectangle* expected = nullptr;
					if (ordinal == 0) {
						expected = &root_clip;
					} else if (ordinal > 0 && ordinal <= 4) {
						expected = &clips.OwnedRectangle(static_cast<std::size_t>(ordinal - 1));
					} else {
						Require(ordinal == -1, "native clip ordinal exceeds its actual owner span");
					}

					Require(clips.Clips()[index] == expected, "native clip identity differs from its actual root/selected-owner/null link");
					if (expected != nullptr) {
						RequireRectangle(*expected, std::span<const std::uint8_t>(record).subspan(58, 16));
					}
				}
			} catch (const std::exception& error) {
				throw std::runtime_error("native clip case " + std::to_string(case_index) + " step " + std::to_string(step) + ": " + error.what());
			}
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native clip fixture contains trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_layer_clip_rectangles_tests [native-fixture]");
		TestActualOwnedAndBorrowedClips();
		TestSelectedOriginsDiagonalAndSseEquality();
		TestBoundedOwnersAndFailurePublication();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion layer clip rectangle tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
