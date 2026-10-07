#include "vk/VulkanModel.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>

// res/shaders/skin.compが想定している並び(float 20個 -> 11個)
static_assert(sizeof(vk_::SkinnedVertex) == sizeof(float) * 20 && sizeof(vk_::Vertex) == sizeof(float) * 11);

std::shared_ptr<VulkanModel> VulkanModel::create(const std::shared_ptr<VulkanContext> &ctx,
	const std::shared_ptr<VulkanBonePool> &bonePool, const std::shared_ptr<VulkanTexturePool> &texturePool,
	const std::shared_ptr<const model::ModelData> &data,
	const TextureLoader &loadTexture, int frameSlots, const EmbeddedTextureLoader &loadEmbeddedTexture)
{
	std::shared_ptr<VulkanModel> result(new VulkanModel());
	result->data_ = data;
	// テクスチャは同じ画像・同じアルファの扱いなら共有する(添字*2 + アルファを無視するか)
	std::vector<std::shared_ptr<VulkanTexture>> textures(data->texturePaths.size() * 2);
	std::vector<bool> textureTried(data->texturePaths.size() * 2, false);
	// 画像の番号からテクスチャを作る(同じ画像・同じアルファの扱いなら共有する)
	const auto textureOf = [&](int imageIndex, bool ignoreAlpha) -> std::shared_ptr<VulkanTexture> {
		if(imageIndex < 0 || static_cast<size_t>(imageIndex) >= data->texturePaths.size()){
			return nullptr;
		}
		const size_t image = static_cast<size_t>(imageIndex);
		const size_t t = image * 2 + (ignoreAlpha ? 1 : 0);
		if(!textureTried[t]){
			textureTried[t] = true;
			if(image < data->embeddedImages.size() && !data->embeddedImages[image].empty()){
				if(loadEmbeddedTexture){
					textures[t] = loadEmbeddedTexture(data->embeddedImages[image], ignoreAlpha);
				}
			}
			else{
				textures[t] = loadTexture(data->texturePaths[image]);
			}
		}
		return textures[t];
	};
	// 頂点モーフがあるか(あるときだけ、頂点に元の番号を持たせて、移動量のバッファを全頂点分作る)
	bool anyVertexMorph = false;
	size_t typeCount[11] = {};
	for(const auto &morph : data->morphs){
		++typeCount[static_cast<size_t>(morph.type)];
		anyVertexMorph = anyVertexMorph || (morph.type == model::ModelMorph::Type::Vertex && !morph.vertexOffsets.empty())
			|| (morph.type == model::ModelMorph::Type::Uv && !morph.uvOffsets.empty());
	}

	if(!data->morphs.empty()){
		SDL_Log("Model morphs: %zu (group %zu, vertex %zu, uv %zu, material %zu, bone %zu, other %zu)", data->morphs.size(),
			typeCount[0], typeCount[1], typeCount[3], typeCount[8], typeCount[2], data->morphs.size() - typeCount[0] - typeCount[1] - typeCount[3] - typeCount[8] - typeCount[2]);
	}

	// スキニング用: 部品の頂点を、1つのstorage bufferの上限に収まるグループにまとめる(普通は全部で1グループ)
	VkPhysicalDeviceProperties deviceProps;
	vkGetPhysicalDeviceProperties(ctx->physicalDevice(), &deviceProps);
	const uint64_t maxGroupVertices = std::max<uint64_t>(deviceProps.limits.maxStorageBufferRange / sizeof(vk_::SkinnedVertex), 1);
	std::vector<std::vector<vk_::SkinnedVertex>> groupVertices;

	for(size_t materialIndex = 0; materialIndex < data->materials.size(); ++materialIndex){
		const auto &material = data->materials[materialIndex];
		if(material.indexCount == 0){
			continue;
		}
		Part part;
		part.materialIndex = static_cast<uint32_t>(materialIndex);
		std::vector<uint32_t> sourceVertices; // メッシュの頂点i → ModelData::verticesの番号
		// この材質の三角形が使う頂点だけを、詰め直して1つのメッシュにする(頂点は材質をまたいで共有されることがあるので複製する)
		std::unordered_map<uint32_t, uint32_t> remap;
		std::vector<uint32_t> indices;
		indices.reserve(material.indexCount);
		// 三角形の頂点順は、Z反転(鏡像)で見かけの向きが逆になる(元の時計回り→時計回りのまま)ので、2頂点を入れ替えて
		// このエンジンの表面(反時計回り)にそろえる。これを怠ると外向きの面が裏面扱いになり、法線が反転して暗くなる
		for(uint32_t i = 0; i < material.indexCount; ++i){
			const uint32_t triangle = i - i % 3;
			const uint32_t corner = i % 3 == 0 ? 0 : (i % 3 == 1 ? 2 : 1);
			const uint32_t original = data->indices[material.firstIndex + triangle + corner];
			const auto found = remap.find(original);
			if(found != remap.end()){
				indices.push_back(found->second);
				continue;
			}
			const uint32_t index = static_cast<uint32_t>(sourceVertices.size());
			remap.emplace(original, index);
			sourceVertices.push_back(original);
			indices.push_back(index);
		}

		// 頂点は休止ポーズのまま、元の座標系で持つ(スキニングとZ反転は頂点シェーダーが行う)。
		// ボーンの無いモデルは、材質の拡散色を頂点色に焼き込む(テクスチャの色に掛けられる)
		const bool skinned = !data->bones.empty();
		const bool useMorph = anyVertexMorph && skinned;
		std::vector<vk_::SkinnedVertex> vertices(sourceVertices.size());
		for(size_t i = 0; i < vertices.size(); ++i){
			const auto &src = data->vertices[sourceVertices[i]];
			vk_::SkinnedVertex &v = vertices[i];
			v = {};
			for(int k = 0; k < 3; ++k){
				v.base.position[k] = src.position[k];
				v.base.normal[k] = src.normal[k];
				v.base.color[k] = skinned ? 1.0f : material.diffuse[k]; // スキニングのモデルは、拡散色を材質ごとのバッファから受け取る(材質モーフで変わる)
			}
			v.base.uv[0] = src.uv[0];
			v.base.uv[1] = src.uv[1];
			v.source = useMorph ? static_cast<int32_t>(sourceVertices[i]) : -1;
			for(int k = 0; k < 4; ++k){
				v.bones[k] = src.weights[k] > 0.0f ? src.bones[k] : -1;
				v.weights[k] = src.weights[k];
			}
		}
		if(skinned){
			if(vertices.size() > maxGroupVertices){
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Model part has too many vertices for skinning. (%s: %zu)", material.name.c_str(), vertices.size());
				return nullptr;
			}
			if(groupVertices.empty() || groupVertices.back().size() + vertices.size() > maxGroupVertices){
				groupVertices.emplace_back();
			}
			part.skinGroup = static_cast<uint32_t>(groupVertices.size() - 1);
			part.skinFirstVertex = static_cast<uint32_t>(groupVertices.back().size());
			groupVertices.back().insert(groupVertices.back().end(), vertices.begin(), vertices.end());
			part.mesh = std::make_shared<VulkanMesh>(ctx, vertices.data(), static_cast<uint32_t>(sizeof(vk_::SkinnedVertex)),
				static_cast<uint32_t>(vertices.size()), indices.data(), static_cast<uint32_t>(indices.size()), true);
		}
		else{
			// ボーンの無いモデルは普通の頂点で作る。Z反転はここで済ませる
			std::vector<vk_::Vertex> plain(vertices.size());
			for(size_t i = 0; i < plain.size(); ++i){
				plain[i] = vertices[i].base;
				plain[i].position[2] = -plain[i].position[2];
				plain[i].normal[2] = -plain[i].normal[2];
			}
			part.mesh = std::make_shared<VulkanMesh>(ctx, plain.data(), static_cast<uint32_t>(plain.size()),
				indices.data(), static_cast<uint32_t>(indices.size()));
		}
		if(!part.mesh->isValid()){
			SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Model mesh creation failed. (%s)", material.name.c_str());
			return nullptr;
		}

		if(material.texture >= 0){
			part.material.texture = textureOf(material.texture, material.ignoreTextureAlpha); // 読めなかったらnullptr(既定の白テクスチャ=拡散色のみ)
			// MToonで影の色・発光のテクスチャがあれば、基本の色と合わせて1組のdescriptor setにする(描画はそれをバインドする)
			if(material.mtoon.enabled && (material.mtoon.shadeTexture >= 0 || material.mtoon.emissiveTexture >= 0) && texturePool && part.material.texture){
				part.textureSet = VulkanTextureSet::create(texturePool, part.material.texture,
					material.mtoon.shadeTexture >= 0 ? textureOf(material.mtoon.shadeTexture, true) : nullptr,
					material.mtoon.emissiveTexture >= 0 ? textureOf(material.mtoon.emissiveTexture, true) : nullptr);
			}
		}
		part.material.specular = material.specular * 0.5f;
		part.material.shininess = material.shininess;
		part.material.alpha = material.diffuse[3];
		part.material.alphaCutoff = material.alphaMask ? std::max(material.alphaCutoff, 0.01f) : 0.0f;
		part.material.castShadow = material.diffuse[3] > 0.5f; // 半透明の材質は影を落とさない
		part.material.receiveShadow = true;
		result->parts_.push_back(std::move(part));
	}

	{
		size_t sets = 0;
		for(const auto &part : result->parts_){ sets += part.textureSet ? 1 : 0; }
		if(sets > 0){
			SDL_Log("Model texture sets (MToon shade/emissive): %zu of %zu parts", sets, result->parts_.size());
		}
	}

	if(!data->bones.empty()){
		if(!bonePool){
			return nullptr;
		}
		result->skeleton_ = std::make_unique<model::Skeleton>(data->bones);
		result->physics_ = model::Physics::create(*data, *result->skeleton_);
		if(!result->physics_){
			result->springBones_ = model::SpringBones::create(*data, *result->skeleton_);
		}
		result->bonePool_ = bonePool;
		// フラスタムカリング用: 各頂点を、いちばん重みの大きいボーンの範囲へ入れる
		{
			constexpr float kInf = 1e30f;
			result->boneBounds_.assign(data->bones.size(), {kInf, kInf, kInf, -kInf, -kInf, -kInf});
			result->unweightedBounds_ = {kInf, kInf, kInf, -kInf, -kInf, -kInf};
			for(const auto &v : data->vertices){
				int best = -1;
				float bestWeight = 0.0f;
				for(int k = 0; k < 4; ++k){
					if(v.weights[k] > bestWeight && v.bones[k] >= 0 && v.bones[k] < static_cast<int>(data->bones.size())){
						best = v.bones[k];
						bestWeight = v.weights[k];
					}
				}
				auto &box = best >= 0 ? result->boneBounds_[static_cast<size_t>(best)] : result->unweightedBounds_;
				result->hasUnweighted_ = result->hasUnweighted_ || best < 0;
				for(int k = 0; k < 3; ++k){
					box[k] = std::min(box[k], v.position[k]);
					box[3 + k] = std::max(box[3 + k], v.position[k]);
				}
			}
			result->updatePoseBounds();
		}
		// フレーム枠ごとのボーン行列(storage buffer)とdescriptor set
		if(!data->morphs.empty()){
			result->morphs_ = std::make_unique<model::MorphSet>(data->morphs, data->vertices.size());
		}
		const bool hasMorph = anyVertexMorph;
		if(hasMorph){
			result->morphDeltas_.assign(data->vertices.size() * model::MorphSet::kVertexStride, 0.0f); // 最初のupdatePose()までは移動量なし
		}
		const VkDeviceSize size = sizeof(float) * 12 * data->bones.size();
		const VkDeviceSize morphSize = sizeof(float) * model::MorphSet::kVertexStride * (hasMorph ? data->vertices.size() : 1);
		const VkDeviceSize materialsSize = sizeof(float) * kMaterialStride * std::max<size_t>(data->materials.size(), 1);
		result->materialColors_.assign(std::max<size_t>(data->materials.size(), 1) * 4, 1.0f);
		for(size_t m = 0; m < data->materials.size(); ++m){
			for(int k = 0; k < 4; ++k){
				result->materialColors_[m * 4 + k] = data->materials[m].diffuse[k];
			}
			result->hasMToon_ = result->hasMToon_ || data->materials[m].mtoon.enabled;
		}
		result->rebuildMaterialBuffer();
		// グループごとの入力(休止ポーズの頂点)。全フレーム枠で共有する
		for(const auto &vertices : groupVertices){
			SkinGroup group;
			group.vertexCount = static_cast<uint32_t>(vertices.size());
			group.in = std::make_unique<VulkanBuffer>(ctx, sizeof(vk_::SkinnedVertex) * vertices.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
			if(!group.in->isValid() || !group.in->write(vertices.data(), sizeof(vk_::SkinnedVertex) * vertices.size())){
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Model skinning input buffer creation failed.");
				return nullptr;
			}
			result->skinGroups_.push_back(std::move(group));
		}
		result->slots_.resize(frameSlots);
		for(auto &slot : result->slots_){
			slot.bones = std::make_unique<VulkanBuffer>(ctx, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
			slot.morph = std::make_unique<VulkanBuffer>(ctx, morphSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
			slot.materials = std::make_unique<VulkanBuffer>(ctx, materialsSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
			slot.set = bonePool->allocate();
			if(!slot.bones->isValid() || !slot.morph->isValid() || !slot.materials->isValid() || !slot.set){
				SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Model bone buffer creation failed.");
				return nullptr;
			}
			const std::vector<float> zero(model::MorphSet::kVertexStride, 0.0f); // モーフ無しのときの1要素(シェーダーは頂点番号-1なら読まない)
			if(!hasMorph){
				slot.morph->write(zero.data(), sizeof(float) * zero.size());
			}
			const VkDescriptorBufferInfo infos[3] = {{slot.bones->get(), 0, size}, {slot.morph->get(), 0, morphSize}, {slot.materials->get(), 0, materialsSize}};
			VkWriteDescriptorSet writes[3]{};
			for(uint32_t i = 0; i < 3; ++i){
				writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet = slot.set;
				writes[i].dstBinding = i;
				writes[i].descriptorCount = 1;
				writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				writes[i].pBufferInfo = &infos[i];
			}
			vkUpdateDescriptorSets(ctx->device(), 3, writes, 0, nullptr);

			// グループごとの、スキニング済みの頂点の出力先と、コンピュートのdescriptor set(入力=グループの頂点、出力=このバッファ)
			for(size_t g = 0; g < groupVertices.size(); ++g){
				SkinGroupSlot groupSlot;
				const VkDeviceSize inSize = sizeof(vk_::SkinnedVertex) * groupVertices[g].size();
				const VkDeviceSize outSize = sizeof(vk_::Vertex) * groupVertices[g].size();
				groupSlot.out = std::make_unique<VulkanBuffer>(ctx, outSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true); // GPUだけが書いて読む
				groupSlot.set = bonePool->allocatePart();
				if(!groupSlot.out->isValid() || !groupSlot.set){
					SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Model skinning buffer creation failed.");
					if(groupSlot.set){
						bonePool->freePart(groupSlot.set);
					}
					return nullptr;
				}
				const VkDescriptorBufferInfo groupInfos[2] = {{result->skinGroups_[g].in->get(), 0, inSize}, {groupSlot.out->get(), 0, outSize}};
				VkWriteDescriptorSet groupWrites[2]{};
				for(uint32_t i = 0; i < 2; ++i){
					groupWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					groupWrites[i].dstSet = groupSlot.set;
					groupWrites[i].dstBinding = i;
					groupWrites[i].descriptorCount = 1;
					groupWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					groupWrites[i].pBufferInfo = &groupInfos[i];
				}
				vkUpdateDescriptorSets(ctx->device(), 2, groupWrites, 0, nullptr);
				slot.groups.push_back(std::move(groupSlot));
			}
		}
	}
	return result;
}

VulkanModel::~VulkanModel()
{
	if(bonePool_){
		for(auto &slot : slots_){
			for(auto &group : slot.groups){
				bonePool_->freePart(group.set);
			}
			bonePool_->free(slot.set);
		}
	}
}

// 材質ごとのvec4を7つ: [0]拡散色rgba、[1]影の色rgb+影の境目のずらし、[2]影の境目のぼかし/GI均一化/リムの混ぜ具合/リムの鋭さ、
// [3]リムの色rgb+持ち上げ、[4]輪郭線の色rgb+太さ、[5](輪郭線の種類, MToonか, 輪郭線の光の混ぜ具合, テクスチャのフラグ)、[6]発光rgb+0
void VulkanModel::rebuildMaterialBuffer()
{
	const size_t count = std::max<size_t>(data_->materials.size(), 1);
	materialBuffer_.assign(count * kMaterialStride, 0.0f);
	for(size_t m = 0; m < data_->materials.size(); ++m){
		float *p = &materialBuffer_[m * kMaterialStride];
		const model::ModelMaterial::MToon &t = data_->materials[m].mtoon;
		for(int k = 0; k < 4; ++k){ p[k] = materialColors_[m * 4 + k]; }
		p[4] = t.shadeColor[0]; p[5] = t.shadeColor[1]; p[6] = t.shadeColor[2]; p[7] = t.shadingShift;
		p[8] = t.shadingToony; p[9] = t.giEqualization; p[10] = t.rimLightingMix; p[11] = t.rimFresnelPower;
		p[12] = t.rimColor[0]; p[13] = t.rimColor[1]; p[14] = t.rimColor[2]; p[15] = t.rimLift;
		p[16] = t.outlineColor[0]; p[17] = t.outlineColor[1]; p[18] = t.outlineColor[2]; p[19] = t.outlineWidth;
		p[20] = static_cast<float>(t.outlineMode); p[21] = t.enabled ? 1.0f : 0.0f; p[22] = t.outlineLightingMix;
		p[23] = (t.shadeTexture >= 0 ? 1.0f : 0.0f) + (t.emissiveTexture >= 0 ? 2.0f : 0.0f) + (t.skin ? 4.0f : 0.0f) + 0.0f; // 顔だけの補正(MToon::faceSkin)は、顔に影がかかったように見えたので使わない(体の肌と同じ補正にそろえる) // 影の色・発光のテクスチャを持つか(ビット0, 1)、肌の材質か(ビット2)、顔の肌か(ビット3)
		p[24] = t.emissive[0]; p[25] = t.emissive[1]; p[26] = t.emissive[2];
		p[27] = data_->materials[m].alphaMask ? std::max(data_->materials[m].alphaCutoff, 0.01f) : 0.05f; // 輪郭線のアルファの切り抜きのしきい値(本体の切り抜きと同じにする)
	}
	if(data_->materials.empty()){
		for(int k = 0; k < 4; ++k){ materialBuffer_[k] = 1.0f; }
	}
}

bool VulkanModel::hipsPosition(float out[3]) const
{
	if(!skeleton_){
		return false;
	}
	for(const auto &entry : data_->humanoidBones){
		if(entry.first == "hips" && entry.second >= 0 && static_cast<size_t>(entry.second) < skeleton_->boneCount()){
			const model::Vec3 p = skeleton_->globalPosition(entry.second);
			out[0] = p.x;
			out[1] = p.y;
			out[2] = -p.z; // 元の座標系(左手系)→右手系(描画のスキニングと同じZ反転)
			return true;
		}
	}
	return false;
}

void VulkanModel::resetPhysics()
{
	if(physics_ && skeleton_){
		physics_->reset(*skeleton_);
	}
	if(springBones_ && skeleton_){
		springBones_->reset(*skeleton_);
	}
}

void VulkanModel::updatePose(float physicsDt)
{
	if(skeleton_){
		skeleton_->update();
		if(physics_ && physicsDt > 0.0f){
			physics_->step(*skeleton_, physicsDt); // 揺れるボーンの姿勢を書き換え、スキニング行列も作り直す
		}
		else if(springBones_){
			springBones_->step(*skeleton_, physicsDt); // 0なら状態は進めず、現在の姿勢に合わせるだけ
		}
		++poseVersion_;
		updatePoseBounds();
	}
	if(morphs_ && morphs_->refresh()){
		if(morphs_->hasVertexMorph()){
			morphs_->computeVertexDeltas(morphDeltas_);
			++morphVersion_;
		}
		if(morphs_->hasMaterialMorph()){
			morphs_->computeMaterialColors(data_->materials, materialColors_);
			rebuildMaterialBuffer();
			++materialVersion_;
		}
	}
}

// 休止ポーズのボーンごとの範囲(8隅)を現在のスキニング行列で動かして足し合わせ、Z反転(右手系)して余裕を足す
void VulkanModel::updatePoseBounds()
{
	if(!skeleton_){
		return;
	}
	const auto &skin = skeleton_->skinMatrices();
	float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
	const auto add = [&](const float p[3]){
		const float q[3] = {p[0], p[1], -p[2]};
		for(int k = 0; k < 3; ++k){
			lo[k] = std::min(lo[k], q[k]);
			hi[k] = std::max(hi[k], q[k]);
		}
	};
	const auto addBox = [&](const std::array<float, 6> &box, const model::Mat34 *m){
		if(box[0] > box[3]){
			return; // 頂点が無い
		}
		for(int corner = 0; corner < 8; ++corner){
			const float p[3] = {box[(corner & 1) ? 3 : 0], box[(corner & 2) ? 4 : 1], box[(corner & 4) ? 5 : 2]};
			if(m){
				float out[3];
				for(int row = 0; row < 3; ++row){
					out[row] = m->r[row][0] * p[0] + m->r[row][1] * p[1] + m->r[row][2] * p[2] + m->t[row];
				}
				add(out);
			}
			else{
				add(p);
			}
		}
	};
	for(size_t b = 0; b < boneBounds_.size(); ++b){
		addBox(boneBounds_[b], b < skin.size() ? &skin[b] : nullptr);
	}
	if(hasUnweighted_){
		addBox(unweightedBounds_, nullptr);
	}
	if(lo[0] > hi[0]){
		return;
	}
	// 頂点モーフ・物理演算の揺れなど、境界に入らない動きの余裕(大きさの10%)
	for(int k = 0; k < 3; ++k){
		const float margin = (hi[k] - lo[k]) * 0.1f + 0.05f;
		poseBoundsMin_[k] = lo[k] - margin;
		poseBoundsMax_[k] = hi[k] + margin;
	}
	hasPoseBounds_ = true;
}

void VulkanModel::prepareSlot(int slot)
{
	if(!skeleton_ || slot < 0 || slot >= static_cast<int>(slots_.size())){
		return;
	}
	if(slots_[slot].morphVersion != morphVersion_){
		if(!morphDeltas_.empty()){
			slots_[slot].morph->write(morphDeltas_.data(), sizeof(float) * morphDeltas_.size());
		}
		slots_[slot].morphVersion = morphVersion_;
		slots_[slot].skinDirty = true;
	}
	if(slots_[slot].materialVersion != materialVersion_){
		slots_[slot].materials->write(materialBuffer_.data(), sizeof(float) * materialBuffer_.size());
		slots_[slot].materialVersion = materialVersion_;
	}
	if(slots_[slot].version == poseVersion_){
		return;
	}
	// スキニング行列を、ボーンごとに行優先の3行(vec4)に詰めて書く
	const auto &skin = skeleton_->skinMatrices();
	scratch_.resize(skin.size() * 12);
	for(size_t b = 0; b < skin.size(); ++b){
		for(int row = 0; row < 3; ++row){
			float *out = &scratch_[b * 12 + row * 4];
			for(int col = 0; col < 3; ++col){
				out[col] = skin[b].r[row][col];
			}
			out[3] = skin[b].t[row];
		}
	}
	slots_[slot].bones->write(scratch_.data(), sizeof(float) * scratch_.size());
	slots_[slot].version = poseVersion_;
	slots_[slot].skinDirty = true;
}
