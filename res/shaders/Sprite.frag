#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D tex;

// ライティングをしない、アルファブレンド用のスプライト(2D/3D共通)
void main()
{
	outColor = texture(tex, fragUv) * fragColor;
}
