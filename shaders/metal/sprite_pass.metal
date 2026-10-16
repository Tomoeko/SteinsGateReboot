#include <metal_stdlib>

constant uint TextureInterpretation [[function_constant(0)]];
constant uint VertexColorOperation [[function_constant(1)]];
constant uint PixelOperation [[function_constant(2)]];

struct SpriteVertexInput {
	metal::float2 Position [[attribute(0)]];
	metal::float4 Color [[attribute(1)]];
	metal::float2 TextureCoordinates [[attribute(2)]];
};

struct SpriteVertexOutput {
	metal::float4 Position [[position]];
	metal::float4 Color;
	metal::float2 TextureCoordinates;
};

struct SpritePassConstants {
	// Raw cb0 register order, avoiding assumptions about CPU-side matrix type names.
	metal::float4 ProjectionRegisters[4];
};

// VS1 SHA-256 f24652f5: each output component dots (x,y,1) against one register's x/y/w.
vertex SpriteVertexOutput TransformSpriteVertex(SpriteVertexInput input [[stage_in]], constant SpritePassConstants& constants [[buffer(1)]]) {
	const metal::float3 position = metal::float3(input.Position, 1.0f);
	SpriteVertexOutput output;
	output.Position.x = metal::dot(position, constants.ProjectionRegisters[0].xyw);
	output.Position.y = metal::dot(position, constants.ProjectionRegisters[1].xyw);
	output.Position.z = metal::dot(position, constants.ProjectionRegisters[2].xyw);
	output.Position.w = metal::dot(position, constants.ProjectionRegisters[3].xyw);
	output.Color = input.Color;
	output.TextureCoordinates = input.TextureCoordinates;

	return output;
}

struct RawTextureVertexInput {
	metal::float2 Position [[attribute(0)]];
	metal::float2 TextureCoordinates [[attribute(1)]];
};

struct RawTexturePassConstants {
	metal::float4 ProjectionRegisters[4];
	metal::float4 VertexColor;
};

// VS4 SHA-256 5ff91389: XY/UV input, then constVtxColor from cb0 register 4.
vertex SpriteVertexOutput TransformRawTextureVertex(RawTextureVertexInput input [[stage_in]], constant RawTexturePassConstants& constants [[buffer(1)]]) {
	const metal::float3 position = metal::float3(input.Position, 1.0f);
	SpriteVertexOutput output;
	output.Position.x = metal::dot(position, constants.ProjectionRegisters[0].xyw);
	output.Position.y = metal::dot(position, constants.ProjectionRegisters[1].xyw);
	output.Position.z = metal::dot(position, constants.ProjectionRegisters[2].xyw);
	output.Position.w = metal::dot(position, constants.ProjectionRegisters[3].xyw);
	output.Color = constants.VertexColor;
	output.TextureCoordinates = input.TextureCoordinates;

	return output;
}

struct FillRectangleVertexInput {
	metal::float2 Position [[attribute(0)]];
};

// VS3 b01deddf: native format0 XY, uniform color from cb0[4], literal zero UV.
vertex SpriteVertexOutput TransformFillRectangleVertex(FillRectangleVertexInput input [[stage_in]], constant RawTexturePassConstants& constants [[buffer(1)]]) {
	const metal::float3 position = metal::float3(input.Position, 1.0f);
	SpriteVertexOutput output;
	output.Position.x = metal::dot(position, constants.ProjectionRegisters[0].xyw);
	output.Position.y = metal::dot(position, constants.ProjectionRegisters[1].xyw);
	output.Position.z = metal::dot(position, constants.ProjectionRegisters[2].xyw);
	output.Position.w = metal::dot(position, constants.ProjectionRegisters[3].xyw);
	output.Color = constants.VertexColor;
	output.TextureCoordinates = metal::float2(0.0f);

	return output;
}

// PS60 93c2d188 multiplies COLOR by itself, including alpha; it reads no texture.
fragment metal::float4 SquareFillRectangleColorFragment(SpriteVertexOutput input [[stage_in]]) {
	return input.Color * input.Color;
}

struct ScreenProjectionVertexInput {
	metal::float3 Position [[attribute(0)]];
	metal::float2 TextureCoordinates [[attribute(1)]];
};

// VS10 SHA3ec17e4a: XYZ/UV and the real constant color; all four position components are dp4.
vertex SpriteVertexOutput TransformScreenProjectionVertex(ScreenProjectionVertexInput input [[stage_in]], constant RawTexturePassConstants& constants [[buffer(1)]]) {
	const metal::float4 position = metal::float4(input.Position, 1.0f);
	SpriteVertexOutput output;
	output.Position.x = metal::dot(position, constants.ProjectionRegisters[0]);
	output.Position.y = metal::dot(position, constants.ProjectionRegisters[1]);
	output.Position.z = metal::dot(position, constants.ProjectionRegisters[2]);
	output.Position.w = metal::dot(position, constants.ProjectionRegisters[3]);
	output.Color = constants.VertexColor;
	output.TextureCoordinates = input.TextureCoordinates;

	return output;
}

static metal::float4 InterpretTexture(metal::float4 sampled) {
	if (TextureInterpretation == 1) {
		return metal::float4(1.0f, 1.0f, 1.0f, sampled.a);
	}

	if (TextureInterpretation == 2) {
		return metal::float4(sampled.r, sampled.r, sampled.r, 1.0f);
	}

	return sampled;
}

fragment metal::float4 SampleSpriteFragment(SpriteVertexOutput input [[stage_in]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]]) {
	metal::float4 color = InterpretTexture(color_texture.sample(color_sampler, input.TextureCoordinates));
	if (VertexColorOperation != 0) {
		color = input.Color * color;
	}

	if (VertexColorOperation == 2) {
		color = color * metal::float4(2.0f, 2.0f, 2.0f, 1.0f);
	}

	if (PixelOperation == 1) {
		color.rgb = metal::float3(1.0f) - color.rgb;
	} else if (PixelOperation == 2) {
		color.rgb = color.rgb * color.a;
	}

	return color;
}

struct CopiedAdditiveConstants {
	metal::float4 ScreenMulOffset;
};

// PS1205 SHA-256 25b33897: the complete readable HLSL container is certified separately.
fragment metal::float4 SampleCopiedAdditiveFragment(SpriteVertexOutput input [[stage_in]], constant CopiedAdditiveConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	const metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	metal::float3 combined_color = source_color.rgb * input.Color.rgb + destination_color.rgb;
	const float opacity = source_color.a * input.Color.a;
	combined_color = metal::min(combined_color, metal::float3(1.0f));
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1129: source alpha interpolates the clamped sum against the actual copied destination; vertex color is absent.
fragment metal::float4 SampleCopiedAdditiveSkipColorFragment(SpriteVertexOutput input [[stage_in]], constant CopiedAdditiveConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	const metal::float3 combined_color = metal::min(destination_color.rgb + source_color.rgb, metal::float3(1.0f));
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float3 result = source_color.a * difference + destination_color.rgb;

	return metal::float4(result, source_color.a);
}

// Runtime PS1131: the comparison is strict and per destination channel, matching the original DXBC lt/movc order.
fragment metal::float4 SampleCopiedOverlaySkipColorFragment(SpriteVertexOutput input [[stage_in]], constant CopiedAdditiveConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	const metal::float3 inverse_destination = metal::float3(1.0f) - destination_color.rgb;
	const metal::float3 inverse_source = metal::float3(1.0f) - source_color.rgb;
	const metal::float3 upper = metal::float3(1.0f) - (inverse_destination * inverse_source) * 2.0f;
	const metal::float3 product = destination_color.rgb * source_color.rgb;
	const metal::float3 lower = product + product;
	const metal::float3 combined_color = metal::select(upper, lower, destination_color.rgb < metal::float3(0.5f));
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float3 result = source_color.a * difference + destination_color.rgb;

	return metal::float4(result, source_color.a);
}

// Runtime PS1281: native vertex modulation doubles RGB only; copied alpha is not consumed.
fragment metal::float4 SampleCopiedAdditiveDoubleColorFragment(SpriteVertexOutput input [[stage_in]], constant CopiedAdditiveConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	source_color = source_color * input.Color;
	source_color = source_color * metal::float4(2.0f, 2.0f, 2.0f, 1.0f);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 combined_color = metal::min(source_color.rgb + destination_color.rgb, metal::float3(1.0f));
	const metal::float3 difference = combined_color - destination_color.rgb;
	const float opacity = source_color.a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1283: native vertex modulation doubles RGB only; copied alpha is not consumed.
fragment metal::float4 SampleCopiedOverlayDoubleColorFragment(SpriteVertexOutput input [[stage_in]], constant CopiedAdditiveConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	source_color = source_color * input.Color;
	const metal::float3 inverse_source = metal::float3(1.0f) - source_color.rgb * 2.0f;
	source_color = source_color * metal::float4(2.0f, 2.0f, 2.0f, 1.0f);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 inverse_destination = metal::float3(1.0f) - destination_color.rgb;
	const metal::float3 upper = metal::float3(1.0f) - (inverse_source * inverse_destination) * 2.0f;
	const metal::float3 product = source_color.rgb * destination_color.rgb;
	const metal::float3 lower = product + product;
	const metal::float3 combined_color = metal::select(upper, lower, destination_color.rgb < metal::float3(0.5f));
	const metal::float3 difference = combined_color - destination_color.rgb;
	const float opacity = source_color.a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1137: exact copied darken operation9; copied alpha is not consumed.
fragment metal::float4 SampleCopiedDarkenSkipColorFragment(SpriteVertexOutput input [[stage_in]], constant CopiedAdditiveConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	const metal::float3 combined_color = metal::min(destination_color.rgb, source_color.rgb);
	const metal::float3 difference = combined_color - destination_color.rgb;
	const float opacity = source_color.a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1289: exact copied darken operation9; copied alpha is not consumed.
fragment metal::float4 SampleCopiedDarkenDoubleColorFragment(SpriteVertexOutput input [[stage_in]], constant CopiedAdditiveConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	source_color = source_color * input.Color;
	source_color = source_color * metal::float4(2.0f, 2.0f, 2.0f, 1.0f);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 combined_color = metal::min(source_color.rgb, destination_color.rgb);
	const metal::float3 difference = combined_color - destination_color.rgb;
	const float opacity = source_color.a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

#include <metal_stdlib>

// Native kinds: absent/color=0, RGBA=1, A8=2, L8=3, R8G8=4.
constant uint MotionMaskTextureKind [[function_constant(3)]];
constant bool MotionMaskUseAdditional [[function_constant(4)]];
constant uint MotionMaskTexturePhase [[function_constant(5)]];
constant uint MotionMaskResolveColorOperation [[function_constant(6)]];
constant uint MotionMaskResolvePixelOperation [[function_constant(7)]];

struct MotionMaskPixelInput {
	metal::float4 Position [[position]];
	metal::float4 Color;
	metal::float2 TextureCoordinates;
};

struct MotionMaskConstants {
	// Native cb0[0]=alphaMulAdd; cb0[1].xy=screenMul. The first register exists even when unused in phase 4.
	metal::float4 AlphaMulAdd;
	metal::float4 ScreenMul;
};

struct MotionResolvedMaskConstants {
	metal::float4 ScreenMul;
};

struct MotionMaskedCopiedConstants {
	// Native PS1148/1150 cb0[0].xy then cb0[1], not the unmasked one-register layout.
	metal::float4 ScreenMul;
	metal::float4 ScreenMulOffset;
};

static metal::float4 InterpretMotionMaskSource(metal::float4 sampled, metal::float4 vertex_color) {
	if (MotionMaskTextureKind == 0) {
		return vertex_color;
	}
	if (MotionMaskTextureKind == 2) {
		return metal::float4(1.0f, 1.0f, 1.0f, sampled.a);
	}
	if (MotionMaskTextureKind == 3) {
		return metal::float4(sampled.rrr, 1.0f);
	}
	if (MotionMaskTextureKind == 4) {
		return metal::float4(sampled.rrr, sampled.g);
	}

	return sampled;
}

// Runtime PS2042..2071. CPU selection must admit phases 3/4/5 and match the declared function constants.
static metal::float4 PrepareMotionTextureMaskFragment(MotionMaskPixelInput input, constant MotionMaskConstants& constants, metal::texture2d<float> color_texture, metal::sampler color_sampler, metal::texture2d<float> previous_mask, metal::sampler mask_sampler) {
	metal::float4 sampled = metal::float4(1.0f);
	if (MotionMaskTextureKind != 0) {
		sampled = color_texture.sample(color_sampler, input.TextureCoordinates);
	}

	metal::float4 result = InterpretMotionMaskSource(sampled, input.Color);
	if (MotionMaskUseAdditional) {
		result.a = metal::saturate(result.a * constants.AlphaMulAdd.x + constants.AlphaMulAdd.y);
	}
	if (MotionMaskTexturePhase == 4) {
		const metal::float2 coordinates = input.Position.xy * constants.ScreenMul.xy;
		result.a = previous_mask.sample(mask_sampler, coordinates).a * result.a;
	}

	return result;
}

// Logical native A8 output is physically R8. Source alpha remains in .a for the recovered blend factor.
fragment metal::float4 SampleMotionTextureMaskRedFragment(MotionMaskPixelInput input [[stage_in]], constant MotionMaskConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> previous_mask [[texture(1)]], metal::sampler mask_sampler [[sampler(1)]]) {
	const metal::float4 native_color = PrepareMotionTextureMaskFragment(input, constants, color_texture, color_sampler, previous_mask, mask_sampler);

	return metal::float4(native_color.a, 0.0f, 0.0f, native_color.a);
}

// Phase 6 ordinary family, no color filter. Premultiplication must include the resolved mask alpha.
fragment metal::float4 SampleMotionResolvedMaskFragment(MotionMaskPixelInput input [[stage_in]], constant MotionResolvedMaskConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> resolved_mask [[texture(1)]], metal::sampler mask_sampler [[sampler(1)]]) {
	metal::float4 sampled = metal::float4(1.0f);
	if (MotionMaskTextureKind != 0) {
		sampled = color_texture.sample(color_sampler, input.TextureCoordinates);
	}

	metal::float4 color = InterpretMotionMaskSource(sampled, input.Color);
	if (MotionMaskResolveColorOperation != 0) {
		color *= input.Color;
	}
	if (MotionMaskResolveColorOperation == 2) {
		color.rgb *= 2.0f;
	}
	if (MotionMaskResolvePixelOperation == 1) {
		color.rgb = metal::float3(1.0f) - color.rgb;
	}

	const metal::float2 mask_coordinates = input.Position.xy * constants.ScreenMul.xy;
	color.a = resolved_mask.sample(mask_sampler, mask_coordinates).a * color.a;
	if (MotionMaskResolvePixelOperation == 2) {
		color.rgb *= color.a;
	}

	return color;
}

// Runtime PS1148, original occurrence1782. The mask modulates interpolation opacity, not combined RGB.
fragment metal::float4 SampleCopiedAdditiveMaskedSkipColorFragment(MotionMaskPixelInput input [[stage_in]], constant MotionMaskedCopiedConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> mask_texture [[texture(1)]], metal::sampler mask_sampler [[sampler(1)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	const metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 combined_color = metal::min(source_color.rgb + destination_color.rgb, metal::float3(1.0f));
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float2 mask_coordinates = input.Position.xy * constants.ScreenMul.xy;
	const float opacity = source_color.a * mask_texture.sample(mask_sampler, mask_coordinates).a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1150, original occurrence1784. Preserve the strict destination<0.5 per-channel branch.
fragment metal::float4 SampleCopiedOverlayMaskedSkipColorFragment(MotionMaskPixelInput input [[stage_in]], constant MotionMaskedCopiedConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> mask_texture [[texture(1)]], metal::sampler mask_sampler [[sampler(1)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	const metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 inverse_destination = metal::float3(1.0f) - destination_color.rgb;
	const metal::float3 inverse_source = metal::float3(1.0f) - source_color.rgb;
	const metal::float3 upper = metal::float3(1.0f) - (inverse_destination * inverse_source) * 2.0f;
	const metal::float3 product = destination_color.rgb * source_color.rgb;
	const metal::float3 lower = product + product;
	const metal::float3 combined_color = metal::select(upper, lower, destination_color.rgb < 0.5f);
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float2 mask_coordinates = input.Position.xy * constants.ScreenMul.xy;
	const float opacity = source_color.a * mask_texture.sample(mask_sampler, mask_coordinates).a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1300: native vertex modulation doubles RGB only; copied alpha is not consumed.
fragment metal::float4 SampleCopiedAdditiveMaskedDoubleColorFragment(MotionMaskPixelInput input [[stage_in]], constant MotionMaskedCopiedConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> mask_texture [[texture(1)]], metal::sampler mask_sampler [[sampler(1)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	source_color = source_color * input.Color;
	source_color = source_color * metal::float4(2.0f, 2.0f, 2.0f, 1.0f);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 combined_color = metal::min(source_color.rgb + destination_color.rgb, metal::float3(1.0f));
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float2 mask_coordinates = input.Position.xy * constants.ScreenMul.xy;
	const float opacity = source_color.a * mask_texture.sample(mask_sampler, mask_coordinates).a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1302: native vertex modulation doubles RGB only; copied alpha is not consumed.
fragment metal::float4 SampleCopiedOverlayMaskedDoubleColorFragment(MotionMaskPixelInput input [[stage_in]], constant MotionMaskedCopiedConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> mask_texture [[texture(1)]], metal::sampler mask_sampler [[sampler(1)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	source_color = source_color * input.Color;
	const metal::float3 inverse_source = metal::float3(1.0f) - source_color.rgb * 2.0f;
	source_color = source_color * metal::float4(2.0f, 2.0f, 2.0f, 1.0f);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 inverse_destination = metal::float3(1.0f) - destination_color.rgb;
	const metal::float3 upper = metal::float3(1.0f) - (inverse_source * inverse_destination) * 2.0f;
	const metal::float3 product = source_color.rgb * destination_color.rgb;
	const metal::float3 lower = product + product;
	const metal::float3 combined_color = metal::select(upper, lower, destination_color.rgb < metal::float3(0.5f));
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float2 mask_coordinates = input.Position.xy * constants.ScreenMul.xy;
	const float opacity = source_color.a * mask_texture.sample(mask_sampler, mask_coordinates).a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1156: exact copied darken operation9; copied alpha is not consumed.
fragment metal::float4 SampleCopiedDarkenMaskedSkipColorFragment(MotionMaskPixelInput input [[stage_in]], constant MotionMaskedCopiedConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> mask_texture [[texture(1)]], metal::sampler mask_sampler [[sampler(1)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 combined_color = metal::min(source_color.rgb, destination_color.rgb);
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float2 mask_coordinates = input.Position.xy * constants.ScreenMul.xy;
	const float opacity = source_color.a * mask_texture.sample(mask_sampler, mask_coordinates).a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

// Runtime PS1308: exact copied darken operation9; copied alpha is not consumed.
fragment metal::float4 SampleCopiedDarkenMaskedDoubleColorFragment(MotionMaskPixelInput input [[stage_in]], constant MotionMaskedCopiedConstants& constants [[buffer(0)]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]], metal::texture2d<float> mask_texture [[texture(1)]], metal::sampler mask_sampler [[sampler(1)]], metal::texture2d<float> destination_texture [[texture(2)]], metal::sampler destination_sampler [[sampler(2)]]) {
	metal::float4 source_color = color_texture.sample(color_sampler, input.TextureCoordinates);
	source_color = source_color * input.Color;
	source_color = source_color * metal::float4(2.0f, 2.0f, 2.0f, 1.0f);
	const metal::float2 destination_coordinates = input.Position.xy * constants.ScreenMulOffset.xy + constants.ScreenMulOffset.zw;
	const metal::float4 destination_color = destination_texture.sample(destination_sampler, destination_coordinates);
	const metal::float3 combined_color = metal::min(source_color.rgb, destination_color.rgb);
	const metal::float3 difference = combined_color - destination_color.rgb;
	const metal::float2 mask_coordinates = input.Position.xy * constants.ScreenMul.xy;
	const float opacity = source_color.a * mask_texture.sample(mask_sampler, mask_coordinates).a;
	const metal::float3 result = opacity * difference + destination_color.rgb;

	return metal::float4(result, opacity);
}

struct CapturedOpacityMapVertexInput {
	metal::float2 Position [[attribute(0)]];
	metal::float4 Color [[attribute(1)]];
	metal::float2 SourceCoordinates [[attribute(2)]];
	metal::float2 MapCoordinates [[attribute(3)]];
};

struct CapturedOpacityMapVertexOutput {
	metal::float4 Position [[position]];
	metal::float4 Color;
	metal::float2 SourceCoordinates;
	metal::float2 MapCoordinates;
};

// VS2 7a23071d: native format5, same XY/W dot products and separate TEXCOORD0/1.
vertex CapturedOpacityMapVertexOutput TransformCapturedOpacityMapVertex(CapturedOpacityMapVertexInput input [[stage_in]], constant SpritePassConstants& constants [[buffer(1)]]) {
	const metal::float3 position = metal::float3(input.Position, 1.0f);
	CapturedOpacityMapVertexOutput output;
	output.Position.x = metal::dot(position, constants.ProjectionRegisters[0].xyw);
	output.Position.y = metal::dot(position, constants.ProjectionRegisters[1].xyw);
	output.Position.z = metal::dot(position, constants.ProjectionRegisters[2].xyw);
	output.Position.w = metal::dot(position, constants.ProjectionRegisters[3].xyw);
	output.Color = input.Color;
	output.SourceCoordinates = input.SourceCoordinates;
	output.MapCoordinates = input.MapCoordinates;
	return output;
}

struct CapturedOpacityMapConstants {
	float Phase;
	float InverseVague;
};

// PS2040 c9252e77: raw map red controls alpha; source RGB and alpha come from the actual Capture companion.
fragment metal::float4 SampleCapturedOpacityMapFragment(CapturedOpacityMapVertexOutput input [[stage_in]], constant CapturedOpacityMapConstants& constants [[buffer(0)]], metal::texture2d<float> source_texture [[texture(0)]], metal::sampler source_sampler [[sampler(0)]], metal::texture2d<float> map_texture [[texture(1)]], metal::sampler map_sampler [[sampler(1)]]) {
	float alpha = map_texture.sample(map_sampler, input.MapCoordinates).r;
	alpha = metal::saturate((alpha - constants.Phase) * constants.InverseVague + 1.0f);
	alpha = alpha * input.Color.a;
	metal::float4 color = source_texture.sample(source_sampler, input.SourceCoordinates);
	color.a = alpha * color.a;
	return color;
}

// PS2041: same texture, RGB at TEXCOORD0 and red-derived alpha at TEXCOORD1.
fragment metal::float4 SampleMovieAlphaFragment(CapturedOpacityMapVertexOutput input [[stage_in]], metal::texture2d<float> color_texture [[texture(0)]], metal::sampler color_sampler [[sampler(0)]]) {
	metal::float4 color = color_texture.sample(color_sampler, input.SourceCoordinates);
	color.rgb = color.rgb * input.Color.rgb;
	const float alpha = color_texture.sample(color_sampler, input.MapCoordinates).r;
	color.a = alpha * input.Color.a;

	return color;
}
