#if !defined(VULKANMATERIAL_H_)
#define VULKANMATERIAL_H_

#include "vk/VulkanTexture.h"
#include <memory>

// ライティング付きメッシュ(VulkanWindow::draw)の材質。オブジェクトごとに指定できる
struct VulkanMaterial
{
	// 拡散色に掛けるテクスチャ(VulkanWindow::createTexture()で作る)。nullptrならウィンドウの既定テクスチャ
	std::shared_ptr<VulkanTexture> texture;
	// 鏡面反射の強さ(0で無し)
	float specular = 0.5f;
	// 光沢度(大きいほどハイライトが小さく鋭い)
	float shininess = 32.0f;
	// 平行光源のシャドウマップに影を落とすか/影を受けるか
	bool castShadow = true;
	bool receiveShadow = true;
	// 0以上なら、その番号の点光源の色で光源計算なしに描く(点光源の位置を示す目印のランプ用)。負なら通常のライティング
	int emissiveLight = -1;
	// 不透明度(0〜1)。テクスチャのアルファに掛けてアルファブレンドされる。半透明のものは、不透明なものの後に描くこと
	// (既存の位置指定の初期化がずれないよう、末尾に置いている)
	float alpha = 1.0f;
	// 環境光に足す明るさ(0〜2.55。0.01刻みで丸められる)。影の中や光の当たらない面が暗くなりすぎるものを、そのオブジェクトだけ持ち上げる
	float ambientBoost = 0.0f;
	// 0より大きければ、アルファの切り抜き(MASK): テクスチャx不透明度のアルファがこの値(0〜1)未満の部分を描かない。ブレンドはせず、深度を書く
	// (半透明の描画順の問題が無く、重い半透明のパイプラインも使わない)。0ならアルファブレンド
	float alphaCutoff = 0.0f;
};

#endif // VULKANMATERIAL_H_
