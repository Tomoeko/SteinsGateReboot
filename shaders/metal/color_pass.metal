#include <metal_stdlib>

struct ColorVertexInput {
	metal::float3 Position [[attribute(0)]];
	metal::float4 Color [[attribute(1)]];
	metal::float2 TextureCoordinates [[attribute(2)]];
};

struct ColorVertexOutput {
	metal::float4 Position [[position]];
	metal::float4 Color;
	metal::float2 TextureCoordinates;
};

struct ColorPassConstants {
	metal::float4x4 ModelViewProjection;
};

// Reference DXBC 4a84c1e4 stores matrix columns, then evaluates a row-vector product.
vertex ColorVertexOutput TransformColorVertex(ColorVertexInput input [[stage_in]], constant ColorPassConstants& constants [[buffer(1)]]) {
	const metal::float4 position = metal::float4(input.Position, 1.0f);
	ColorVertexOutput output;
	output.Position = metal::transpose(constants.ModelViewProjection) * position;
	output.Color = input.Color;
	output.TextureCoordinates = input.TextureCoordinates;

	return output;
}

// Reference DXBC 4585cc2f reads only the interpolated color.
fragment metal::float4 PassthroughColorFragment(ColorVertexOutput input [[stage_in]]) {
	return input.Color;
}
