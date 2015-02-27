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

#include "SDL.h"
#include "SDL_thread.h"

#include "packet_queue.h"

//#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
//#endif

#include <stdio.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000


PacketQueue audioq;
int quit = 0;



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

void audio_callback(void *userdata, Uint8 *stream, int len) 
{
	AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
	int len1;
	int audio_size;

	static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;

	while(len > 0)
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
			else {
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
}

int ff_play_media_av0(const char *filein)
{
	AVFormatContext *pFormatCtx = NULL;
	AVCodecContext  *pCodecCtx = NULL;
	AVCodec             *pCodec = NULL;
	int   videoStream;
	int   audioStream;
	int   i;

	AVFrame   *pFrame = NULL; 
	AVFrame	 *pFrameYUV = NULL;
	uint8_t       *out_buffer = NULL;
	AVPacket        packet;
	int             frameFinished;

	AVCodecContext  *aCodecCtx = NULL;
	AVCodec         *aCodec = NULL;

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

	if(!filein) return -1;

	av_register_all();

	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		return -2;
	}

	// Open video file
	if(avformat_open_input(&pFormatCtx, filein, NULL, NULL)!=0)
		return -3; 

	if(avformat_find_stream_info(pFormatCtx, NULL)<0)
		return -4; 

	av_dump_format(pFormatCtx, 0, filein, 0);

	// Find the first video stream
	videoStream=-1;
	audioStream=-1;
	for(i=0; i<pFormatCtx->nb_streams; i++) 
	{
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO &&
			videoStream < 0) {
				videoStream=i;
		}
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO &&
			audioStream < 0) {
				audioStream=i;
		}
	}
	if(videoStream==-1)
		return -1; // Didn't find a video stream
	if(audioStream==-1)
		return -1;

	aCodecCtx=pFormatCtx->streams[audioStream]->codec;
	// Set audio settings from codec info
	wanted_spec.freq = aCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = aCodecCtx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
	wanted_spec.callback = audio_callback;
	wanted_spec.userdata = aCodecCtx;

	if(SDL_OpenAudio(&wanted_spec, &spec) < 0) 
	{
		fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
		return -1;
	}

	aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
	if(!aCodec) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1;
	}
	avcodec_open2(aCodecCtx, aCodec, &audioOptionsDict);

	// audio_st = pFormatCtx->streams[index]
	packet_queue_init(&audioq);
	SDL_PauseAudio(0);

	// video

	pCodecCtx=pFormatCtx->streams[videoStream]->codec;

	// Find the decoder for the video stream
	pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec==NULL) {
		fprintf(stderr, "Unsupported codec!\n");
		return -1; // Codec not found
	}

	if(avcodec_open2(pCodecCtx, pCodec, &videoOptionsDict)<0)
		return -1; // Could not open codec

	// Allocate video frame
	pFrame=avcodec_alloc_frame();
	pFrameYUV = av_frame_alloc();
	out_buffer=(uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);

	//-------SDL---------//
	// Make a screen to put our video
	screen = SDL_CreateWindow("videp player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		pCodecCtx->width, pCodecCtx->height, SDL_WINDOW_OPENGL);
	if(!screen) {  
		printf("SDL: could not create window - exiting:%s\n",SDL_GetError());  
		return -1;
	}

	sdlRenderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);  
	//IYUV: Y + U + V  (3 planes)
	//YV12: Y + V + U  (3 planes)
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,pCodecCtx->width,pCodecCtx->height);  
	rect.x=0;
	rect.y=0;
	rect.w=pCodecCtx->width;
	rect.h=pCodecCtx->height;


	// Allocate a place to put our YUV image on that screen
	//bmp = SDL_CreateYUVOverlay(pCodecCtx->width,
	//		 pCodecCtx->height, SDL_YV12_OVERLAY,  screen);

	sws_ctx = sws_getContext( pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, 
		SWS_BILINEAR, NULL, NULL, NULL );

	// Read frames and save first five frames to disk
	i=0;
	while(av_read_frame(pFormatCtx, &packet)>=0) 
	{
		// Is this a packet from the video stream?
		if(packet.stream_index==videoStream) {
			// Decode video frame
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

			// Did we get a video frame?
			if(frameFinished) 
			{
				//SDL_LockYUVOverlay(bmp);

				//AVPicture pict;
				//pict.data[0] = bmp->pixels[0];
				//pict.data[1] = bmp->pixels[2];
				//pict.data[2] = bmp->pixels[1];

				//pict.linesize[0] = bmp->pitches[0];
				//pict.linesize[1] = bmp->pitches[2];
				//pict.linesize[2] = bmp->pitches[1];

				// Convert the image into YUV format that SDL uses
				sws_scale (sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 
					0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

				//SDL_UnlockYUVOverlay(bmp);
				//
				//rect.x = 0;
				//rect.y = 0;
				//rect.w = pCodecCtx->width;
				//rect.h = pCodecCtx->height;
				//SDL_DisplayYUVOverlay(bmp, &rect);

				SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );  
				SDL_RenderClear( sdlRenderer );  
				//SDL_RenderCopy( sdlRenderer, sdlTexture, &rect, &rect );  
				SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);  
				SDL_RenderPresent( sdlRenderer );  

				av_free_packet(&packet);
			}
		} 
		else if(packet.stream_index==audioStream) {
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

	// Free the YUV frame
	//av_free(pFrame);
	av_frame_free(&pFrame);
	av_frame_free(&pFrameYUV);

	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);

	return 0;
}
