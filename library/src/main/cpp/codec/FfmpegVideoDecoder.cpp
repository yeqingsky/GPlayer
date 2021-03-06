#include <base/Log.h>
#include "FfmpegVideoDecoder.h"
#include "CodecUtils.h"

FfmpegVideoDecoder::FfmpegVideoDecoder() {
    isInitSuccess = false;
    mCodec = nullptr;
    mCodecContext = nullptr;
    mOutFrame = nullptr;
    mInPacket = nullptr;
}

FfmpegVideoDecoder::~FfmpegVideoDecoder() = default;

void FfmpegVideoDecoder::init(MediaInfo *header) {
    LOGI(TAG, "init width=%d,height=%d,frameRate=%d", header->videoWidth, header->videoHeight, header->videoFrameRate);
    mHeader = *header;

    av_register_all();

    auto codecId = (AVCodecID) CodecUtils::codecType2CodecId(mHeader.videoType);
    mCodec = avcodec_find_decoder(codecId);
    mCodecContext = avcodec_alloc_context3(mCodec);
    mCodecContext->codec_id = codecId;
    mCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    mCodecContext->pix_fmt = AV_PIX_FMT_YUV420P; //默认全都是YUV420P格式
    mCodecContext->width = mHeader.videoWidth;
    mCodecContext->height = mHeader.videoHeight;
    mCodecContext->bit_rate = 0;
    mCodecContext->time_base.den = mHeader.videoFrameRate;
    mCodecContext->time_base.num = 1;

    if (avcodec_open2(mCodecContext, mCodec, nullptr) < 0) {
        LOGE(TAG, "could not open encode-codec");
        return;
    }

    mInPacket = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(mInPacket);
    mInPacket->flags = AV_PKT_FLAG_KEY;

    mOutFrame = av_frame_alloc();

    isInitSuccess = true;
}

int FfmpegVideoDecoder::send_packet(MediaData *inPacket) {
    if (inPacket == nullptr) {
        LOGE(TAG, "decode the param is nullptr");
        return -1;
    }

    if (!isInitSuccess) {
        LOGE(TAG, "decoder init error");
        return -2;
    }

    mInPacket->data = inPacket->data;
    mInPacket->size = inPacket->size;
    mInPacket->pts = inPacket->pts;
    mInPacket->dts = inPacket->dts;

    int result = avcodec_send_packet(mCodecContext, mInPacket);
    if (result < 0) {
        LOGE(TAG, "Error: avcodec_send_packet %d %s", result, av_err2str(result));
        return result;
    }

    return 0;
}

int FfmpegVideoDecoder::receive_frame(MediaData *outFrame) {
    if (outFrame == nullptr) {
        LOGE(TAG, "decode the param is nullptr");
        return -1;
    }

    if (!isInitSuccess) {
        LOGE(TAG, "decoder init error");
        return -2;
    }

    int result = avcodec_receive_frame(mCodecContext, mOutFrame);
    if (result < 0) {
        LOGE(TAG, "Error: avcodec_receive_frame %d %s", result, av_err2str(result));
        return result;
    }

    //处理线宽
    if (mOutFrame->linesize[0] > mOutFrame->width) {
        uint8_t *srcY = mOutFrame->data[0];
        uint8_t *srcU = mOutFrame->data[1];
        uint8_t *srcV = mOutFrame->data[2];
        uint8_t *dstY = outFrame->data;
        uint8_t *dstU = outFrame->data1;
        uint8_t *dstV = outFrame->data2;
        for (int i = 0; i < mOutFrame->height; i++) {
            memcpy(dstY, srcY, static_cast<size_t>(mOutFrame->width));
            dstY += mOutFrame->width;
            srcY += mOutFrame->linesize[0];
        }
        for (int i = 0; i < (mOutFrame->height / 2); i++) {
            memcpy(dstU, srcU, static_cast<size_t>(mOutFrame->width / 2));
            dstU += (mOutFrame->width / 2);
            srcU += mOutFrame->linesize[1];
            memcpy(dstV, srcV, static_cast<size_t>(mOutFrame->width / 2));
            dstV += (mOutFrame->width / 2);
            srcV += mOutFrame->linesize[2];
        }
        auto dataSize = mOutFrame->height * mOutFrame->width;
        outFrame->size = dataSize;
        outFrame->size1 = dataSize / 4;
        outFrame->size2 = dataSize / 4;
    } else {
        outFrame->size = static_cast<uint32_t>(mOutFrame->height * mOutFrame->width);
        outFrame->size1 = outFrame->size / 4;
        outFrame->size2 = outFrame->size1;
        memcpy(outFrame->data, mOutFrame->data[0], outFrame->size);
        memcpy(outFrame->data1, mOutFrame->data[1], outFrame->size1);
        memcpy(outFrame->data2, mOutFrame->data[2], outFrame->size2);
    }

    outFrame->width = static_cast<uint32_t>(mOutFrame->width);
    outFrame->height = static_cast<uint32_t>(mOutFrame->height);
    outFrame->pts = static_cast<uint64_t>(mOutFrame->pts);
    outFrame->dts = outFrame->dts;
    outFrame->flag = static_cast<uint8_t>(mOutFrame->key_frame);

    return 0;
}

void FfmpegVideoDecoder::release() {
    LOGI(TAG, "release");
    if (mInPacket != nullptr) {
        av_packet_unref(mInPacket);
        mInPacket = nullptr;
    }

    if (mOutFrame != nullptr) {
        av_frame_free(&mOutFrame);
        mOutFrame = nullptr;
    }

    if (mCodecContext != nullptr) {
        // Close the codec
        avcodec_close(mCodecContext);
        avcodec_free_context(&mCodecContext);
        mCodecContext = nullptr;
    }
}