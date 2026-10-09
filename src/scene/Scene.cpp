#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "app/Application.h"
#include "resources/ResourceSet.h"
#include "resources/Resources.h"
#include "task/TaskManager.h"

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
	// TaskManager::compute() は全タスク終了時に true を返す。
	// onIdle は、まだタスクが残っていたら true(再描画を続ける)を返す
	const bool stillRunning = !manager_->compute(tick);
	return stillRunning;
}

void Scene::registerTask(int taskId, std::shared_ptr<Task> task)
{
	manager_->registerTask(taskId, std::move(task));
}

void Scene::unregisterTask(int taskId, bool runFinishAction)
{
	manager_->unregisterTask(taskId, runFinishAction);
}

void Scene::quit()
{
	app_->quit();
}
