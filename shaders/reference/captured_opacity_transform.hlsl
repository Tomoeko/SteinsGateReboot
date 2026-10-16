// Recovered VS2: SHA-256 7a23071dd62f5ca1eb0a65ee98f0858ae9a5e79be552c049fd187bf079f705b5.
column_major float4x4 modelViewProj : register(c0);

struct CapturedOpacityVertexInput {
	float2 Position : POSITION;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
	float2 OpacityCoordinates : TEXCOORD1;
};

struct CapturedOpacityVertexOutput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
	float2 OpacityCoordinates : TEXCOORD1;
};

CapturedOpacityVertexOutput main(CapturedOpacityVertexInput input) {
	const float3 position = float3(input.Position, 1.0f);
	const float4x4 registers = transpose(modelViewProj);
	CapturedOpacityVertexOutput output;
	output.Position.x = dot(position, registers[0].xyw);
	output.Position.y = dot(position, registers[1].xyw);
	output.Position.z = dot(position, registers[2].xyw);
	output.Position.w = dot(position, registers[3].xyw);
	output.Color = input.Color;
	output.TextureCoordinates = input.TextureCoordinates;
	output.OpacityCoordinates = input.OpacityCoordinates;

	return output;
}
