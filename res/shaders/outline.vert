#version 450

// トゥーンの輪郭線(反転した殻): 頂点を法線方向へ、画面上で一定の太さ(ピクセル)だけふくらませる。
// 裏面だけを単色で描くと、元の形の外側に縁だけが残る(面の向きの切り替えはパイプライン側で行う)
layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec3 inNormal;

layout(location = 0) flat out vec3 outlineColor; // 輪郭線の色(材質ごとに一定。ここでは既定のほぼ黒)
layout(location = 1) out vec2 outlineUv;
layout(location = 2) flat out float outlineAlpha;
layout(location = 3) flat out float outlineCutoff;

layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 params; // xy = 1ピクセルのクリップ空間の大きさ(2/画面サイズ)、z = 太さ(ピクセル)、w = 奥へずらす量(クリップ空間の深度)
} push;

void main()
{
	vec4 clip = push.mvp * vec4(inPosition, 1.0);
	vec2 n = (push.mvp * vec4(inNormal, 0.0)).xy;
	float len = length(n);
	if(len > 1e-5){
		n /= len;
	}
	clip.xy += n * push.params.xy * push.params.z * clip.w;
	clip.z += push.params.w * clip.w; // 殻を少し奥へずらして、服の下の面(体など)の殻が表面に突き抜けないようにする
	gl_Position = clip;
	outlineColor = vec3(0.03, 0.02, 0.05);
	outlineUv = inUv;
	outlineAlpha = 1.0;
	outlineCutoff = 0.05;
}
