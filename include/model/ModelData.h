#if !defined(MODEL_MODELDATA_H_)
#define MODEL_MODELDATA_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace model
{

// 描画APIに依存しない、モデルファイルから読んだ生データ(CPU側)。
// 座標系は元ファイルのまま(PMXは左手系: +Xが右、+Yが上、モデルの正面が-Z)。変換は描画側(VulkanModel)が行う
struct ModelVertex
{
	float position[3];
	float normal[3];
	float uv[2];
	// スキニング: 影響を受けるボーン(最大4)とその重み(合計1)。使わない枠はボーン-1・重み0。
	// SDEFは2ボーンのBDEF2として、QDEFは4ボーンのBDEF4として扱う
	int bones[4] = {-1, -1, -1, -1};
	float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// ボーン(PMX)。座標はモデルの元の座標系(左手系)
struct ModelBone
{
	enum Flag : uint16_t
	{
		TailIsBone = 0x0001,
		Rotatable = 0x0002,
		Translatable = 0x0004,
		Visible = 0x0008,
		Operable = 0x0010,
		IK = 0x0020,
		AppendRotation = 0x0100,    // 付与(回転): 別のボーンの回転を、割合を掛けて受け取る
		AppendTranslation = 0x0200, // 付与(移動)
		FixedAxis = 0x0400,
		LocalAxis = 0x0800,
		AfterPhysics = 0x1000,
		ExternalParent = 0x2000,
	};
	struct IkLink
	{
		int bone = -1;
		bool hasLimit = false;
		float limitMin[3] = {0.0f, 0.0f, 0.0f}; // 回転角の下限(ラジアン、X,Y,Z)
		float limitMax[3] = {0.0f, 0.0f, 0.0f};
	};

	std::string name;
	float position[3] = {0.0f, 0.0f, 0.0f}; // 休止ポーズでのボーンの位置(ワールド)
	// 休止ポーズでの、親から見た局所の回転(x,y,z,w)。PMXは常に回転なし。glTF/VRMのノードは回転を持つことがある
	float restRotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	int parent = -1;
	int layer = 0; // 変形階層。小さい順に(同じなら番号順に)処理する
	uint16_t flags = 0;
	int appendParent = -1; // 付与の元になるボーン
	float appendRatio = 0.0f;
	// IK
	int ikTarget = -1;
	int ikLoop = 0;
	float ikLimit = 0.0f; // 1回の反復で動かせる最大角(ラジアン)
	std::vector<IkLink> ikLinks; // ターゲットに近い順
};

// 材質。indices[firstIndex, firstIndex+indexCount)がこの材質で描く三角形(元ファイルの並び順で描くこと)
struct ModelMaterial
{
	std::string name;
	float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // rgb=拡散色、a=不透明度
	float specular = 0.5f;                         // 鏡面反射の強さ(0〜1)
	float shininess = 16.0f;                       // 光沢度(PMXの鏡面係数)
	bool doubleSided = false;
	int texture = -1;                              // texturePathsの添字(無ければ-1)
	bool ignoreTextureAlpha = false;               // テクスチャのアルファを無視する(glTFの不透明な材質)
	bool alphaMask = false;                        // アルファの切り抜き(glTFのalphaMode MASK): アルファがalphaCutoff未満の部分を描かない。ブレンドはしない
	float alphaCutoff = 0.5f;
	// MToon(VRMのトゥーン材質。VRMC_materials_mtoon)。enabledのときだけ、シェーダーがMToonの陰影で描く。
	// 影の色は shadeColor x 影の色のテクスチャ(無ければ基本色のテクスチャ)、発光は emissive x 発光のテクスチャ(無ければ係数だけ)。
	// マットキャップ・リム・輪郭線の太さのテクスチャは扱わない
	struct MToon
	{
		bool enabled = false;
		bool faceSkin = false;    // 顔の肌(skinで、名前に"FACE"も含む)。顔は白く飛びやすいので、体より血色を強くする
		bool skin = false;        // 肌の材質(名前に"SKIN"か"FACEMOUTH"を含む。VRoidの命名)。陰の赤みなど、肌向けの質感補正をかける
		int shadeTexture = -1;    // 影の色のテクスチャ(shadeMultiplyTexture)のtexturePathsの添字。無ければ-1
		int emissiveTexture = -1; // 発光のテクスチャ(emissiveTexture)。同上
		float shadeColor[3] = {1.0f, 1.0f, 1.0f};
		float shadingShift = 0.0f;
		float shadingToony = 0.9f;
		float giEqualization = 0.9f;
		float rimColor[3] = {0.0f, 0.0f, 0.0f};
		float rimLightingMix = 1.0f;
		float rimFresnelPower = 5.0f;
		float rimLift = 0.0f;
		float emissive[3] = {0.0f, 0.0f, 0.0f};
		// 輪郭線: 0=既定(トゥーンの切替に従う画面上の太さ)、1=ワールド座標の太さ(outlineWidthはモデルの単位)、2=なし
		int outlineMode = 0;
		float outlineWidth = 0.0f;
		float outlineColor[3] = {0.0f, 0.0f, 0.0f};
		float outlineLightingMix = 1.0f;
	} mtoon;
	uint32_t firstIndex = 0;
	uint32_t indexCount = 0;
};

// モーフ(表情などの変形、PMX)。頂点モーフは、頂点を重みに比例して動かす。グループモーフは、他のモーフをまとめて動かす。
// UVモーフは頂点のUVを、材質モーフは材質の拡散色(rgba)を動かす。それ以外の種類(ボーン・追加UV・インパルス)や、
// 材質モーフの拡散色以外の項目(鏡面・環境光・輪郭・テクスチャ係数)は、いまは読み飛ばす
struct ModelMorph
{
	enum class Type : uint8_t { Group = 0, Vertex = 1, Bone = 2, Uv = 3, UvExt1 = 4, UvExt2 = 5, UvExt3 = 6, UvExt4 = 7, Material = 8, Flip = 9, Impulse = 10 };
	struct VertexOffset
	{
		uint32_t vertex = 0;
		float delta[3] = {0.0f, 0.0f, 0.0f}; // 元の座標系(左手系)での移動量
	};
	struct UvOffset
	{
		uint32_t vertex = 0;
		float delta[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // UVの移動量(x,yだけ使う)
	};
	// 材質モーフ: 拡散色(rgba)だけ扱う。material=-1は全材質。add=falseは乗算、trueは加算
	struct MaterialOffset
	{
		int material = -1;
		bool add = false;
		float diffuse[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	};
	struct GroupOffset
	{
		int morph = -1;
		float rate = 0.0f;
	};

	std::string name;
	Type type = Type::Vertex;
	std::vector<VertexOffset> vertexOffsets; // Vertex
	std::vector<UvOffset> uvOffsets;         // Uv
	std::vector<MaterialOffset> materialOffsets; // Material
	std::vector<GroupOffset> groupOffsets;   // Group(Flipも同じ形で入れるが、いまは使わない)
};

// 剛体(PMX)。形状・位置・回転はモデルの元の座標系(左手系)。回転はオイラー角(ラジアン、R = Ry*Rx*Rz)
struct ModelRigidBody
{
	enum class Shape : uint8_t { Sphere = 0, Box = 1, Capsule = 2 };
	// BoneFollow: ボーンに追従(動かされる側)、Physics: 物理演算でボーンを動かす、PhysicsAligned: 物理演算で回転だけボーンを動かし、位置はボーンに合わせる
	enum class Type : uint8_t { BoneFollow = 0, Physics = 1, PhysicsAligned = 2 };

	std::string name;
	int bone = -1; // 関連するボーン(無ければ-1)
	uint8_t group = 0;
	uint16_t nonCollisionMask = 0; // 衝突しないグループ(ビット=グループ番号)
	Shape shape = Shape::Sphere;
	float size[3] = {0.0f, 0.0f, 0.0f}; // 球: x=半径、箱: 半分の大きさ、カプセル: x=半径 y=高さ(半球の中心間)
	float position[3] = {0.0f, 0.0f, 0.0f};
	float rotation[3] = {0.0f, 0.0f, 0.0f};
	float mass = 1.0f;
	float linearDamping = 0.0f;
	float angularDamping = 0.0f;
	float restitution = 0.0f;
	float friction = 0.5f;
	Type type = Type::BoneFollow;
};

// ジョイント(PMX)。2つの剛体をつなぐバネ付きの6自由度拘束
struct ModelJoint
{
	std::string name;
	int rigidA = -1;
	int rigidB = -1;
	float position[3] = {0.0f, 0.0f, 0.0f};
	float rotation[3] = {0.0f, 0.0f, 0.0f};
	float moveLimitMin[3] = {0.0f, 0.0f, 0.0f};
	float moveLimitMax[3] = {0.0f, 0.0f, 0.0f};
	float rotationLimitMin[3] = {0.0f, 0.0f, 0.0f};
	float rotationLimitMax[3] = {0.0f, 0.0f, 0.0f};
	float springMove[3] = {0.0f, 0.0f, 0.0f};
	float springRotation[3] = {0.0f, 0.0f, 0.0f};
};

// スプリングボーン(VRM 1.0 VRMC_springBone。髪やスカートなどの揺れ)。座標はモデルの座標系(PMXと同じ左手系に直したもの)
struct SpringJoint
{
	int bone = -1;
	float hitRadius = 0.0f; // 衝突判定の半径
	float stiffness = 1.0f; // 元の向きへ戻ろうとする力
	float gravityPower = 0.0f;
	float gravityDir[3] = {0.0f, -1.0f, 0.0f};
	float dragForce = 0.5f; // 速度が減る割合
};

struct SpringChain
{
	std::string name;
	std::vector<SpringJoint> joints; // 根元から先へ。最後のジョイントは、1つ前の先端として使うだけで、自分は動かさない(virtualTailのときを除く)
	// trueなら、最後のジョイントも動かす。その先端は、仮想の先端(親から自分への向きへ7cm。VRM 0.xの末端のボーンの扱い)
	bool virtualTail = false;
	std::vector<int> colliderGroups; // springColliderGroupsの添字
};

struct SpringCollider
{
	enum class Shape : uint8_t { Sphere = 0, Capsule = 1 };
	int bone = -1;
	Shape shape = Shape::Sphere;
	float offset[3] = {0.0f, 0.0f, 0.0f}; // ボーンの座標系での位置
	float tail[3] = {0.0f, 0.0f, 0.0f};   // Capsule: 線分の反対側の端(ボーンの座標系)
	float radius = 0.0f;
};

struct SpringColliderGroup
{
	std::vector<int> colliders; // springCollidersの添字
};

struct ModelData
{
	std::string name;
	std::vector<ModelVertex> vertices;
	std::vector<uint32_t> indices; // 三角形リスト
	std::vector<ModelMaterial> materials;
	std::vector<ModelBone> bones; // 無い(0個)モデルもある
	std::vector<ModelMorph> morphs; // 無い(0個)モデルもある
	// VRMのヒューマノイドのボーン(名前はVRMの仕様のもの: hips, spine, leftUpperArm など)→ボーンの番号。PMXでは空
	std::vector<std::pair<std::string, int>> humanoidBones;
	// 読み込み時に元のファイルの座標へ掛けた符号(x,y,z。PMXは掛けない=1,1,1、VRM 1.0はZ方向の鏡像=1,1,-1、VRM 0.xはX方向の鏡像=-1,1,1)。
	// VRMAなど、元のファイルの座標系のデータへ戻すのに使う
	float importAxisSign[3] = {1.0f, 1.0f, 1.0f};
	std::vector<SpringChain> springChains; // 無いモデルもある
	std::vector<SpringCollider> springColliders;
	std::vector<SpringColliderGroup> springColliderGroups;
	std::vector<ModelRigidBody> rigidBodies; // 物理演算用。無いモデルもある
	std::vector<ModelJoint> joints;
	std::vector<std::string> texturePaths; // リポジトリ直下からの相対パス(Resources::loadImageに渡せる)。埋め込み画像(VRM)は空文字列
	std::vector<std::vector<uint8_t>> embeddedImages; // texturePathsと同じ並びで、ファイルに埋め込まれた画像(PNG/JPEG等)のバイト列。外部ファイルのものは空
};

} // namespace model

#endif // MODEL_MODELDATA_H_
