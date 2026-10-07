#version 450

// スキニング済みの頂点(skin.compの結果。右手系)を読む輪郭線用の頂点シェーダー(outline.vertと同じ殻のふくらませ方)。
// 材質ごとのデータ(輪郭線の色・太さ・種類)は、set 0(ボーン用のdescriptor set)のstorage bufferから読む。
layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec3 inNormal;

// 材質ごとのデータ(vec4が7つ。triangle_skin.vertを参照)。材質の番号は、描画のfirstInstanceで渡される(gl_InstanceIndex)
layout(std430, set = 0, binding = 2) readonly buffer Materials {
	vec4 data[];
} materials;

layout(location = 0) flat out vec3 outlineColor; // 輪郭線の色(材質ごとに一定)
layout(location = 1) out vec2 outlineUv;
layout(location = 2) flat out float outlineAlpha; // 材質の不透明度(材質モーフ込み)
layout(location = 3) flat out float outlineCutoff; // 切り抜きのしきい値(材質の[6].w)

layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 params; // xy = 1ピクセルのクリップ空間の大きさ(2/画面サイズ)、z = 太さ(ピクセル)、w = 奥へずらす量(クリップ空間の深度)
	vec4 params2; // x = トゥーンシェーディングが有効か(1/0)
} push;

void main()
{
	int materialBase = gl_InstanceIndex * 7;
	vec4 outlineParams = materials.data[materialBase + 4]; // 輪郭線の色rgb, 太さ
	int mode = int(materials.data[materialBase + 5].x + 0.5); // 0=既定(トゥーンの切替に従う画面上の太さ)、1=ワールド座標の太さ(MToon)、2=なし
	bool visible = materials.data[materialBase].a >= 0.5 && mode != 2 && (mode == 1 || push.params2.x > 0.5);
	if(!visible){ // 消えている材質(モーフなど)・輪郭線の無い材質は、輪郭線も描かない
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		outlineColor = vec3(0.0);
		return;
	}
	outlineColor = mode == 1 ? outlineParams.rgb : vec3(0.03, 0.02, 0.05);
	outlineUv = inUv; // 頂点モーフのUVはskin.compで足してある
	outlineAlpha = materials.data[materialBase].a;
	outlineCutoff = materials.data[materialBase + 6].w;
	vec3 localPosition = inPosition;
	vec3 localNormal = inNormal;

	vec4 clip = push.mvp * vec4(localPosition, 1.0);
	if(mode == 1){
		// MToon: ワールド座標(モデルの単位)の太さだけ、法線方向へふくらませる(mvpは、モデルの拡大縮小も含む)
		vec3 normal = normalize(localNormal);
		clip += push.mvp * vec4(normal * outlineParams.w, 0.0);
	}
	else{
		vec2 n = (push.mvp * vec4(localNormal, 0.0)).xy;
		float len = length(n);
		if(len > 1e-5){
			n /= len;
		}
		clip.xy += n * push.params.xy * push.params.z * clip.w;
	}
	clip.z += push.params.w * clip.w; // 殻を少し奥へずらして、服の下の面(体など)の殻が表面に突き抜けないようにする
	gl_Position = clip;
}
