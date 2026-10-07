#include "scene/common/FieldRenderer.h"
#include "geo/Calculator.h"
#include "sdl/SDLImage.h"
#include "sdl/SDLVulkanWindow.h"
#include "vk/Vertex.h"

namespace game
{

FieldRenderer::FieldRenderer(SDL_::VulkanWindow &window, const field::FieldMap &map, const std::vector<field::TileDef> &tiles)
{
	const float cell = map.cellSize();
	for(size_t type = 0; type < tiles.size(); ++type){
		std::vector<vk_::Vertex> vertices;
		std::vector<uint32_t> indices;
		for(int z = 0; z < map.depth(); ++z){
			for(int x = 0; x < map.width(); ++x){
				if(map.get(x, z) != type){
					continue;
				}
				const float x0 = x * cell, x1 = (x + 1) * cell, z0 = z * cell, z1 = (z + 1) * cell;
				const uint32_t base = static_cast<uint32_t>(vertices.size());
				// +Y側から見て反時計回りの頂点順(上向きの面)
				vertices.push_back({{x0, 0.0f, z0}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
				vertices.push_back({{x0, 0.0f, z1}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
				vertices.push_back({{x1, 0.0f, z1}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
				vertices.push_back({{x1, 0.0f, z0}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
				for(uint32_t i : {0u, 1u, 2u, 2u, 3u, 0u}){
					indices.push_back(base + i);
				}
			}
		}
		if(vertices.empty()){
			continue;
		}
		auto image = std::make_shared<SDL_::Image>(1, 1);
		image->fillRect(SDL_::Color(tiles[type].r, tiles[type].g, tiles[type].b, 255));
		Layer layer;
		layer.mesh = std::make_shared<VulkanMesh>(window.getContext(), vertices.data(), static_cast<uint32_t>(vertices.size()),
			indices.data(), static_cast<uint32_t>(indices.size()));
		layer.material.texture = window.createTexture(image);
		layer.material.specular = 0.0f;
		layer.material.castShadow = false;
		layer.material.receiveShadow = false;
		layers_.push_back(std::move(layer));
	}
}

void FieldRenderer::draw(SDL_::VulkanWindow &window, const geo::Matrix4x4f &viewProj) const
{
	const auto identity = geo::createIdentityMatrix4x4<float>();
	for(const auto &layer : layers_){
		window.draw(layer.mesh, viewProj, identity, layer.material);
	}
}

} // namespace game
