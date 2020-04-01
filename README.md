# Imitate TTS Service
The Imitate TTS Service is a Linux daemon that provides a DBus interface for the mimic1 text to speech (TTS) engine.

## Installation
#### Dependencies
Imitate TTS uses [dbus-cxx](https://github.com/dbus-cxx/dbus-cxx) to interface with DBus. Imitate also uses `gstreamer` and to play audio. Imitate uses [mimic1](https://github.com/MycroftAI/mimic1) for the speech synthesis and voices. Imitate also uses `glib2.0` for utility functions.
You will have to install `dbus-cxx` and `mimic1` from source, but `glib2.0`, `gstreamer-1.0` and `gstreamer-audio-1.0` should all be in your distro's package manager.

Imitate TTS Service uses CMake for its build system.   
To install the `imitate-service` binary, configuration files, and header files, run the following commands:  

	cmake CMakeLists.txt
	make -j 4
	sudo make install

There are a few compilation flags available to adjust for Imitate TTS, the most important being `ENABLE_DEBUG` which will turn on debug logging to syslog. Additionally, it is possible to save space by excluding certain voices from compilation. Take a look at `CMakeLists.txt` for more information.

## Configuration
The configuration for the service is stored in `/etc/imitate/imitate.conf`.  
Prepared/pre-synthesized audio clips are stored in `/etc/imitate/prepared`.

## Usage

	This program should be run as root because it is a daemon.
	
		imitate [ -h | -v | -d | -a ADDRESS ]
		
		-h	Show this usage text.
		-d	Start a new Imitate TTS Service daemon if one is not running already.
		-v	Display the current version of this program.
		-a	Connects the Imitate TTS Service to the specified bus given by ADDRESS.	

        If no options are supplied, a new Imitate TTS Service instance will be made unless another Imitate TTS Service instance is already running.
        Daemonization will fail if another Imitate TTS Service instance is running.

## DBus Interface
The Imitate TTS service exposes a set of methods to the session DBus. These methods are detailed in the DBus introspection file `ca.l5.expandingdev.ImitateTTS.xml` that is in the `res` directory, or [here](https://github.com/ExpandingDev/ImitateTTS/blob/master/res/ca.l5.expandingdev.ImitateTTS.xml).
