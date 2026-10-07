#include "sdl/SDLMixTrack.h"
#include <SDL3_mixer/SDL_mixer.h>

namespace SDL_
{
namespace Mix_
{

Track::Track(MIX_Mixer *mixer)
	: track_(mixer ? ::MIX_CreateTrack(mixer) : nullptr)
{
}

Track::~Track()
{
	::MIX_DestroyTrack(track_);
}

} // namespace Mix_
} // namespace SDL_
