// ffMixer.c : Defines the entry point for the console application.
//

#include "ffMixer.h"
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


#define INPUT_SAMPLERATE     48000
#define INPUT_FORMAT         AV_SAMPLE_FMT_FLTP
#define INPUT_CHANNEL_LAYOUT AV_CH_LAYOUT_5POINT0
#define VOLUME_VAL 0.90


int init_mix_filter_graph(AVFilterGraph **graph, AVFilterContext **src, AVFilterContext **src1,
	AVFilterContext **sink);

int ff_mix_audio(const char *filein1, const char* filein2)
{
	AVFilterGraph *graph = NULL;
	AVFilterContext *src = NULL;
	AVFilterContext *src1 = NULL;
	AVFilterContext *sink = NULL;
	AVFrame *frame = NULL;
	
	uint8_t errstr[1024];
	int err;
	int i = 0;

	avfilter_register_all();

	err = init_mix_filter_graph(&graph, &src, &src1, &sink);
	if (err < 0) {
		fprintf(stderr, "Unable to init filter graph:");
		goto failout;
	}

	avfilter_graph_free(&graph);
//	av_frame_free(&frame);
	return 0;

failout:
	av_strerror(err, errstr, sizeof(errstr));
	fprintf(stderr, "%s\n", errstr);
	return 1;
}


static int init_mix_filter_graph(AVFilterGraph **graph, AVFilterContext **src, AVFilterContext **src1,
                             AVFilterContext **sink)
{
    AVFilterGraph *filter_graph;
    AVFilterContext *abuffer_ctx;
    AVFilter        *abuffer;
    AVFilterContext *aformat_ctx;
    AVFilter        *aformat;
	AVFilterContext *abuffer_ctx1;
	AVFilter        *abuffer1;
	AVFilterContext *aformat_ctx1;
	AVFilter        *aformat1;

	AVFilterContext *amerge_ctx;
	AVFilter        *amerge;
    AVFilterContext *abuffersink_ctx;
    AVFilter        *abuffersink;

    AVDictionary *options_dict = NULL;
    uint8_t options_str[1024];
    uint8_t ch_layout[64];

	AVRational tm_base = {1, INPUT_SAMPLERATE};
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
    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, INPUT_CHANNEL_LAYOUT);
    av_opt_set    (abuffer_ctx, "channel_layout", ch_layout,     AV_OPT_SEARCH_CHILDREN);
    av_opt_set    (abuffer_ctx, "sample_fmt",     av_get_sample_fmt_name(INPUT_FORMAT), AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q  (abuffer_ctx, "time_base",      tm_base,  AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(abuffer_ctx, "sample_rate",    INPUT_SAMPLERATE,  AV_OPT_SEARCH_CHILDREN);

    /* Now initialize the filter; we pass NULL options, since we have already  set all the options above. */
    err = avfilter_init_str(abuffer_ctx, NULL);
    if (err < 0) {
        fprintf(stderr, "Could not initialize the abuffer filter.\n");
        return err;
    }

	//Create the abuffer filter1
	abuffer1 = avfilter_get_by_name("abuffer");
	if (!abuffer1) {
		fprintf(stderr, "Could not find the abuffer filter.\n");
		return AVERROR_FILTER_NOT_FOUND;
	}

	abuffer_ctx1 = avfilter_graph_alloc_filter(filter_graph, abuffer1, "src1");
	if (!abuffer_ctx1) {
		fprintf(stderr, "Could not allocate the abuffer instance.\n");
		return AVERROR(ENOMEM);
	}

	/* Set the filter options through the AVOptions API. */
	av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, INPUT_CHANNEL_LAYOUT);
	av_opt_set    (abuffer_ctx1, "channel_layout", ch_layout,     AV_OPT_SEARCH_CHILDREN);
	av_opt_set    (abuffer_ctx1, "sample_fmt",     av_get_sample_fmt_name(INPUT_FORMAT), AV_OPT_SEARCH_CHILDREN);
	av_opt_set_q  (abuffer_ctx1, "time_base",      tm_base,  AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(abuffer_ctx1, "sample_rate",    INPUT_SAMPLERATE,  AV_OPT_SEARCH_CHILDREN);

	/* Now initialize the filter; we pass NULL options, since we have already  set all the options above. */
	err = avfilter_init_str(abuffer_ctx1, NULL);
	if (err < 0) {
		fprintf(stderr, "Could not initialize the abuffer filter.\n");
		return err;
	}

	// Create the aformat filter1
    aformat1 = avfilter_get_by_name("aformat");
    if (!aformat1) {
        fprintf(stderr, "Could not find the aformat filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    aformat_ctx1 = avfilter_graph_alloc_filter(filter_graph, aformat1, "aformat1");
    if (!aformat_ctx1) {
        fprintf(stderr, "Could not allocate the aformat instance.\n");
        return AVERROR(ENOMEM);
    }

    /* A third way of passing the options is in a string of the form
     * key1=value1:key2=value2.... */
    sprintf(options_str, "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
             av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), 44100,
             (uint64_t)AV_CH_LAYOUT_STEREO);
    err = avfilter_init_str(aformat_ctx1, options_str);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not initialize the aformat filter.\n");
        return err;
    }


    // Create merge filter. 
    amerge = avfilter_get_by_name("amerge");
    if (!amerge) {
        fprintf(stderr, "Could not find the amerge filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    amerge_ctx = avfilter_graph_alloc_filter(filter_graph, amerge, "amerge");
    if (!amerge_ctx) {
        fprintf(stderr, "Could not allocate the volume instance.\n");
        return AVERROR(ENOMEM);
    }

    /* A different way of passing the options is as key/value pairs in a  dictionary. */
    av_dict_set(&options_dict, "inputs", AV_STRINGIFY(2), 0);
    err = avfilter_init_dict(amerge_ctx, &options_dict);
    av_dict_free(&options_dict);
    if (err < 0) {
        fprintf(stderr, "Could not initialize the volume filter.\n");
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
             av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), 44100,
             (uint64_t)AV_CH_LAYOUT_STEREO);
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
        err = avfilter_link(aformat_ctx, 0, amerge_ctx, 0);

	err = avfilter_link(abuffer_ctx1, 0, aformat_ctx1, 0);
	if (err >= 0)
		err = avfilter_link(aformat_ctx1, 0, amerge_ctx, 1);

   if (err >= 0)
        err = avfilter_link(amerge_ctx, 0, abuffersink_ctx, 0);
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
	*src1   = abuffer_ctx1;
    *sink  = abuffersink_ctx;

    return 0;
}

