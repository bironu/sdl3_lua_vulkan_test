#include "sdl/SDLMixMixer.h"
#include "sdl/SDLMixAudio.h"
#include "sdl/SDLMixTrack.h"
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_properties.h>
#include <random>

namespace SDL_
{
namespace Mix_
{

namespace {

// MIX_SetTrackLoops()は「再生中のトラックのループ回数を途中で変更する」ための
// 関数であり、停止中のトラックに対しては効果を持たない。MIX_PlayTrack()が
// 開始時にMIX_PROP_PLAY_LOOPS_NUMBER(未指定時は0=ループなし)でループ回数を
// 上書きしてしまうため、再生開始時のループ回数はここでプロパティとして指定する。
bool playTrackLooped(MIX_Track *track, int loops)
{
	SDL_PropertiesID options = ::SDL_CreateProperties();
	::SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
	const bool result = ::MIX_PlayTrack(track, options);
	::SDL_DestroyProperties(options);
	return result;
}

} // namespace

Mixer::Mixer()
	: mixer_(::MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr))
	, seTracks_()
	, musicTrack_(std::make_unique<Track>(mixer_))
{
}

Mixer::~Mixer()
{
	// Track(MIX_Track)はMIX_DestroyMixer()より先に破棄しておく必要があるため、
	// メンバの暗黙のデストラクト順(宣言の逆順)任せにせず明示的にここで破棄する
	musicTrack_.reset();
	seTracks_.clear();
	::MIX_DestroyMixer(mixer_);
}

int Mixer::allocateChannels(int size)
{
	seTracks_.clear();
	seTracks_.reserve(size);
	for (int i = 0; i < size; ++i) {
		seTracks_.push_back(std::make_unique<Track>(mixer_));
	}
	return static_cast<int>(seTracks_.size());
}

int Mixer::playSound(Audio &sound, int channel, int loops)
{
	if (seTracks_.empty()) {
		return -1;
	}
	int index = 0;
	if (channel >= 0 && static_cast<std::size_t>(channel) < seTracks_.size()) {
		index = channel;
	}
	else {
		// -1(空きチャンネル自動選択)相当: 再生中でないトラックを探す。
		// 見つからなければ先頭のトラックを横取りする。
		for (std::size_t i = 0; i < seTracks_.size(); ++i) {
			if (!::MIX_TrackPlaying(seTracks_[i]->get())) {
				index = static_cast<int>(i);
				break;
			}
		}
	}
	MIX_Track *track = seTracks_[index]->get();
	::MIX_SetTrackAudio(track, sound.get());
	playTrackLooped(track, loops);
	return index;
}

bool Mixer::isChannelPlaying(int channel) const
{
	if (channel < 0 || static_cast<std::size_t>(channel) >= seTracks_.size()) {
		return false;
	}
	return ::MIX_TrackPlaying(seTracks_[channel]->get());
}

bool Mixer::playMusic(Audio &sound, int loops)
{
	::MIX_SetTrackAudio(musicTrack_->get(), sound.get());
	return playTrackLooped(musicTrack_->get(), loops);
}

bool Mixer::isMusicPlaying() const
{
	return ::MIX_TrackPlaying(musicTrack_->get());
}

bool Mixer::isMusicPaused() const
{
	return ::MIX_TrackPaused(musicTrack_->get());
}

bool Mixer::stopMusic()
{
	return ::MIX_StopTrack(musicTrack_->get(), 0);
}

void Mixer::pauseMusic()
{
	::MIX_PauseTrack(musicTrack_->get());
}

void Mixer::resumeMusic()
{
	::MIX_ResumeTrack(musicTrack_->get());
}

void Mixer::rewindMusic()
{
	::MIX_SetTrackPlaybackPosition(musicTrack_->get(), 0);
}

void Mixer::setMusicGain(float gain)
{
	::MIX_SetTrackGain(musicTrack_->get(), gain);
}

void Mixer::setMusicTempo(float ratio)
{
	::MIX_SetTrackFrequencyRatio(musicTrack_->get(), ratio);
}

void Mixer::randomizeMusicTempo(bool enable)
{
	if (!enable) {
		setMusicTempo(1.0f);
		return;
	}
	static std::mt19937 rng{std::random_device{}()};
	static std::uniform_real_distribution<float> dist(0.9f, 1.1f);
	setMusicTempo(dist(rng));
}

bool Mixer::setSoundFonts(const char *path)
{
	// SDL3_mixer(MIX_*)には旧Mix_SetSoundFontsに相当するグローバルSoundFont
	// 設定APIが見当たらないため、ここではパスを保持しておくだけにし、
	// Audio::Audio()がMIDIを読み込む際にMIX_LoadAudioWithProperties()の
	// デコーダ固有プロパティ(SDL_mixer.decoder.fluidsynth.soundfont_path)として
	// 個別に指定する(SDLMixAudio.cpp参照)。
	soundFontPath_ = path ? path : "";
	return !soundFontPath_.empty();
}

} // namespace Mix_
} // namespace SDL_
