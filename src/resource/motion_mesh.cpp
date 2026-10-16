#include "sgre/resource/motion_mesh.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace sgre::resource {

	static constexpr std::size_t kMaxMeshVertices = 1000000;
	static constexpr std::size_t kMaxMeshIndices = 4000000;
	static constexpr double kMaxMeshCoordinate = 1000000000;

	static const PsbValue& MeshField(const PsbValue& mesh, std::string_view name) {
		const auto* value = mesh.Find(name);
		if (!value) {
			throw std::runtime_error("motion mesh field is missing: " + std::string(name));
		}

		return *value;
	}

	static float MeshNumber(const PsbValue& value) {
		const auto* real = std::get_if<double>(&value.Data);
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		const auto number = real ? *real : integer ? static_cast<double>(*integer)
		                                           : std::numeric_limits<double>::quiet_NaN();
		if (!std::isfinite(number) || std::abs(number) > kMaxMeshCoordinate) {
			throw std::runtime_error("motion mesh coordinate is invalid or exceeds its limit");
		}

		return static_cast<float>(number);
	}

	MotionDeformation ReadMotionDeformation(const PsbValue& mesh, const MotionResource& resource) {
		if (!std::holds_alternative<PsbObject>(mesh.Data)) {
			throw std::runtime_error("motion deformation requires a mesh object");
		}

		MotionDeformation deformation;
		if (const auto* easing = mesh.Find("cc"); easing && !std::holds_alternative<std::monostate>(easing->Data)) {
			const auto* real = std::get_if<double>(&easing->Data);
			const auto* integer = std::get_if<std::int64_t>(&easing->Data);
			const auto number = real ? *real : integer ? static_cast<double>(*integer)
			                                           : std::numeric_limits<double>::quiet_NaN();
			if (!std::isfinite(number) || number < 0 || std::trunc(number) >= static_cast<double>(resource.Easing.size())) {
				throw std::runtime_error("motion mesh easing index exceeds the resource easing table");
			}

			deformation.EasingIndex = static_cast<std::uint32_t>(number);
		}

		if (const auto* points = mesh.Find("bp"); points && !std::holds_alternative<std::monostate>(points->Data)) {
			const auto* values = std::get_if<PsbArray>(&points->Data);
			if (!values || values->size() != 32) {
				throw std::runtime_error("motion bicubic deformation requires exactly 32 control coordinates");
			}

			MotionBezierPatch patch;
			for (std::size_t index = 0; index < patch.Controls.size(); ++index) {
				patch.Controls[index].X = MeshNumber((*values)[index * 2]);
				patch.Controls[index].Y = MeshNumber((*values)[index * 2 + 1]);
			}

			deformation.Patch = patch;
		}

		return deformation;
	}

	static std::span<const std::uint8_t> MeshBytes(const PsbDocument& document, const PsbValue& value, std::span<const std::uint8_t> bytes) {
		const auto* reference = std::get_if<PsbChunkReference>(&value.Data);
		if (!reference) {
			throw std::runtime_error("motion mesh data requires a PSB chunk reference");
		}

		const auto& chunks = reference->Extra ? document.ExtraChunks : document.Chunks;
		if (reference->Index >= chunks.size()) {
			throw std::runtime_error("motion mesh chunk reference exceeds its table");
		}

		const auto& range = chunks[reference->Index];
		if (range.Offset > bytes.size() || range.Size > bytes.size() - range.Offset) {
			throw std::runtime_error("motion mesh chunk exceeds supplied bytes");
		}

		return bytes.subspan(static_cast<std::size_t>(range.Offset), static_cast<std::size_t>(range.Size));
	}

	static std::uint32_t MeshWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return std::uint32_t{bytes[offset]} | std::uint32_t{bytes[offset + 1]} << 8 | std::uint32_t{bytes[offset + 2]} << 16 | std::uint32_t{bytes[offset + 3]} << 24;
	}

	static float MeshFloat(std::span<const std::uint8_t> bytes, std::size_t offset) {
		const auto number = std::bit_cast<float>(MeshWord(bytes, offset));
		if (!std::isfinite(number) || std::abs(number) > kMaxMeshCoordinate) {
			throw std::runtime_error("motion mesh serialized coordinate is nonfinite or exceeds its limit");
		}

		return number;
	}

	MotionIconMesh ReadMotionIconMesh(const PsbDocument& document, const PsbValue& mesh, std::span<const std::uint8_t> bytes) {
		const auto vertices = MeshBytes(document, MeshField(mesh, "vertices"), bytes);
		const auto indices = MeshBytes(document, MeshField(mesh, "tristrip"), bytes);
		const auto matrix = MeshBytes(document, MeshField(mesh, "meshMatrix"), bytes);
		if (vertices.size() % 8 != 0 || vertices.size() / 8 > kMaxMeshVertices || indices.size() % 4 != 0 || indices.size() / 4 > kMaxMeshIndices || matrix.size() != 24) {
			throw std::runtime_error("motion mesh vertex/index/matrix lengths are invalid or exceed their limits");
		}

		MotionIconMesh result;
		result.Vertices.reserve(vertices.size() / 8);
		for (std::size_t offset = 0; offset < vertices.size(); offset += 8) {
			MotionPoint point;
			point.X = MeshFloat(vertices, offset);
			point.Y = MeshFloat(vertices, offset + 4);
			result.Vertices.push_back(point);
		}

		result.StripIndices.reserve(indices.size() / 4);
		for (std::size_t offset = 0; offset < indices.size(); offset += 4) {
			const auto index = MeshWord(indices, offset);
			if (index >= result.Vertices.size()) {
				throw std::runtime_error("motion mesh strip index exceeds its vertex table");
			}

			result.StripIndices.push_back(index);
		}

		result.Matrix.XX = MeshFloat(matrix, 0);
		result.Matrix.XY = MeshFloat(matrix, 8);
		result.Matrix.TranslationX = MeshFloat(matrix, 16);
		result.Matrix.YX = MeshFloat(matrix, 4);
		result.Matrix.YY = MeshFloat(matrix, 12);
		result.Matrix.TranslationY = MeshFloat(matrix, 20);

		return result;
	}

} // namespace sgre::resource
