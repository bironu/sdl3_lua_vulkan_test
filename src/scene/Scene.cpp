#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "sdl/SDLWindow.h"
#include "app/Application.h"
#include "resources/ResourceSet.h"
#include "resources/Resources.h"
#include "task/TaskManager.h"
#include <SDL3/SDL_log.h>

Scene::Scene()
	: app_(nullptr)
	, host_(nullptr)
	, res_(nullptr)
	, manager_(nullptr)
	, isFinished_(false)
{
}

Scene::~Scene() = default;

void Scene::prepare(Application *app, Resources *res, TaskManager *manager, SceneHost *host)
{
	resourceSet_ = std::make_unique<ResourceSet>(*res);
	app_ = app;
	host_ = host;
	res_ = res;
	manager_ = manager;
}

SDL_::Window &Scene::getWindow()
{
	return host_->getWindow();
}

void Scene::swap()
{
	host_->swap();
}

bool Scene::onIdle(uint32_t tick)
{
	const bool stillRunning = !manager_->compute(tick);
	/* レンダラはSDL_SetRenderVSync()でVSync有効にして生成されているため、
	   ここで毎回present()してもリフレッシュレートで自然にペーシングされる */
	return stillRunning;
}

void Scene::onCreate(uint32_t tick)
{
}

void Scene::registerTask(int id, std::shared_ptr<Task> task)
{
	manager_->registerTask(id, std::move(task));
}

void Scene::unregisterTask(int id, bool isFinishAction)
{
	manager_->unregisterTask(id, isFinishAction);
}

void Scene::onDestroy(uint32_t tick)
{
}

void Scene::onResume(uint32_t tick)
{
}

void Scene::quit()
{
	app_->quit();
}
