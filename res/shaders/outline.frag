#version 450

// 輪郭線も、材質のアルファ(MASK/BLENDの切り抜き)で本体と同じ範囲だけ描く(描かないと、切り抜かれた部分に黒い殻だけが残る)
layout(set = 1, binding = 0) uniform sampler2D tex;
layout(location = 0) flat in vec3 outlineColor;
layout(location = 1) in vec2 outlineUv;
layout(location = 2) flat in float outlineAlpha; // 材質の不透明度(テクスチャのアルファに掛ける)
layout(location = 3) flat in float outlineCutoff; // 切り抜きのしきい値(本体と同じ。MASKでなければ0.05)
layout(location = 0) out vec4 outColor;

void main()
{
	if(texture(tex, outlineUv).a * outlineAlpha < outlineCutoff){
		discard;
	}
	outColor = vec4(outlineColor, 1.0);
}
