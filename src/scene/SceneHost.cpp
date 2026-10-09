#include "scene/SceneHost.h"
#include "scene/Scene.h"
#include "resources/Resources.h"
#include "sdl/SDLWindow.h"
#include <SDL3/SDL_timer.h>
#include <utility>

void SceneHost::attach(Application *app, Resources *res, TaskManager *manager)
{
	app_ = app;
	res_ = res;
	manager_ = manager;
}

void SceneHost::dispatch(const SDL_Event &event)
{
	if (currentScene_) {
		currentScene_->dispatch(event);
	}
}

bool SceneHost::onIdle(uint32_t tick)
{
	return currentScene_ ? currentScene_->onIdle(tick) : false;
}

void SceneHost::updateScenes(uint32_t tick)
{
	for (;;) {
		if (currentScene_ && currentScene_->isFinished()) {
			currentScene_->onSuspend();
			currentScene_->onDestroy(tick);
			currentScene_.reset();
		}

		if (nextScene_) {
			if (currentScene_) {
				currentScene_->onSuspend();
				stackResumeScene_.push(std::move(currentScene_));
			}
			currentScene_ = std::move(nextScene_); // 移動元は空になる
			currentScene_->prepare(app_, res_, manager_, this);
			currentScene_->onCreate(tick);
			// 古いシーンが手放して、少し預かっていたデータを、新しいシーンのonCreateが読み終わったので、手放す
			if (res_) {
				res_->collect();
			}
			currentScene_->onResume(tick);
			continue;
		}
		else if (!currentScene_) {
			if (!stackResumeScene_.empty()) {
				currentScene_ = std::move(stackResumeScene_.top());
				stackResumeScene_.pop();
				currentScene_->onResume(tick);
				continue;
			}
		}
		break;
	}
}

void SceneHost::clearResumeStack(size_t keepCount)
{
	const auto tick = ::SDL_GetTicks();
	while (stackResumeScene_.size() > keepCount) {
		auto scene = std::move(stackResumeScene_.top());
		stackResumeScene_.pop();
		// 積まれていた時点でonSuspend済み。ここではonDestroyだけ
		scene->onDestroy(tick);
	}
	if (currentScene_) {
		currentScene_->finish();
	}
}

void SceneHost::terminate()
{
	// 現在のSceneにも終了処理(onDestroy)をさせてから破棄する。TaskManager等、Sceneより長く生きる
	// 共有物に登録したものを、Sceneが自分で片付けられるようにするため(クラッシュの原因になる)
	if (currentScene_) {
		const auto tick = ::SDL_GetTicks();
		currentScene_->onSuspend();
		currentScene_->onDestroy(tick);
	}
	currentScene_.reset();
	clearResumeStack();
	nextScene_.reset();
}

void SceneHost::swap()
{
	owner_.swap();
}
