#include "extractor_interface.h"
#include "player_logger.h"

namespace lge {
namespace mm {

#define MAX_DBUS_TIMEOUT 5 //seconds
#define THUMBNAIL_EXTRACTOR_SERVICE  "com.lge.thumbnailextractor"
#define THUMBNAIL_EXTRACTOR_OBJECT_PATH  "/com/lge/thumbnailextractor/service"
#define THUMBNAIL_EXTRACTOR_INTERFACE "com.lge.thumbnailextractor.service"

std::function<void(MmError *)> *onConnectedCb = NULL;

static void
onExtractorEventCb (GDBusConnection* connection,
                    const gchar* sender_name,
                    const gchar* object_path,
                    const gchar* interface_name,
                    const gchar* signal_name,
                    GVariant* parameters,
                    gpointer user_data) {
    MMLogInfo("");
    MMLogInfo("%s is received \n", signal_name);
    ExtractorInterface *_this = ((ExtractorInterface *) user_data);
    if (!g_strcmp0(signal_name, "ExtractorEvent")) {
        const gchar *status = NULL, *path = NULL, *thumbnail = NULL;
        guint32 duration = 0, playTime = 0;

        g_variant_get(parameters, "(sssii)", &status, &path, &thumbnail, &duration, &playTime);

        _this->onExtractorEvent(std::string(status), std::string(path), std::string(thumbnail), duration, playTime);
    } else if (!g_strcmp0(signal_name, "CoverartEvent")) {
        const gchar *status = NULL, *artist = NULL, *album = NULL, *coverArt = NULL;

        g_variant_get(parameters, "(ssss)", &status, &artist, &album, &coverArt);

        _this->onCoverArtEvent(std::string(status), std::string(artist), std::string(album), std::string(coverArt));
    } else if (!g_strcmp0(signal_name, "SingleAudioInfoEvent")) {
        const gchar *status = NULL;
        const gchar *path = NULL;
        const gchar *coverArt = NULL;
        const gchar *title = NULL;
        const gchar *album = NULL;
        const gchar *artist = NULL;
        const gchar *genre = NULL;
        guint64 duration = 0;

        g_variant_get(parameters, "(sssssssi)",
                                    &status,
                                    &path,
                                    &coverArt,
                                    &title,
                                    &album,
                                    &artist,
                                    &genre,
                                    &duration);

        _this->onSingleAudioInfoEvent(std::string(status),
                                      std::string(path),
                                      std::string(coverArt),
                                      std::string(title),
                                      std::string(album),
                                      std::string(artist),
                                      std::string(genre),
                                      duration);
    }
}

static
void onNameAppeared (GDBusConnection *connection,
                     const gchar     *name,
                     const gchar     *name_owner,
                     void            *user_data) {
    MMLogInfo("Found thumbnailextractor on D-Bus\n");
    ((ExtractorInterface *) user_data)->mConnection = connection;
    ((ExtractorInterface *) user_data)->signalSubscribe();
    (*onConnectedCb) (NULL);
}

static
void onNameVanished (GDBusConnection *connection,
                     const gchar     *name,
                     void        *user_data) {
    MMLogError ("Failed to get name owner for %s\n", name);
    (*onConnectedCb) (new MmError("Failed to connect to thumbnailextractor, is process running?"));
}

/**
 * ================================================================================
 * @fn : ExtractorInterface
 * @brief : constructor of ExtractorInterface
 * @section : Function flow (Pseudo-code or Decision Table)
 *      register meta-object-type
 *      connect to thumbnail-extractor and register signal-slot.
 * @param [in] parent: QObject * - parent object
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
ExtractorInterface::ExtractorInterface()
  : mWatcherId(0),
    mConnection(NULL),
    mImageExtractedHandlerId(0),
    mCoverArtExtractedHandlerId(0) {

    connectExtractor([&](MmError *e) {
        if (!e) {
            return true;
        } else {
            MMLogError("Error connecting to : %s", e->message.c_str());
           return false;
        }
    });

    command_thread_.reset(new std::thread([&]{
      MMLogInfo("run Thread");
      command_context_ = g_main_context_new();
      g_main_context_push_thread_default(command_context_);

      GMainLoop* loop = g_main_loop_new(command_context_, FALSE);
      g_main_context_unref(command_context_);

      g_main_loop_run(loop);

      g_main_loop_unref(loop);
    }));
}

ExtractorInterface::~ExtractorInterface() {
    g_object_unref(mConnection);
    g_bus_unwatch_name (mWatcherId);
}

bool ExtractorInterface::connectExtractor(std::function<void(MmError* e)> cb) {
    MMLogInfo("");
    onConnectedCb = new std::function<void(MmError* e)>(cb);

    GError *error = NULL;
    mWatcherId = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                   THUMBNAIL_EXTRACTOR_SERVICE,
                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                   onNameAppeared,
                                   onNameVanished,
                                   this,
                                   NULL);
    if (error) {
        MMLogError("Could not create proxy: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }
    return true;
}

bool ExtractorInterface::signalSubscribe() {
    MMLogInfo("");

    if (!mImageExtractedHandlerId)
        mImageExtractedHandlerId = g_dbus_connection_signal_subscribe(mConnection,
                                   NULL,
                                   THUMBNAIL_EXTRACTOR_INTERFACE,
                                   "ExtractorEvent",
                                   THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                                   NULL,
                                   G_DBUS_SIGNAL_FLAGS_NONE,
                                   onExtractorEventCb,
                                   this,
                                   NULL);

    if (!mCoverArtExtractedHandlerId)
        mCoverArtExtractedHandlerId = g_dbus_connection_signal_subscribe(mConnection,
                                      NULL,
                                      THUMBNAIL_EXTRACTOR_INTERFACE,
                                      "CoverartEvent",
                                      THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                                      NULL,
                                      G_DBUS_SIGNAL_FLAGS_NONE,
                                      onExtractorEventCb,
                                      this,
                                      NULL);

    if (!mSingleAudioInfoExtractedHandlerId)
        mSingleAudioInfoExtractedHandlerId = g_dbus_connection_signal_subscribe(mConnection,
                                             NULL,
                                             THUMBNAIL_EXTRACTOR_INTERFACE,
                                             "SingleAudioInfoEvent",
                                             THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                                             NULL,
                                             G_DBUS_SIGNAL_FLAGS_NONE,
                                             onExtractorEventCb,
                                             this,
                                             NULL);

    if (mImageExtractedHandlerId == 0 || mCoverArtExtractedHandlerId == 0 || mSingleAudioInfoExtractedHandlerId == 0 ) {
        MMLogError("Fail to subscribe imgExtracted :%d , coverArtExtracted : %d, singleAudio : %d \n" ,
        mImageExtractedHandlerId, mCoverArtExtractedHandlerId, mSingleAudioInfoExtractedHandlerId);
        return false;
    }

    return true;
}

/**
 * ================================================================================
 * @fn : onExtractorEvent
 * @brief : slot for "ExtractorEvent" from thumbnail-extractor
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - pass signal to upper side
 * @param [in] status<"ThumbnailDone", "Error"> : result of extraction.
 * @param [in] path: video file path
 * @param [in] thumbnail: thumbnail path of the video.
 * @param [in] duration: video duration.
 * @param [in] playTime: saved play time of the video file.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void ExtractorInterface::onExtractorEvent(std::string status, std::string path,
                                          std::string thumbnail, int duration, int playTime) {
    MMLogInfo("status = %s, path = %s, thumbnail = %s, duration = %ld, playTime = %d",
               status.c_str(), path.c_str(), thumbnail.c_str(), duration, playTime);

    MMLogInfo("send imageExtractedSig signal\n");
    imageExtractedSig(status, path, thumbnail, duration, playTime);
}

/**
 * ================================================================================
 * @fn : onCoverArtEvent
 * @brief : slot for "CoverartEvent" from thumbnail-extractor
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - pass signal to upper side
 * @param [in] status<"OK", "Error"> : result of cover art extraction.
 * @param [in] album: album name
 * @param [in] artist: artist name
 * @param [in] coverArtPath: path of the cover art
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void ExtractorInterface::onCoverArtEvent(std::string status, std::string artist,
                                         std::string album, std::string coverArtPath) {
    MMLogInfo("status = %s, album = %s, coverArtPath = %s",
               status.c_str(), artist.c_str(), album.c_str(), coverArtPath.c_str());

    coverArtExtractedSig(status, artist, album, coverArtPath);
}

/**
 * ================================================================================
 * @fn : onSingleAudioInfoEvent
 * @brief : slot for "SingleAudioInfoEvent" from thumbnail-extractor
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - pass signal to upper side
 * @param [in] status<"OK", "Error"> : result of singla audio meta-data extraction.
 * @param [in] path: audio file path
 * @param [in] coverArt: path of the cover art
 * @param [in] title: title of audio
 * @param [in] album: album name
 * @param [in] artist: artist name
 * @param [in] genre: genre name
 * @param [in] duration: audio duration
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : none
 * ===================================================================================
 */
void ExtractorInterface::onSingleAudioInfoEvent(std::string status, std::string path,
                                                std::string coverArt, std::string title,
                                                std::string album, std::string artist,
                                                std::string genre, int64_t duration)
{
    MMLogInfo("stat = %s, path = %s, ca = %s, tit = %s, alb = %s, art = %s, gen = %s, dur = %lld",
               status.c_str(), path.c_str(), coverArt.c_str(), title.c_str(),
               album.c_str(), artist.c_str(), genre.c_str(), duration);

    singleAudioInfoExtractedSig(status, path, coverArt, title, album, artist, genre,(int64_t)duration);
}

/**
 * ================================================================================
 * @fn : requestImage
 * @brief : request to extract thumbnail image.
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - Make DBUS message
 *  - Set arguements to message
 *  - Send message over DBUS
 * @param [in] url : file path of video file.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : bool (true: success false: fail)
 * ===================================================================================
 */
bool ExtractorInterface::requestImage(std::string url) {
    std::vector<std::string> urls;
    urls.push_back(url);

    GError *error = NULL;
    GVariant *param;
    GVariantBuilder *builder;
    builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
    for(std::vector<std::string>::iterator iter = urls.begin() ; iter!=urls.end() ; iter++){
        g_variant_builder_add(builder, "s", iter->c_str());
    }
    param = g_variant_new ("(as)", builder);

    g_dbus_connection_call(mConnection,
                           THUMBNAIL_EXTRACTOR_SERVICE,
                           THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                           THUMBNAIL_EXTRACTOR_INTERFACE,
                           "Start",
                           param,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           MAX_DBUS_TIMEOUT * 1000,
                           NULL,
                           NULL,
                           &error);

    if (builder)
        g_variant_builder_unref (builder);

    if (param)
        g_variant_unref(param);

    if (error) {
        MMLogError("Could not start com.lge.thumbnailextractor: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }
    return true;
}

/**
 * ================================================================================
 * @fn : requestImages
 * @brief : request to extract thumbnail image.
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - Make DBUS message
 *  - Set arguements to message
 *  - Send message over DBUS
 * @param [in] urls : list of file path of video file.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return :  bool (true: success false: fail)
 * ===================================================================================
 */
bool ExtractorInterface::requestImages(std::vector<std::string>& urls) {
    MMLogInfo("urls.size() = %d", urls.size());
    GError *error = NULL;
    GVariantBuilder *builder;
    GVariant *param;

    builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
    for(std::vector<std::string>::iterator iter = urls.begin() ; iter!=urls.end() ; iter++){
        g_variant_builder_add(builder, "s", (char*)iter->c_str());
    }
    param = g_variant_new ("(as)", builder);

    g_dbus_connection_call(mConnection,
                           THUMBNAIL_EXTRACTOR_SERVICE,
                           THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                           THUMBNAIL_EXTRACTOR_INTERFACE,
                           "Start",
                           param,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           MAX_DBUS_TIMEOUT * 1000,
                           NULL,
                           NULL,
                           &error);

    if (builder)
        g_variant_builder_unref (builder);

    if (param)
        g_variant_unref(param);

    if (error) {
        MMLogError("Could not start com.lge.thumbnailextractor: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }

    return true;
}

/**
 * ================================================================================
 * @fn : setPlayTime
 * @brief : request to save playtime of video file to thumbnail-extractor
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - Make DBUS message
 *  - Set arguements to message
 *  - Send message over DBUS
 * @param [in] url : file path of video file.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return :  bool (true: success false: fail)
 * ===================================================================================
 */
bool ExtractorInterface::setPlayTime(std::string path, uint32_t playTime) {
    GError *error = NULL;
    MMLogInfo("path = %s, playTime = %d", path.c_str(), playTime);
    g_dbus_connection_call_sync(mConnection,
                                THUMBNAIL_EXTRACTOR_SERVICE,
                                THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                                THUMBNAIL_EXTRACTOR_INTERFACE,
                                "SetPlaytime",
                                g_variant_new ("(si)",
                                    path.c_str(),
                                    playTime),
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                MAX_DBUS_TIMEOUT * 1000,
                                NULL,
                                &error);

    if (error) {
        MMLogError("Could not start com.lge.thumbnailextractor: %s\n", error->message);
        g_error_free(error);
        return false;
    }

    return true;
}

/**
 * ================================================================================
 * @fn : requestCoverArts
 * @brief : request to extract cover arts to thumbnail-extractor
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - Make DBUS message
 *  - Set arguements to message
 *  - Send message over DBUS
 * @param [in] keys :  QList of QStringList , QStringList (album, artist pair)
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : bool (true: success false: fail)
 * ===================================================================================
 */
bool ExtractorInterface::requestCoverArts(std::vector<std::vector<std::string>> &keys) {
    MMLogInfo("keys.size() = %d", keys.size());

    std::map<std::string, std::string> albums;
    for (std::vector<std::string> key : keys) {
        MMLogInfo("album = %s", key[0].c_str());
        MMLogInfo("artist = %s", key[1].c_str());
        albums.insert(std::pair<std::string, std::string>(key[0], key[1]));
    }

    GError *error = NULL;
    GVariantBuilder *builder;
    GVariant *param;

    builder = g_variant_builder_new(G_VARIANT_TYPE("a{ss}"));
    for(std::map<std::string, std::string>::iterator iter = albums.begin() ; iter !=albums.end() ; iter++) {
        std::string album = (*iter).first;
        std::string artist = (*iter).second;
        g_variant_builder_add(builder, "{ss}", (char*)album.c_str(), (char*)artist.c_str());
    }
    param = g_variant_new ("(a{ss})", builder);

    g_dbus_connection_call(mConnection,
                           THUMBNAIL_EXTRACTOR_SERVICE,
                           THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                           THUMBNAIL_EXTRACTOR_INTERFACE,
                           "StartCoverart",
                           param,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           MAX_DBUS_TIMEOUT * 1000,
                           NULL,
                           NULL,
                           &error);

    if (builder) {
        g_variant_builder_unref (builder);
    }

    if (param) {
        g_variant_unref(param);
    }
    if (error) {
        MMLogError("Could not start com.lge.thumbnailextractor: %s\n", error->message);
        g_error_free(error);
        return false;
    }

    return true;
}

/**
 * ================================================================================
 * @fn : stopCoverArts
 * @brief : request to stop extracting cover arts to thumbnail-extractor
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - Make DBUS message
 *  - Set arguements to message
 *  - Send message over DBUS
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : bool (true: success false: fail)
 * ===================================================================================
 */
bool ExtractorInterface::stopCoverArts() {
    MMLogInfo("");
    GError *error = NULL;
    g_dbus_connection_call(mConnection,
                           THUMBNAIL_EXTRACTOR_SERVICE,
                           THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                           THUMBNAIL_EXTRACTOR_INTERFACE,
                           "StopCoverart",
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           MAX_DBUS_TIMEOUT * 1000,
                           NULL,
                           NULL,
                           &error);

    if (error) {
        MMLogError("Could not start com.lge.thumbnailextractor: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }
    return true;
}

/**
 * ================================================================================
 * @fn : stopCoverArts
 * @brief : request to stop extracting thumbnail to thumbnail-extractor
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - Make DBUS message
 *  - Set arguements to message
 *  - Send message over DBUS
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : bool (true: success false: fail)
 * ===================================================================================
 */
bool ExtractorInterface::stopThumbnails() {
    MMLogInfo("");
    GError *error = NULL;
    g_dbus_connection_call(mConnection,
                           THUMBNAIL_EXTRACTOR_SERVICE,
                           THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                           THUMBNAIL_EXTRACTOR_INTERFACE,
                           "Stop",
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           MAX_DBUS_TIMEOUT * 1000,
                           NULL,
                           NULL,
                           &error);
    if (error) {
        MMLogError("Could not start com.lge.thumbnailextractor: %s\n", error->message);
        g_error_free(error);
        return false;
    }
    return true;
}

/**
 * ================================================================================
 * @fn : requestAudioInfo
 * @brief : request to extract single audio meta data to thumbnail-extractor
 * @section : Function flow (Pseudo-code or Decision Table)
 *  - Make DBUS message
 *  - Set arguements to message
 *  - Send message over DBUS
 * @param [in] url: file path of audio file.
 * @section Global Variables: none
 * @section Dependencies: none
 * @return : bool (true: success false: fail)
 * ===================================================================================
 */
bool ExtractorInterface::requestAudioInfo(std::string url) {
    MMLogInfo("");
    GError *error = NULL;
    g_dbus_connection_call(mConnection,
                           THUMBNAIL_EXTRACTOR_SERVICE,
                           THUMBNAIL_EXTRACTOR_OBJECT_PATH,
                           THUMBNAIL_EXTRACTOR_INTERFACE,
                           "GetSingleAudioInfo",
                           g_variant_new ("(s)", url.c_str()),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           MAX_DBUS_TIMEOUT * 1000,
                           NULL,
                           NULL,
                           &error);
    if (error) {
        MMLogError("Could not start com.lge.thumbnailextractor: %s\n", error->message);
        g_error_free(error);
        return false;
    }
    return true;
}

}
}
