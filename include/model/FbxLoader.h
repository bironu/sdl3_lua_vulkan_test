#if !defined(MODEL_FBXLOADER_H_)
#define MODEL_FBXLOADER_H_

#include "model/Vrma.h"
#include <memory>
#include <string>

namespace model
{

// FBX(バイナリ形式。Mixamoなどのボーン付きアニメーション)から、人型(ヒューマノイド)のアニメーションを読んで、VRMAと同じ
// HumanoidAnimation にする(VRMAとして保存したり、VrmaPlayerでVRMに当てたりできる)。
//   - ボーン名は、Mixamo(mixamorig:Hips など)をVRMのヒューマノイド名へ対応づける。それ以外の名前は読まない
//   - ノードの階層・PreRotation・Lcl Rotation(オイラー角)から、各ボーンの「休止ポーズからのワールドの回転」を求めて、
//     VRMAの仕様(休止ポーズの回転を全部なしにした、ヒューマノイドの親から見た局所の回転)に直す
//   - キーフレームは、30fpsに焼き込む(自動接線のエルミート補間)。腰の移動は、メートルに直す(FBXの単位はUnitScaleFactor)
//   - 座標系は右手系・Y上(Mixamoは正面+Z)で、VRMAと同じ。ほかの向きのFBXは扱わない
// 失敗したらnullptr(理由はSDL_LogErrorに出す)
std::shared_ptr<HumanoidAnimation> loadFbxAnimation(const std::string &fullPath);

} // namespace model

#endif // MODEL_FBXLOADER_H_
