#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUv;
layout(location = 2) out vec3 fragNormal;   // ワールド空間
layout(location = 3) out vec3 fragWorldPos; // ワールド空間
layout(location = 4) flat out vec4 fragMaterial; // 材質(描画ごとに一定。下のmaterialと同じ並び)
// MToon用(スキニング付きの頂点シェーダーだけが使う。ここでは無効の0)
layout(location = 5) flat out vec4 fragMt0;
layout(location = 6) flat out vec4 fragMt1;
layout(location = 7) flat out vec4 fragMt2;
layout(location = 8) flat out vec4 fragMt3;

// オブジェクトごとのデータ(push constant。合計128B=仕様の保証最小値)。
//   mvp         : geo::Matrix4x4f::data()をそのまま渡す(列優先)
//   modelRow0-2 : モデル行列の上3行(4x3)。アフィン変換なので最下行(0,0,0,1)は省略している
//   material    : x=鏡面反射の強さ, y=光沢度, z=フラグ(下位1bit=影を受けるか、残り=自発光する点光源の番号+1。0なら通常)、w=不透明度
layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 modelRow0;
	vec4 modelRow1;
	vec4 modelRow2;
	vec4 material;
} push;

void main()
{
	gl_Position = push.mvp * vec4(inPosition, 1.0);
	fragColor = inColor;
	fragUv = inUv;
	vec4 position = vec4(inPosition, 1.0);
	fragWorldPos = vec3(dot(push.modelRow0, position), dot(push.modelRow1, position), dot(push.modelRow2, position));
	fragMaterial = push.material;
	fragMt0 = vec4(0.0);
	fragMt1 = vec4(0.0);
	fragMt2 = vec4(0.0);
	fragMt3 = vec4(0.0);
	// 法線はモデル行列の回転(3x3)で変換する。非一様スケールがある場合は逆転置行列が必要
	fragNormal = vec3(dot(push.modelRow0.xyz, inNormal), dot(push.modelRow1.xyz, inNormal), dot(push.modelRow2.xyz, inNormal));
}
