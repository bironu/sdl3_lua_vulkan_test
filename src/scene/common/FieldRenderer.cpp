#include "scene/common/FieldRenderer.h"
#include "geo/Calculator.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLVulkanWindow.h"
#include "vk/Vertex.h"
#include <algorithm>

namespace game
{

FieldRenderer::FieldRenderer(SDL_::VulkanWindow &window, const field::FieldMap &map, const std::vector<field::TileDef> &tiles)
	: window_(window)
	, tiles_(tiles)
{
	materials_.reserve(tiles_.size());
	for(const auto &tile : tiles_){
		auto image = std::make_shared<SDL_::Image>(1, 1);
		image->fillRect(SDL_::Color(tile.r, tile.g, tile.b, 255));
		VulkanMaterial material;
		material.texture = window.createTexture(image);
		material.specular = 0.0f;
		material.castShadow = false;
		material.receiveShadow = false;
		materials_.push_back(material);
	}
	chunksX_ = (map.width() + kChunkSize - 1) / kChunkSize;
	chunksZ_ = (map.depth() + kChunkSize - 1) / kChunkSize;
	meshes_.assign(static_cast<size_t>(chunksX_) * chunksZ_, std::vector<std::shared_ptr<VulkanMesh>>(tiles_.size()));
	dirty_.assign(meshes_.size(), true);
	update(map);
}

void FieldRenderer::invalidate(int x0, int z0, int x1, int z1)
{
	// 頂点の法線は隣のマスの高さまで使うので、1マス広げる
	const int cx0 = std::max(x0 - 1, 0) / kChunkSize, cz0 = std::max(z0 - 1, 0) / kChunkSize;
	const int cx1 = std::min((x1 + 1) / kChunkSize, chunksX_ - 1), cz1 = std::min((z1 + 1) / kChunkSize, chunksZ_ - 1);
	for(int cz = cz0; cz <= cz1; ++cz){
		for(int cx = cx0; cx <= cx1; ++cx){
			dirty_[chunkIndex(cx, cz)] = true;
		}
	}
}

void FieldRenderer::update(const field::FieldMap &map)
{
	for(int cz = 0; cz < chunksZ_; ++cz){
		for(int cx = 0; cx < chunksX_; ++cx){
			const size_t idx = chunkIndex(cx, cz);
			if(dirty_[idx]){
				build(map, cx, cz);
				dirty_[idx] = false;
			}
		}
	}
}

void FieldRenderer::build(const field::FieldMap &map, int chunkX, int chunkZ)
{
	const float cell = map.cellSize();
	const int x0 = chunkX * kChunkSize, z0 = chunkZ * kChunkSize;
	const int x1 = std::min(x0 + kChunkSize, map.width()), z1 = std::min(z0 + kChunkSize, map.depth());
	auto &chunk = meshes_[chunkIndex(chunkX, chunkZ)];
	for(int type = 0; type < static_cast<int>(tiles_.size()); ++type){
		std::vector<vk_::Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve((x1 - x0) * (z1 - z0) * 4);
		indices.reserve((x1 - x0) * (z1 - z0) * 6);
		for(int z = z0; z < z1; ++z){
			for(int x = x0; x < x1; ++x){
				if(map.get(x, z) != type){
					continue;
				}
				const uint32_t base = static_cast<uint32_t>(vertices.size());
				// 頂点の順: (x,z) (x,z+1) (x+1,z+1) (x+1,z)。+Y側から見て反時計回り(上向きの面)。対角線は (x,z)-(x+1,z+1)(FieldMap::heightAtと同じ)
				const int vx[4] = {x, x, x + 1, x + 1}, vz[4] = {z, z + 1, z + 1, z};
				const float u[4] = {0.0f, 0.0f, 1.0f, 1.0f}, v[4] = {0.0f, 1.0f, 1.0f, 0.0f};
				for(int k = 0; k < 4; ++k){
					vk_::Vertex vertex{};
					vertex.position[0] = vx[k] * cell;
					vertex.position[1] = map.vertexHeight(vx[k], vz[k]);
					vertex.position[2] = vz[k] * cell;
					vertex.color[0] = vertex.color[1] = vertex.color[2] = 1.0f;
					vertex.uv[0] = u[k];
					vertex.uv[1] = v[k];
					map.vertexNormal(vx[k], vz[k], vertex.normal);
					vertices.push_back(vertex);
				}
				for(uint32_t i : {0u, 1u, 2u, 2u, 3u, 0u}){
					indices.push_back(base + i);
				}
			}
		}
		if(vertices.empty()){
			chunk[static_cast<size_t>(type)] = nullptr;
			continue;
		}
		chunk[static_cast<size_t>(type)] = std::make_shared<VulkanMesh>(window_.getContext(), vertices.data(), static_cast<uint32_t>(vertices.size()),
			indices.data(), static_cast<uint32_t>(indices.size()));
	}
}

void FieldRenderer::draw(const geo::Matrix4x4f &viewProj) const
{
	const auto identity = geo::createIdentityMatrix4x4<float>();
	for(const auto &chunk : meshes_){
		for(size_t type = 0; type < chunk.size(); ++type){
			if(chunk[type]){
				window_.draw(chunk[type], viewProj, identity, materials_[type]);
			}
		}
	}
}

} // namespace game
