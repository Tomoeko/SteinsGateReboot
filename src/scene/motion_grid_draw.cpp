#include "sgre/scene/motion_grid_draw.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static float RectangleInteger(float number) {
		if (!std::isfinite(number) || number < 0 || number >= 2147483648.0f) {
			throw std::runtime_error("motion grid rectangle exceeds native integer coordinates");
		}

		return static_cast<float>(static_cast<std::int32_t>(number));
	}

	std::size_t MotionMeshGridIndexCount(const MotionMeshSubdivision& subdivision) {
		static_cast<void>(MotionMeshGridVertexCount(subdivision));
		if (subdivision.X == 1 && subdivision.Y == 1) {
			return 4;
		}

		return static_cast<std::size_t>(subdivision.X) * subdivision.Y * 6;
	}

	resource::MotionMeshMatrix MotionBlankDeformationSpace(const MotionHierarchyState& state, const MotionBlankSourceRectangle& rectangle, float depth_projection) {
		if (!std::isfinite(depth_projection)) {
			throw std::runtime_error("motion grid depth projection is nonfinite");
		}

		const auto& sample = state.Attributes;
		const auto& matrix = state.Transform;
		if (rectangle.Width <= 0 || rectangle.Height <= 0) {
			throw std::runtime_error("motion blank deformation requires its actual positive signed rectangle extent");
		}
		const auto width = static_cast<float>(rectangle.Width);
		const auto height = static_cast<float>(rectangle.Height);
		const auto origin_x = rectangle.OriginX + sample.OriginX;
		const auto origin_y = rectangle.OriginY + sample.OriginY;
		const auto translation_x = sample.Coordinate[0] - (origin_x * matrix.XX + origin_y * matrix.XY);
		const auto translation_y = depth_projection * sample.Coordinate[2] + sample.Coordinate[1] - (origin_x * matrix.YX + origin_y * matrix.YY);
		// Original 0x140520d80 composes the rectangle matrix explicitly, retaining zero products.
		const auto horizontal_x = width * matrix.XX + height * matrix.XY * 0.0f;
		const auto horizontal_y = width * matrix.YX + height * matrix.YY * 0.0f;
		const auto vertical_x = height * matrix.XY + matrix.XX * 0.0f;
		const auto vertical_y = height * matrix.YY + matrix.YX * 0.0f;
		const auto zero_x = height * matrix.XY * 0.0f + matrix.XX * 0.0f + 0.0f;
		const auto zero_y = height * matrix.YY * 0.0f + matrix.YX * 0.0f + 0.0f;
		resource::MotionMeshMatrix space;
		space.XX = horizontal_y * 0.0f + horizontal_x;
		space.XY = vertical_y * 0.0f + vertical_x;
		space.TranslationX = zero_y * 0.0f + zero_x + translation_x;
		space.YX = horizontal_x * 0.0f + horizontal_y;
		space.YY = vertical_x * 0.0f + vertical_y;
		space.TranslationY = zero_x * 0.0f + zero_y + translation_y;

		return space;
	}

	resource::MotionMeshMatrix MotionSpriteDeformationSpace(const MotionHierarchyState& state, const resource::MotionIcon& icon, float depth_projection) {
		if (!std::isfinite(depth_projection)) {
			throw std::runtime_error("motion grid depth projection is nonfinite");
		}

		const auto& sample = state.Attributes;
		const auto& matrix = state.Transform;
		const auto width = RectangleInteger(icon.Width);
		const auto height = RectangleInteger(icon.Height);
		const auto origin_x = icon.OriginX + sample.OriginX;
		const auto origin_y = icon.OriginY + sample.OriginY;
		const auto translation_x = sample.Coordinate[0] - (origin_x * matrix.XX + origin_y * matrix.XY);
		const auto translation_y = depth_projection * sample.Coordinate[2] + sample.Coordinate[1] - (origin_x * matrix.YX + origin_y * matrix.YY);
		// Original 0x140520d80 composes the rectangle matrix explicitly, retaining zero products.
		const auto horizontal_x = width * matrix.XX + height * matrix.XY * 0.0f;
		const auto horizontal_y = width * matrix.YX + height * matrix.YY * 0.0f;
		const auto vertical_x = height * matrix.XY + matrix.XX * 0.0f;
		const auto vertical_y = height * matrix.YY + matrix.YX * 0.0f;
		const auto zero_x = height * matrix.XY * 0.0f + matrix.XX * 0.0f + 0.0f;
		const auto zero_y = height * matrix.YY * 0.0f + matrix.YX * 0.0f + 0.0f;
		resource::MotionMeshMatrix space;
		space.XX = horizontal_y * 0.0f + horizontal_x;
		space.XY = vertical_y * 0.0f + vertical_x;
		space.TranslationX = zero_y * 0.0f + zero_x + translation_x;
		space.YX = horizontal_x * 0.0f + horizontal_y;
		space.YY = vertical_x * 0.0f + vertical_y;
		space.TranslationY = zero_x * 0.0f + zero_y + translation_y;

		return space;
	}

	std::uint32_t InterpolateMotionGridColor(std::uint32_t first, std::uint32_t last, std::uint32_t index, std::uint32_t subdivision) {
		if (subdivision == 0 || subdivision > 4096 || index > subdivision) {
			throw std::runtime_error("motion grid color exceeds its bounded subdivision");
		}

		if (first == last || index == 0) {
			return first;
		}

		if (index == subdivision) {
			return last;
		}

		const auto factor = index * 256 / subdivision;
		std::uint32_t color = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			const auto start = (first >> shift) & 0xff;
			const auto end = (last >> shift) & 0xff;
			color |= ((factor * end + (256 - factor) * start) >> 8) << shift;
		}

		return color;
	}

	static void WriteGridIndices(const MotionMeshSubdivision& subdivision, std::span<std::uint32_t> indices) {
		if (subdivision.X == 1 && subdivision.Y == 1) {
			indices[0] = 0;
			indices[1] = 2;
			indices[2] = 1;
			indices[3] = 3;

			return;
		}

		const auto columns = subdivision.X + 1;
		std::size_t offset = 0;
		for (std::uint32_t row = 0; row < subdivision.Y; ++row) {
			for (std::uint32_t column = 0; column < subdivision.X; ++column) {
				const auto top_left = row * columns + column;
				const auto bottom_left = top_left + columns;
				indices[offset++] = top_left;
				indices[offset++] = bottom_left;
				indices[offset++] = top_left + 1;
				indices[offset++] = top_left + 1;
				indices[offset++] = bottom_left;
				indices[offset++] = bottom_left + 1;
			}
		}
	}

	struct GridTextureRectangle {
		float Left = 0;
		float Top = 0;
		float Width = 0;
		float Height = 0;
	};

	static void ValidateGridBuffers(const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers) {
		const auto vertex_count = MotionMeshGridVertexCount(subdivision);
		if (buffers.Positions.size() != vertex_count || buffers.Vertices.size() != vertex_count || buffers.Indices.size() != MotionMeshGridIndexCount(subdivision)) {
			throw std::runtime_error("motion grid command buffers differ from its prepared geometry counts");
		}
	}

	static GridTextureRectangle ReadGridTextureRectangle(const resource::MotionSource& source, const resource::MotionIcon& icon) {
		if (!std::isfinite(icon.Resolution) || icon.Resolution <= 0 || source.Texture.Width == 0 || source.Texture.Height == 0) {
			throw std::runtime_error("motion grid texture dimensions or resolution are invalid");
		}

		GridTextureRectangle rectangle;
		rectangle.Left = RectangleInteger(icon.Left);
		rectangle.Top = RectangleInteger(icon.Top);
		rectangle.Width = RectangleInteger(icon.Width) * icon.Resolution;
		rectangle.Height = RectangleInteger(icon.Height) * icon.Resolution;
		if (rectangle.Left > source.Texture.Width || rectangle.Top > source.Texture.Height || rectangle.Width > source.Texture.Width - rectangle.Left || rectangle.Height > source.Texture.Height - rectangle.Top) {
			throw std::runtime_error("motion grid sampled rectangle exceeds its atlas");
		}

		return rectangle;
	}

	static MotionGridCommand PackGridVertices(const MotionSpriteMaterial& material, const MotionHierarchyState& state, const resource::MotionSource& source, const GridTextureRectangle& rectangle, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers) {
		if (!std::isfinite(state.Attributes.Coordinate[2])) {
			throw std::runtime_error("motion grid depth is nonfinite");
		}

		for (const auto& position : buffers.Positions) {
			if (!std::isfinite(position.X) || !std::isfinite(position.Y)) {
				throw std::runtime_error("motion grid input geometry is nonfinite");
			}
		}

		const auto columns = static_cast<std::size_t>(subdivision.X) + 1;
		const auto inverse_width = 1.0f / static_cast<float>(source.Texture.Width);
		const auto inverse_height = 1.0f / static_cast<float>(source.Texture.Height);
		const auto strip_shortcut = subdivision.X == 1 && subdivision.Y == 1;
		for (std::uint32_t row = 0; row <= subdivision.Y; ++row) {
			const auto first = InterpolateMotionGridColor(material.Colors[0], material.Colors[2], row, subdivision.Y);
			const auto last = InterpolateMotionGridColor(material.Colors[1], material.Colors[3], row, subdivision.Y);
			const auto v = strip_shortcut ? (rectangle.Top + (row == 0 ? 0.0f : rectangle.Height)) / static_cast<float>(source.Texture.Height) : (static_cast<float>(row) * rectangle.Height / static_cast<float>(subdivision.Y) + rectangle.Top) * inverse_height;
			for (std::uint32_t column = 0; column <= subdivision.X; ++column) {
				const auto index = static_cast<std::size_t>(row) * columns + column;
				const auto u = strip_shortcut ? (rectangle.Left + (column == 0 ? 0.0f : rectangle.Width)) / static_cast<float>(source.Texture.Width) : (static_cast<float>(column) * rectangle.Width / static_cast<float>(subdivision.X) + rectangle.Left) * inverse_width;
				buffers.Vertices[index].Position = buffers.Positions[index];
				buffers.Vertices[index].Uv.X = u;
				buffers.Vertices[index].Uv.Y = v;
				buffers.Vertices[index].Color = InterpolateMotionGridColor(first, last, column, subdivision.X);
			}
		}

		WriteGridIndices(subdivision, buffers.Indices);
		MotionGridCommand command;
		command.Material = material;
		command.Vertices = buffers.Vertices;
		command.Indices = buffers.Indices;
		command.Topology = strip_shortcut ? MotionDrawTopology::TriangleStrip : MotionDrawTopology::TriangleList;
		command.Depth = state.Attributes.Coordinate[2];

		return command;
	}

	static MotionGridCommand PackBoundGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers, const MotionSpriteMaterial& material) {
		const auto ignored_mask = layer.MeshTransform == 0 ? std::uint32_t{0x40000} : 0;
		if (layer.Type != 0 || layer.MeshTransform > 1 || layer.MeshCombinator || (state.Attributes.UnresolvedMask & ~ignored_mask) != 0) {
			throw std::runtime_error("motion grid packing requires resolved type-0 sprite attributes");
		}

		ValidateGridBuffers(subdivision, buffers);
		const auto rectangle = ReadGridTextureRectangle(source, icon);

		return PackGridVertices(material, state, source, rectangle, subdivision, buffers);
	}

	static MotionGridCommand BuildBoundOwnGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers, float depth_projection, bool inherited_deformation, const MotionSpriteMaterial& material) {
		const auto& sample = state.Attributes;
		if (layer.Type != 0 || layer.MeshTransform != 1 || layer.MeshCombinator || inherited_deformation || !sample.Deformation || sample.UnresolvedMask != 0) {
			throw std::runtime_error("motion grid command requires a resolved local type-0 bicubic patch");
		}

		ValidateGridBuffers(subdivision, buffers);
		const auto rectangle = ReadGridTextureRectangle(source, icon);
		const auto space = MotionSpriteDeformationSpace(state, icon, depth_projection);
		const auto world_patch = TransformMotionBezierPatch(*sample.Deformation, space);
		EvaluateMotionBezierGrid(world_patch, subdivision, {}, buffers.Positions);

		return PackGridVertices(material, state, source, rectangle, subdivision, buffers);
	}

	std::optional<MotionGridCommand> PackMotionGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers) {
		const auto material = EvaluateMotionSpriteMaterial(state, source, icon);
		if (!material) {
			return std::nullopt;
		}

		return PackBoundGridCommand(layer, state, source, icon, subdivision, buffers, *material);
	}

	std::optional<MotionGridCommand> PackMotionGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const ResolvedMotionIcon& binding, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers) {
		const auto material = EvaluateMotionSpriteMaterial(state, binding);
		if (!material) {
			return std::nullopt;
		}

		return PackBoundGridCommand(layer, state, *binding.Source, *binding.Icon, subdivision, buffers, *material);
	}

	std::optional<MotionGridCommand> BuildOwnMotionGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers, float depth_projection, bool inherited_deformation) {
		const auto material = EvaluateMotionSpriteMaterial(state, source, icon);
		if (!material) {
			return std::nullopt;
		}

		return BuildBoundOwnGridCommand(layer, state, source, icon, subdivision, buffers, depth_projection, inherited_deformation, *material);
	}

	std::optional<MotionGridCommand> BuildOwnMotionGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const ResolvedMotionIcon& binding, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers, float depth_projection, bool inherited_deformation) {
		const auto material = EvaluateMotionSpriteMaterial(state, binding);
		if (!material) {
			return std::nullopt;
		}

		return BuildBoundOwnGridCommand(layer, state, *binding.Source, *binding.Icon, subdivision, buffers, depth_projection, inherited_deformation, *material);
	}

} // namespace sgre::scene
