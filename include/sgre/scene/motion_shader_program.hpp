#pragma once

#include <array>
#include <cstdint>

namespace sgre::scene {

	enum class MotionVertexLayout : std::uint32_t {
		Xy = 0,
		XyColor = 1,
		XyUv = 2,
		XyTwoUv = 3,
		XyColorUv = 4,
		XyColorTwoUv = 5,
		Xyz = 6,
		XyzColor = 7,
		XyzUv = 8,
		XyzTwoUv = 9,
		XyzColorUv = 10,
		XyzColorTwoUv = 11
	};

	enum class MotionShaderTexture {
		None,
		Rgba,
		Alpha,
		Luminance,
		RedGreen
	};

	struct MotionShaderProgramInput {
		MotionVertexLayout Layout = MotionVertexLayout::XyColorUv;
		MotionShaderTexture Texture = MotionShaderTexture::Rgba;
		std::uint32_t TextureAttributes = 0;
		std::uint32_t BlendMode = 0;
		std::uint32_t ColorFilter = 0;
		// Ordinary dispatch is recovered for native pipeline phases 0, 1 and 6.
		std::uint32_t NativeRenderPhase = 0;
		bool HasAuxiliaryTarget = false;
		bool BackbufferCopyPhase = false;
	};

	struct MotionShaderProgram {
		std::uint32_t VertexIndex = 1;
		std::uint32_t PixelIndex = 240;
	};

	struct MotionShaderMatrix {
		std::array<float, 12> Rows{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
	};

	struct MotionShaderTransform {
		bool UseIdentity = false;
		bool UseExplicitMatrix = false;
		MotionShaderMatrix Explicit;
		MotionShaderMatrix Graphics;
		MotionShaderMatrix Current;
	};

	// Native texture format metadata is retained independently of a converted platform texture.
	[[nodiscard]] MotionShaderTexture ClassifyMotionShaderTexture(bool has_texture, std::uint32_t original_dxgi_format);
	// Selects programs only; this does not certify auxiliary-target shader translation or GPU execution.
	[[nodiscard]] MotionShaderProgram SelectOrdinaryMotionShaderProgram(const MotionShaderProgramInput& input);
	[[nodiscard]] std::array<float, 16> PrepareMotionShaderMatrix(const MotionShaderTransform& transform);
	[[nodiscard]] std::array<float, 4> UnpackMotionShaderColor(std::uint32_t packed_argb);
	[[nodiscard]] std::array<float, 2> PrepareMotionTargetReciprocals(std::uint32_t width, std::uint32_t height);

} // namespace sgre::scene
