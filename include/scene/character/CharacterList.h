#if !defined(CHARACTER_CHARACTERLIST_H_)
#define CHARACTER_CHARACTERLIST_H_

#include <string>
#include <vector>

namespace game
{

// 選べるキャラクタ(res/lua/data/characters.lua)。キャラクタ選択画面・GameSceneが共通で読む
struct CharacterInfo
{
	std::string name;   // 表示名
	std::string model;  // モデル(VRM)のパス(リポジトリ直下からの相対パス)
	std::string motion; // キャラクタ選択画面の立ち姿のモーション(VRMA)。空なら既定
};

// 一覧を読む(失敗したら空)
std::vector<CharacterInfo> loadCharacterList(const std::string &relativePath);

} // namespace game

#endif // CHARACTER_CHARACTERLIST_H_
