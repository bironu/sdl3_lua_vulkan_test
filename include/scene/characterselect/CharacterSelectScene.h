#if !defined(CHARACTERSELECT_CHARACTERSELECTSCENE_H_)
#define CHARACTERSELECT_CHARACTERSELECTSCENE_H_

#include "model/Vrma.h"
#include "resources/ResourceSet.h"
#include "scene/character/CharacterList.h"
#include "scene/common/BlobShadow.h"
#include "scene/common/LuaUiScene.h"
#include "vk/VulkanModel.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace game
{

// キャラクタ選択画面。キャラクタの一覧(res/lua/data/characters.lua)の全員が、水平な円状に並んで回る(3D。モデルは常にカメラの方を向く)。
// 正面のキャラが選択中。立ち姿は、キャラごとのモーション(VRMA)で動く。文字・矢印・読み込み中の円は、Luaのウィジェット(res/lua/ui/character_select.lua)。
// 操作(Luaが game.command で伝える): 左右で回す("rotate", ±1)、Enter/PadAで決定("confirm")。決定すると、選んだキャラで GameScene へ進む(GameSession)
// 読み込み: 選んだキャラ(と両隣)で、まだ読んでいないモデルを、別スレッドで読む(ファイル・埋め込み画像のデコード)。
// GPUの資源は、読み終わったものからメインスレッドで1フレームに1体ずつ作る。読み込み中の選択中キャラには、Lua側がぐるぐる回る円を出す。
// 読んだデータはシーンの終わりでResourcesのキャッシュに残り(少しの間)、GameSceneが速く取れる。GPU上のモデルは、選んだ1体を GameSession で引き継ぐ
class CharacterSelectScene : public LuaUiScene
{
public:
	static constexpr const char *kCharacterList = "res/lua/data/characters.lua";
	static constexpr const char *kDefaultMotion = "res/motion/VRMA_01.vrma";

	CharacterSelectScene() : LuaUiScene("res/lua/ui/character_select.lua") {}
	~CharacterSelectScene() override;

	void onCreate(uint32_t tick) override;

protected:
	void onFrame(uint32_t tick) override;
	void onCommand(const std::string &name, double value) override;

private:
	enum State
	{
		kNotRequested = 0,
		kQueued,   // 読み込み待ち・読み込み中(別スレッド)
		kDataReady, // データ(CPU)が読めた。GPUの資源はこれから
		kReady,    // 表示できる
		kFailed,
	};
	struct Slot
	{
		CharacterInfo info;
		std::atomic<int> state{kNotRequested};
		std::shared_ptr<VulkanModel> model;
		std::unique_ptr<model::VrmaPlayer> player;
		float time = 0.0f;
	};

	int current() const; // 選択中の番号
	void request(int index, bool urgent); // まだなら読み込みを依頼する(urgent: 待ち行列の先頭へ)
	void requestAroundCurrent();
	void createGpuResources(Slot &slot);
	void workerLoop();

	std::vector<std::unique_ptr<Slot>> slots_;
	std::unique_ptr<BlobShadow> blob_;
	float rotation_ = 0.0f;     // 表示上の回転(番号の単位。滑らかに目標へ近づく)
	int target_ = 0;            // 目標(回した分だけ増減。選択中は target_ を個数で割った余り)
	uint32_t lastTick_ = 0;
	bool confirmed_ = false;

	// 読み込みの別スレッド。preload_はこのスレッドだけが使う(読んだデータを持っておく入れ物)
	std::unique_ptr<ResourceSet> preload_;
	std::thread worker_;
	std::mutex mutex_;
	std::condition_variable wake_;
	std::deque<int> queue_;
	bool stop_ = false;
};

} // namespace game

#endif // CHARACTERSELECT_CHARACTERSELECTSCENE_H_
