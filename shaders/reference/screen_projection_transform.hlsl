// Recovered VS10: SHA-2563ec17e4a717f17df7f6401dfbe160c1b531833a3af35daaa52ca41892dba66f2.
column_major float4x4 modelViewProj : register(c0);
float4 constVtxColor : register(c4);

struct ScreenProjectionVertexInput {
	float3 Position : POSITION;
	float2 TextureCoordinates : TEXCOORD0;
};

struct ScreenProjectionVertexOutput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

ScreenProjectionVertexOutput main(ScreenProjectionVertexInput input) {
	const float4 position = float4(input.Position, 1.0f);
	const float4x4 registers = transpose(modelViewProj);
	ScreenProjectionVertexOutput output;
	output.Position.x = dot(position, registers[0]);
	output.Position.y = dot(position, registers[1]);
	output.Position.z = dot(position, registers[2]);
	output.Position.w = dot(position, registers[3]);
	output.Color = constVtxColor;
	output.TextureCoordinates = input.TextureCoordinates;

	return output;
}
