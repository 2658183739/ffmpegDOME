#include <stdio.h>

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL.h"
};


//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit = 0;

int sfp_refresh_thread(void* opaque) {
	thread_exit = 0;
	while (!thread_exit) {
		SDL_Event event;
		event.type = SFM_REFRESH_EVENT;
		SDL_PushEvent(&event);
		SDL_Delay(40);
	}
	thread_exit = 0;
	//Break
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}


int main(int argc, char* argv[])
{

	AVFormatContext* pFormatCtx;
	//AVFormatContext 封装格式上下文结构体，也是统领全局的结构体，保存了视频文件封装
	//格式相关信息。
	int	i, videoindex;
	AVCodecContext* pCodecCtx;
	//AVCodecContext 编码器上下文结构体，保存了视频（音频）编解码相关信息。
	AVCodec* pCodec;
	//AVCodec 每种视频（音频）编解码器(例如H.264解码器)对应一个该结构体。
	AVFrame* pFrame, * pFrameYUV;
	//AVFrame 编码器上下文结构体，保存了视频（音频）编解码相关信息。
	uint8_t* out_buffer;
	AVPacket* packet;
	//AVPacket 存储一帧压缩编码数据。
	int ret, got_picture;

	//------------SDL----------------
	int screen_w, screen_h;
	SDL_Window* screen;
	//SDL_Window 代表了一个“窗口”
	SDL_Renderer* sdlRenderer;
	//SDL_Renderer 代表了一个“渲染器”
	SDL_Texture* sdlTexture;
	//SDL_Texture 代表了一个“纹理”
	SDL_Rect sdlRect;
	//SDL_Rect 一个简单的矩形结构
	SDL_Thread* video_tid;
	SDL_Event event;

	struct SwsContext* img_convert_ctx;

	char filepath[] = "屌丝男士.mov";//文件名
	//初始化
	av_register_all();//注册所有组件
	avformat_network_init();//初始化
	pFormatCtx = avformat_alloc_context();
	//avformat_open_input()打开输入视频文件
	if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}
	//avformat_find_stream_info 获取视频文件信息
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}
	videoindex = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoindex = i;
			break;
		}
	if (videoindex == -1) {
		printf("Didn't find a video stream.\n");
		return -1;
	}
	pCodecCtx = pFormatCtx->streams[videoindex]->codec;
	//avcodec_find_decoder 查找解码器
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	//avcodec_open2 打开解码器
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("Could not open codec.\n");
		return -1;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	out_buffer = (uint8_t*)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture*)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);

	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	//SDL_Init 初始化SDL系
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	//SDL 2.0 Support for multiple windows
	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	//SDL_CreateWindow 创建窗口SDL_Window
	screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);

	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	//SDL_CreateRenderer 创建渲染器SDL_Renderer
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	//IYUV: Y + U + V  (3 planes)
	//YV12: Y + V + U  (3 planes)
	//SDL_CreateTexture创建纹理SDL_Texture
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	packet = (AVPacket*)av_malloc(sizeof(AVPacket));

	video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL, NULL, NULL);
	//------------SDL End------------
	//Event Loop

	for (;;) {
		//Wait
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT) {
			//------------------------------
			//av_read_frame 从输入文件读取一帧压缩数据
			if (av_read_frame(pFormatCtx, packet) >= 0) {
				if (packet->stream_index == videoindex) {
					ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
					//avcodec_decode_video2 解压一帧压缩数据
					if (ret < 0) {
						printf("Decode Error.\n");
						return -1;
					}
					if (got_picture) {
						sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
						//SDL---------------------------
						SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
						//SDL_UpdateTexture设置纹理的数据
						SDL_RenderClear(sdlRenderer);
						//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
						SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
						//SDL_RenderCopy 将纹理的数据拷贝给渲染器
						SDL_RenderPresent(sdlRenderer);
						//SDL_RenderPresent 显示
						//SDL End-----------------------
					}
				}
				av_free_packet(packet);
			}
			else {
				//Exit Thread
				thread_exit = 1;
			}
		}
		else if (event.type == SDL_QUIT) {
			thread_exit = 1;
		}
		else if (event.type == SFM_BREAK_EVENT) {
			break;
		}

	}

	sws_freeContext(img_convert_ctx);

	SDL_Quit();//退出SDL系统
	//--------------
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);//关闭解码器
	avformat_close_input(&pFormatCtx);//关闭输入视频文件

	return 0;
}
