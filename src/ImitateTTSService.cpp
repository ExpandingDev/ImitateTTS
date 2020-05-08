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

//This ensures that we have access to all of the configured mimic1 voices
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
    if(g_main_loop_is_running(eventLoop)) {

        //First check to see if these words are prepared already and speak them
        if(speakPreparedSpeech(words)) {
            return;
        }

        syslog(LOG_DEBUG, "Speech has not been prepared yet, beginning synthesis");

        //If not prepared yet, synthesize them and speak them

        reconfigureLock.lock();
        syslog(LOG_DEBUG, "Locked reconfiguration lock");
        cst_wave * w = mimic_text_to_wave(words.c_str(), voice);
        reconfigureLock.unlock();
        syslog(LOG_DEBUG, "Unlocked reconfigure lock");

        queueLock.lock();
	audioQueue.push(std::pair<std::string, cst_wave *>(words, w));
        queueLock.unlock();

        return;
    }
    return;
}

/**
  * This function is a blocking function that prepares the words and triggers the onSpeechPrepared event when done.
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

    delete_wave(w);

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

void ImitateTTSService::pauseSpeech() {
    userPaused = true;
    gst_element_set_state(audioInfo.pipeline, GST_STATE_PAUSED);
}

void ImitateTTSService::resumeSpeech() {
    userPaused = false;
    gst_element_set_state(audioInfo.pipeline, GST_STATE_PLAYING);
}

void ImitateTTSService::bufferDestroyCallback(void * v) {
        if(v != nullptr) {
                syslog(LOG_DEBUG, "destroying wave");
                cst_wave * w = (cst_wave *) v;
                delete_wave(w);
        }
}

void ImitateTTSService::startAudioFeedCallback(GstElement * source, guint size, PipelineInfo * audioInfo) {
    syslog(LOG_DEBUG, "startAudioFeedCallback");
    if(audioInfo->sourceid == 0) {
        audioInfo->sourceid = g_idle_add((GSourceFunc) ImitateTTSService::pushAudioCallback, audioInfo);
    }
}

void ImitateTTSService::stopAudioFeedCallback(GstElement * source, PipelineInfo * audioInfo) {
    syslog(LOG_DEBUG, "stopAudioFeedCallback");
    if(audioInfo->sourceid != 0) {
       g_source_remove(audioInfo->sourceid);
       audioInfo->sourceid = 0;
    }
}

/// Called during idle time in the GMainLoop to push an audio sample to gstreamer to play
gboolean ImitateTTSService::pushAudioCallback(PipelineInfo * audioInfo) {
    ImitateTTSService * that = audioInfo->imitate;
    that->queueLock.lock();
    if(!that->audioQueue.empty()) {
        GstBuffer * buffer;
        guint num_feeding_samples;
        num_feeding_samples = CHUNK_SIZE / 2;

        cst_wave * w = that->audioQueue.front().second;
        that->queueLock.unlock();

        // Check to see if we've passed all of the wave file to gstreamer, so add the words to the speech history and pop the queue
        bool reachedEnd = audioInfo->waveSampleCount + num_feeding_samples > w->num_samples;
        unsigned int remaining = 0;
        if(reachedEnd) {
                syslog(LOG_DEBUG, "reached end of wave");
                that->history.push_back(audioInfo->imitate->audioQueue.front().first);
                that->queueLock.lock();
                that->audioQueue.pop();
                that->queueLock.unlock();
                remaining = (w->num_samples - audioInfo->waveSampleCount) * 2;
                num_feeding_samples = remaining / 2;
        }

        // Make a new buffer that wrapps the audio data to be played. gstreamer shouldn't modify the data, set user_data to a pointer of the wave if we have reached the end of the wave object so that the callback function knows to destroy it
        buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, (unsigned char *) w->samples, (gsize) w->num_samples * 2, audioInfo->waveSampleCount * 2, reachedEnd ? remaining : CHUNK_SIZE, reachedEnd ? w : nullptr, &ImitateTTSService::bufferDestroyCallback);
        GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale(num_feeding_samples, GST_SECOND, SAMPLE_RATE);
        audioInfo->streamSampleCount += num_feeding_samples;
        audioInfo->waveSampleCount += num_feeding_samples;

        if(reachedEnd) {
            audioInfo->waveSampleCount = 0;
        }

        // Push the buffer to gstreamer and check for any errors
        GstFlowReturn ret;
        g_signal_emit_by_name(audioInfo->appsrc, "push-buffer", buffer, &ret);

        gst_buffer_unref(buffer);

        if(ret != GST_FLOW_OK) {
                syslog(LOG_ERR, "Got error while pushing buffer to gstreamer! Removing pushAudioCallback from the GMainLoop!");
                return FALSE;
        }

        return TRUE;
    }
    that->queueLock.unlock();
    return TRUE;
}

void ImitateTTSService::audioSourceSetupCallback(GstElement * pipeline, GstElement * source, PipelineInfo * audioInfo) {
    syslog(LOG_DEBUG, "Received setup-source callback from gstreamer");
    audioInfo->appsrc = source;

    GstAudioInfo info;
    GstCaps * audio_caps;
    gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);

    audio_caps = gst_audio_info_to_caps (&info);
    g_object_set (source, "caps", audio_caps, "format", GST_FORMAT_TIME, NULL);
    g_object_set(G_OBJECT(source), "do-timestamp", TRUE, NULL);
    g_signal_connect(source, "enough-data", G_CALLBACK(&stopAudioFeedCallback), audioInfo);
    g_signal_connect(source, "need-data", G_CALLBACK(&startAudioFeedCallback), audioInfo);

    gst_caps_unref(audio_caps);

    syslog(LOG_DEBUG, "Done setting up source for gstreamer");
}

ImitateTTSService::ImitateTTSService(GKeyFile * config) : audioQueue(), userPaused(false), currentlySpeaking(false), history(0), Buckey::TTSService(IMITATE_VERSION, "imitate") {
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

    syslog(LOG_DEBUG, "Initializing mimic1");
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
        signalError("Failed to load Mimic TTS voice file.");

        exit(-1);
    }

    // Start up the gstreamer pipeline
    syslog(LOG_DEBUG, "Setting up gstreamer pipeline");
    audioInfo.imitate = this;
    audioInfo.waveSampleCount = 0;
    audioInfo.streamSampleCount = 0;
    audioInfo.sourceid = 0;
    reconfigureLock.unlock();

    std::cout << "Imitate TTS successfully initialized." << std::endl;
}

void ImitateTTSService::startLoop() {
    syslog(LOG_DEBUG, "startLoop called");
    eventLoop = g_main_loop_new(NULL, FALSE);
    audioInfo.pipeline = gst_parse_launch("playbin uri=appsrc://", NULL);
    g_signal_connect(audioInfo.pipeline, "source-setup", G_CALLBACK (ImitateTTSService::audioSourceSetupCallback), &audioInfo);
    gst_element_set_state(audioInfo.pipeline, GST_STATE_PLAYING);
    setState(Buckey::Service::State::RUNNING);
    setStatusMessage("Imitate TTS successfully initialized.");
    g_main_loop_run(eventLoop);
}

ImitateTTSService::~ImitateTTSService()
{
    syslog(LOG_DEBUG, "Destructor start");

    //Free all synthesized audio snippets
    queueLock.lock();
    if(!audioQueue.empty()) {
	syslog(LOG_DEBUG, "Freeing stored waves");
	while(!audioQueue.empty()) {
		delete_wave(audioQueue.front().second);
		audioQueue.pop();
	}
    }
    queueLock.unlock();

    //Free all gstreamer elements
    gst_element_set_state(audioInfo.pipeline, GST_STATE_NULL);
    gst_object_unref(audioInfo.appsrc);
    gst_object_unref(audioInfo.pipeline);

    mimic_exit();
    syslog(LOG_DEBUG, "Destructor finished");
}
