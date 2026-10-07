#if !defined(VULKANMODEL_H_)
#define VULKANMODEL_H_

#include "model/ModelData.h"
#include "model/MorphSet.h"
#include "model/Physics.h"
#include "model/SpringBones.h"
#include "model/Skeleton.h"
#include "vk/VulkanBonePool.h"
#include "vk/VulkanBuffer.h"
#include "vk/VulkanContext.h"
#include "vk/VulkanMaterial.h"
#include "vk/VulkanMesh.h"
#include "vk/VulkanTextureSet.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// モデルデータ(model::ModelData)をVulkanで描ける形にしたもの。材質ごとに1つのメッシュ(+材質)を持つ。
// 描くときは、元ファイルの材質の並び順(アルファブレンドのため)でVulkanWindow::draw(model, ...)に渡す。
//
// ボーンがあるモデルは、スキニング(頂点をボーンの姿勢で動かす)をGPU(頂点シェーダー)で行う。頂点バッファは休止ポーズのまま
// 一度だけ作り、毎フレーム書き換えるのはボーン行列(storage buffer)だけ。使い方:
//   skeleton()->resetPose() → setBoneRotation/setBoneTranslation → updatePose() → draw
// ボーン行列は、GPUがまだ前のフレームで使っているかもしれないバッファを書き換えないよう、フレーム枠ごとに別のバッファへ書く
// (書き込みはVulkanWindow::swap()が、その枠のフェンス待ちの後にprepareSlot()で行う)
class VulkanModel
{
public:
	struct Part
	{
		std::shared_ptr<VulkanMesh> mesh; // 頂点はvk_::SkinnedVertex(ボーンの無いモデルはvk_::Vertex)
		VulkanMaterial material;
		// MToonで影の色・発光のテクスチャを持つ材質のテクスチャの組(無ければnullptr。あれば、描画はmaterial.textureの代わりにこれをバインドする)
		std::shared_ptr<VulkanTextureSet> textureSet;
		uint32_t materialIndex = 0; // 元ファイルの材質の番号(描画のfirstInstanceで渡し、材質ごとの色の参照に使う)
		// スキニングのモデルの、スキニング済みの頂点の置き場所: 何番目のグループの、何頂点目から始まるか(描画のvertexOffsetに使う)
		uint32_t skinGroup = 0;
		uint32_t skinFirstVertex = 0;
	};
	// パスからテクスチャを作る(画像の読み込みはResourcesの責務なので、呼び出し側が
	// Resources::loadImage + VulkanWindow::createTextureで実装して渡す)。読めなければnullptrを返す
	using TextureLoader = std::function<std::shared_ptr<VulkanTexture>(const std::string &path)>;
	// ファイルに埋め込まれた画像(VRMなど)のバイト列からテクスチャを作る。ignoreAlphaなら、テクスチャのアルファを無視して不透明にする。
	// 埋め込み画像のあるモデルでは必須(読めなければnullptrを返す)
	using EmbeddedTextureLoader = std::function<std::shared_ptr<VulkanTexture>(const std::vector<uint8_t> &bytes, bool ignoreAlpha)>;

	// 元ファイルの座標系(PMX: 左手系、正面が-Z)を、このエンジンの右手系(正面が+Z)に直して作る。
	// 同じテクスチャを使う材質でテクスチャは共有する。frameSlotsはフレーム枠の数(VulkanWindowの同時進行フレーム数)。
	// 作れなければnullptr
	static std::shared_ptr<VulkanModel> create(const std::shared_ptr<VulkanContext> &ctx,
		const std::shared_ptr<VulkanBonePool> &bonePool, const std::shared_ptr<VulkanTexturePool> &texturePool,
		const std::shared_ptr<const model::ModelData> &data,
		const TextureLoader &loadTexture, int frameSlots, const EmbeddedTextureLoader &loadEmbeddedTexture = nullptr);

	// 元のモデルデータ(ヒューマノイドのボーンの対応など、読み込み時の情報を引くのに使う)
	const model::ModelData &data() const { return *data_; }
	size_t partCount() const { return parts_.size(); }
	const Part &part(size_t index) const { return parts_[index]; }
	// ボーンのあるモデルなら、フレーム枠slotのボーン行列のdescriptor set(スキニング用パイプラインのset 2)。無ければVK_NULL_HANDLE
	VkDescriptorSet boneSet(int slot) const { return slot >= 0 && slot < static_cast<int>(slots_.size()) ? slots_[slot].set : VK_NULL_HANDLE; }

	// スキニング(コンピュート): ボーンのあるモデルは、現在の姿勢でスキニング済みの頂点(vk_::Vertex。右手系)を、
	// フレーム枠ごとのバッファに書き出す。描画(影・本体・輪郭線)は、メッシュの頂点バッファの代わりにこれを読む。
	// 全部品の頂点を1つの「グループ」にまとめて、1回のdispatchで書き出す(部品の数や大きさによらない。モデルによって部品の数は大きく違うため)。
	// ただし1つのstorage bufferの上限(maxStorageBufferRange)を超えるモデルは、上限に収まる複数のグループに分ける。
	// 書き出しはVulkanWindow::swap()がコマンドバッファへ記録する(グループごとに skinGroupSet をset 1にして、skinGroupVertexCount だけdispatch)
	size_t skinGroupCount() const { return skinGroups_.size(); }
	uint32_t skinGroupVertexCount(size_t group) const { return skinGroups_[group].vertexCount; }
	VkDescriptorSet skinGroupSet(size_t group, int slot) const
	{
		return slot >= 0 && slot < static_cast<int>(slots_.size()) && group < slots_[slot].groups.size() ? slots_[slot].groups[group].set : VK_NULL_HANDLE;
	}
	// 部品partの描画に使う、スキニング済みの頂点バッファ(フレーム枠slotのもの)と、その中での部品の先頭の頂点(vkCmdDrawIndexedのvertexOffset)
	VkBuffer skinnedVertexBuffer(size_t part, int slot) const
	{
		const size_t group = part < parts_.size() ? parts_[part].skinGroup : 0;
		return slot >= 0 && slot < static_cast<int>(slots_.size()) && group < slots_[slot].groups.size() ? slots_[slot].groups[group].out->get() : VK_NULL_HANDLE;
	}
	int32_t skinnedVertexOffset(size_t part) const { return part < parts_.size() ? static_cast<int32_t>(parts_[part].skinFirstVertex) : 0; }
	// フレーム枠slotのスキニング済みの頂点が古い(ボーン行列かモーフが変わった)か。trueを返したら、記録した前提でフラグを下ろす
	bool takeSkinDirty(int slot)
	{
		if(slot < 0 || slot >= static_cast<int>(slots_.size()) || !slots_[slot].skinDirty){
			return false;
		}
		slots_[slot].skinDirty = false;
		return true;
	}

	// ヒューマノイドの腰(hips)の現在の位置(モデル空間。描画に使う右手系)。腰が分からないモデル(ボーン無し・hips無し)はfalse。
	// モーションでキャラが動くと、モデルのルートは動かず、腰のボーンが動くので、足元の影の位置などに使う
	bool hipsPosition(float out[3]) const;
	// 全材質の環境光に足す明るさ(VulkanMaterial::ambientBoost)。モデルだけ明るくしたいときに使う
	void setAmbientBoost(float boost)
	{
		for(auto &part : parts_){ part.material.ambientBoost = boost; }
	}

	// ボーンのあるモデルならスケルトン(姿勢の設定に使う)。無ければnullptr
	model::Skeleton *skeleton() { return skeleton_.get(); }
	// ボーンのあるモデルで、モーフがあればそのモーフの重み(頂点モーフは表情など)。無ければnullptr。
	// 重みを変えたら、updatePose()で次の描画から反映する
	model::MorphSet *morphs() { return morphs_.get(); }
	// スケルトンの姿勢とモーフを解いて(update)、次の描画から反映する。物理演算(髪やスカートの揺れ)があるモデルは、
	// physicsDt(前回からの経過秒。0なら物理演算を進めない。VRMのスプリングボーンも同じ)だけ進めて、その結果のボーンの姿勢も反映する
	void updatePose(float physicsDt = 0.0f);
	// 物理演算の剛体をボーンの現在の姿勢へ戻して揺れを止める(モーションの頭出しや、大きく動いた直後に呼ぶ)
	void resetPhysics();
	// MToonの材質を持つか(輪郭線を、トゥーンの切替に関係なく描く)
	bool hasMToon() const { return hasMToon_; }
	// 材質のモーフ(表情などで材質の色・不透明度が変わる)を持つか。持つ材質は、不透明度が実行中に変わりうるので、不透明として描けない
	bool hasMaterialMorph() const { return morphs_ && morphs_->hasMaterialMorph(); }
	// 材質1つあたりのGPUへ送るデータの大きさ(vec4が7つ。res/shaders/triangle_skin.vertのコメント参照)
	static constexpr size_t kMaterialStride = 28;
	// ボーンのあるモデルの、現在の姿勢でのモデル空間(描画に使う右手系)のAABB。フラスタムカリング用で、
	// ボーンごとの休止ポーズの頂点範囲をスキニング行列で動かして合わせ、頂点モーフ等の余裕を足してある。ボーンの無いモデルはfalse
	bool hasPoseBounds() const { return hasPoseBounds_; }
	const float* poseBoundsMin() const { return poseBoundsMin_; }
	const float* poseBoundsMax() const { return poseBoundsMax_; }
	bool hasPhysics() const { return physics_ != nullptr || springBones_ != nullptr; }
	// フレーム枠slotのボーン行列を現在の姿勢に合わせる(姿勢が変わっていなければ何もしない)。VulkanWindow::swap()が呼ぶ
	void prepareSlot(int slot);

	~VulkanModel();

private:
	VulkanModel() = default;

	struct SkinGroup
	{
		std::unique_ptr<VulkanBuffer> in; // グループの全部品の、休止ポーズの頂点(vk_::SkinnedVertex)を並べたもの
		uint32_t vertexCount = 0;
	};
	std::vector<SkinGroup> skinGroups_;
	struct SkinGroupSlot
	{
		std::unique_ptr<VulkanBuffer> out; // スキニング済みの頂点(vk_::Vertex x 頂点数)。STORAGE(コンピュートの出力)+VERTEX
		VkDescriptorSet set = VK_NULL_HANDLE; // binding 0=入力(SkinGroup::in)、1=out
	};
	struct Slot
	{
		std::vector<SkinGroupSlot> groups; // skinGroups_と同じ並び
		bool skinDirty = true;       // ボーン行列かモーフを書き換えた(出力のスキニング済みの頂点が古い)
		std::unique_ptr<VulkanBuffer> bones; // ボーンごとにvec4が3つ(行ごとに 回転3要素+移動)
		std::unique_ptr<VulkanBuffer> morph; // 頂点ごとにvec4が2つ(位置とUVの移動量)。頂点/UVモーフが無いモデルは最小の2要素
		std::unique_ptr<VulkanBuffer> materials; // 材質ごとのvec4(拡散色rgba。材質モーフ込み)
		uint64_t morphVersion = 0;
		uint64_t materialVersion = 0;
		VkDescriptorSet set = VK_NULL_HANDLE;
		uint64_t version = 0;
	};

	std::shared_ptr<const model::ModelData> data_;
	std::vector<Part> parts_;
	std::unique_ptr<model::Skeleton> skeleton_;
	std::shared_ptr<VulkanBonePool> bonePool_;
	uint64_t poseVersion_ = 1; // 枠のversion(初期値0)と違う値にして、最初のprepareSlotで必ず書く
	std::vector<Slot> slots_;
	std::unique_ptr<model::Physics> physics_;
	std::unique_ptr<model::SpringBones> springBones_; // VRMのスプリングボーン(物理演算とは同時に使わない)
	std::unique_ptr<model::MorphSet> morphs_;
	std::vector<float> morphDeltas_; // 頂点数*MorphSet::kVertexStride(updatePose()で更新)
	std::vector<float> materialColors_; // 材質数*4(拡散色rgba。材質モーフ込み)
	std::vector<float> materialBuffer_; // 材質数*kMaterialStride(GPUへ送る材質のデータ。拡散色 + MToonのパラメータ)
	bool hasMToon_ = false;
	void rebuildMaterialBuffer();
	void updatePoseBounds();
	std::vector<std::array<float, 6>> boneBounds_; // ボーンごとの休止ポーズの頂点範囲(min xyz, max xyz。元の座標系)。頂点が無ければmin>max
	std::array<float, 6> unweightedBounds_{}; // ボーンに属さない頂点の範囲(動かない)
	bool hasUnweighted_ = false;
	float poseBoundsMin_[3] = {0.0f, 0.0f, 0.0f};
	float poseBoundsMax_[3] = {0.0f, 0.0f, 0.0f};
	bool hasPoseBounds_ = false;
	uint64_t morphVersion_ = 1;
	uint64_t materialVersion_ = 1;
	std::vector<float> scratch_;
};

#endif // VULKANMODEL_H_
