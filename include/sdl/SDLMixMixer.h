#if !defined(SDLMIXMIXER_H_)
#define SDLMIXMIXER_H_

#include "misc/Uncopyable.h"
#include <memory>
#include <string>
#include <vector>

struct MIX_Mixer;

namespace SDL_
{
namespace Mix_
{

class Audio;
class Track;

// MIX_Mixer(再生デバイス+トラック群)のラッパー。
class Mixer final
{
public:
	UNCOPYABLE(Mixer);
	Mixer();
	~Mixer();

    MIX_Mixer *get() const { return mixer_; }

	int allocateChannels(int);
	int playSound(Audio &, int = -1, int = 0);
	bool isChannelPlaying(int channel) const;
	bool playMusic(Audio &, int = -1);
	bool isMusicPlaying() const;
	bool isMusicPaused() const;
	bool stopMusic();
	void pauseMusic();
	void resumeMusic();
    void rewindMusic();
    void setMusicGain(float gain); // 0.0(無音)〜1.0(通常音量)

    // MIX_SetTrackFrequencyRatio()のラッパー。再生速度(=周波数比)を変える
    // ため、本来のMIDIテンポ変更とは異なりピッチも一緒に変わるが、
    // 「戦闘中は少しテンポが速くなる」演出には十分使える。1.0が通常速度
    // (旧LegacyPlatform.cpp bgm_tempo()、2026-09-26にMixerへ移動)
    void setMusicTempo(float ratio);

    // trueの間、呼ばれるたびメルセンヌツイスターでランダムなテンポへ
    // 揺らす(食料切れ等の不穏な演出用)。falseで通常(1.0)へ戻す
    // (旧LegacyPlatform.cpp bgm_random()、2026-09-26にMixerへ移動)
    void randomizeMusicTempo(bool enable);
    bool setSoundFonts(const char *);

	// AudioがMIDI読み込み時にMIX_LoadAudioWithProperties()へ
	// デコーダ固有プロパティとして渡すために参照する
	const std::string &getSoundFontPath() const { return soundFontPath_; }

private:
	MIX_Mixer * const mixer_;
	std::vector<std::unique_ptr<Track>> seTracks_;
	std::unique_ptr<Track> musicTrack_;
	std::string soundFontPath_;

};

} // namespace Mix_
} // namespace SDL_

#endif // SDLMIXMIXER_H_
