// Recovered shader for bytecode SHA-256 4a84c1e43cf99f4e635f66d25d1074ebad5127a610953214277160896859de23.
// RDEF identifies a column-major float4x4 at byte zero in b0.
// Native recompilation matches the complete original DXBC; Metal pixels remain unverified.
// Compiler and layout evidence: docs/recovery/shaders.md.
column_major float4x4 modelViewProj : register(c0);

struct VertexInput {
	float3 Position : POSITION;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

struct VertexOutput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

VertexOutput main(VertexInput input) {
	const float4 homogeneous_position = float4(input.Position, 1.0f);
	VertexOutput output;
	output.Position = mul(homogeneous_position, modelViewProj);
	output.Color = input.Color;
	output.TextureCoordinates = input.TextureCoordinates;

	return output;
}
