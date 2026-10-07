#if !defined(MODEL_MORPHSET_H_)
#define MODEL_MORPHSET_H_

#include "model/ModelData.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace model
{

// モデルのモーフの現在の重み(0〜1)と、そこから頂点ごとの移動量・材質の色を求める処理。
// 使い方: resetWeights() → setWeight() → refresh() → (変わっていれば) computeVertexDeltas()/computeMaterialColors()。
// グループモーフは他のモーフの重みに割合を掛けて足す
class MorphSet
{
public:
	// 頂点ごとに書き出すfloatの数: 位置の移動量(x,y,z,0) + UVの移動量(u,v,0,0)
	static constexpr size_t kVertexStride = 8;

	MorphSet(const std::vector<ModelMorph> &morphs, size_t vertexCount);

	size_t count() const { return morphs_.size(); }
	// 名前からモーフの番号(無ければ-1)
	int findMorph(const std::string &name) const;
	// 頂点ごとに動かすモーフ(頂点モーフ・UVモーフ)を1つでも持つか(GPUに頂点の移動量を送る必要があるか)
	bool hasVertexMorph() const { return hasVertexMorph_; }
	// 材質モーフを1つでも持つか
	bool hasMaterialMorph() const { return hasMaterialMorph_; }

	void resetWeights();
	void setWeight(int morph, float weight);
	float weight(int morph) const { return weights_[morph]; }

	// 前回のrefresh()から重みが変わっていれば、実効的な重み(グループの割合を含む)を求め直してtrueを返す。
	// resetWeights()してsetWeight()し直しても同じ重みなら、falseを返す
	bool refresh();

	// refresh()が真のときに呼ぶ。全頂点の移動量を out に書く(頂点数*kVertexStride個)。
	// 動かす頂点だけを触り、前回触った頂点は0に戻す
	void computeVertexDeltas(std::vector<float> &out);
	// 材質ごとの拡散色(rgba)を、モーフを当てた結果で out に書く(材質数*4個)。baseは元の材質
	void computeMaterialColors(const std::vector<ModelMaterial> &base, std::vector<float> &out) const;

private:
	// morphの実効的な重み(グループの割合を含む)を、頂点/UV/材質モーフごとの重みeffective_へ足す
	void accumulate(int morph, float weight, int depth);

	std::vector<ModelMorph> morphs_;
	std::unordered_map<std::string, int> nameToIndex_;
	std::vector<float> weights_;
	std::vector<float> effective_;       // モーフごとの実効的な重み
	std::vector<uint32_t> touched_;      // 前回の出力で0以外にした頂点
	std::vector<float> computedWeights_; // 前回のrefresh()の時の重み
	size_t vertexCount_;
	bool hasVertexMorph_ = false;
	bool hasMaterialMorph_ = false;
	bool computed_ = false;
};

} // namespace model

#endif // MODEL_MORPHSET_H_
