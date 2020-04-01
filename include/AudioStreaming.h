#include <queue>
#include <string>
#include "cst_wave.h"


std::queue<std::pair<std::string, cst_wave *>> audioQue;

/* TODO:
- history should have words added to it once the audio is fully passed through gstreamer
- pass audio from audioQue to gstreamer
- set up gstreamer pipeline
- invoke callbacks for speech state changes (signals to DBus)
- change the currentlySpeaking variable according to playback
- implement pausing?
- delete/free the cst_wave in the audio que
- ensure that cst_waves are freed from the audio que before exiting program

*/
