#if !defined(SCENE_H_)
#define SCENE_H_

#include "misc/Uncopyable.h"
#include <memory>

class SceneHost;
namespace SDL_
{
class Window;
}
class Application;
class Resources;
class ResourceSet;
class TaskManager;
union SDL_Event;
class Task;
class Scene
{
public:
	UNCOPYABLE(Scene);
	explicit Scene();
	virtual ~Scene();
	void prepare(Application*, Resources*, TaskManager*, SceneHost*);

	void finish() { isFinished_ = true; }
	bool isFinished() const { return isFinished_; }
	void registerTask(int, std::shared_ptr<Task>);
	void unregisterTask(int, bool);

	virtual void dispatch(const SDL_Event &) = 0;
	virtual void onSuspend() {}
	virtual bool onIdle(uint32_t);
	virtual void onCreate(uint32_t);
	virtual void onDestroy(uint32_t);
	virtual void onResume(uint32_t);

	void swap();
	void quit();

	Application &getApplication() { return *app_; }
	// このSceneが属するホスト(ウィンドウ)
	SceneHost &getHost() { return *host_; }
	SDL_::Window &getWindow();
	Resources &getResources() { return *res_; }
	// このシーンが使うデータの入れ物。onCreateで必要なものを全部ここ経由で読めば、シーンの破棄で自動で手放される(resources/ResourceSet.h)。
	// prepare()の後(onCreate以降)に使える
	ResourceSet &resources() { return *resourceSet_; }
	TaskManager &getManager() { return *manager_; }

private:
	Application *app_;
	SceneHost *host_;
	Resources *res_;
	std::unique_ptr<ResourceSet> resourceSet_;
	TaskManager *manager_;
	bool isFinished_;
};

#endif // SCENE_H_
