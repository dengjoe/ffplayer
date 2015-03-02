// tutorial03.c
// A pedagogical video player that will stream through every video frame as fast as it can
// and play audio (out of sync).
//
//
// Run using
// tutorial03 myvideofile.mpg
//
// to play the stream on your screen.


#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/samplefmt.h"

#include "SDL.h"
#include "SDL_thread.h"

#include "media_input.h"
#include "packet_queue.h"

//#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
//#endif

#include <stdio.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000


PacketQueue audioq;
int quit = 0;

static SwrContext * init_swr_context(AVCodecContext *acodec_ctx, uint64_t out_channel_layout, enum AVSampleFormat out_sample_fmt)
{
	SwrContext *swr_ctx = NULL;
	int out_sample_rate = 44100;
	int64_t in_channel_layout = 0;

	if(!acodec_ctx) return NULL;

	in_channel_layout=av_get_default_channel_layout(acodec_ctx->channels);
	swr_ctx = swr_alloc();
	swr_ctx=swr_alloc_set_opts(swr_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, acodec_ctx->sample_fmt , acodec_ctx->sample_rate, 0, NULL);
	swr_init(swr_ctx);

	return swr_ctx;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) 
{
	static AVPacket pkt;
	static uint8_t *audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	static AVFrame frame;

	int len1 = 0;
	int data_size = 0;

	for(;;) 
	{
		while(audio_pkt_size > 0) 
		{
			int got_frame = 0;
			len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
			if(len1 < 0) {
				audio_pkt_size = 0;
				break;
			}

			audio_pkt_data += len1;
			audio_pkt_size -= len1;
			if (got_frame)
			{
				data_size = av_samples_get_buffer_size ( NULL, aCodecCtx->channels, 
					frame.nb_samples, aCodecCtx->sample_fmt, 1 );
				memcpy(audio_buf, frame.data[0], data_size);
			}
			if(data_size <= 0) {
				continue;// No data yet, get more frames
			}

			return data_size;// We have data, return it and come back for more later
		}

		if(pkt.data)
			av_free_packet(&pkt);

		if(quit) return -1;

		if(packet_queue_get(&audioq, &pkt, 1) < 0) {
			return -1;
		}
		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}
}

static void audio_callback(void *userdata, Uint8 *stream, int len) 
{
	AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	int len1;
	int audio_size;

	static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;

	while(len > 0) //want to fill all len 
	{
		if(audio_buf_index >= audio_buf_size) 
		{
			//We have already sent all our data; get more 
			audio_size = audio_decode_frame(aCodecCtx, audio_buf, audio_buf_size);
			if(audio_size < 0) 
			{
				// If error, output silence
				audio_buf_size = 1024; // arbitrary?
				memset(audio_buf, 0, audio_buf_size);
			} 
			else 
			{
				audio_buf_size = audio_size;
			}
			audio_buf_index = 0;
		}

		len1 = audio_buf_size - audio_buf_index;
		if(len1 > len)
			len1 = len;

		memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}

	return;
}

static void audio_callback2(void *userdata, Uint8 *stream, int len) 
{
	int ret = 0;
	MediaInput *mediain = (MediaInput *)userdata;
	AVCodecContext *acodec_ctx = NULL;
	struct SwrContext   *swr_ctx = NULL;

	static AVPacket packet;
	static AVFrame frame;
	static int got_frame = 0;

	static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static unsigned int audio_buf_size = 0;
	static int index = 0;
	static int out_buffer_size = 0;

	if(!mediain) return;
	if(!mediain->acodec_ctx) return;
	if(!mediain->swr_ctx) return;

	acodec_ctx = mediain->acodec_ctx;
	swr_ctx = mediain->swr_ctx;

	for(;;)
	{
		if(packet_queue_get(&audioq, &packet, 1) < 0) 
			break;

		ret = avcodec_decode_audio4( mediain->acodec_ctx, &frame, &got_frame, &packet);
		if ( ret < 0 ) 
		{
			printf("Error in decoding audio frame.\n");
			break;
		}

		if ( got_frame > 0 )
		{
			swr_convert(swr_ctx, &audio_buf, audio_buf_size, (const uint8_t **)frame.data , frame.nb_samples);

			printf("index:%5d\t pts=%10d,\t packet size:%d\n", index, packet.pts, packet.size);

			////FIX:FLAC,MP3,AAC Different number of samples
			//if(mediain->a_spec.samples!=frame.nb_samples)
			//{
			//	SDL_CloseAudio();
			//	mediain->out_buffer_size = av_samples_get_buffer_size(NULL,mediain->out_channels ,frame.nb_samples,mediain->out_sample_fmt, 1);

			//	mediain->a_spec.samples = frame.nb_samples;
			//	SDL_OpenAudio(&mediain->a_spec, NULL);
			//	SDL_PauseAudio(0); //Play
			//}

			//push mem
			SDL_memset(stream, 0, len);
			len=(len>mediain->out_buffer_size ? mediain->out_buffer_size:len);	// get min value.Mix  as much data as possible 

			SDL_MixAudio(stream, audio_buf, len, SDL_MIX_MAXVOLUME);

			index++;
			av_free_packet(&packet);
			return;
		}

		av_free_packet(&packet);
	}

	return;
}

int ff_play_media_av0(const char *filein)
{
	MediaInput *mediain = NULL;
	int   err = 0;
	int   ret = 0;
	int   i;

	uint8_t  *out_buffer = NULL;
	AVPacket   packet;
	AVFrame   *pFrame = NULL; 
	AVFrame	 *pFrameYUV = NULL;

	int             got_frame = 0;

	SDL_Window *screen = NULL; 
	SDL_Renderer *sdlRenderer = NULL;
	SDL_Texture *sdlTexture = NULL;// SDL_Overlay     *bmp = NULL;
	SDL_Rect        rect;
	SDL_Event       event;
	SDL_AudioSpec   wanted_spec;
	SDL_AudioSpec   spec;

	struct SwsContext   *sws_ctx            = NULL;
	AVDictionary        *videoOptionsDict   = NULL;
	AVDictionary        *audioOptionsDict   = NULL;

	// audio out param==
	enum AVSampleFormat aout_sample_fmt=0;
	uint64_t aout_channel_layout=0;
	int      aout_channels  = 0;
	int      aout_nb_samples=0;
	uint8_t *aout_buffer= NULL;
	int      aout_buffer_size =0;
	//===

	if(!filein) return -1;

	av_register_all();

	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		return -2;
	}

	// Open video file
	mediain = media_input_init(filein, &err);
	if(mediain == 0) 
	{
		printf("error in init input, error=%d\n", err);
		goto eout;
	}

	// init audio

	// Set audio settings from codec info
	wanted_spec.freq = mediain->acodec_ctx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = mediain->acodec_ctx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
	//wanted_spec.callback = audio_callback;
	//wanted_spec.userdata = mediain->acodec_ctx;
	wanted_spec.callback = audio_callback2;
	wanted_spec.userdata = mediain;

	if(SDL_OpenAudio(&wanted_spec, &spec) < 0) 
	{
		fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
		goto eout;
	}

	//Out Audio Param===
	aout_channel_layout=AV_CH_LAYOUT_STEREO;
	aout_nb_samples=1024; 
	aout_sample_fmt=AV_SAMPLE_FMT_S16;
	aout_channels=av_get_channel_layout_nb_channels(aout_channel_layout);
	aout_buffer_size=av_samples_get_buffer_size(NULL,aout_channels ,aout_nb_samples,aout_sample_fmt, 1);

	mediain->swr_ctx = init_swr_context(mediain->acodec_ctx, aout_channel_layout, aout_sample_fmt);
	if(!mediain->swr_ctx) {printf("init_swr_context error.\n"); goto eout;}
	mediain->a_spec = wanted_spec;
	mediain->out_channels = aout_channels;
	mediain->out_sample_fmt = aout_sample_fmt;
	mediain->out_buffer_size = aout_buffer_size;
	//===

	packet_queue_init(&audioq);
	SDL_PauseAudio(0);

	// init video
	// Allocate video frame
	pFrame=avcodec_alloc_frame();
	pFrameYUV = av_frame_alloc();
	out_buffer=(uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, mediain->vcodec_ctx->width, mediain->vcodec_ctx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, mediain->vcodec_ctx->width, mediain->vcodec_ctx->height);

	// SDL---------
	// Make a screen to put our video
	screen = SDL_CreateWindow("ff_player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		mediain->vcodec_ctx->width, mediain->vcodec_ctx->height, SDL_WINDOW_OPENGL);
	if(!screen) {  
		printf("SDL: could not create window - exiting:%s\n",SDL_GetError());  
		goto eout;
	}

	sdlRenderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);  
	//IYUV: Y + U + V  (3 planes)
	//YV12: Y + V + U  (3 planes)
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
													mediain->vcodec_ctx->width, mediain->vcodec_ctx->height);  
	rect.x=0;
	rect.y=0;
	rect.w=mediain->vcodec_ctx->width;
	rect.h=mediain->vcodec_ctx->height;


	sws_ctx = sws_getContext( mediain->vcodec_ctx->width, mediain->vcodec_ctx->height, mediain->vcodec_ctx->pix_fmt,
		mediain->vcodec_ctx->width, mediain->vcodec_ctx->height, PIX_FMT_YUV420P, 
		SWS_BILINEAR, NULL, NULL, NULL );


	i=0;
	while(av_read_frame(mediain->ifmt_ctx, &packet)>=0) 
	{
		if(packet.stream_index==mediain->vstream_index)
		{
			ret = avcodec_decode_video2(mediain->vcodec_ctx, pFrame, &got_frame, &packet);
			if(got_frame) 
			{
				// Convert the image into YUV format that SDL uses
				sws_scale (sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 
					0, mediain->vcodec_ctx->height, pFrameYUV->data, pFrameYUV->linesize);

				SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );  
				SDL_RenderClear( sdlRenderer );  
				//SDL_RenderCopy( sdlRenderer, sdlTexture, &rect, &rect );  
				SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);  
				SDL_RenderPresent( sdlRenderer );  

				av_free_packet(&packet);
			}
		} 
		else if(packet.stream_index==mediain->astream_index) {
			packet_queue_put(&audioq, &packet);
		} 
		else {
			av_free_packet(&packet);
		}

		// Free the packet that was allocated by av_read_frame
		SDL_PollEvent(&event);
		switch(event.type) 
		{
		case SDL_QUIT:
			quit = 1;
			SDL_Quit();
			exit(0);
			break;
		default:
			break;
		}

	}

	SDL_CloseAudio();//Close SDL
	SDL_Quit();

eout:
	av_frame_free(&pFrame);
	av_frame_free(&pFrameYUV);
	media_input_clean(mediain);

	return 0;
}
