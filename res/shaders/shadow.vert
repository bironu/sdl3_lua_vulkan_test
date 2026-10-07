#version 450

// シャドウマップ用(デプスのみ。フラグメントシェーダーは無い)。
// 平行光源から見たMVP行列(push constant)で、位置だけを変換する
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform Push {
	mat4 lightMvp;
} push;

void main()
{
	gl_Position = push.lightMvp * vec4(inPosition, 1.0);
}
