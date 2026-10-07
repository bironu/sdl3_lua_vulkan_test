#if !defined(MODEL_VMDLOADER_H_)
#define MODEL_VMDLOADER_H_

#include "model/Motion.h"
#include <memory>
#include <string>

namespace model
{

// VMD(MikuMikuDanceのモーション)を読む。ボーンのキーフレームとIKのオン/オフを読む。
// モーフ・カメラ・照明・影のフレームは読み飛ばす。ボーン名はShift-JISからUTF-8に直す。失敗したらnullptr
std::shared_ptr<Motion> loadVmd(const std::string &fullPath);

} // namespace model

#endif // MODEL_VMDLOADER_H_
