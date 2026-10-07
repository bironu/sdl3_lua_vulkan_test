#version 450

// スキニング済みの頂点(skin.compの結果。右手系)を読む、ボーンモデル用の頂点シェーダー。出力はtriangle.vertと同じで、フラグメントシェーダーはtriangle.fragを共用する。
// 材質ごとのデータ(拡散色・MToon)だけ、set 2(ボーン用のdescriptor set)のstorage bufferから読む。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec3 inNormal;
// 材質ごとのデータ(vec4が7つ)。材質の番号は、描画のfirstInstanceで渡される(gl_InstanceIndex)。
//   [0] 拡散色rgba(材質モーフを当てた結果)  [1] 影の色rgb, 影の境目のずらし  [2] 影の境目のぼかし, GI均一化, リムの混ぜ具合, リムの鋭さ
//   [3] リムの色rgb, リムの持ち上げ  [4] 輪郭線の色rgb, 太さ  [5] 輪郭線の種類, MToonか, 輪郭線の光の混ぜ具合, テクスチャのフラグ  [6] 発光rgb
layout(std430, set = 2, binding = 2) readonly buffer Materials {
	vec4 data[];
} materials;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUv;
layout(location = 2) out vec3 fragNormal;   // ワールド空間
layout(location = 3) out vec3 fragWorldPos; // ワールド空間
layout(location = 4) flat out vec4 fragMaterial; // 材質(描画ごとに一定。下のmaterialと同じ並び)
// MToon(材質ごとに一定)。triangle.fragのfragMt0〜3と同じ並び: (影の色rgb, 影のずらし)、(ぼかし, GI均一化, リムの混ぜ具合, リムの鋭さ)、(リムの色rgb, 持ち上げ)、(発光rgb, MToonか)
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
	int materialBase = gl_InstanceIndex * 7;
	vec4 materialColor = materials.data[materialBase];
	fragMt0 = materials.data[materialBase + 1];
	fragMt1 = materials.data[materialBase + 2];
	fragMt2 = materials.data[materialBase + 3];
	vec4 materialFlags = materials.data[materialBase + 5]; // (輪郭線の種類, MToonか, 輪郭線の光の混ぜ具合, テクスチャのフラグ(影の色=1, 発光=2))
	fragMt3 = vec4(materials.data[materialBase + 6].rgb, materialFlags.y + 2.0 * materialFlags.w); // w: ビット0=MToonか、ビット1=影の色のテクスチャ、ビット2=発光のテクスチャ
	fragColor = inColor * materialColor.rgb;
	fragUv = inUv; // 頂点モーフのUVはskin.compで足してある
	vec4 position = vec4(inPosition, 1.0);
	fragWorldPos = vec3(dot(push.modelRow0, position), dot(push.modelRow1, position), dot(push.modelRow2, position));
	fragMaterial = push.material;
	fragMaterial.w = materialColor.a; // 不透明度は材質の色(材質モーフ込み)から
	// 法線はモデル行列の回転(3x3)で変換する。非一様スケールがある場合は逆転置行列が必要
	fragNormal = vec3(dot(push.modelRow0.xyz, inNormal), dot(push.modelRow1.xyz, inNormal), dot(push.modelRow2.xyz, inNormal));
}
