#ifndef MIMICTTSSERVICE_H
#define MIMICTTSSERVICE_H

#include <fstream>
#include <atomic>
#include <mutex>
#include <functional>
#include <queue>

#include <TTSService.h>

#include "mimic.h"
#include "usenglish.h"
#include "cmu_lex.h"
#include "cst_wave.h"
#include "cst_audio.h"
#include <glib.h>
#include <dbus-cxx.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>

#define CHUNK_SIZE 1024
#define SAMPLE_RATE 44100

class ImitateTTSService;

typedef struct sPipelineInfo {
    GstElement * pipeline;
    /// Element that we will be feeding our audio samples into
    GstElement * appsrc;
    /// Stores the number of samples played from the current wave
    guint64 waveSampleCount;
    /// Stores the number of samples played over the entirety of the audio stream
    guint64 streamSampleCount;
    /// Used to add/remove the pushAudioCallback onto the GMainLoop, see startAudioFeedCallback and stopAudioFeedCallback
    std::atomic<guint> sourceid;
    /// Pointer to the ImitateTTSService that this info belongs to. Used so that members within ImitateTTSService don't have to be made static.
    ImitateTTSService * imitate;
} PipelineInfo;

class ImitateTTSService : public Buckey::TTSService
{
public:
    ImitateTTSService(GKeyFile * config);
    void speak(std::string words);
    bool speakPreparedSpeech(std::string words);
    void prepareSpeech(std::string words);
    void selectVoice(std::string voiceFile);
    void pauseSpeech();
    void resumeSpeech();

    std::vector<std::string> getSpeechHistory(uint16_t index, uint8_t range);

    ///TODO: Implement this
    std::string getState() {
        return "standard";
    };

    std::vector<std::string> history;

    GMainLoop * eventLoop;
    void startLoop();

    std::atomic<bool> userPaused;

    /// Queue of audio waves to be played. The string element is the words that are spoken in the wave
    std::queue<std::pair<std::string, cst_wave *>> audioQueue;

    static void audioSourceSetupCallback(GstElement * pipeline, GstElement * source, PipelineInfo * audioInfo);
    static void startAudioFeedCallback(GstElement * source, guint size, PipelineInfo * audioInfo);
    static void stopAudioFeedCallback(GstElement * source, guint size, PipelineInfo * audioInfo);
    static void bufferDestroyCallback(void * d);
    static gboolean pushAudioCallback(PipelineInfo * audioInfo);

    virtual ~ImitateTTSService();

protected:
    void loadPreparedAudioList();

    GKeyFile * configFile;
    const char * CONFIG_FILENAME = "imitate.conf";

    cst_voice * voice;

    PipelineInfo audioInfo;

    std::vector<std::pair<std::string, std::string>> preparedAudio;
    std::mutex reconfigureLock;
    std::mutex queueLock;
    std::mutex feedingLock;
    bool feeding;

    std::atomic<bool> currentlySpeaking;
    unsigned long lastAudioIndex;

    std::string preparedAudioListFilePath;

    static void emptyStringCallback(std::string a) { };
    static void emptyBoolCallback(bool b) { };

};

#endif // MIMICTTSSERVICE_H
