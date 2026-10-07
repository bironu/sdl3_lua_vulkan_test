#if !defined(MODEL_PMXLOADER_H_)
#define MODEL_PMXLOADER_H_

#include "model/ModelData.h"
#include <memory>
#include <string>

namespace model
{

// PMX(MikuMikuDance形式。2.0/2.1)を読む。頂点(ボーンウェイト付き)・面・テクスチャ・材質・ボーン(IK・付与を含む)を読む。
// モーフ・表示枠・剛体・ジョイントは読まない(ここまで読んだら終わり)。
// fullPathはファイルの絶対パス、relativeDirはそのファイルのあるディレクトリ(リポジトリ直下からの相対)で、
// テクスチャの相対パスを組み立てるのに使う。失敗したらnullptr(理由はSDL_LogErrorに出す)
std::shared_ptr<ModelData> loadPmx(const std::string &fullPath, const std::string &relativeDir);

} // namespace model

#endif // MODEL_PMXLOADER_H_
