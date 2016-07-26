/*
 * Copyright (c) 2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual
 * property and proprietary rights in and to this software,
 * related documentation and any modifications thereto.  Any use,
 * reproduction, disclosure or distribution of this software and
 * related documentation without an express license agreement from
 * NVIDIA Corporation is strictly prohibited.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE
 * IS PROVIDED *AS IS* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL
 * WARRANTIES, EITHER EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED
 * TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  IN NO EVENT SHALL NVIDIA OR ITS SUPPLIERS BE
 * LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR
 * LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS
 * INFORMATION, OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF
 * OR INABILITY TO USE THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGES
 */
#pragma once


#include "libnvpipecodec/nvpipecodec264.h"
#include <cstdio>
#include <limits>

#define NVPIPE_H264_ENCODER_NAME "h264_nvenc"
#define NVPIPE_H264_DECODER_NAME "h264_cuvid"

NvPipeCodec264::NvPipeCodec264() {
    printf("nv_codec_h264 created\n");
    
    encoder_context_ = NULL;
    encoder_codec_ = NULL;
    encoder_frame_ = NULL;

    decoder_context_ = NULL;
    decoder_codec_ = NULL;
    decoder_frame_ = NULL;
    
    frame_pixel_format_ = AV_PIX_FMT_RGB24; 
    
    // doesn't really matter
    encoder_config_corrupted_ = true;
    decoder_config_corrupted_ = true;
    // register all available file formats and codecs
    // could be initialized multiple times.

    av_register_all();
}

NvPipeCodec264::~NvPipeCodec264() {
    printf("nv_codec_h264 destroyed\n");
    
    if (decoder_codec_ && decoder_context_) {
        avcodec_close(decoder_context_);
    }
    if (decoder_context_) {
        av_free(decoder_context_);
    }
    if (decoder_frame_) {
        av_frame_free(&decoder_frame_);
    }
    
    
    if (encoder_codec_ && encoder_context_) {
        avcodec_close(encoder_context_);
    }
    if (encoder_context_) {
        av_free(encoder_context_);
    }
    if (encoder_frame_) {
        av_frame_free(&encoder_frame_);
    }

}

void NvPipeCodec264::setImageSize(   int width,
                                int height,
                                enum NVPipeImageFormat format ) {
    if ( width != width_ || height != height_ || format != format_ ) {
        encoder_config_corrupted_ = true;
        decoder_config_corrupted_ = true;

        switch( format ) {
        case NVPIPE_IMAGE_FORMAT_RGBA:
            frame_pixel_format_ = AV_PIX_FMT_RGBA;
            break;

        case NVPIPE_IMAGE_FORMAT_RGB:
            frame_pixel_format_ = AV_PIX_FMT_RGB24; 
            break;

        case NVPIPE_IMAGE_FORMAT_YUV420P:
            frame_pixel_format_ = AV_PIX_FMT_YUV420P;
            break;

        case NVPIPE_IMAGE_FORMAT_YUV444P:
            frame_pixel_format_ = AV_PIX_FMT_YUV444P;
            break;

        default:
            printf("unrecognized pixel format, set to RGB24 as default");
            frame_pixel_format_ = AV_PIX_FMT_RGB24;
            break;
        }
        NvPipeCodec::setImageSize(width, height, format);
    }
}

void NvPipeCodec264::setInputFrameBuffer(void* frame_buffer, size_t buffer_size) {

    if ( frame_buffer != frame_ || buffer_size != frame_buffer_size_ ) {
        encoder_config_corrupted_ = true;
        NvPipeCodec::setInputFrameBuffer(frame_buffer, buffer_size);
    }
}

int NvPipeCodec264::encode(void* buffer, size_t &output_buffer_size) {

    if (width_ == 0 || height_ == 0 
        || format_ == NVPIPE_IMAGE_FORMAT_NULL ) {
            printf("input frame has to be defined \
                    before calling NvPipeCodec264::encoding");
            return -1;
    }
    
    // Check if encoder codec has been initialized
    if (encoder_codec_ == NULL) {
        encoder_codec_ = avcodec_find_encoder_by_name(NVPIPE_H264_ENCODER_NAME);
        if (encoder_codec_ == NULL) {
            printf("cannot find encoder: %s", NVPIPE_H264_ENCODER_NAME);
            return -1;
        }
    }

    // Check if encoder codecContext has been allocated
    if (encoder_context_ == NULL) {
        encoder_context_ = avcodec_alloc_context3( encoder_codec_ );
        if (encoder_context_ == NULL) {
            printf("cannot allocate codec context");
            return -1;
        }
    }

    // Check if encoder frame has been initialized
    if (encoder_frame_ == NULL) {
        encoder_frame_ = av_frame_alloc();
        if (encoder_frame_ == NULL) {
            printf("cannot allocate frame");
            return -1;
        }
        
    }

    // Check if encoder frame parameter has been updated
    if (encoder_config_corrupted_) {

        /*
         * setup codecContext
         * Default low latency setup for nvenc
         */
        // put bit_rate
        encoder_context_->bit_rate = 400000;
        // frames per second
        encoder_context_->time_base = (AVRational){1,25};
        encoder_context_->gop_size = std::numeric_limits<int>::max();
        encoder_context_->max_b_frames = 0;
        encoder_context_->width = width_;
        encoder_context_->height = height_;
        encoder_context_->pix_fmt = frame_pixel_format_;
        // nvenc private setting
        av_opt_set(encoder_context_->priv_data,"preset", "llhq", 0);
        av_opt_set(encoder_context_->priv_data, "rc", "ll_2pass_quality", 0);
        av_opt_set_int(encoder_context_->priv_data, "cbr", 1, 0);    
        av_opt_set_int(encoder_context_->priv_data, "2pass", 1, 0);
        av_opt_set_int(encoder_context_->priv_data, "delay", 0, 0);


        encoder_frame_->format = frame_pixel_format_;
        encoder_frame_->width = width_;
        encoder_frame_->height = height_;

        if (avcodec_open2(encoder_context_, encoder_codec_, NULL) != 0) {
            printf("cannot open codec\n");
            return -1;
        }

        // setup input data buffer to encoder_frame_
        // Note the allocation of data buffer is done by user
        if ( av_image_fill_arrays ( encoder_frame_->data, 
                                    encoder_frame_->linesize,
                                    (const uint8_t*) frame_, 
                                    frame_pixel_format_,
                                    width_,
                                    height_,
                                    64 ) < 0 ) {
            printf("could not associate image buffer to frame");
            return -1;
        }
        encoder_config_corrupted_ = false;
    }

    av_init_packet(&encoder_packet_);
    encoder_packet_.data = NULL;
    encoder_packet_.size = 0;

    int ret = avcodec_send_frame(encoder_context_, encoder_frame_);

    // debug information (remove before release)
    if ( ret < 0 ){
        printf("\nreceive_packet went wrong! %d\n", ret);
        switch(ret) {
            case AVERROR(EOF):
                printf("eof\n");
                break;
            case AVERROR(EAGAIN):
                printf("EAGAIN\n");
                break;
            case AVERROR(EINVAL):
                printf("EINVAL\n");
                break;
            case AVERROR(ENOMEM):
                printf("ENOMEN\n");
                break;
        }
    }

    ret = avcodec_receive_packet(encoder_context_, &encoder_packet_);
    
    // debug information (remove before release)
    if ( ret < 0 ){
        printf("\nreceive_packet went wrong! %d\n", ret);
        switch(ret) {
            case AVERROR(EOF):
                printf("eof\n");
                break;
            case AVERROR(EAGAIN):
                printf("EAGAIN\n");
                break;
            case AVERROR(EINVAL):
                printf("EINVAL\n");
                break;
            case AVERROR(ENOMEM):
                printf("ENOMEN\n");
                break;
        }
    }

    if ( encoder_packet_.size > output_buffer_size ) {
        output_buffer_size = encoder_packet_.size;
        av_packet_unref(&encoder_packet_);
        printf("packet size larger than  buffer_size went wrong!");
        return -1;
    }

    memcpy(buffer, encoder_packet_.data, encoder_packet_.size);

    // output the packet size;
    output_buffer_size = encoder_packet_.size;

    av_packet_unref(&encoder_packet_);

    //avcodec_flush_buffers(encoder_context_);
    
    return 0;
}

int NvPipeCodec264::decode(void* output_picture, int &width, int &height, size_t &output_size) {

    
    // Check if decoder codec has been initialized
    if (decoder_codec_ == NULL) {
        decoder_codec_ = avcodec_find_decoder_by_name(NVPIPE_H264_DECODER_NAME);
        if (decoder_codec_ == NULL) {
            printf("cannot find decoder: %s", NVPIPE_H264_DECODER_NAME);
            return -1;
        }
    }

    // Check if decoder codecContext has been allocated
    if (decoder_context_ == NULL) {
        decoder_context_ = avcodec_alloc_context3(decoder_codec_);
        if (decoder_context_ == NULL) {
            printf("cannot allocate codec context");
            return -1;
        }
    }

    // Check if decoder frame has been allocated
    if ( decoder_frame_ == NULL ) {
        decoder_frame_ = av_frame_alloc();
        if (decoder_frame_ == NULL) {
            printf("cannot allocate frame");
            return -1;
        }
    }

    if ( decoder_config_corrupted_ ) {
        decoder_context_->delay = 0;
        if (avcodec_open2(decoder_context_, 
                            decoder_codec_, NULL) < 0) {
            printf("cannot open codec\n");
            return -1;
        }
        decoder_config_corrupted_ = false;
    }

    av_init_packet(&decoder_packet_);
    decoder_packet_.data = (uint8_t *) packet_;
    decoder_packet_.size = packet_buffer_size_;

    if ( avcodec_send_packet(   decoder_context_,
                                &decoder_packet_) != 0 ) {
        printf("send_packet went wrong!\n");
    }

    int ret = avcodec_receive_frame( decoder_context_,
                                    decoder_frame_);

    // debug information (remove before release)
    if ( ret < 0 ){
        printf("\nreceive_frame went wrong! %d\n", ret);
        switch(ret) {
            case AVERROR(EOF):
                printf("eof\n");
                break;
            case AVERROR(EAGAIN):
                printf("EAGAIN\n");
                break;
            case AVERROR(EINVAL):
                printf("EINVAL\n");
                break;
            case AVERROR(ENOMEM):
                printf("ENOMEN\n");
                break;
        }
    }

    size_t frameSize = decoder_frame_->height;
    frameSize *= decoder_frame_->linesize[0];

    width = decoder_frame_->width;
    height = decoder_frame_->height;

    if (frameSize > output_size ) {
        output_size = frameSize;
        printf("frame size larger than frame_buffer_size_,\
                something went wrong!\n");
        return -1;
    }

    memcpy(output_picture, decoder_frame_->data[0], frameSize);
    // output the image size;
    output_size = frameSize;

    av_packet_unref(&decoder_packet_);

    return 0;
}
