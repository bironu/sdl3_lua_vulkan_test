#if !defined(GAME_GAMESESSION_H_)
#define GAME_GAMESESSION_H_

#include "vk/VulkanModel.h"
#include <memory>
#include <string>

namespace game
{

// シーンをまたいで引き継ぐ、ゲームの開始時の情報(キャラクタ選択 → GameScene)。
// character: 選んだキャラクタのモデル(VRM)のパス。model: そのモデルを、選択画面ですでにGPU上に作ってあれば、それ(GameSceneが作り直さずに済む)
struct GameSession
{
	std::string character;
	std::shared_ptr<VulkanModel> model;

	static GameSession &instance()
	{
		static GameSession session;
		return session;
	}
};

} // namespace game

#endif // GAME_GAMESESSION_H_
