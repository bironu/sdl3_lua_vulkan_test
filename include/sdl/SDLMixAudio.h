#if !defined(SDLMIXAUDIO_H_)
#define SDLMIXAUDIO_H_

#include "misc/Uncopyable.h"

struct MIX_Audio;

namespace SDL_
{
namespace Mix_
{

class Mixer;

// 効果音(SE)・BGM(MIDI)の両方に使う、デコード前音声データのラッパー。
// SDL3_mixerでは旧SDL2_mixerのMix_Chunk(効果音)とMix_Music(BGM)の区別が
// MIX_Audioという単一の型に統合されたため、SE/BGM共通でこのクラスを使う。
class Audio {
public:
	UNCOPYABLE(Audio);
	Audio(Mixer &owner, const char *, bool isMusic = false);
	~Audio();

	MIX_Audio *get() const { return audio_; }

	// 再生時間(ミリ秒)。フォーマット上長さが分からない場合(MIX_DURATION_UNKNOWN/
	// MIX_DURATION_INFINITE)は-1を返す。ミュート中等、再生完了をチャンネル監視
	// (Mixer::isChannelPlaying)で検知できないときの代替タイムアウトに使う
	// (BattleMode::onEnter()/playVictorySoundIfNeeded()参照、2026-10-01)
	long long getDurationMs() const;

private:
	MIX_Audio * const audio_;
};

} // namespace Mix_
} // namespace SDL_

#endif // SDLMIXAUDIO_H_
