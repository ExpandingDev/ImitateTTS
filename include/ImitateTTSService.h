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


class ImitateTTSService : public Buckey::TTSService
{
public:
    ImitateTTSService(GKeyFile * config);
    void speak(std::string words);
    bool speakPreparedSpeech(std::string words);
    void prepareSpeech(std::string words);
    void selectVoice(std::string voiceFile);
    std::vector<std::string> getSpeechHistory(uint16_t index, uint8_t range);

    ///TODO: Implement this
    std::string getState() {
        return "standard";
    };

    std::vector<std::string> history;

    ///TODO: Add listener for shutdown call
    std::atomic<bool> running;

    std::que<std::pair<std::string, cst_wave *>> audioQue;

    virtual ~ImitateTTSService();

protected:
    void loadPreparedAudioList();

    GKeyFile * configFile;
    const char * CONFIG_FILENAME = "imitate.conf";

    cst_voice * voice;

    std::vector<std::pair<std::string, std::string>> preparedAudio;
    std::mutex reconfigureLock;
    std::atomic<bool> currentlySpeaking;
    unsigned long lastAudioIndex;

    std::string preparedAudioListFilePath;

    static void emptyStringCallback(std::string a) { };
    static void emptyBoolCallback(bool b) { };

};

#endif // MIMICTTSSERVICE_H
