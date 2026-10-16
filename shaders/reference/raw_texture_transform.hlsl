// Recovered VS4: SHA-256 5ff91389ad191d7238d7ee8066eac13547fcd670a195192f08b66f4b58f42b80.
// Each raw constant register supplies x/y/translation to one output component.
column_major float4x4 modelViewProj : register(c0);
float4 constVtxColor : register(c4);

struct RawTextureVertexInput {
	float2 Position : POSITION;
	float2 TextureCoordinates : TEXCOORD0;
};

struct RawTextureVertexOutput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

RawTextureVertexOutput main(RawTextureVertexInput input) {
	const float3 position = float3(input.Position, 1.0f);
	const float4x4 registers = transpose(modelViewProj);
	RawTextureVertexOutput output;
	output.Position.x = dot(position, registers[0].xyw);
	output.Position.y = dot(position, registers[1].xyw);
	output.Position.z = dot(position, registers[2].xyw);
	output.Position.w = dot(position, registers[3].xyw);
	output.Color = constVtxColor;
	output.TextureCoordinates = input.TextureCoordinates;

	return output;
}
