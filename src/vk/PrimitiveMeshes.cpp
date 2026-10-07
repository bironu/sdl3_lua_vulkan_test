#include "vk/PrimitiveMeshes.h"
#include "vk/Vertex.h"
#include <cstdint>
#include <vector>

namespace vk_
{

std::shared_ptr<VulkanMesh> createFloorMesh(const std::shared_ptr<VulkanContext> &ctx, float repeat)
{
	// +Y側から見て反時計回りの頂点順
	const vk_::Vertex vertices[] = {
		{{-0.5f, 0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
		{{-0.5f, 0.0f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, repeat}, {0.0f, 1.0f, 0.0f}},
		{{ 0.5f, 0.0f,  0.5f}, {1.0f, 1.0f, 1.0f}, {repeat, repeat}, {0.0f, 1.0f, 0.0f}},
		{{ 0.5f, 0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}, {repeat, 0.0f}, {0.0f, 1.0f, 0.0f}},
	};
	const uint32_t indices[] = {0, 1, 2, 2, 3, 0};
	return std::make_shared<VulkanMesh>(ctx, vertices, 4, indices, 6);
}

std::shared_ptr<VulkanMesh> createCubeMesh(const std::shared_ptr<VulkanContext> &ctx, bool lit, bool faceColors)
{
	struct Face { float origin[3]; float u[3]; float v[3]; float color[3]; };
	const Face faces[] = {
		{{-0.5f, -0.5f,  0.5f}, { 1, 0, 0}, {0, 1,  0}, {1.0f, 0.4f, 0.4f}}, // +Z
		{{ 0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {0, 1,  0}, {0.4f, 1.0f, 0.4f}}, // -Z
		{{ 0.5f, -0.5f,  0.5f}, { 0, 0,-1}, {0, 1,  0}, {0.4f, 0.4f, 1.0f}}, // +X
		{{-0.5f, -0.5f, -0.5f}, { 0, 0, 1}, {0, 1,  0}, {1.0f, 1.0f, 0.4f}}, // -X
		{{-0.5f,  0.5f,  0.5f}, { 1, 0, 0}, {0, 0, -1}, {1.0f, 0.4f, 1.0f}}, // +Y
		{{-0.5f, -0.5f, -0.5f}, { 1, 0, 0}, {0, 0,  1}, {0.4f, 1.0f, 1.0f}}, // -Y
	};
	std::vector<vk_::Vertex> vertices;
	std::vector<uint32_t> indices;
	for(const auto &f : faces){
		const uint32_t base = static_cast<uint32_t>(vertices.size());
		// 面の外向き法線 = u × v(頂点は(u,v)=(0,0),(1,0),(1,1),(0,1)の反時計回り)
		const float normal[3] = {
			f.u[1] * f.v[2] - f.u[2] * f.v[1],
			f.u[2] * f.v[0] - f.u[0] * f.v[2],
			f.u[0] * f.v[1] - f.u[1] * f.v[0],
		};
		for(int j = 0; j < 2; ++j){
			for(int i = 0; i < 2; ++i){
				// 4隅: (i,j) = (0,0),(1,0),(1,1),(0,1)の順になるよう並べる
				const int a = (j == 0) ? i : 1 - i;
				const int b = j;
				vk_::Vertex vtx{};
				for(int k = 0; k < 3; ++k){
					vtx.position[k] = f.origin[k] + a * f.u[k] + b * f.v[k];
					vtx.color[k] = faceColors ? f.color[k] : 1.0f;
					vtx.normal[k] = lit ? normal[k] : 0.0f;
				}
				vtx.uv[0] = static_cast<float>(a);
				vtx.uv[1] = static_cast<float>(b);
				vertices.push_back(vtx);
			}
		}
		for(uint32_t idx : {0u, 1u, 2u, 2u, 3u, 0u}){
			indices.push_back(base + idx);
		}
	}
	return std::make_shared<VulkanMesh>(ctx, vertices.data(), static_cast<uint32_t>(vertices.size()),
		indices.data(), static_cast<uint32_t>(indices.size()));
}

} // namespace vk_
