#if !defined(APPLICATION_H_)
#define APPLICATION_H_

#include "misc/Uncopyable.h"
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
	 *
	 * キーボード状態配列(getKeybordState)はSDL_Init成功時のみ取得する。
	 * 状態が更新されるにはflagsにSDL_INIT_VIDEOが必要。
	 * @param flags SDL_Init()に渡す初期化フラグ
	 */
	explicit Application(Uint32 flags);

	/** @brief デストラクタ。ウィンドウ・Mixerを破棄し、SDL関連ライブラリを終了する */
	~Application();

	/**
	 * @brief メインウィンドウを登録する(一覧にも追加される)
	 *
	 * このウィンドウのSceneが無くなるとrun()が終了する。nullptrは無視する。
	 * @param mainWindow 登録するメインウィンドウ
	 */
	void registerMainWindow(std::shared_ptr<SDL_::Window> mainWindow);

	/**
	 * @brief メインウィンドウを登録解除する(一覧からも外れる)
	 *
	 * 未登録なら何もしない。解除後はrun()のループ継続条件を満たさなくなる。
	 */
	void unregisterMainWindow();

	/**
	 * @brief メインウィンドウを取得する
	 * @return メインウィンドウ。未登録なら空
	 */
	std::shared_ptr<SDL_::Window> getMainWindow() const { return mainWindow_; }

	/**
	 * @brief ウィンドウを一覧に登録する
	 *
	 * nullptrと登録済みのウィンドウは無視する。
	 * @param window 登録するウィンドウ
	 */
	void registerWindow(std::shared_ptr<SDL_::Window> window);

	/**
	 * @brief ウィンドウを一覧から登録解除する
	 *
	 * メインウィンドウが渡された場合はunregisterMainWindow()に委譲する。
	 * @param window 解除するウィンドウ
	 */
	void unregisterWindow(std::shared_ptr<SDL_::Window> window);

	/**
	 * @brief ウィンドウIDからウィンドウを検索する
	 * @param id 検索するウィンドウID
	 * @return 該当ウィンドウ。見つからなければ空
	 */
	std::shared_ptr<SDL_::Window> getWindow(SDL_WindowID id) const;

	/**
	 * @brief 登録済みの全ウィンドウを破棄する
	 *
	 * シーン・描画の資源も解放される。run()の後、Resourcesを破棄する前に呼ぶ。
	 */
	void releaseWindows();

	/**
	 * @brief SDLのサブシステムを初期化する
	 * @param flags 初期化するサブシステムのフラグ
	 * @return 成功なら true
	 */
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
	 * @brief キーボードの押下状態配列を取得する
	 *
	 * SDL_GetKeyboardState()の結果をコンストラクタで一度だけ取得して保持している。
	 * 配列はSDLが所有し、SDL終了まで有効。毎フレームの関数呼び出しを避けるためこれを読む。
	 * @return スキャンコード添字の状態配列。Application生成前・破棄後は nullptr
	 */
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
	/** @brief 音声ミキサー */
	std::unique_ptr<SDL_::Mix_::Mixer> mixer_;
	/** @brief 登録済みの全ウィンドウ */
	std::vector<std::shared_ptr<SDL_::Window>> listWindow_;
	/** @brief メインウィンドウ(このSceneが無くなるとアプリ終了) */
	std::shared_ptr<SDL_::Window> mainWindow_;
	/** @brief run()が返す終了コード */
	int return_code_;

	/** @brief キーボード押下状態配列(SDL_GetKeyboardStateの結果。コンストラクタで一度だけ取得) */
	static const bool *keybordState_;
};

#endif // APPLICATION_H_
