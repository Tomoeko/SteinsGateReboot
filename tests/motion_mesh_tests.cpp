#include "sgre/resource/motion_mesh.hpp"

#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}
static void RequireFailure(const std::function<void()>& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("invalid motion mesh was accepted");
}
template <typename Value>
static sgre::resource::PsbValue MakeValue(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}
static void AppendWord(std::vector<std::uint8_t>& bytes, std::uint32_t word) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		bytes.push_back(static_cast<std::uint8_t>(word >> shift));
	}
}
static void TestIconMeshBytes() {
	std::vector<std::uint8_t> bytes = {0};
	const std::array<float, 8> coordinates = {0, 0, 1, 0, 0, 1, 1, 1};
	for (const auto number : coordinates) {
		AppendWord(bytes, std::bit_cast<std::uint32_t>(number));
	}
	for (const auto index : {0u, 2u, 1u, 3u}) {
		AppendWord(bytes, index);
	}
	for (const auto coefficient : {2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f}) {
		AppendWord(bytes, std::bit_cast<std::uint32_t>(coefficient));
	}
	sgre::resource::PsbDocument document;
	sgre::resource::PsbChunkRange range;
	range.Offset = 1;
	range.Size = 32;
	document.ExtraChunks.push_back(range);
	range.Offset = 33;
	range.Size = 16;
	document.Chunks.push_back(range);
	range.Offset = 49;
	range.Size = 24;
	document.Chunks.push_back(range);
	sgre::resource::PsbChunkReference reference;
	reference.Extra = true;
	auto vertex_value = MakeValue(reference);
	reference.Extra = false;
	auto strip_value = MakeValue(reference);
	reference.Index = 1;
	const auto mesh = MakeValue(sgre::resource::PsbObject{{"vertices", vertex_value}, {"tristrip", strip_value}, {"meshMatrix", MakeValue(reference)}});
	const auto result = sgre::resource::ReadMotionIconMesh(document, mesh, bytes);
	Require(result.Vertices.size() == 4 && result.Vertices[3].X == 1 && result.Vertices[3].Y == 1, "mesh float vertex stride or extra-chunk selection changed");
	Require(result.StripIndices == std::vector<std::uint32_t>({0, 2, 1, 3}), "native uint32 strip indices changed");
	Require(result.Matrix.XX == 2 && result.Matrix.XY == 4 && result.Matrix.TranslationX == 6 && result.Matrix.YX == 3 && result.Matrix.YY == 5 && result.Matrix.TranslationY == 7, "serialized column-to-row matrix mapping changed");
	bytes[33] = 4;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionIconMesh(document, mesh, bytes));
	});
	bytes[33] = 0;
	document.ExtraChunks[0].Size = 31;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionIconMesh(document, mesh, bytes));
	});
	document.ExtraChunks[0].Size = 32;
	bytes.pop_back();
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionIconMesh(document, mesh, bytes));
	});
}
static void TestDeformationMetadata() {
	sgre::resource::PsbArray points;
	for (std::size_t index = 0; index < 32; ++index) {
		points.push_back(MakeValue(static_cast<double>(index)));
	}
	auto mesh = MakeValue(sgre::resource::PsbObject{{"bp", MakeValue(points)}, {"cc", MakeValue(1.99999999)}});
	sgre::resource::MotionResource resource;
	resource.Easing.resize(2);
	const auto deformation = sgre::resource::ReadMotionDeformation(mesh, resource);
	Require(deformation.Patch->Controls[15].X == 30 && deformation.Patch->Controls[15].Y == 31 && deformation.EasingIndex == 1, "mesh control pairs or native integer easing conversion changed");
	auto& object = std::get<sgre::resource::PsbObject>(mesh.Data);
	object[0].second.Data = sgre::resource::PsbArray(points.begin(), points.end() - 1);
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionDeformation(mesh, resource));
	});
	object[0].second.Data = std::monostate{};
	object[1].second.Data = std::monostate{};
	const auto absent = sgre::resource::ReadMotionDeformation(mesh, resource);
	Require(!absent.Patch && !absent.EasingIndex, "absent mesh patch acquired fabricated controls");
	object[1].second.Data = -1.0;
	RequireFailure([&]() {
		static_cast<void>(sgre::resource::ReadMotionDeformation(mesh, resource));
	});
}
int main() {
	try {
		TestIconMeshBytes();
		TestDeformationMetadata();
		std::cout << "Native mesh vertex/index/matrix fields, bicubic metadata, and parser safety checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
