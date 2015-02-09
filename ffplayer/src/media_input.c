#include "media_input.h"
#include "media_defs.h"


int media_input_init(MediaInput *mediain, const char *filein)
{
	AVFormatContext *ifmt_ctx = NULL;

	// audio
	AVCodecContext	*acodec_ctx=NULL;
	AVCodec			*acodec=NULL;
	int		astream_index = -1;

	int ret = 0;
	int i = 0;

	if(!mediain) return -1;
	if(!filein)       return -2;

	 ifmt_ctx = avformat_alloc_context();

	// open input
	if ((ret = avformat_open_input(&ifmt_ctx, filein, 0, 0)) < 0) {
		ret = FF_ERROR_AVFORMAT_OPEN_INPUT; return ret;
	}
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
		ret = FF_ERROR_FIND_STREAM_INFO; goto eout;
	}

	av_dump_format(ifmt_ctx, 0, filein, 0);

	astream_index = -1;

	for(i=0; i < ifmt_ctx->nb_streams; i++)
	{
		if(ifmt_ctx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO){
			astream_index=i;
			break;
		}
	}

	if(astream_index != -1)
	{
		acodec_ctx = ifmt_ctx->streams[astream_index]->codec;

		// Find the decoder for the audio stream
		acodec=avcodec_find_decoder(acodec_ctx->codec_id);
		if(acodec==NULL){
			ret =FF_ERROR_FIND_CODEC;
			goto eout;
		}

		if(avcodec_open2(acodec_ctx, acodec,NULL)<0){
			ret =FF_ERROR_OPEN_CODEC;
			goto eout;
		}
	}

	mediain->ifmt_ctx = ifmt_ctx;
	mediain->astream_index = astream_index;
	mediain->acodec_ctx = acodec_ctx;
	mediain->acodec = acodec;
	return 0;

eout:
	avformat_close_input(ifmt_ctx);
	avcodec_close(acodec_ctx);
	return ret;
}

int media_input_clean(MediaInput *mediain)
{
	if(mediain)
	{
		avcodec_close(mediain->acodec_ctx);
		avformat_close_input(&mediain->ifmt_ctx);
	}

	return 0;
}