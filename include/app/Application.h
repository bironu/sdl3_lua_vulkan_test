#if !defined(APPLICATION_H_)
#define APPLICATION_H_

#include "misc/Uncopyable.h"
#include "sdl/SDLTimer.h"
#include <SDL3/SDL.h>
#include <memory>
#include <vector>

namespace SDL_
{
class Window;
namespace Mix_
{
class Mixer;
}
}

class Resources;
class TaskManager;
/**
 * @brief アプリケーション全体を管理するクラス
 *
 * SDL・TTF・Mixerの初期化/終了、ウィンドウの登録管理、
 * イベントループの実行を担う。コピー不可。
 */
class Application final
{
public:
	UNCOPYABLE(Application);

	/**
	 * @brief コンストラクタ。SDL/TTF/Mixerを初期化する
	 * @param flags SDL_Init()に渡す初期化フラグ
	 */
	explicit Application(Uint32 flags);

	/** @brief デストラクタ。ウィンドウ・Mixerを破棄し、SDL関連ライブラリを終了する */
	~Application();

	/**
	 * @brief メインウィンドウを登録する(一覧にも追加される)
	 * @param mainWindow 登録するメインウィンドウ
	 */
	void registerMainWindow(std::shared_ptr<SDL_::Window> mainWindow);

	/**
	 * @brief メインウィンドウを取得する
	 * @return メインウィンドウ。未登録なら空
	 */
	std::shared_ptr<SDL_::Window> getMainWindow();

	/**
	 * @brief ウィンドウを一覧に登録する
	 * @param window 登録するウィンドウ
	 */
	void registerWindow(std::shared_ptr<SDL_::Window> window);

	/**
	 * @brief ウィンドウを一覧から登録解除する
	 * @param window 解除するウィンドウ
	 */
	void unregisterWindow(std::shared_ptr<SDL_::Window> window);

	/**
	 * @brief ウィンドウIDからウィンドウを検索する
	 * @param id 検索するウィンドウID
	 * @return 該当ウィンドウ。見つからなければ空
	 */
	std::shared_ptr<SDL_::Window> getWindow(int id);

	/**
	 * @brief SDLのサブシステムを初期化する
	 * @param flags 初期化するサブシステムのフラグ
	 * @return 成功なら true
	 */
	// 登録済みの全ウィンドウを破棄する(シーン・描画の資源も解放される)。run()の後、Resourcesを破棄する前に呼ぶ
	void releaseWindows();

	bool initSubSystem(Uint32 flags) { return ::SDL_InitSubSystem(flags); }

	/**
	 * @brief SDLのサブシステムを終了する
	 * @param flags 終了するサブシステムのフラグ
	 */
	void quitSubSystem(Uint32 flags) { ::SDL_QuitSubSystem(flags); }

	/** @brief SDL本体の初期化に成功したか @return 成功なら true */
	bool isApplication() const { return is_application_; }

	/** @brief SDL_ttfの初期化に成功したか @return 成功なら true */
	bool isTtf() const { return is_ttf_; }

	/** @brief SDL_mixerの初期化に成功したか @return 成功なら true */
	bool isMixer() const { return is_mixer_; }

	/** @brief ミキサーを取得する @return ミキサーへの参照 */
	SDL_::Mix_::Mixer &getMixer() { return *mixer_; }

	/**
	 * @brief イベントループを実行する
	 *
	 * メインウィンドウのSceneが無くなるまで継続する。
	 * @param res リソース管理
	 * @param manager タスク管理
	 * @return 終了コード(quit()で指定された値)
	 */
	int run(Resources &res, TaskManager &manager);

	/**
	 * @brief アプリケーションの終了を要求する(SDL_EVENT_QUITを送出)
	 * @param val 終了コード
	 */
	void quit(const int val = 0);

	/** @brief SDL初期化からの経過ミリ秒を取得する @return 経過ミリ秒 */
	static uint32_t getTickCount() { return ::SDL_GetTicks(); }

	/**
	 * @brief タイマーを設定する
	 * @param interval 呼び出し間隔(ミリ秒)
	 * @param timer_proc タイマー満了時に呼ばれるコールバック
	 */
	void setTimer(int interval, SDL_::Timer::Callback timer_proc);

	/** @brief 設定済みのタイマーを停止・破棄する */
	void killTimer(void);

	/** @brief キーボードの押下状態配列を取得する @return スキャンコード添字の状態配列。未初期化時は nullptr */
	static const bool *getKeybordState() { return keybordState_; }

private:
	/**
	 * @brief Scene配送前にアプリ全体で処理すべきイベントを処理する
	 * @param res リソース管理
	 * @param manager タスク管理
	 * @param event 処理対象のイベント
	 * @return イベントを消費した(Sceneへ配送しない)なら true
	 */
	bool handlePreEvent(Resources &res, TaskManager &manager, SDL_Event &);

	/** @brief SDL本体の初期化結果 */
	const bool is_application_;
	/** @brief SDL_ttfの初期化結果 */
	const bool is_ttf_;
	/** @brief SDL_mixerの初期化結果 */
	const bool is_mixer_;
	/** @brief アプリ共通のタイマー */
	std::unique_ptr<SDL_::Timer> timer_;
	/** @brief 音声ミキサー */
	std::unique_ptr<SDL_::Mix_::Mixer> mixer_;
	/** @brief 登録済みの全ウィンドウ */
	std::vector<std::shared_ptr<SDL_::Window>> listWindow_;
	/** @brief メインウィンドウ(このSceneが無くなるとアプリ終了) */
	std::shared_ptr<SDL_::Window> mainWindow_;
	/** @brief run()が返す終了コード */
	int return_code_;

	/** @brief キーボード押下状態配列(SDL_GetKeyboardStateの結果) */
	static const bool *keybordState_;
};

#endif // APPLICATION_H_
