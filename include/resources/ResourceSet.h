#if !defined(RESOURCES_RESOURCESET_H_)
#define RESOURCES_RESOURCESET_H_

#include "misc/Uncopyable.h"
#include <memory>
#include <string>
#include <vector>

class Resources;
namespace SDL_
{
class Image;
namespace Mix_
{
class Audio;
}
}
namespace model
{
struct ModelData;
struct HumanoidAnimation;
struct Motion;
}

// シーンが使うデータを「持っておく」入れ物(RAII)。Sceneのメンバ(Scene::resources())で、シーンの終わり(破棄)で、持っていたものを自動で手放す。
// 手放したものは、他のシーンが使っていれば残り、誰も使わなければ解放される(Resourcesのキャッシュ=AssetCache)。
// 使い方: onCreateで、必要なものを全部、ここ経由で読む(読み込み済みなら、キャッシュから共有される)。onDestroyで何もしなくてよい(明示的にrelease()してもよい)。
// 読み込み一覧(マニフェスト)をLuaで書くこともできる(loadManifest)
class ResourceSet
{
public:
	UNCOPYABLE(ResourceSet);
	explicit ResourceSet(const Resources &resources);
	~ResourceSet();

	std::shared_ptr<SDL_::Image> image(const std::string &path);
	std::shared_ptr<const model::ModelData> model(const std::string &path);
	// モデルを読み、あわせて、モデルが使う埋め込み画像(VRM)を全部デコードして持っておく(VulkanModel::createが使う組み合わせ)。
	// 重いデコードを、モデルを作る前に(別スレッドで)済ませるため。デコード済みの画像は modelImage() で取り出す
	std::shared_ptr<const model::ModelData> modelWithImages(const std::string &path);
	// モデルの埋め込み画像(デコード済み。無ければここでデコードして持つ)
	std::shared_ptr<SDL_::Image> modelImage(const std::string &modelPath, size_t index, bool ignoreAlpha);
	std::shared_ptr<const model::HumanoidAnimation> animation(const std::string &path);
	std::shared_ptr<const model::Motion> motion(const std::string &path);
	std::shared_ptr<SDL_::Mix_::Audio> sound(const std::string &path);
	std::shared_ptr<SDL_::Mix_::Audio> music(const std::string &path);

	// Luaの読み込み一覧(リポジトリ直下からの相対パス)を読んで、書かれたものを全部読む。無いファイルは何もしない。
	// 一覧の形(どのキーも省略できる):
	//   assets = {
	//     images = {"res/image/a.png"}, models = {"res/model/a.vrm"}(埋め込み画像のデコードも含む), animations = {"res/motion/a.vrma"},
	//     motions = {"res/motion/a.vmd"}, sounds = {"res/sound/a.wav"}, music = {"res/sound/a.mid"},
	//   }
	// 読めなかったものがあれば、ログに出す。読めた数を返す
	size_t loadManifest(const std::string &path);

	// 持っているものを手放す(Resourcesが、次のシーンのonCreateが終わるまで、少しだけ預かる)。破棄時にも自動で呼ばれる
	void release();
	size_t size() const { return held_.size(); }

private:
	const Resources &resources_;
	std::vector<std::shared_ptr<const void>> held_;
};

#endif // RESOURCES_RESOURCESET_H_
