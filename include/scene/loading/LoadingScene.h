#if !defined(LOADING_LOADINGSCENE_H_)
#define LOADING_LOADINGSCENE_H_

#include "scene/common/LuaUiScene.h"
#include "resources/ResourceSet.h"
#include <atomic>
#include <memory>
#include <thread>

namespace game
{

// 読み込み画面。見た目は res/lua/ui/loading.lua(Luaのウィジェット。F5で読み直せる)が制御する:
// 白い文字 "Now Loading..." が、画面の左下から右下まで往復する。
// その間、別スレッドでGameSceneの読み込み一覧(GameScene::kAssetManifest)のデータを全部読み、終わったらGameSceneへ進む
class LoadingScene : public LuaUiScene
{
public:
	LoadingScene() : LuaUiScene("res/lua/ui/loading.lua") {}
	~LoadingScene() override;

	void onCreate(uint32_t tick) override;

protected:
	void onFrame(uint32_t tick, float dt) override;

private:
	// 読んだデータを持っておく入れ物(別スレッドで使う)。シーンの終わりで手放され、少しの間Resourcesが預かるので、GameSceneが取れる
	std::unique_ptr<ResourceSet> preload_;
	std::thread worker_;
	std::atomic<bool> done_{false};
};

} // namespace game

#endif // LOADING_LOADINGSCENE_H_
