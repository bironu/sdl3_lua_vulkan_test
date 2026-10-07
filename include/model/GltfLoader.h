#if !defined(MODEL_GLTFLOADER_H_)
#define MODEL_GLTFLOADER_H_

#include "model/ModelData.h"
#include <memory>
#include <string>

namespace model
{

// VRM(glTF 2.0のバイナリ形式 .vrm/.glb)を読んで、PMXと同じModelDataにする。
//   - 頂点(スキニング付き)・面・材質(基本色とテクスチャ)・ボーン(ノードの階層。休止ポーズの回転を含む)・モーフ(ターゲット)
//   - VRM 1.0 の表情(expressions)と、VRM 0.x のブレンドシェイプグループは、モーフターゲットをまとめたグループモーフにする
//   - 画像は埋め込みのままModelData::embeddedImagesに入れる(デコードは呼び出し側)
// ModelDataはPMXと同じ座標系(左手系、モデルの正面が-Z)に直す: glTFの右手系(VRM 1.0は正面が+Z)をZ方向へ鏡像にする
// (VRM 0.xは正面が-Zなので、Y軸まわりに180度回してから同様に直す=X方向の鏡像)。三角形の頂点順もPMXと同じ扱いになるよう入れ替える。
// 単位はメートルのまま。物理(スプリングボーン)・MToonの陰影・視線は扱わない。失敗したらnullptr(理由はSDL_LogErrorに出す)
std::shared_ptr<ModelData> loadVrm(const std::string &fullPath);

} // namespace model

#endif // MODEL_GLTFLOADER_H_
