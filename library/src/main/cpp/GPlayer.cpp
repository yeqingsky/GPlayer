#include <jni.h>
#include <string>
#include <source/MediaSource.h>
#include <utils/JniHelper.h>
#include <base/Log.h>
#include <source/GPlayerMgr.h>
#include <media/MediaInfoJni.h>
#include <media/MediaDataJni.h>

extern "C" {
#include <protocol/protocol.h>
#include <codec/ffmpeg/libavcodec/jni.h>
}

//#define ENABLE_FFMPEG_JNI 1

static MediaCallback sMediaCallback;

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    LOGI("GPlayerC", "JNI_OnLoad");
    JNIEnv *env = nullptr;
    jint result = JNI_ERR;
    JniHelper::sJavaVM = vm;

    if (vm->GetEnv((void **) &env, JNI_VERSION_1_4) != JNI_OK) {
        LOGE("GPlayerC", "JNI_OnLoad fail %d", result);
        return result;
    }

    sMediaCallback.av_init = &(GPlayerMgr::av_init);
    sMediaCallback.av_feed_audio = &(GPlayerMgr::av_feed_audio);
    sMediaCallback.av_feed_video = &(GPlayerMgr::av_feed_video);
    sMediaCallback.av_destroy = &(GPlayerMgr::av_destroy);

    MediaInfoJni::initClassAndMethodJni();
    MediaDataJni::initClassAndMethodJni();

#ifdef ENABLE_FFMPEG_JNI
    int init_ffmpeg_jni_result = av_jni_set_java_vm(vm, nullptr);
    if (init_ffmpeg_jni_result < 0) {
        LOGE("GPlayerC", "av_jni_set_java_vm fail : %s", av_err2str(init_ffmpeg_jni_result));
    }
#endif

    result = JNI_VERSION_1_4;

    return result;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_gibbs_gplayer_GPlayer_nInitAVSource(JNIEnv *env, jobject clazz, jobject jAVSource) {
    auto pGPlayerImp = new GPlayerImp(jAVSource);
    std::string url = pGPlayerImp->getOutputSource()->getUrl();
    int channelId = -1;
    if (!url.empty()) {
        if (url.find("file:") == 0) {
            string fileUrl = url.substr(5, url.length());
            channelId = create_channel(PROTOCOL_TYPE_FILE, fileUrl.c_str(), sMediaCallback);
            LOGI("GPlayerC", "nInitP2PSource channelId = %d, url = %s", channelId, fileUrl.c_str());
            pGPlayerImp->getInputSource()->setChannelId(channelId);
            GPlayerMgr::sGPlayerMap[channelId] = pGPlayerImp;
        } else if (url.find("p2p:") == 0) {
            string deviceId = url.substr(4, url.length());
            channelId = create_channel(PROTOCOL_TYPE_P2P, deviceId.c_str(), sMediaCallback);
            LOGI("GPlayerC", "nInitP2PSource channelId = %d, url = %s", channelId,
                 deviceId.c_str());
            pGPlayerImp->getInputSource()->setChannelId(channelId);
            GPlayerMgr::sGPlayerMap[channelId] = pGPlayerImp;
        } else {
            LOGI("GPlayerC", "nInitP2PSource not support url ", url.c_str());
        }
    }
    return channelId;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_gibbs_gplayer_GPlayer_nFinish(JNIEnv *env, jobject thiz, jlong channel_id,
                                       jboolean force) {
    LOGI("GPlayerC", "nFinish channelId : %lld, force : %d", channel_id, force);
    auto targetPlayer = GPlayerMgr::sGPlayerMap[channel_id];
    if (!targetPlayer) {
        return;
    }
    auto targetSource = targetPlayer->getInputSource();
    auto outputSource = targetPlayer->getOutputSource();
    if (!targetSource || !outputSource) {
        return;
    }
    string url = outputSource->getUrl();
    int type = -1;
    if (!url.empty()) {
        if (url.find("file:") == 0) {
            type = PROTOCOL_TYPE_FILE;
        } else if (url.find("p2p:") == 0) {
            type = PROTOCOL_TYPE_P2P;
        }
    }
    int channelId = targetSource->getChannelId();
    if (is_channel_connecting(type, channelId)) {
        destroy_channel(type, channelId);
    }
    targetSource->pendingFlushBuffer();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_gibbs_gplayer_GPlayer_nDestroyAVSource(JNIEnv *env, jobject clazz, jlong channel_id) {
    LOGI("GPlayerC", "nDestroyAVSource channelId : %lld", channel_id);
    GPlayerMgr::deleteFromMap(channel_id);
}
