#version 450

// スキニング済みの頂点(skin.compの結果。右手系)を読む、シャドウマップ用(デプスのみ)。
// 位置だけ読む。材質の不透明度(set 0のstorage buffer)が小さい(モーフなどで消えている)材質は、影を落とさない
layout(location = 0) in vec3 inPosition;

layout(std430, set = 0, binding = 2) readonly buffer Materials {
	vec4 data[]; // 材質ごとにvec4が7つ([0]が拡散色rgba)。材質の番号はgl_InstanceIndex(triangle_pre.vertを参照)
} materials;

layout(push_constant) uniform Push {
	mat4 lightMvp;
} push;

void main()
{
	if(materials.data[gl_InstanceIndex * 7].a < 0.5){ // 消えている材質は影を落とさない(画面の外へ追い出す)
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		return;
	}
	gl_Position = push.lightMvp * vec4(inPosition, 1.0);
}
