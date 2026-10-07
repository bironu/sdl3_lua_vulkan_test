#if !defined(FIELD_PROPCATALOG_H_)
#define FIELD_PROPCATALOG_H_

#include <string>
#include <vector>

namespace field
{

// フィールドに置く物(ビル・岩・瓦礫・木など)のモデルの置き場: res/prop/*.glb(人物のモデルの res/model/ とは別)。
// フォルダにあるファイルが、そのまま一覧になる(外部のファイルは importProp() で、ここへコピーして使えるようにする)
inline constexpr const char kPropDir[] = "res/prop";

// 一覧(ファイル名。名前順)
std::vector<std::string> listProps();
// 名前 → リポジトリ直下からの相対パス(Resources::loadModelに渡せる)
std::string propPath(const std::string &name);
// 外部のファイル(.glb/.vrm)を res/prop/ へコピーして、一覧で使えるようにする(開発用: ビルド元のリポジトリの res/prop/ へ書く)。
// 成功したら true で、nameにファイル名を返す
bool importProp(const std::string &sourcePath, std::string &name);

} // namespace field

#endif // FIELD_PROPCATALOG_H_
