#include "sgre/scene/motion_draw.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static float NativeRectangleInteger(float number) {
		if (!std::isfinite(number) || number < 0 || number >= 2147483648.0f) {
			throw std::runtime_error("motion sprite rectangle exceeds the native integer range");
		}

		return static_cast<float>(static_cast<std::int32_t>(number));
	}

	static resource::MotionPoint TransformSpritePoint(const MotionLinearTransform& matrix, float x, float y, float translation_x, float translation_y) {
		resource::MotionPoint point;
		point.X = y * matrix.XY + x * matrix.XX + translation_x;
		point.Y = y * matrix.YY + x * matrix.YX + translation_y;

		return point;
	}

	std::array<resource::MotionPoint, 4> EvaluateMotionBlankQuad(const MotionHierarchyState& state, const MotionBlankSourceRectangle& rectangle, float depth_projection) {
		if (!std::isfinite(depth_projection)) {
			throw std::runtime_error("motion sprite depth projection is nonfinite");
		}

		if (rectangle.Width <= 0 || rectangle.Height <= 0) {
			throw std::runtime_error("motion blank geometry requires its actual positive signed rectangle extent");
		}
		const auto width = static_cast<float>(rectangle.Width);
		const auto height = static_cast<float>(rectangle.Height);
		const auto& sample = state.Attributes;
		const auto& matrix = state.Transform;
		const auto origin_x = rectangle.OriginX + sample.OriginX;
		const auto origin_y = rectangle.OriginY + sample.OriginY;
		const auto translation_x = sample.Coordinate[0] - (origin_x * matrix.XX + origin_y * matrix.XY);
		const auto translation_y = depth_projection * sample.Coordinate[2] + sample.Coordinate[1] - (origin_x * matrix.YX + origin_y * matrix.YY);
		const std::array<resource::MotionPoint, 4> local = {{{0, 0}, {width, 0}, {0, height}, {width, height}}};
		std::array<resource::MotionPoint, 4> corners;
		for (std::size_t index = 0; index < corners.size(); ++index) {
			corners[index] = TransformSpritePoint(matrix, local[index].X, local[index].Y, translation_x, translation_y);
			if (!std::isfinite(corners[index].X) || !std::isfinite(corners[index].Y)) {
				throw std::runtime_error("motion sprite transformed position is nonfinite");
			}
		}

		return corners;
	}

	std::array<resource::MotionPoint, 4> EvaluateMotionSpriteQuad(const MotionHierarchyState& state, const resource::MotionIcon& icon, float depth_projection) {
		if (!std::isfinite(depth_projection)) {
			throw std::runtime_error("motion sprite depth projection is nonfinite");
		}

		const auto width = NativeRectangleInteger(icon.Width);
		const auto height = NativeRectangleInteger(icon.Height);
		const auto& sample = state.Attributes;
		const auto& matrix = state.Transform;
		const auto origin_x = icon.OriginX + sample.OriginX;
		const auto origin_y = icon.OriginY + sample.OriginY;
		const auto translation_x = sample.Coordinate[0] - (origin_x * matrix.XX + origin_y * matrix.XY);
		const auto translation_y = depth_projection * sample.Coordinate[2] + sample.Coordinate[1] - (origin_x * matrix.YX + origin_y * matrix.YY);
		const std::array<resource::MotionPoint, 4> local = {{{0, 0}, {width, 0}, {0, height}, {width, height}}};
		std::array<resource::MotionPoint, 4> corners;
		for (std::size_t index = 0; index < corners.size(); ++index) {
			corners[index] = TransformSpritePoint(matrix, local[index].X, local[index].Y, translation_x, translation_y);
			if (!std::isfinite(corners[index].X) || !std::isfinite(corners[index].Y)) {
				throw std::runtime_error("motion sprite transformed position is nonfinite");
			}
		}

		return corners;
	}

	static std::optional<MotionSpriteMaterial> EvaluateMaterial(const MotionHierarchyState& state, std::string_view source_name, std::string_view icon_name, resource::PixelFormat format, std::uint32_t attribute_flags) {
		const auto& sample = state.Attributes;
		if (sample.Hidden || (state.ModulationColor & 0xff) == 0) {
			return std::nullopt;
		}

		if (sample.Opacity > 255) {
			throw std::runtime_error("motion sprite material opacity is invalid");
		}

		auto modulation = state.ModulationColor;
		const auto blend_operation = sample.BlendMode & 0xff0f;
		if (blend_operation == 2 || blend_operation == 3 || blend_operation == 5) {
			modulation = (modulation & 0xff) | 0x80808000;
		}

		MotionSpriteMaterial material;
		material.Source = source_name;
		material.Icon = icon_name;
		material.TextureFormat = format;
		material.TextureAttributes = attribute_flags;
		material.BlendMode = sample.BlendMode;
		const auto check_alpha = !sample.UniformColor || modulation != 0x808080ff || sample.Opacity != 255;
		if (blend_operation != 0) {
			material.TextureAttributes &= ~std::uint32_t{2};
		}

		for (std::size_t index = 0; index < material.Colors.size(); ++index) {
			const auto packed = sample.UniformColor ? sample.Colors[0] : sample.Colors[index];
			const auto alpha = (packed & 0xff) * sample.Opacity / 255;
			const auto color = (packed & 0xffffff00) | alpha;
			material.Colors[index] = ComposeMotionColor(color, modulation);
			if (check_alpha && (material.Colors[index] & 0xff) != 255) {
				material.TextureAttributes &= ~std::uint32_t{2};
			}
		}

		if (sample.UniformColor && sample.UsesDefaultColor && modulation == 0x808080ff && sample.Opacity == 255) {
			material.TextureAttributes |= 4;
		}

		return material;
	}

	static std::optional<MotionSpriteMaterial> EvaluateBoundMaterial(const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon) {
		return EvaluateMaterial(state, source.Name, icon.Name, source.Texture.Format, icon.AttributeFlags);
	}

	std::optional<MotionSpriteMaterial> EvaluateMotionExternalMaterial(const MotionHierarchyState& state) {
		// Native50a350 external descriptors start with zero source attributes, empty names and resolution one.
		// This RGBA channel selector is material-only; the actual external DXGI format remains on its descriptor.
		return EvaluateMaterial(state, {}, {}, resource::PixelFormat::Bc7, 0);
	}

	std::optional<MotionSpriteMaterial> EvaluateMotionSpriteMaterial(const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon) {
		if (!state.Attributes.Hidden && (state.ModulationColor & 0xff) != 0 && (state.Attributes.Source != source.Name || state.Attributes.Icon != icon.Name)) {
			throw std::runtime_error("motion sprite material identity is invalid");
		}

		return EvaluateBoundMaterial(state, source, icon);
	}

	std::optional<MotionSpriteMaterial> EvaluateMotionSpriteMaterial(const MotionHierarchyState& state, const ResolvedMotionIcon& binding) {
		if (binding.Source == nullptr || binding.Icon == nullptr || binding.Project.DocumentInstance == 0) {
			throw std::runtime_error("motion sprite material requires a resolved source binding");
		}

		return EvaluateBoundMaterial(state, *binding.Source, *binding.Icon);
	}

	static std::optional<MotionSpriteCommand> BuildBoundSpriteCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, float depth_projection, bool inherited_deformation) {
		const auto& sample = state.Attributes;
		if (sample.Hidden || (state.ModulationColor & 0xff) == 0) {
			return std::nullopt;
		}

		if (layer.Type != 0 || layer.MeshTransform > 1 || inherited_deformation || sample.Deformation) {
			throw std::runtime_error("motion sprite command requires an undeformed type-0 layer");
		}

		// The original decoder ignores the mesh-content bit when meshTransform is zero.
		const auto ignored_mask = layer.MeshTransform == 0 ? std::uint32_t{0x40000} : 0;
		if ((sample.UnresolvedMask & ~ignored_mask) != 0) {
			throw std::runtime_error("motion sprite command has unresolved frame attributes");
		}

		if (!std::isfinite(depth_projection) || !std::isfinite(icon.Resolution) || icon.Resolution <= 0 || source.Texture.Width == 0 || source.Texture.Height == 0 || sample.Opacity > 255) {
			throw std::runtime_error("motion sprite projection, resolution, texture dimensions, or opacity is invalid");
		}

		const auto width = NativeRectangleInteger(icon.Width);
		const auto height = NativeRectangleInteger(icon.Height);
		const auto left = NativeRectangleInteger(icon.Left);
		const auto top = NativeRectangleInteger(icon.Top);
		const auto sampled_width = width * icon.Resolution;
		const auto sampled_height = height * icon.Resolution;
		if (left > source.Texture.Width || top > source.Texture.Height || sampled_width > source.Texture.Width - left || sampled_height > source.Texture.Height - top) {
			throw std::runtime_error("motion sprite sampled rectangle exceeds its atlas");
		}

		const auto positions = EvaluateMotionSpriteQuad(state, icon, depth_projection);
		const auto inverse_width = 1.0f / static_cast<float>(source.Texture.Width);
		const auto inverse_height = 1.0f / static_cast<float>(source.Texture.Height);
		const auto u = left * inverse_width;
		const auto v = top * inverse_height;
		const auto u_end = u + sampled_width * inverse_width;
		const auto v_end = v + sampled_height * inverse_height;
		const std::array<resource::MotionPoint, 4> uv = {{{u, v}, {u_end, v}, {u, v_end}, {u_end, v_end}}};
		const auto material = EvaluateBoundMaterial(state, source, icon);
		if (!material) {
			return std::nullopt;
		}

		MotionSpriteCommand command;
		command.Source = material->Source;
		command.Icon = material->Icon;
		command.TextureFormat = material->TextureFormat;
		command.TextureAttributes = material->TextureAttributes;
		command.BlendMode = material->BlendMode;
		command.Depth = sample.Coordinate[2];
		for (std::size_t index = 0; index < command.Vertices.size(); ++index) {
			command.Vertices[index].Position = positions[index];

			command.Vertices[index].Uv = uv[index];
			command.Vertices[index].Color = material->Colors[index];
		}

		return command;
	}

	std::optional<MotionSpriteCommand> BuildMotionSpriteCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, float depth_projection, bool inherited_deformation) {
		if (!state.Attributes.Hidden && (state.ModulationColor & 0xff) != 0 && (state.Attributes.Source != source.Name || state.Attributes.Icon != icon.Name)) {
			throw std::runtime_error("motion sprite source or icon differs from the evaluated identity");
		}

		return BuildBoundSpriteCommand(layer, state, source, icon, depth_projection, inherited_deformation);
	}

	std::optional<MotionSpriteCommand> BuildMotionSpriteCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const ResolvedMotionIcon& binding, float depth_projection, bool inherited_deformation) {
		if (binding.Source == nullptr || binding.Icon == nullptr || binding.Project.DocumentInstance == 0) {
			throw std::runtime_error("motion sprite command requires a resolved source binding");
		}

		return BuildBoundSpriteCommand(layer, state, *binding.Source, *binding.Icon, depth_projection, inherited_deformation);
	}

} // namespace sgre::scene
