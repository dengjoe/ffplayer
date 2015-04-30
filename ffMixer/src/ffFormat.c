// ffFormat.c : Defines the entry point for the console application.
//

#include "ffFormat.h"
#include "media_input.h"

#include <stdio.h>
#include <stdlib.h>

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

//SDL
#include "SDL.h"
#include "SDL_thread.h"


//#define INPUT_SAMPLERATE     48000
//#define INPUT_FORMAT         AV_SAMPLE_FMT_FLTP  //AV_SAMPLE_FMT_S16
//#define INPUT_CHANNEL_LAYOUT AV_CH_LAYOUT_5POINT0
#define VOLUME_VAL 0.90

typedef struct audio_param_s{
	enum AVSampleFormat sample_fmt;
	int sample_rate;
	int channel_layout;
	AVRational time_base;
}audio_param_t;

int init_format_filter_graph(AVFilterGraph **graph, AVFilterContext **src, AVFilterContext **sink,
						audio_param_t *src_param, audio_param_t *dst_param);


int ff_format_audio(const char *filein, const char* fileout)
{
	AVFilterGraph *graph = NULL;
	AVFilterContext *src = NULL;
	AVFilterContext *src1 = NULL;
	AVFilterContext *sink = NULL;
	AVPacket packet;
	AVFrame *frame = NULL;
	MediaInput *media_in = NULL;
	audio_param_t src_param;
	audio_param_t dst_param;

	uint8_t errstr[1024];
	int ret = 0;
	int got_frame = 0;
	int i = 0;

	av_register_all();
	avfilter_register_all();


	media_in = media_input_init(filein, &ret);
	if(!media_in){
		fprintf(stderr, "Unable to open input file!");
		goto failout;
	}

	src_param.channel_layout = media_in->acodec_ctx->channel_layout;
	src_param.sample_fmt = media_in->acodec_ctx->sample_fmt;
	src_param.sample_rate = media_in->acodec_ctx->sample_rate;
	src_param.time_base.num = 1;
	src_param.time_base.den = src_param.sample_rate;

	dst_param.channel_layout = AV_CH_LAYOUT_STEREO;
	dst_param.sample_fmt = AV_SAMPLE_FMT_S16;
	dst_param.sample_rate = 48000;
	dst_param.time_base.num = 1;
	dst_param.time_base.den = dst_param.sample_rate;

	ret = init_format_filter_graph(&graph, &src, &sink, &src_param, &dst_param);
	if (ret < 0) {
		fprintf(stderr, "Unable to init filter graph:");
		goto failout;
	}

	frame  = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Error allocating the frame\n");
		goto failout;;
	}

	while(av_read_frame(media_in->ifmt_ctx, &packet)>=0)
	{
		if(packet.stream_index==media_in->astream_index)
		{
			ret = avcodec_decode_audio4( media_in->acodec_ctx, frame, &got_frame, &packet);
			if ( ret < 0 ) {
				printf("Error in decoding audio frame.\n");
				return -1;
			}

			if ( got_frame > 0 )
			{
				ret = av_buffersrc_add_frame(src, frame);
				if (ret < 0) {
					av_frame_unref(frame);
					fprintf(stderr, "Error submitting the frame to the filtergraph:");
					goto failout;
				}

				/* Get all the filtered output that is available. */
				while ((ret = av_buffersink_get_frame(sink, frame)) >= 0) {
					//err = process_output(frame);
					//if (err < 0) {
					//	fprintf(stderr, "Error processing the filtered frame:");
					//	goto failout;
					//}
					av_frame_unref(frame);
				
				}

#if OUTPUT_PCM
				fwrite(out_buffer, 1, out_buffer_size, pfile);//Write PCM
#endif	
				i++;
			}

		}
		av_free_packet(&packet);
	}

failout:

	if(graph) {avfilter_graph_free(&graph); graph=NULL;}
	if(media_in) {media_input_clean(media_in);media_in=NULL;}
	if(frame) {av_frame_free(&frame);frame=NULL;}

	return ret;
}


static int init_format_filter_graph(AVFilterGraph **graph, AVFilterContext **src, AVFilterContext **sink,
					audio_param_t *src_param, audio_param_t *dst_param)
{
    AVFilterGraph *filter_graph;
    AVFilterContext *abuffer_ctx;
    AVFilter        *abuffer;
    AVFilterContext *aformat_ctx;
    AVFilter        *aformat;
    AVFilterContext *abuffersink_ctx;
    AVFilter        *abuffersink;

    AVDictionary *options_dict = NULL;
    char options_str[1024];
    uint8_t ch_layout[64];

    int err;

    // Create a new filtergraph
    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        fprintf(stderr, "Unable to create filter graph.\n");
        return AVERROR(ENOMEM);
    }

    //Create the abuffer filter
    abuffer = avfilter_get_by_name("abuffer");
    if (!abuffer) {
        fprintf(stderr, "Could not find the abuffer filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    abuffer_ctx = avfilter_graph_alloc_filter(filter_graph, abuffer, "src");
    if (!abuffer_ctx) {
        fprintf(stderr, "Could not allocate the abuffer instance.\n");
        return AVERROR(ENOMEM);
    }

    /* Set the filter options through the AVOptions API. */
    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, src_param->channel_layout);
    av_opt_set    (abuffer_ctx, "channel_layout", ch_layout,     AV_OPT_SEARCH_CHILDREN);
    av_opt_set    (abuffer_ctx, "sample_fmt",     av_get_sample_fmt_name(src_param->sample_fmt), AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q  (abuffer_ctx, "time_base",      src_param->time_base,  AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(abuffer_ctx, "sample_rate",    src_param->sample_rate,  AV_OPT_SEARCH_CHILDREN);

    /* Now initialize the filter; we pass NULL options, since we have already  set all the options above. */
    err = avfilter_init_str(abuffer_ctx, NULL);
    if (err < 0) {
        fprintf(stderr, "Could not initialize the abuffer filter.\n");
        return err;
    }

    // Create the aformat filter
    aformat = avfilter_get_by_name("aformat");
    if (!aformat) {
        fprintf(stderr, "Could not find the aformat filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    aformat_ctx = avfilter_graph_alloc_filter(filter_graph, aformat, "aformat");
    if (!aformat_ctx) {
        fprintf(stderr, "Could not allocate the aformat instance.\n");
        return AVERROR(ENOMEM);
    }

    /* A third way of passing the options is in a string of the form 
     * key1=value1:key2=value2.... */
    sprintf(options_str, "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
             av_get_sample_fmt_name(dst_param->sample_fmt), dst_param->sample_rate,
             (uint64_t)dst_param->channel_layout);
    err = avfilter_init_str(aformat_ctx, options_str);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not initialize the aformat filter.\n");
        return err;
    }

    // Finally create the abuffersink filter
    abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffersink) {
        fprintf(stderr, "Could not find the abuffersink filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    abuffersink_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersink, "sink");
    if (!abuffersink_ctx) {
        fprintf(stderr, "Could not allocate the abuffersink instance.\n");
        return AVERROR(ENOMEM);
    }

    /* This filter takes no options. */
    err = avfilter_init_str(abuffersink_ctx, NULL);
    if (err < 0) {
        fprintf(stderr, "Could not initialize the abuffersink instance.\n");
        return err;
    }

    // Connect the filters
    err = avfilter_link(abuffer_ctx, 0, aformat_ctx, 0);
    if (err >= 0)
        err = avfilter_link(aformat_ctx, 0, abuffersink_ctx, 0);

    if (err < 0) {
        fprintf(stderr, "Error connecting filters\n");
        return err;
    }

    /* Configure the graph. */
    err = avfilter_graph_config(filter_graph, NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error configuring the filter graph\n");
        return err;
    }

    *graph = filter_graph;
    *src   = abuffer_ctx;
    *sink  = abuffersink_ctx;

    return 0;
}