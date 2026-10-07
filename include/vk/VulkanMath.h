#if !defined(VK_VULKANMATH_H_)
#define VK_VULKANMATH_H_

#include "geo/Matrix.h"
#include "geo/Calculator.h"
#include <array>
#include <cmath>

// geo::Matrix4x4f(列優先格納・列ベクトル規約: v' = M * v)をVulkanで使うための補助。
// Matrix::data()のメモリ並びはGLSLのmat4とそのまま一致する。
// geo::createPerspective/createOrthoはOpenGL流儀(クリップ空間のzが-1〜1、Yが上向き)なので、
// Vulkan(zが0〜1、Yが下向き)では、下の vk_::createPerspective/createOrtho を使う。
// ワールド/ビューは右手系(geo::createLookAtのまま、カメラは-Zを見る)で、変わらない:
//   mvp = vk_::createPerspective(...) * view * model
namespace vk_
{

// 右手系のビュー空間(-Zを見る)から、Vulkanのクリップ空間(y下向き、z: near→0, far→1)への透視投影。
// geo::createPerspectiveと同じ引数で、クリップ空間の違いを最初から含む
inline geo::Matrix4x4f createPerspective(float fovY, float width, float height, float nearZ, float farZ)
{
	const float yScale = 1.0f / std::tan(fovY * 0.5f);
	const float xScale = yScale * height / width;
	const float range = nearZ - farZ;
	const std::array<float, 16> data = {
		xScale, 0.0f,    0.0f,                  0.0f, // 列0
		0.0f,   -yScale, 0.0f,                  0.0f, // 列1
		0.0f,   0.0f,    farZ / range,          -1.0f, // 列2
		0.0f,   0.0f,    nearZ * farZ / range,  0.0f, // 列3
	};
	return geo::Matrix4x4f(data);
}

// 同、平行投影。geo::createOrthoと同じ引数
inline geo::Matrix4x4f createOrtho(float width, float height, float nearZ, float farZ)
{
	const float range = farZ - nearZ;
	const std::array<float, 16> data = {
		2.0f / width, 0.0f,          0.0f,           0.0f, // 列0
		0.0f,         -2.0f / height, 0.0f,          0.0f, // 列1
		0.0f,         0.0f,          -1.0f / range,  0.0f, // 列2
		0.0f,         0.0f,          -nearZ / range, 1.0f, // 列3
	};
	return geo::Matrix4x4f(data);
}

// 画面座標(左上原点・y下向き・単位は論理ピクセル)から、Vulkanのクリップ空間への射影。
// 2Dスプライト用: mvp = createScreenProjection(w, h) * translate(x, y) * scale(sw, sh)。
// VulkanのNDCはy下向きなので、OpenGL流儀の補正は不要。z=0が最前面
inline geo::Matrix4x4f createScreenProjection(float width, float height)
{
	const std::array<float, 16> data = {
		2.0f / width, 0.0f,          0.0f, 0.0f, // 列0
		0.0f,         2.0f / height, 0.0f, 0.0f, // 列1
		0.0f,         0.0f,          1.0f, 0.0f, // 列2
		-1.0f,        -1.0f,         0.0f, 1.0f, // 列3
	};
	return geo::Matrix4x4f(data);
}

// キューブマップの面(0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z)へ、eyeから90度の視野で描くためのビュー射影
// (Vulkanのクリップ空間まで込み。createPerspective/createOrthoと同じくクリップ空間まで込み)。点光源の全方向シャドウマップ用。
// キューブマップの面のテクセル配置(Vulkan仕様のsc/tc規約)に合わせて、各面の
// 「画像の右方向R」と「画像の下方向D」を決めている。この座標系は左手系なので、geo::createLookAtは使わず直接組む。
// 深度は 手前=0〜奥=1 の透視深度で、面の軸方向の距離d(= キューブマップのサンプル方向で絶対値が最大の成分)に対して
// z = far*(d-near) / (d*(far-near))
inline geo::Matrix4x4f createCubeFaceViewProj(const geo::Vector3f& eye, int face, float nearZ, float farZ)
{
	struct Axes { float forward[3]; float right[3]; float down[3]; };
	static const Axes axes[6] = {
		{{ 1, 0, 0}, { 0, 0,-1}, { 0,-1, 0}}, // +X
		{{-1, 0, 0}, { 0, 0, 1}, { 0,-1, 0}}, // -X
		{{ 0, 1, 0}, { 1, 0, 0}, { 0, 0, 1}}, // +Y
		{{ 0,-1, 0}, { 1, 0, 0}, { 0, 0,-1}}, // -Y
		{{ 0, 0, 1}, { 1, 0, 0}, { 0,-1, 0}}, // +Z
		{{ 0, 0,-1}, {-1, 0, 0}, { 0,-1, 0}}, // -Z
	};
	const Axes& a = axes[face];
	const float e[3] = {eye.getX(), eye.getY(), eye.getZ()};
	auto dot = [](const float* u, const float* v){ return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
	const float depthScale = farZ / (farZ - nearZ);
	const float depthOffset = -farZ * nearZ / (farZ - nearZ);

	// 行ごとに係数を決める(クリップ座標 = M * (x,y,z,1))
	float m[4][4];
	for(int i = 0; i < 3; ++i){
		m[0][i] = a.right[i];                // x: 右方向の距離
		m[1][i] = a.down[i];                 // y: 下方向の距離(Vulkanのクリップ空間はy下向き)
		m[2][i] = depthScale * a.forward[i]; // z: 奥行きの透視深度(線形部分)
		m[3][i] = a.forward[i];              // w: 奥行きd
	}
	m[0][3] = -dot(a.right, e);
	m[1][3] = -dot(a.down, e);
	m[2][3] = -depthScale * dot(a.forward, e) + depthOffset;
	m[3][3] = -dot(a.forward, e);

	std::array<float, 16> data{};
	for(int row = 0; row < 4; ++row){
		for(int col = 0; col < 4; ++col){
			data[col * 4 + row] = m[row][col]; // 列優先
		}
	}
	return geo::Matrix4x4f(data);
}

} // namespace vk_

#endif // VK_VULKANMATH_H_
