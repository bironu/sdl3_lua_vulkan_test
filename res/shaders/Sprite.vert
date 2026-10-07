#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragColor;

// スプライトごとの行列と色(push constant)。colorのaがアルファ(透明度)
layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 color;
	vec4 uvRect; // テクスチャのどの範囲を貼るか(u0, v0, 幅, 高さ)。全体なら(0, 0, 1, 1)。文字(ビットマップフォント)の1文字分など
} push;

void main()
{
	gl_Position = push.mvp * vec4(inPosition, 1.0);
	fragUv = push.uvRect.xy + inUv * push.uvRect.zw;
	fragColor = push.color;
}
