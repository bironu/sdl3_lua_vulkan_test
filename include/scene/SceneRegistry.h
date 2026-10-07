#if !defined(SCENEREGISTRY_H_)
#define SCENEREGISTRY_H_

#include <functional>
#include <memory>
#include <string>

class Scene;

// シーンの名前と生成関数の対応表。Luaスクリプトの scene.change("名前") が、名前からシーンを作るのに使う。
// main.cppで、起動時に登録する
class SceneRegistry
{
public:
	using Creator = std::function<std::shared_ptr<Scene>()>;
	static void add(const std::string &name, Creator creator);
	// 名前のシーンを作る。登録が無ければnullptr
	static std::shared_ptr<Scene> create(const std::string &name);
};

#endif // SCENEREGISTRY_H_
