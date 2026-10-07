#if !defined(SAMPLESCENE_H_)
#define SAMPLESCENE_H_

#include "scene/Scene.h"

// 起動確認用のシーン。画面を真っ黒に塗りつぶすだけ。
class SampleScene : public Scene
{
public:
	SampleScene() = default;

	void dispatch(const SDL_Event &) override;
	void onSuspend() override;
	bool onIdle(uint32_t tick) override;
};

#endif // SAMPLESCENE_H_
