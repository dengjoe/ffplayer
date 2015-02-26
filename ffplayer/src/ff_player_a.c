/**
 * 最简单的基于FFmpeg的音频播放器 2 (SDL 2.0)
 * Simplest FFmpeg Audio Player 2 (SDL 2.0)
 *
 * 该版本使用SDL 2.0替换了第一个版本中的SDL 1.0。
 * 注意：SDL 2.0中音频解码的API并无变化。唯一变化的地方在于
 * 其回调函数的中的Audio Buffer并没有完全初始化，需要手动初始化。
 * 本例子中即SDL_memset(stream, 0, len);
 *
 * 本程序实现了音频的解码和播放。
 *
 * This software decode and play audio streams.
 * Version 2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>


#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/samplefmt.h"

//SDL
#include "SDL.h"
#include "SDL_thread.h"

#include "media_input.h"
#include "media_chunk.h"

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio


//Output PCM
//#define OUTPUT_PCM 1
//Use SDL
#define USE_SDL 1

static ChunkData *audio_chunk = NULL;



/* The audio function callback takes the following parameters: 
 * stream: A pointer to the audio buffer to be filled 
 * len: The length (in bytes) of the audio buffer 
*/ 
static void  audio_callback(void *udata,uint8_t *stream,int len)
{ 
	//SDL 2.0
	SDL_memset(stream, 0, len);
	if(audio_chunk->data_len==0)		//  Only play if we have data left 
			return; 
			
	len=(len>audio_chunk->data_len?audio_chunk->data_len:len);	// Mix  as much data as possible 

	SDL_MixAudio(stream,audio_chunk->data,len,SDL_MIX_MAXVOLUME);
	audio_chunk->data += len; 
	audio_chunk->data_len -= len; 
} 


int ff_play_audio(const char *filein)
{
	FILE *pfile=NULL;
	AVPacket *packet=NULL;

	//Out Audio Param
	enum AVSampleFormat out_sample_fmt=0;
	uint64_t out_channel_layout=0;
	int         out_nb_samples=0;
	int         out_sample_rate=0;
	int         out_channels = 0;

	//Out Buffer Size
	uint8_t *out_buffer= NULL;
	int        out_buffer_size =0;
	AVFrame	*pFrame;

	SDL_AudioSpec wanted_spec;

	int ret = 0;
	int len = 0;
	int got_picture = 0;
	int index = 0;
	int64_t in_channel_layout = 0;
	struct SwrContext *au_convert_ctx;

	MediaInput *mediain = NULL;
	int err = 0;

	av_register_all();
	avformat_network_init();

	mediain = media_input_init(filein, &err);
	if(mediain == 0) 
	{
		printf("error in init input, error=%d\n", err);
		goto eout;
	}


#if OUTPUT_PCM
	pfile=fopen("output.pcm", "wb");
#endif

	packet=(AVPacket *)av_malloc(sizeof(AVPacket));
	av_init_packet(packet);

	//Out Audio Param
	out_channel_layout=AV_CH_LAYOUT_STEREO;
	out_nb_samples=1024; 
	out_sample_fmt=AV_SAMPLE_FMT_S16;
	out_sample_rate=44100;
	out_channels=av_get_channel_layout_nb_channels(out_channel_layout);

	//Out Buffer Size
	out_buffer_size=av_samples_get_buffer_size(NULL,out_channels ,out_nb_samples,out_sample_fmt, 1);
	audio_chunk = chunk_data_init(out_buffer_size*4, &err);
	if(!audio_chunk)
	{
		printf("error in chunk_data_init, error=%d\n", err);
		goto eout;
	}

	pFrame=av_frame_alloc();
	
//SDL------------------
#if USE_SDL
	//Init
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {  
		printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
		return -1;
	}

	wanted_spec.freq = mediain->acodec_ctx->sample_rate; //out_sample_rate; 
	wanted_spec.format = AUDIO_S16SYS; 
	wanted_spec.channels = mediain->acodec_ctx->channels; 
	wanted_spec.silence = 0; 
	wanted_spec.samples = out_nb_samples; // Audio buffer size in samples (power of 2)
	wanted_spec.callback = audio_callback; 
	wanted_spec.userdata = mediain->acodec_ctx; 

	if (SDL_OpenAudio(&wanted_spec, NULL)<0)
	{ 
		printf("can't open audio.\n"); 
		return -1; 
	} 
#endif

	printf("Bitrate:\t %3d\n",          mediain->ifmt_ctx->bit_rate);
	printf("Decoder Name:\t %s\n", mediain->acodec_ctx->codec->long_name);
	printf("Channels:\t %d\n",         mediain->acodec_ctx->channels);
	printf("Sample per Second\t %d \n", mediain->acodec_ctx->sample_rate);

	//FIX:Some Codec's Context Information is missing
	in_channel_layout=av_get_default_channel_layout(mediain->acodec_ctx->channels);
	au_convert_ctx = swr_alloc();
	au_convert_ctx=swr_alloc_set_opts(au_convert_ctx,out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout,mediain->acodec_ctx->sample_fmt , mediain->acodec_ctx->sample_rate,0, NULL);
	swr_init(au_convert_ctx);

	while(av_read_frame(mediain->ifmt_ctx, packet)>=0)
	{
		if(packet->stream_index==mediain->astream_index)
		{
			ret = avcodec_decode_audio4( mediain->acodec_ctx, pFrame,&got_picture, packet);
			if ( ret < 0 ) {
                printf("Error in decoding audio frame.\n");
                return -1;
            }

			if ( got_picture > 0 )
			{
				swr_convert(au_convert_ctx,&audio_chunk->buf, audio_chunk->bufsize,
					(const uint8_t **)pFrame->data , pFrame->nb_samples);

				printf("index:%5d\t pts:%10d\t packet size:%d\n",index,packet->pts,packet->size);

				//FIX:FLAC,MP3,AAC Different number of samples
				if(wanted_spec.samples!=pFrame->nb_samples)
				{
					SDL_CloseAudio();
					out_nb_samples = pFrame->nb_samples;
					out_buffer_size = av_samples_get_buffer_size(NULL,out_channels ,out_nb_samples,out_sample_fmt, 1);

					wanted_spec.samples = out_nb_samples;
					SDL_OpenAudio(&wanted_spec, NULL);
				}

#if OUTPUT_PCM
				fwrite(out_buffer, 1, out_buffer_size, pfile);//Write PCM
#endif
				
				index++;
			}
//SDL------------------
#if USE_SDL
			chunk_data_reset(audio_chunk, out_buffer_size);//Set audio buffer (PCM data)
			//Play
			SDL_PauseAudio(0);
			while(audio_chunk->data_len>0)
				SDL_Delay(1);  //Wait until finish
#endif
		}
		av_free_packet(packet);
	}

	swr_free(&au_convert_ctx);

#if USE_SDL
	SDL_CloseAudio();//Close SDL
	SDL_Quit();
#endif

#if OUTPUT_PCM
	fclose(pfile);
#endif

eout:
	if(out_buffer)
	{
		av_free(out_buffer);
		out_buffer = NULL;
	}
	media_input_clean(mediain);
	chunk_data_clean(audio_chunk);
	return 0;
}


