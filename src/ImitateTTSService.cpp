#include "config.h"
#include "ImitateTTSService.h"
#include <iostream>

///TODO: Windows portability
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gkeyfile.h>
#include <syslog.h>
#include <algorithm>
#include <vector>
#include <string>

#ifdef __win32__
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

extern "C" {
    cst_val *mimic_set_voice_list(const char *voxdir);
}

/// Speaks speech that has been prepared and returns true. Returns false if the specified words don't exist
bool ImitateTTSService::speakPreparedSpeech(std::string words) {
    syslog(LOG_DEBUG, "speakPreparedSpeech called");
    for(std::pair<std::string, std::string> p : preparedAudio) {
        if(p.first == words) {
            syslog(LOG_DEBUG, "Loading prepared speech...");

/*            Mix_Chunk *sample;
            sample=Mix_LoadWAV(p.second.c_str());
            if(!sample) {
                printf("Mix_LoadWAV: %s\n", Mix_GetError());// handle error
            }
*/
            syslog(LOG_DEBUG, "Loaded");

            while(currentlySpeaking) {
                //Wait until an opening occurs. This is mainly for async speech calls.
            }

            currentlySpeaking.store(true);
            speechStateChangedCallback(true);
/*
            int channel = Mix_PlayChannel(-1, sample, false);
            if(channel == -1) {
                ///ERROR
                return false;
            }
            Mix_Volume(channel, 128);
            while(Mix_Playing(channel)) {
                //Wait
            }
*/
            syslog(LOG_DEBUG, "Finished speaking");
//            Mix_FreeChunk(sample);

            currentlySpeaking.store(false);
            speechStateChangedCallback(false);
            return true;
        }
    }
    return false;
}


/**
  * Speaks the specified words whether they are prepared or not
  */

void ImitateTTSService::speak(std::string words) {
    syslog(LOG_DEBUG, "speak() called");
    if(running.load()) {

        history.push_back(words);

        //First check to see if these words are prepared already and speak them
        if(speakPreparedSpeech(words)) {
            return;
        }

        syslog(LOG_DEBUG, "Speech has not been prepared yet, beginning synthesis");

        //If not prepared yet, synthesize them and speak them
        while(currentlySpeaking.load()) {
            //Wait until an opening occurs
        }
        currentlySpeaking.store(true);
        reconfigureLock.lock();
        syslog(LOG_DEBUG, "Locked reconfiguration lock");

        speechStateChangedCallback(true);

        cst_wave * w = mimic_text_to_wave(words.c_str(), voice);
/*        Mix_Chunk * sample = new Mix_Chunk();
        sample->alen = (unsigned int) w->num_samples * sizeof(short); //Don't touch this, it just works....
        sample->abuf = (unsigned char *) w->samples;
        sample->volume = 128;
        sample->allocated = 0;

        int channel = Mix_PlayChannel(-1, sample, false);
        if(channel == -1) {
            errorCallback("Failed to reserved mix channel through SDL2_mixer!");
            return;
        }
        while(Mix_Playing(channel)) {
            //Idle
        }
        Mix_FreeChunk(sample);
        delete_wave(w);
*/

        reconfigureLock.unlock();
        syslog(LOG_DEBUG, "Unlocked reconfigure lock");
        currentlySpeaking.store(false);

        speechStateChangedCallback(false);

        return;
    }
    return;
}

/**
  * This function is a synchronous function that prepares the words and triggers the onSpeechPrepared event when done.
  */

void ImitateTTSService::prepareSpeech(std::string words) {
    syslog(LOG_DEBUG, "prepareSpeech() has been called");
    std::transform(words.begin(), words.end(), words.begin(), ::tolower);
    for(std::pair<std::string, std::string> p : preparedAudio) {
        if(p.first == words) {
            syslog(LOG_DEBUG, "Speech has already been prepared, returning.");
            return; // This audio already exists
        }
    }

    std::string fileName = "prepared";
    fileName += PATH_SEPARATOR;
    fileName += std::to_string(lastAudioIndex) + ".wav";
    cst_wave * w = mimic_text_to_wave(words.c_str(), voice);
    cst_wave_save_riff(w, fileName.c_str());
    lastAudioIndex++;

    preparedAudio.push_back(std::pair<std::string, std::string>(words, fileName));
    //Add a endline and write out.
    words += '\n';

    std::ofstream preparedAudioListFile;
    preparedAudioListFile.open(preparedAudioListFilePath.c_str(), std::ofstream::out | std::ofstream::app);
    preparedAudioListFile.write(words.c_str(), words.length());
    preparedAudioListFile.flush();

    delete w;

    //Trigger speech prepared signal/callback
    speechPreparedCallback(words);
    syslog(LOG_DEBUG, "Finished preparing speech");
}

///TODO: Bug where switching voices leads to super fast audio.
void ImitateTTSService::selectVoice(std::string voiceName) {

    syslog(LOG_DEBUG, "Received request to switch to voice: %s", voiceName.c_str());

    reconfigureLock.lock();
    mimic_exit();
    mimic_init();
    mimic_add_lang("eng", usenglish_init, cmu_lex_init);
    voice = mimic_voice_select(voiceName.c_str());
    reconfigureLock.unlock();

    //Trigger any signals
    voiceSwitchedCallback(voiceName);

    syslog(LOG_DEBUG, "Finished switching voice");
}

std::vector<std::string> ImitateTTSService::getSpeechHistory(uint16_t index, uint8_t range) {
    syslog(LOG_DEBUG, "getSpeechHistory() called");
    if(history.size() == 0) {
        return std::vector<std::string>();
    }
    unsigned long s = history.size();

    if(index > s - 1) {

        syslog(LOG_DEBUG, "index reaches past end of history, setting index to last element");
        index = s - 1;
        range = 1;
    }
    else if(index + range >= s) {
        syslog(LOG_DEBUG,"Range selects past the end of the vector, setting to the last element");
        range = s - index;
    }

    std::vector<std::string> selected(range);
    for(unsigned short i = 0; i < range; i++) {
        selected[i] = history[index + i];
    }

    return selected;
}

void ImitateTTSService::loadPreparedAudioList() {
    lastAudioIndex = 0;

    preparedAudioListFilePath = "prepared";
    preparedAudioListFilePath += PATH_SEPARATOR;
    preparedAudioListFilePath += "prepared.txt";

    std::ifstream preparedAudioListFile;
    preparedAudioListFile.open(preparedAudioListFilePath.c_str(), std::fstream::in);

    unsigned int lineNumber = 0;
    if(preparedAudioListFile.good()) {
        //Parse the prepared audio list file
        char readBuffer[256];
        while(preparedAudioListFile.good()) {
            memset(readBuffer, '\0', 256);
            preparedAudioListFile.getline(readBuffer, 255);
            if(readBuffer[0] == ';') { //Lines beginning with a ; are treated as comments
                continue;
            }
            else {
                if(readBuffer[0] != '\0') { // We reached eof
                    std::string audioFileName = "prepared";
                    audioFileName += PATH_SEPARATOR;
                    audioFileName += std::to_string(lineNumber) + ".wav";
                    preparedAudio.push_back(std::pair<std::string,std::string>(std::string(readBuffer), audioFileName));

                    syslog(LOG_DEBUG,"Found prepared audio: %s", readBuffer);
                    lineNumber++;
                }
            }
        }
#ifdef DEBUG
        //Iterate through the list that we parsed
        for(std::pair<std::string, std::string> p : preparedAudio) {
            syslog(LOG_DEBUG,"%s contains: %s", p.second.c_str(), p.first.c_str());
        }
#endif
    }
    else {

        syslog(LOG_WARNING, "WARNING: The prepared.txt failed to open, maybe it is missing?");
        std::cerr << "WARNING: The prepared.txt failed to open, maybe it is missing?" << std::endl;
    }
    lastAudioIndex = lineNumber;
}

ImitateTTSService::ImitateTTSService(GKeyFile * config) : running(true), currentlySpeaking(false), history(0), Buckey::TTSService(IMITATE_VERSION, "imitate") {
    reconfigureLock.lock();

    configFile = config;
    syslog(LOG_DEBUG,"Constructor");

    errorCallback = emptyStringCallback;
    voiceSwitchedCallback = emptyStringCallback;
    speechStateChangedCallback = emptyBoolCallback;
    speechPreparedCallback = emptyStringCallback;

    loadPreparedAudioList();

    GError * error = NULL;

    //Load the default voice file path from the config file
    const char * defaultVoiceValue = DEFAULT_VOICE_NAME;
    char * voiceName = g_key_file_get_string(configFile, "Default", "voice", &error);
    if(voiceName == NULL) {
        //voice option is optional, just set it to the default value
        strcpy(voiceName, defaultVoiceValue);
    }

    mimic_init();
    ///TODO: Maybe not be limited to english? That's usually a good thing
    mimic_add_lang("eng", usenglish_init, cmu_lex_init);
    mimic_voice_list = mimic_set_voice_list(NULL);

    voice = nullptr;
    voice = mimic_voice_select(voiceName);
    if(voice == nullptr) {
        syslog(LOG_ERR, "Failed to load voice: %s ! Exiting...", voiceName);
        std::cerr << "Failed to load voice: " << voiceName << "! Exiting..." << std::endl;

        setState(Buckey::Service::State::ERROR);
        signalError("Filed to load Mimic TTS voice file.");

        running.store(false);
        exit(-1);
    }
/*
    // start SDL with audio support
    if(SDL_Init(SDL_INIT_AUDIO)==-1) {
        printf("SDL_Init: %s\n", SDL_GetError());
        syslog(LOG_ERR, "Error initializing SDL_mixer");
        std::cerr << "Error initializing SDL_mixer" << std::endl;
        exit(1);
    }

    // open 44.1KHz, signed 16bit, system byte order,
    //      mono audio, using 1024 byte chunks
    if(Mix_OpenAudio(44100, AUDIO_S16SYS, 1, 1024)==-1) {
        printf("Mix_OpenAudio: %s\n", Mix_GetError());
        syslog(LOG_ERR, "Error initializing SDL_mixer: %s", Mix_GetError());
        std::cerr << "Error opening SDL_mixer audio" << std::endl;
        exit(2);
    }

    Mix_Init(MIX_INIT_FLAC | MIX_INIT_MP3 | MIX_INIT_OGG);

    syslog(LOG_DEBUG, "Initialized SDL mixer");
    std::cout << "Initialized SDL mixer" << std::endl;
*/

    reconfigureLock.unlock();
    std::cout << "Imitate TTS successfully initialized." << std::endl;

    setState(Buckey::Service::State::RUNNING);
    setStatusMessage("Imitate TTS successfully initialized.");
}

ImitateTTSService::~ImitateTTSService()
{
    syslog(LOG_DEBUG, "Destructor start");

    //Don't free this because this should be passed to us, so let whoever passed it to use to free it.
    //g_key_file_free(configFile);

    mimic_exit();
//    Mix_Quit();
//    SDL_CloseAudio();
    syslog(LOG_DEBUG, "Destructor finished");
}
