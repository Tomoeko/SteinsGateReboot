// Recovered shader for bytecode SHA-256 4585cc2f570773b0d06b9eb2eef8ba0f5259192a2caf96bdbd0c890e1dbaeb16.
// ISGN retains all three fields; only COLOR is read by SHDR.
// Native recompilation matches the complete original DXBC; Metal pixels remain unverified.
// Compiler and layout evidence: docs/recovery/shaders.md.
struct PixelInput {
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 TextureCoordinates : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target {
	return input.Color;
}
