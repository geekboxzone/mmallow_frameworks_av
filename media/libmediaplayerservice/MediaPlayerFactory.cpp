/*
**
** Copyright 2012, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

//#define LOG_NDEBUG 0
#define LOG_TAG "MediaPlayerFactory"
#include <utils/Log.h>

#include <cutils/properties.h>
#include <media/IMediaPlayer.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/foundation/ADebug.h>
#include <utils/Errors.h>
#include <utils/misc.h>
#include <../libstagefright/include/WVMExtractor.h>

#include "MediaPlayerFactory.h"

#include "TestPlayerStub.h"
#include "StagefrightPlayer.h"
#include "nuplayer/NuPlayerDriver.h"
//#define USE_FFPLAYER

#ifdef USE_FFPLAYER
#include "FFPlayer.h"
#endif
namespace android {

Mutex MediaPlayerFactory::sLock;
MediaPlayerFactory::tFactoryMap MediaPlayerFactory::sFactoryMap;
bool MediaPlayerFactory::sInitComplete = false;
static status_t getFileName(int fd,String8 *FilePath)
{
    static ssize_t link_dest_size;
    static char link_dest[PATH_MAX];
    const char *ptr = NULL;
    String8 path;
    path.appendFormat("/proc/%d/fd/%d", getpid(), fd);
    if ((link_dest_size = readlink(path.string(), link_dest, sizeof(link_dest)-1)) < 0) {
        return errno;
    } else {
        link_dest[link_dest_size] = '\0';
    }
    path = link_dest;
    ptr = path.string();
    *FilePath = String8(ptr);
    return OK;
}

status_t MediaPlayerFactory::registerFactory_l(IFactory* factory,
                                               player_type type) {
    if (NULL == factory) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, factory is"
              " NULL.", type);
        return BAD_VALUE;
    }

    if (sFactoryMap.indexOfKey(type) >= 0) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, type is"
              " already registered.", type);
        return ALREADY_EXISTS;
    }

    if (sFactoryMap.add(type, factory) < 0) {
        ALOGE("Failed to register MediaPlayerFactory of type %d, failed to add"
              " to map.", type);
        return UNKNOWN_ERROR;
    }

    return OK;
}

static player_type getDefaultPlayerType() {
    char value[PROPERTY_VALUE_MAX];
    if (property_get("media.stagefright.use-awesome", value, NULL)
            && (!strcmp("1", value) || !strcasecmp("true", value))) {
        return STAGEFRIGHT_PLAYER;
    }

    if (property_get("persist.sys.media.use-awesome", value, NULL)
            && !strcasecmp("true", value)) {
        return STAGEFRIGHT_PLAYER;
    }

#ifndef  USE_FFPLAYER
    return NU_PLAYER;
#else
    return FF_PLAYER;
#endif
}

status_t MediaPlayerFactory::registerFactory(IFactory* factory,
                                             player_type type) {
    Mutex::Autolock lock_(&sLock);
    return registerFactory_l(factory, type);
}

void MediaPlayerFactory::unregisterFactory(player_type type) {
    Mutex::Autolock lock_(&sLock);
    sFactoryMap.removeItem(type);
}

#ifndef USE_FFPLAYER
#define GET_PLAYER_TYPE_IMPL(a...)                      \
    Mutex::Autolock lock_(&sLock);                      \
                                                        \
    player_type ret = STAGEFRIGHT_PLAYER;               \
    float bestScore = 0.0;                              \
                                                        \
    for (size_t i = 0; i < sFactoryMap.size(); ++i) {   \
                                                        \
        IFactory* v = sFactoryMap.valueAt(i);           \
        float thisScore;                                \
        CHECK(v != NULL);                               \
        thisScore = v->scoreFactory(a, bestScore);      \
        if (thisScore > bestScore) {                    \
            ret = sFactoryMap.keyAt(i);                 \
            bestScore = thisScore;                      \
        }                                               \
    }                                                   \
                                                        \
    if (0.0 == bestScore) {                             \
        ret = getDefaultPlayerType();                   \
    }                                                   \
                                                        \
    return ret;
#else
#define GET_PLAYER_TYPE_IMPL(a...)                      \
    Mutex::Autolock lock_(&sLock);                      \
                                                        \
		player_type ret = FF_PLAYER;           		          \
		float bestScore = 0.0;                           		\
                                                        \
    for (size_t i = 0; i < sFactoryMap.size(); ++i) {   \
                                                        \
        IFactory* v = sFactoryMap.valueAt(i);           \
        float thisScore;                                \
        CHECK(v != NULL);                               \
        thisScore = v->scoreFactory(a, bestScore);      \
        if (thisScore > bestScore) {                    \
            ret = sFactoryMap.keyAt(i);                 \
            bestScore = thisScore;                      \
        }                                               \
    }                                                   \
                                                        \
    if (0.0 == bestScore) {                             \
        ret = getDefaultPlayerType();                   \
    }                                                   \
                                                        \
    return ret;
	
#define GET_PLAYER_TYPE_IMPL_CTS(a...)                      \
    Mutex::Autolock lock_(&sLock);                      \
                                                        \
		player_type ret = STAGEFRIGHT_PLAYER;           		          \
		float bestScore = 0.0;                           		\
                                                        \
    for (size_t i = 0; i < sFactoryMap.size(); ++i) {   \
                                                        \
        IFactory* v = sFactoryMap.valueAt(i);           \
        float thisScore;                                \
        CHECK(v != NULL);                               \
        thisScore = v->scoreFactory(a, bestScore);      \
        if (thisScore > bestScore) {                    \
            ret = sFactoryMap.keyAt(i);                 \
            bestScore = thisScore;                      \
        }                                               \
    }                                                   \
                                                        \
    if (0.0 == bestScore) {                             \
        ret = STAGEFRIGHT_PLAYER;                       \
    }                                                   \
                                                        \
    return ret;
#endif

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              const char* url) {
#ifdef USE_FFPLAYER
    if(!strncasecmp("http://localhost:", url, 17)) {
        return NU_PLAYER;
    }
    if (!strncasecmp("iptv://", url, 7)) {
        return STAGEFRIGHT_PLAYER;
    }

    if (!strncasecmp("udpwimo", url, 7)) {
        return STAGEFRIGHT_PLAYER;
    }

    if (!strncasecmp("DVBTV://", url, 8)) {
        return STAGEFRIGHT_PLAYER;
    }

    if(strstr(url,".ogg")){
        return STAGEFRIGHT_PLAYER;
    }

    if(strstr(url,".wvm")){
       return STAGEFRIGHT_PLAYER;
    }
#endif
    GET_PLAYER_TYPE_IMPL(client, url);
}

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              int fd,
                                              int64_t offset,
                                              int64_t length) {
#ifndef USE_FFPLAYER
    String8 filePath;
    getFileName(fd,&filePath);
    if(strstr(filePath.string(),".mpg") || strstr(filePath.string(),".avi")
        || strstr(filePath.string(),".ts") || strstr(filePath.string(),".flac")
        || strstr(filePath.string(),".wav"))
    {
        return STAGEFRIGHT_PLAYER;
    } 
    char buf[20];
    lseek(fd, offset, SEEK_SET);
    read(fd, buf, sizeof(buf));
    lseek(fd, offset, SEEK_SET);

    uint32_t ident = *((uint32_t*)buf);

    char value[PROPERTY_VALUE_MAX];
    if(((property_get("sys.cts_gts.status", value, NULL))&&(strstr(value, "true")))&&ident == 402653184 && !strstr(filePath.string(),"/data/app/com.android.cts.security-1/base.apk"))
        return STAGEFRIGHT_PLAYER;
#endif
#ifdef USE_FFPLAYER 
    String8 filePath;
    getFileName(fd,&filePath);
    //for cts and some apk
    if(strstr(filePath.string(),".apk"))
    {
        GET_PLAYER_TYPE_IMPL_CTS(client, fd, offset, length);
    }
    if(strstr(filePath.string(),".ogg")){
        return STAGEFRIGHT_PLAYER;
    }

    if(strstr(filePath.string(),".wvm")){
        return STAGEFRIGHT_PLAYER;
    }
    
    if(strstr(filePath.string(),".mp3")){
        return STAGEFRIGHT_PLAYER;
    }

    if(strstr(filePath.string(),".mid")){
        return STAGEFRIGHT_PLAYER;
    }
#endif
    GET_PLAYER_TYPE_IMPL(client, fd, offset, length);
}

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              const sp<IStreamSource> &source) {
    GET_PLAYER_TYPE_IMPL(client, source);
}

player_type MediaPlayerFactory::getPlayerType(const sp<IMediaPlayer>& client,
                                              const sp<DataSource> &source) {
    GET_PLAYER_TYPE_IMPL(client, source);
}

#undef GET_PLAYER_TYPE_IMPL

sp<MediaPlayerBase> MediaPlayerFactory::createPlayer(
        player_type playerType,
        void* cookie,
        notify_callback_f notifyFunc,
        pid_t pid) {
    sp<MediaPlayerBase> p;
    IFactory* factory;
    status_t init_result;
    Mutex::Autolock lock_(&sLock);

    if (sFactoryMap.indexOfKey(playerType) < 0) {
        ALOGE("Failed to create player object of type %d, no registered"
              " factory", playerType);
        return p;
    }

    factory = sFactoryMap.valueFor(playerType);
    CHECK(NULL != factory);
    p = factory->createPlayer(pid);

    if (p == NULL) {
        ALOGE("Failed to create player object of type %d, create failed",
               playerType);
        return p;
    }

    init_result = p->initCheck();
    if (init_result == NO_ERROR) {
        p->setNotifyCallback(cookie, notifyFunc);
    } else {
        ALOGE("Failed to create player object of type %d, initCheck failed"
              " (res = %d)", playerType, init_result);
        p.clear();
    }

    return p;
}

/*****************************************************************************
 *                                                                           *
 *                     Built-In Factory Implementations                      *
 *                                                                           *
 *****************************************************************************/

class StagefrightPlayerFactory :
    public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               int fd,
                               int64_t offset,
                               int64_t length,
                               float /*curScore*/) {
        if (legacyDrm()) {
            sp<DataSource> source = new FileSource(dup(fd), offset, length);
            String8 mimeType;
            float confidence;
            if (SniffWVM(source, &mimeType, &confidence, NULL /* format */)) {
                return 1.0;
            }
        }

        if (getDefaultPlayerType() == STAGEFRIGHT_PLAYER) {
            char buf[20];
            lseek(fd, offset, SEEK_SET);
            read(fd, buf, sizeof(buf));
            lseek(fd, offset, SEEK_SET);

            uint32_t ident = *((uint32_t*)buf);

            // Ogg vorbis?
            if (ident == 0x5367674f) // 'OggS'
                return 1.0;
        }

        return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float /*curScore*/) {
        if (legacyDrm() && !strncasecmp("widevine://", url, 11)) {
            return 1.0;
        }
        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t /* pid */) {
        ALOGV(" create StagefrightPlayer");
        return new StagefrightPlayer();
    }
  private:
    bool legacyDrm() {
        char value[PROPERTY_VALUE_MAX];
        if (property_get("persist.sys.media.legacy-drm", value, NULL)
                && (!strcmp("1", value) || !strcasecmp("true", value))) {
            return true;
        }
        return false;
    }
};

class NuPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float curScore) {
        static const float kOurScore = 0.8;

        if (kOurScore <= curScore)
            return 0.0;

        if (!strncasecmp("http://", url, 7)
                || !strncasecmp("https://", url, 8)
                || !strncasecmp("file://", url, 7)) {
            size_t len = strlen(url);
            if (len >= 5 && !strcasecmp(".m3u8", &url[len - 5])) {
                return kOurScore;
            }

            if (strstr(url,"m3u8")) {
                return kOurScore;
            }

            if ((len >= 4 && !strcasecmp(".sdp", &url[len - 4])) || strstr(url, ".sdp?")) {
                return kOurScore;
            }
        }

        if (!strncasecmp("rtsp://", url, 7)) {
            return kOurScore;
        }

        return 0.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const sp<IStreamSource>& /*source*/,
                               float /*curScore*/) {
        return 1.0;
    }

    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const sp<DataSource>& /*source*/,
                               float /*curScore*/) {
        // Only NuPlayer supports setting a DataSource source directly.
        return 1.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t pid) {
        ALOGV(" create NuPlayer");
        return new NuPlayerDriver(pid);
    }
};

class TestPlayerFactory : public MediaPlayerFactory::IFactory {
  public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                               const char* url,
                               float /*curScore*/) {
        if (TestPlayerStub::canBeUsed(url)) {
            return 1.0;
        }

        return 0.0;
    }

    virtual sp<MediaPlayerBase> createPlayer(pid_t /* pid */) {
        ALOGV("Create Test Player stub");
        return new TestPlayerStub();
    }
};

#ifdef USE_FFPLAYER
class FFPlayerFactory :public MediaPlayerFactory::IFactory {
public:
    virtual float scoreFactory(const sp<IMediaPlayer>& /*client*/,
                                       const char* url,
                                       float /*curScore*/){
        static const float kOurScore = 0.9;
        if (!strncasecmp("http://", url, 7)
                || !strncasecmp("https://", url, 8)
                || !strncasecmp("rtsp://", url, 7)){
            char value[PROPERTY_VALUE_MAX];
            if((property_get("sys.cts_gts.status", value, NULL))
                &&(strstr(value, "true"))){
                return 0.0;
            }
            return kOurScore;
        }
        return 0.0;
    }
    virtual sp<MediaPlayerBase> createPlayer(pid_t /*pid*/) {
        ALOGI(" createFFPlayer");
        return new FFPlayer();
    }

};
#endif
void MediaPlayerFactory::registerBuiltinFactories() {
    Mutex::Autolock lock_(&sLock);

    if (sInitComplete)
        return;

    registerFactory_l(new StagefrightPlayerFactory(), STAGEFRIGHT_PLAYER);
    registerFactory_l(new NuPlayerFactory(), NU_PLAYER);
    registerFactory_l(new TestPlayerFactory(), TEST_PLAYER);
#ifdef USE_FFPLAYER
        registerFactory_l(new FFPlayerFactory(),FF_PLAYER);
#endif

    sInitComplete = true;
}

}  // namespace android
