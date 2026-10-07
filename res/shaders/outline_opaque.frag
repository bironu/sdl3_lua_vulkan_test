#version 450

// 輪郭線(アルファの切り抜きが要らない材質用)。outline.fragと違って、テクスチャを読まず discard もしない
// (切り抜きの無い材質では、outline.fragの判定は常に通るので同じ結果。discardが無いと、TBDRのGPUで隠れた面の除去が効く)
layout(location = 0) flat in vec3 outlineColor;
layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(outlineColor, 1.0);
}
