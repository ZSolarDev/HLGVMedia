#ifndef IPHONE
#define IMPLEMENT_API
#endif

#ifndef STATIC_LINK
#define IMPLEMENT_API
#endif

//#include <hx/CFFI.h>
//#include <hx/CFFIAPI.h>

#define HL_NAME(n) hlext_##n

#include <hl.h>
#include <system/CFFI.h>
#include <system/ValuePointer.h>


extern "C" {
	#include "ffmpeg/libavcodec/avcodec.h"
	#include "ffmpeg/libavutil/frame.h"
	#include "ffmpeg/libavformat/avformat.h"
	#include "ffmpeg/libavformat/avformat.h"
	#include "ffmpeg/libswscale/swscale.h"
	#include "ffmpeg/libswresample/swresample.h"
}

#include <stdio.h>
#include "ffmpeg/libavutil/channel_layout.h"


static int initialized = 0;

void __check_init() 
{
	if (!initialized)
	{
		initialized = 1;
		av_register_all();
	}
}

typedef struct {
	AVFormatContext *pFormatCtx;
	
	AVCodecContext  *videoCodecContext;
	AVCodec         *videoCodec;
	AVDictionary    *videoOptionsDict;
	
	AVCodecContext  *audioCodecContext;
	AVCodec         *audioCodec;
	AVDictionary    *audioOptionsDict;

	AVFrame         *pFrame;
	AVFrame         *pFrameRGB;
	uint8_t         *buffer;
	int videoStream;
	int audioStream;
	
	struct SwsContext      *sws_ctx;
	int numBytes;
} FfmpegContext;

FfmpegContext ffmpeg_context = {0};

#define   USED_PIX_FMT   AV_PIX_FMT_ARGB

int __ffmpeg_open_file(FfmpegContext *context, const char *name)
{
	if (avformat_open_input(&context->pFormatCtx, name, NULL, NULL)!=0) return -1; // Couldn't open file
	
	if(avformat_find_stream_info(context->pFormatCtx, NULL)<0) return -2; // Couldn't find stream information

	av_dump_format(context->pFormatCtx, 0, name, 0);
	
	context->videoStream=-1;
	context->audioStream=-1;
	for(int i=0; i<context->pFormatCtx->nb_streams; i++)
	{
		if(context->pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
			if (context->videoStream < 0) context->videoStream=i;
		}

		if(context->pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
			if (context->audioStream < 0) context->audioStream=i;
		}
	}
	
	if (context->videoStream == -1) return -1; // Didn't find a video stream
	if (context->audioStream == -1) return -1; // Didn't find a video stream
	
	context->videoCodecContext = context->pFormatCtx->streams[context->videoStream]->codec;
	context->videoCodec=avcodec_find_decoder(context->videoCodecContext->codec_id);
	if (context->videoCodec==NULL) return -3; // Codec not found
	if (avcodec_open2(context->videoCodecContext, context->videoCodec, &context->videoOptionsDict)<0) return -4; // Could not open codec
	
	context->audioCodecContext = context->pFormatCtx->streams[context->audioStream]->codec;
	context->audioCodec = avcodec_find_decoder(context->audioCodecContext->codec_id);
	if (context->audioCodec==NULL) return -3; // Codec not found
	if (avcodec_open2(context->audioCodecContext, context->audioCodec, &context->audioOptionsDict)<0) return -4; // Could not open codec

	#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,28,1)
		// Allocate video frame
		context->pFrame = av_frame_alloc();

		// Allocate an AVFrame structure
		context->pFrameRGB = av_frame_alloc();
	#else
		context->pFrame = avcodec_alloc_frame();
		context->pFrameRGB = avcodec_alloc_frame();
	#endif
	
	// Determine required buffer size and allocate buffer
	context->numBytes = avpicture_get_size(USED_PIX_FMT, context->videoCodecContext->width, context->videoCodecContext->height);
	context->buffer = (uint8_t *)av_malloc(context->numBytes*sizeof(uint8_t));

	context->sws_ctx = sws_getContext(context->videoCodecContext->width, context->videoCodecContext->height, context->videoCodecContext->pix_fmt, context->videoCodecContext->width, context->videoCodecContext->height, USED_PIX_FMT, SWS_BILINEAR, NULL, NULL, NULL);

	// Assign appropriate parts of buffer to image planes in pFrameRGB
	// Note that pFrameRGB is an AVFrame, but AVFrame is a superset
	// of AVPicture
	avpicture_fill((AVPicture *)context->pFrameRGB, context->buffer, USED_PIX_FMT, context->videoCodecContext->width, context->videoCodecContext->height);

	return 0;
}

void __fmpeg_close_file(FfmpegContext *context)
{
	// Free the RGB image
	av_free(context->buffer);
	av_free(context->pFrameRGB);

	// Free the YUV frame
	av_free(context->pFrame);

	// Close the codec
	avcodec_close(context->videoCodecContext);

	// Close the video file
	avformat_close_input(&context->pFormatCtx);
}

void __fmpeg_copy_frame_to_pointer(AVFrame *pFrame, int width, int height, char *output_ptr, int output_len)
{
	char *current_output_ptr = output_ptr;
	for (int y = 0; y < height; y++)
	{
		int copyCount = width * 4;
		memcpy(current_output_ptr, pFrame->data[0]+y*pFrame->linesize[0], copyCount);
		current_output_ptr += copyCount;
	}
}

int __fmpeg_decode_frame(FfmpegContext *context, char *output_ptr, int output_len, vclosure* emit_sound_callback)
{
	AVPacket        packet;
	int frameFinished;
	int generated_frame = 0;
	lime::ValuePointer* sound_callback = new lime::ValuePointer(emit_sound_callback);
	while(av_read_frame(context->pFormatCtx, &packet)>=0)
	{
		// Is this a packet from the video stream?
		if (packet.stream_index==context->videoStream) {
			// Decode video frame
			avcodec_decode_video2(context->videoCodecContext, context->pFrame, &frameFinished, &packet);

			// Did we get a video frame?
			if (frameFinished) {
				// Convert the image from its native format to RGB
				sws_scale(
					context->sws_ctx,
					(uint8_t const * const *)context->pFrame->data,
					context->pFrame->linesize,
					0,
					context->videoCodecContext->height,
					context->pFrameRGB->data,
					context->pFrameRGB->linesize
				);

				__fmpeg_copy_frame_to_pointer(context->pFrameRGB, context->videoCodecContext->width, context->videoCodecContext->height, output_ptr, output_len);
				generated_frame = 1;
			}
		}
		else if (packet.stream_index == context->audioStream) {
			static AVFrame audioFrame;
			int got_frame = 0;
			int len1 = avcodec_decode_audio4(context->audioCodecContext, &audioFrame, &got_frame, &packet);
        
			if (got_frame)
			{
			
				// frame_size
				
				//printf("%d, %d, %d\n", context->audioCodecContext->sample_rate, context->audioCodecContext->channels, context->audioCodecContext->sample_fmt);
        
				int in_nchannels = context->audioCodecContext->channels;
				int in_nsamples = audioFrame.nb_samples;
				AVSampleFormat in_sample_format = context->audioCodecContext->sample_fmt;
				int in_channel_layout = context->audioCodecContext->channel_layout;
				
				/*
				int in_size = av_samples_get_buffer_size(NULL, in_nchannels, in_nsamples, in_sample_format, 1);
				int out_size = in_size / in_nchannels;
				
				value array = alloc_array(0);
				for (int n = 0; n < in_nchannels; n++) {
					buffer out_buffer = alloc_buffer_len(out_size);
					memcpy((uint8_t *)buffer_data(out_buffer), audioFrame.data[n], out_size);
					val_array_push(array, buffer_val(out_buffer));
				}
				val_call1(emit_sound_callback, array);
				*/

				{
					SwrContext *swr = swr_alloc_set_opts(
						NULL,
						AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, 44100,
						context->audioCodecContext->channel_layout, context->audioCodecContext->sample_fmt, context->audioCodecContext->sample_rate,
						//context->audioCodecContext->channel_layout, frame.format, frame.sample_rate,
						0, NULL
					);
					
					swr_init(swr);
			
					int in_size = av_samples_get_buffer_size(NULL, in_nchannels, in_nsamples, in_sample_format, 1);
					int out_size = av_samples_get_buffer_size(NULL, in_nchannels, in_nsamples, AV_SAMPLE_FMT_FLT, 1);
					
					unsigned char * out_buffer = hl_alloc_bytes(out_size);

					uint8_t* out = (uint8_t*)out_buffer;
					
					//const uint8_t *in = (uint8_t *)audioFrame.data[0];
					
					swr_convert(swr, &out, in_nsamples, (const uint8_t **)audioFrame.data, in_nsamples);
					
					swr_free(&swr);

					sound_callback->Call(out_buffer);
				}
        
			}
		}
		av_free_packet(&packet);
		if (generated_frame) return 0;
	}
	return 1;
}


// MISC FUNCTIONS
HL_PRIM vbyte* HL_NAME(ffmpeg_get_version)(_NO_ARG)
{
	__check_init();
	const char* string = "2.1.1";
	int length = strlen(string);
	char* res = (char*)malloc(length + 1);
	strcpy(res, string);
	return (vbyte*)res;
}

// FILE FUNCTIONS
HL_PRIM void HL_NAME(ffmpeg_open_file)(hl_vstring* file_name)
{
	__check_init();
	__ffmpeg_open_file(&ffmpeg_context, file_name ? (char*)hl_to_utf8((const uchar*)file_name->bytes) : 0);
}

HL_PRIM void HL_NAME(ffmpeg_close_file)(_NO_ARG)
{
	__check_init();
	__fmpeg_close_file(&ffmpeg_context);
}


// VIDEO PROPERTY FUNCTIONS
HL_PRIM int HL_NAME(ffmpeg_get_width)(_NO_ARG)
{
	__check_init();
	return ffmpeg_context.videoCodecContext->width;
}

HL_PRIM int HL_NAME(ffmpeg_get_height)(_NO_ARG)
{
	__check_init();
	return ffmpeg_context.videoCodecContext->height;
}

// FRAME MANAGEMENT
HL_PRIM int HL_NAME(hx_ffmpeg_decode_frame)(hl_buffer* data_buffer_value, vclosure* emit_sound_callback)
{
	__check_init();
	unsigned char* data_buffer = (unsigned char*)data_buffer_value;
	//char* data_buffer_ptr = buffer_data(data_buffer);
	int data_buffer_len = hl_buffer_length(data_buffer_value);

	return __fmpeg_decode_frame(&ffmpeg_context, 0, data_buffer_len, emit_sound_callback);
}