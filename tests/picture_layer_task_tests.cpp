#include "sgre/scene/picture_layer_task.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

static void Require(bool value, const char* message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

static std::shared_ptr<sgre::scene::LoadedImageAsset> Model() {
	auto model = std::make_shared<sgre::scene::LoadedImageAsset>();
	model->Model.Width = 16;
	model->Model.Height = 8;
	model->TileBounds = {{{0, 0, 8, 8}, 0}, {{8, 0, 16, 8}, 1}};

	return model;
}

static void TestNativeDefaultsAndGeometry() {
	const auto model = Model();
	sgre::scene::PictureLayerTask task(model);
	const auto initial = task.State();
	Require(initial.Width == 16 && initial.Height == 8 && initial.Opacity == 255 && initial.BlendMode == 0 && initial.MeshSize == 16 && initial.Minification == 0 && task.Regions().size() == 2, "Picture constructor defaults and source-order regions differ");
	task.SetCoordinate({3, -2});
	Require(task.Contains({3, -2}) && !task.Contains({19, -2}) && !task.Contains({3, 6}), "Picture translated half-open region containment differs");
	task.ClearImageRanges();
	task.AppendImageRange({4, 0, 12, 4}, {2, 3});
	Require(task.State().Width == 10 && task.State().Height == 7 && task.Regions().size() == 2 && task.Regions()[0].Destination == std::array<float, 4>{2, 3, 6, 7}, "Picture crop appends actual source-local geometry and destination extents");
	task.SetLeft(-4);
	task.SetTop(5);
	task.SetOpacity(0xffff0100u);
	task.SetBlendMode(0xfedcba98u);
	task.SetMeshSize(std::bit_cast<float>(0x7fc00042u));
	task.SetFiltering(-256, 258);
	const auto changed = task.State();
	Require(changed.Coordinate.X == -4 && changed.Coordinate.Y == 5 && changed.Opacity == 0xffff0100u && changed.BlendMode == 0xfedcba98u && std::bit_cast<std::uint32_t>(changed.MeshSize) == 0x7fc00042u && changed.Minification == -256 && changed.Magnification == 258, "Picture scalar controls preserve full raw fields");
}

static void TestWeakModelAndDeferredClose() {
	auto model = Model();
	const auto weak = std::weak_ptr<sgre::scene::LoadedImageAsset>(model);
	sgre::scene::PictureLayerTask task(model);
	model.reset();
	Require(weak.expired() && task.State().Width == 16, "native Picture task must not strongly retain the Image model");
	bool expired = false;
	try {
		task.AppendImageRange({0, 0, 1, 1});
	} catch (const std::runtime_error&) {
		expired = true;
	}
	Require(expired && task.State().Width == 16, "expired Image crop rejects without reading its borrowed tiles or mutating regions");
	task.Close();
	Require(task.State().Closed && task.State().Width == 0 && task.State().Height == 0, "deferred task close clears owned geometry without dereferencing Image");
	task.Close();
}

class ProjectionReader {
public:
	explicit ProjectionReader(const char* path) : m_Input(path, std::ios::binary) {
		Require(static_cast<bool>(m_Input), "Picture task projection cannot be opened");
	}

	std::uint32_t Word() {
		std::array<char, 4> bytes{};
		m_Input.read(bytes.data(), bytes.size());
		Require(static_cast<bool>(m_Input), "Picture task projection is truncated");

		return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24;
	}

	std::vector<std::uint32_t> Values() {
		const auto count = Word();
		Require(count <= 100, "Picture task projection record exceeds bounded region words");
		std::vector<std::uint32_t> values;
		for (std::uint32_t index = 0; index < count; ++index) {
			values.push_back(Word());
		}

		return values;
	}

	void Finish() {
		Require(m_Input.peek() == std::char_traits<char>::eof(), "Picture task projection has trailing bytes");
	}

private:
	std::ifstream m_Input;
};

static std::vector<std::uint32_t> Snapshot(const sgre::scene::PictureLayerTask& task, bool detached) {
	const auto state = task.State();
	std::vector<std::uint32_t> words{0, state.Width, state.Height, std::bit_cast<std::uint32_t>(state.Coordinate.X), std::bit_cast<std::uint32_t>(state.Coordinate.Y), std::bit_cast<std::uint32_t>(state.Minification), std::bit_cast<std::uint32_t>(state.Magnification), state.Opacity, state.BlendMode, std::bit_cast<std::uint32_t>(state.MeshSize), 0, static_cast<std::uint32_t>(detached), static_cast<std::uint32_t>(task.Regions().size())};
	for (const auto& region : task.Regions()) {
		for (const auto value : region.Destination) {
			words.push_back(std::bit_cast<std::uint32_t>(value));
		}
		for (const auto value : region.SourceBounds) {
			words.push_back(std::bit_cast<std::uint32_t>(value));
		}
		words.push_back(region.Source);
	}

	return words;
}

static void TestNativeProjection(const char* path) {
	ProjectionReader input(path);
	Require(input.Word() == 1000, "Picture task projection case count differs");
	for (std::size_t case_id = 0; case_id < 1000; ++case_id) {
		auto model = std::make_shared<sgre::scene::LoadedImageAsset>();
		model->Model.Width = input.Word();
		model->Model.Height = input.Word();
		const auto tile_count = input.Word();
		const auto rectangle_constructor = input.Word();
		Require(tile_count <= 4 && model->Model.Width <= 4096 && model->Model.Height <= 4096, "Picture native constructor exceeds prepared bounds");
		std::array<float, 4> rectangle;
		for (auto& value : rectangle) {
			value = std::bit_cast<float>(input.Word());
		}
		for (std::uint32_t index = 0; index < tile_count; ++index) {
			sgre::scene::PictureTileBounds tile;
			for (auto& value : tile.Bounds) {
				value = std::bit_cast<float>(input.Word());
			}
			tile.Source = index;
			model->TileBounds.push_back(tile);
		}

		const auto initial = input.Values();
		std::array<std::vector<std::uint32_t>, 10> expected;
		for (auto& values : expected) {
			values = input.Values();
		}
		sgre::scene::PictureLayerTask task(model, rectangle_constructor ? std::optional(rectangle) : std::nullopt);
		Require(Snapshot(task, false) == initial, "Picture constructor native words differ");
		bool detached = false;
		for (std::uint32_t operation = 0; operation < 10; ++operation) {
			Require(input.Word() == operation, "Picture task native operation order differs");
			const auto x = std::bit_cast<float>(input.Word());
			const auto y = std::bit_cast<float>(input.Word());
			const auto value = input.Word();
			switch (operation) {
			case 0:
				task.SetCoordinate({x, y});
				break;
			case 1:
				task.SetLeft(x);
				break;
			case 2:
				task.SetTop(x);
				break;
			case 3:
				task.SetOpacity(value);
				break;
			case 4:
				task.SetBlendMode(value);
				break;
			case 5:
				task.SetMeshSize(std::bit_cast<float>(value));
				break;
			case 6:
				task.SetFiltering(static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value));
				break;
			case 8:
				detached = true;
				break;
			default:
				break;
			}

			auto actual = Snapshot(task, detached);
			const auto state = task.State();
			const std::array<std::uint32_t, 6> getters{state.Width, state.Height, std::bit_cast<std::uint32_t>(state.Coordinate.X), std::bit_cast<std::uint32_t>(state.Coordinate.Y), state.Opacity, state.BlendMode};
			for (const auto word : getters) {
				actual.push_back(detached ? 0 : word);
			}
			const auto query_start = expected[operation].size() - 7;
			for (std::size_t index = 0; index < 6; ++index) {
				actual.push_back(expected[operation][query_start + index]);
			}
			std::array<float, 2> point;
			for (std::size_t axis = 0; axis < 2; ++axis) {
				point[axis] = std::bit_cast<float>(actual[query_start + 4 + axis]) + std::bit_cast<float>(actual[query_start + axis]);
				const auto scale = std::bit_cast<float>(actual[query_start + 2 + axis]);
				if (scale != 0) {
					point[axis] /= scale;
				}
			}
			actual.push_back(static_cast<std::uint32_t>(!detached && task.Contains(point)));
			if (actual != expected[operation]) {
				std::cerr << "private Picture projection case " << case_id << " operation " << operation << '\n';
				for (std::size_t index = 0; index < actual.size(); ++index) {
					if (actual[index] != expected[operation][index]) {
						std::cerr << "word " << index << ": " << actual[index] << " versus " << expected[operation][index] << '\n';
					}
				}
			}
			Require(actual == expected[operation], "Picture native scalar/getter/contains projection differs");
		}
	}
	input.Finish();
}

int main(int argc, char** argv) {
	try {
		TestNativeDefaultsAndGeometry();
		TestWeakModelAndDeferredClose();
		if (argc == 2) {
			TestNativeProjection(argv[1]);
		}
		std::cout << "Picture task tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
