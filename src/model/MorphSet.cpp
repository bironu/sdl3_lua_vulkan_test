#include "model/MorphSet.h"
#include <algorithm>

namespace model
{

MorphSet::MorphSet(const std::vector<ModelMorph> &morphs, size_t vertexCount)
	: morphs_(morphs)
	, weights_(morphs.size(), 0.0f)
	, effective_(morphs.size(), 0.0f)
	, vertexCount_(vertexCount)
{
	for(size_t i = 0; i < morphs_.size(); ++i){
		nameToIndex_.emplace(morphs_[i].name, static_cast<int>(i)); // 同名があれば最初のもの
		const ModelMorph &m = morphs_[i];
		if((m.type == ModelMorph::Type::Vertex && !m.vertexOffsets.empty())
			|| (m.type == ModelMorph::Type::Uv && !m.uvOffsets.empty())){
			hasVertexMorph_ = true;
		}
		if(m.type == ModelMorph::Type::Material && !m.materialOffsets.empty()){
			hasMaterialMorph_ = true;
		}
	}
}

int MorphSet::findMorph(const std::string &name) const
{
	const auto it = nameToIndex_.find(name);
	return it == nameToIndex_.end() ? -1 : it->second;
}

void MorphSet::resetWeights()
{
	std::fill(weights_.begin(), weights_.end(), 0.0f);
}

void MorphSet::setWeight(int morph, float weight)
{
	if(morph < 0 || morph >= static_cast<int>(weights_.size())){
		return;
	}
	weights_[morph] = weight;
}

void MorphSet::accumulate(int morph, float weight, int depth)
{
	if(weight == 0.0f || morph < 0 || morph >= static_cast<int>(morphs_.size())){
		return;
	}
	const ModelMorph &m = morphs_[morph];
	if(m.type == ModelMorph::Type::Group){
		if(depth < 4){ // 循環していても止まるよう、深さを制限する
			for(const auto &g : m.groupOffsets){
				accumulate(g.morph, weight * g.rate, depth + 1);
			}
		}
	}
	else{
		effective_[morph] += weight;
	}
}

bool MorphSet::refresh()
{
	if(computed_ && computedWeights_ == weights_){
		return false;
	}
	computed_ = true;
	computedWeights_ = weights_;
	std::fill(effective_.begin(), effective_.end(), 0.0f);
	for(size_t i = 0; i < morphs_.size(); ++i){
		accumulate(static_cast<int>(i), weights_[i], 0);
	}
	return true;
}

void MorphSet::computeVertexDeltas(std::vector<float> &out)
{
	if(out.size() != vertexCount_ * kVertexStride){
		out.assign(vertexCount_ * kVertexStride, 0.0f);
		touched_.clear();
	}
	for(const uint32_t v : touched_){
		std::fill_n(&out[v * kVertexStride], kVertexStride, 0.0f);
	}
	touched_.clear();

	for(size_t i = 0; i < morphs_.size(); ++i){
		const float w = effective_[i];
		if(w == 0.0f){
			continue;
		}
		for(const auto &offset : morphs_[i].vertexOffsets){
			if(offset.vertex >= vertexCount_){
				continue;
			}
			float *p = &out[offset.vertex * kVertexStride];
			p[0] += offset.delta[0] * w;
			p[1] += offset.delta[1] * w;
			p[2] += offset.delta[2] * w;
			touched_.push_back(offset.vertex);
		}
		for(const auto &offset : morphs_[i].uvOffsets){
			if(offset.vertex >= vertexCount_){
				continue;
			}
			float *p = &out[offset.vertex * kVertexStride];
			p[4] += offset.delta[0] * w;
			p[5] += offset.delta[1] * w;
			touched_.push_back(offset.vertex);
		}
	}
}

void MorphSet::computeMaterialColors(const std::vector<ModelMaterial> &base, std::vector<float> &out) const
{
	out.resize(base.size() * 4);
	for(size_t m = 0; m < base.size(); ++m){
		for(int k = 0; k < 4; ++k){
			out[m * 4 + k] = base[m].diffuse[k];
		}
	}
	// 乗算は、重み0で変化なし・1で指定の倍率になるよう補間する。加算は重みを掛けて足す
	for(size_t i = 0; i < morphs_.size(); ++i){
		const float w = effective_[i];
		if(w == 0.0f || morphs_[i].type != ModelMorph::Type::Material){
			continue;
		}
		for(const auto &offset : morphs_[i].materialOffsets){
			const size_t first = offset.material < 0 ? 0 : static_cast<size_t>(offset.material);
			const size_t last = offset.material < 0 ? base.size() : first + 1;
			for(size_t m = first; m < last && m < base.size(); ++m){
				for(int k = 0; k < 4; ++k){
					float &v = out[m * 4 + k];
					v = offset.add ? v + offset.diffuse[k] * w : v * (1.0f + (offset.diffuse[k] - 1.0f) * w);
				}
			}
		}
	}
	for(float &v : out){
		v = std::clamp(v, 0.0f, 1.0f);
	}
}

} // namespace model
