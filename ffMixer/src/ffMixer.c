// ffMixer.c : Defines the entry point for the console application.
//

#include "ffMixer.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavfilter/avfilter.h"
#include "libavutil/samplefmt.h"

//SDL
#include "SDL.h"
#include "SDL_thread.h"

int ff_mix_audio(const char *filein1, const char* filein2)
{
	return 0;
}

