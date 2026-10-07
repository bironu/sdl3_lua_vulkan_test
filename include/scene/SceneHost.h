#if !defined(SCENEHOST_H_)
#define SCENEHOST_H_

#include "misc/Uncopyable.h"
#include <SDL3/SDL_events.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stack>

namespace SDL_
{
class Window;
}

class Scene;
class Application;
class Resources;
class TaskManager;

// Sceneとそのスタック(中断中Scene群)を管理する。
// SDL_::Windowが1つ所有する(1Window:複数Scene)。画面更新はswap()経由で
// 所有ウィンドウの仮想関数swap()(描画方式ごとに実装)へ委譲する。
class SceneHost
{
public:
	UNCOPYABLE(SceneHost);
	explicit SceneHost(SDL_::Window &owner) : owner_(owner) {}
	~SceneHost() = default;

	SDL_::Window &getWindow() { return owner_; }

	// Application::run()が実行前に呼ぶ。以降に開始するSceneへ渡される
	void attach(Application *app, Resources *res, TaskManager *manager);

	// 次のSceneを予約する。実際の切替えはupdateScenes()で行われる
	void registerNextScene(std::shared_ptr<Scene> nextScene) { nextScene_ = std::move(nextScene); }
	std::shared_ptr<Scene> getCurrentScene() const { return currentScene_; }
	// 現在/予約済み/中断中のSceneが1つでもあるか(falseならこのホストは用済み)
	bool hasScene() const { return currentScene_ || nextScene_ || !stackResumeScene_.empty(); }

	void dispatch(const SDL_Event &event);

    // 現在のSceneのonIdle()。Sceneが無ければfalse
	bool onIdle(uint32_t tick);

	void updateScenes(uint32_t tick);

	// stackResumeScene_を、古い方(根本)からkeepCount個だけ残して新しい方を
	// onSuspend()+onDestroy()して捨てる。続けて現在のSceneもfinish()する
	// (nullptrなら何もしない)ので、次のupdateScenes()で根本Sceneが復元される。
	// keepCount=0(デフォルト)で全部捨てる(アプリ終了時)
	void clearResumeStack(size_t keepCount = 0);

	// アプリ終了時: 現在/予約Sceneを破棄し、スタックも空にする
	void terminate();

	// 画面を更新する(Sceneから呼ばれる)。所有ウィンドウのswap()へ委譲
	void swap();

private:
	SDL_::Window &owner_;
	Application *app_ = nullptr;
	Resources *res_ = nullptr;
	TaskManager *manager_ = nullptr;
	std::shared_ptr<Scene> currentScene_;
	std::stack<std::shared_ptr<Scene>> stackResumeScene_;
	std::shared_ptr<Scene> nextScene_;
};

#endif // SCENEHOST_H_
