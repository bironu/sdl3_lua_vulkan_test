#if !defined(OPENING_OPENINGSCENE_H_)
#define OPENING_OPENINGSCENE_H_

#include "scene/common/LuaUiScene.h"

namespace game
{

// 起動直後の画面。見た目と入力は res/lua/ui/opening.lua(Luaのウィジェット。F5で読み直せる)が制御する:
// 画面の真ん中に白い文字 "Push Enter" を0.5秒間隔で点滅させ、Enterキー(PadA)でCharacterSelectSceneへ進む
class OpeningScene : public LuaUiScene
{
public:
	OpeningScene() : LuaUiScene("res/lua/ui/opening.lua") {}
};

} // namespace game

#endif // OPENING_OPENINGSCENE_H_
