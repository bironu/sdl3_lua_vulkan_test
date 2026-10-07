#if !defined(SDLMIXTRACK_H_)
#define SDLMIXTRACK_H_

#include "misc/Uncopyable.h"

struct MIX_Mixer;
struct MIX_Track;

namespace SDL_
{
namespace Mix_
{

// MIX_Track(ミキサー上の再生スロット)のラッパー。MIX_CreateTrack/
// MIX_DestroyTrackをコンストラクタ/デストラクタに対応させ、RAIIで
// 解放漏れを防ぐ。
class Track
{
public:
	UNCOPYABLE(Track);
	explicit Track(MIX_Mixer *mixer);
	~Track();

	MIX_Track *get() const { return track_; }

private:
	MIX_Track * const track_;
};

} // namespace Mix_
} // namespace SDL_

#endif // SDLMIXTRACK_H_
