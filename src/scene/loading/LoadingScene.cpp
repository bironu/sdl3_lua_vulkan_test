#include "scene/loading/LoadingScene.h"
#include "resources/Resources.h"
#include "scene/SceneHost.h"
#include "resources/ResourceSet.h"
#include "scene/game/GameScene.h"
#include <SDL3/SDL_log.h>

namespace game
{

LoadingScene::~LoadingScene()
{
	if(worker_.joinable()){
		worker_.join();
	}
}

void LoadingScene::onCreate(uint32_t tick)
{
	LuaUiScene::onCreate(tick);
	// 読み込みは別スレッドで(GPUの資源は触らない。GameScene::onCreateでメインスレッドが作る)
	preload_ = std::make_unique<ResourceSet>(getResources());
	worker_ = std::thread([this]{
		preload_->loadManifest(GameScene::kAssetManifest);
		done_ = true;
	});
}

void LoadingScene::onFrame(uint32_t)
{
	if(!done_ || isFinished()){
		return;
	}
	worker_.join();
	getHost().registerNextScene(std::make_shared<GameScene>());
	finish();
}

} // namespace game
