#include <iostream>
#include <fstream>
#include <memory>

///TODO: Find Windows replacement method for Daemonizing and unix sockets
#include <ctype.h>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <syslog.h>
#include <dbus-cxx.h>
#include <glib.h>

#include "config.h"
#include "TTSServiceAdapter.h"
#include "ImitateTTSService.h"

#define LOCK_FILE "imitate.lock"

bool customAddressSet;
std::string customAddress;

std::string configPath = DEFAULT_CONFIG_PATH;
GKeyFile * configFile;
pid_t PID;
ImitateTTSService * service;

GMainLoop * eventLoop;

void signalHandler(int);
void daemonize();
void registerSignalHandles();
void doLoop();
void readConfiguration();

void registerSignalHandles() {
    //The below signal handler is commented out because dbus-cxx has its own SIGCHLD handler, so setting it to ignore causes issues.
    //signal(SIGCHLD,SIG_IGN); // ignore child
    signal(SIGTSTP,signalHandler); /* since we haven't daemonized yet, process the TTY signals */
    signal(SIGQUIT,signalHandler);
    signal(SIGHUP,signalHandler);
    signal(SIGTERM,signalHandler);
    signal(SIGPIPE,SIG_IGN); //Ignore bad pipe signals for now
}

void signalHandler(int signal) {
    switch (signal) {
    case SIGHUP:
        //syslog(LOG_INFO,"SIGHUP Signal Received...");
        ///TODO: Make service reload
        break;
    case SIGQUIT:
    case SIGTSTP:
    case SIGTERM:
        //syslog(LOG_INFO,"Terminate Signal Received...");
        ///TODO: Make service stop
        service->running.store(false);
	g_main_loop_quit(eventLoop);
        break;
    default:
        //syslog(LOG_INFO, "Received unknown SIGNAL.");
        break;
    }
}

///A good portion of this code was borrowed from the example code provided at: http://www.enderunix.org/docs/eng/daemon.php
///Which was coded by: Levent Karakas <levent at mektup dot at> May 2001
///Many thank Levent!
void daemonize() {
    int i, lfp;
    char str[10];

    if(getppid() == 1) {
        return; /* already a daemon */
    }

    i = fork();
    if (i<0) {
        exit(1); /* fork error */
    }

    if (i>0) {
        exit(0); /* parent exits */
    }
    /* child (daemon) continues */

    setsid(); /* obtain a new process group */
    for (i=getdtablesize(); i >= 0; --i) {
        close(i); /* close all descriptors */
    }

    /* handle standard I/O */
    i = open("/dev/null",O_RDWR); ///TODO: Maybe make this set togst_init (&argc, &argv); a log file?
    dup(i);
    dup(i);

    umask(027); /* set newly created file permissions */

    lfp = open(LOCK_FILE,O_RDWR|O_CREAT,0640);

    if (lfp < 0) {
        exit(1); /* can not open */
    }

    if (lockf(lfp, F_TLOCK, 0) < 0) {
        syslog(LOG_WARNING, "Unable to start Imitate TTS Service! Lock file is already locked! Make sure that there isn't another Mimic TTS Service process running!");
        exit(-1); /* can not lock */
    }

    /* first instance continues */

    //Record PID to lock file
    PID = getpid();
    sprintf(str,"%d\n",getpid());
    write(lfp,str,strlen(str));

    //signal(SIGCHLD,SIG_IGN); // ignore child because of dbus-cxx
    signal(SIGTSTP,SIG_IGN); /* ignore tty signals */
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
    signal(SIGHUP,signalHandler); /* catch hangup signal */
    signal(SIGTERM,signalHandler); /* catch kill signal */

    doLoop();
}

void doLoop() {
    syslog(LOG_DEBUG, "Entered main loop");

    ///Many thanks to the dbus-cxx quickstart guide: https://dbus-cxx.github.io/quick_start_example_0.html#quick_start_server_0_code
    DBus::init();
    DBus::Connection::pointer conn;
    DBus::Dispatcher::pointer dispatcher;

    dispatcher = DBus::Dispatcher::create();
    syslog(LOG_DEBUG, "dispatcher created");

    //This is used exclusively for when the user specifies a different bus address to connect to.
    DBusConnection * c;

    if (customAddressSet) {
        syslog(LOG_DEBUG, "Attempting to connect to bus %s", customAddress.c_str());
        conn = dispatcher->create_connection(customAddress); //Connect to custom bus
    }
    else { // If no special address is spgst_init (&argc, &argv);ecified, default to the session bus
        conn = dispatcher->create_connection(DBus::BUS_SESSION);
        syslog(LOG_DEBUG, "Connected to session bus");
    }

    if(NULL == conn) { // Make sure the connection is good
        std::cerr << "Failed to connect to address " << customAddress << std::endl;
        exit(1);
    }

    int ret = conn->request_name("ca.l5.expandingdev.ImitateTTS", DBUS_NAME_FLAG_REPLACE_EXISTING);
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) { // Make sure we are the only instance on the DBus
        std::cerr << "Failed to reserve dbus name! Exiting..." << std::endl;
        syslog(LOG_ERR, "Failed to reserve dbus name! Exiting...");
        exit(-1);
    }

    syslog(LOG_DEBUG, "Reserved DBus name");

    service = new ImitateTTSService(configFile);

    Buckey::TTSServiceAdapter::pointer a = Buckey::TTSServiceAdapter::create(service, "/ca/l5/expandingdev/ImitateTTS");
    if(!conn->register_object(a)) {
        std::cerr << "Failed to register the ImitateTTS object!" << std::endl;
        syslog(LOG_ERR, "Failed to register the ImitateTTS object onto the DBus!");
    }
    else {
        std::cout << "Registered ImitateTTS object" << std::endl;
        syslog(LOG_DEBUG, "Registered the ImitateTTS object onto the DBus");
    }

    service->setPID(PID);
    service->signalStatus();

    g_main_loop_run(eventLoop);
    g_main_loop_unref(eventLoop);
    delete service;
}

void readConfiguration() {
    //Open up the key-value config file
    configFile = g_key_file_new();
    GError * error = NULL;
    if(!g_key_file_load_from_file(configFile, configPath.c_str(), G_KEY_FILE_NONE, &error)) {
        std::cerr << "Error opening configuration file " << configPath << ": " << error->message << std::endl;

        g_error_free(error);
        exit(-1);
    }
}

int main(int argc, char *argv[]) {
    //First, process command line args
    bool makeDaemon = false;
    bool showHelp = false;
    bool showVersion = false;
    customAddressSet = false;

    int c;
    opterr = 0;
    ///TODO: Add an option to specify the configuration file to use
    while ((c = getopt (argc, argv, "dhvca:")) != -1) {
        switch (c) {
        case 'd':
            makeDaemon = true;
            break;
        case 'h':
            showHelp = true;
            break;
        case 'v':
            showVersion = true;
            break;
        case 'c':
            configPath = std::string(optarg);
            break;
        case 'a':
            customAddressSet = true;
            customAddress = std::string(optarg);
            break;
        case '?':
            if (isprint (optopt)) {
                fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            }
            else {
                fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
            }
            return 1;
        default:
            exit(-1);
        }
    }

    if(showHelp) {
        std::cout << "Imitate TTS Service" << std::endl << std::endl;
        std::cout << "\tThe Imitate TTS Service provides a DBus interface for using the mimic1 text to speech (TTS) engine." << std::endl;
        std::cout << "This program should be run as root because it is a daemon." << std::endl;
        std::cout << std::endl << "Usage Instructions:" << std::endl;
        std::cout << "\timitate [ -h | -v | -c | -d | -a ADDRESS ]" << std::endl << std::endl;
        std::cout << "\t-h\t\tShow this usage text." << std::endl;
        std::cout << "\t-v\t\tDisplay the current version of this program." << std::endl;
        std::cout << "\t-c\t\tSpecify a configuration file to use" << std::endl;
        std::cout << "\t-d\t\tStart a new Imitate TTS Service daemon if one is not running already."<< std::endl;
        std::cout << "\t-a ADDRESS\tConnect the Imitate TTS Service to the specified custom DBus bus specified by the given address." << std::endl << std::endl;
        std::cout << "If no options are supplied, a new Imitate TTS Service instance will be made unless another Imitate TTS Service instance is already running." << std::endl;
        std::cout << "Daemonization will fail if another Imitate TTS Service instance is running." << std::endl;
        std::cout << "Addresses specified by the -a option should be DBus specification compliant, for example: imitate -a unix:/tmp/imitate.socket" << std::endl << "\tSpecifies to connect to a DBus through a UNIX socket located at /tmp/imitate.socket" << std::endl;
        return 0;
    }

    if(showVersion) {
        std::cout << "Version " << IMITATE_VERSION << std::endl;
        return 0;gst_init (&argc, &argv);
    }

    registerSignalHandles();

    int lfp = open(LOCK_FILE, O_RDWR | O_CREAT, 0640);

    //Attempt to open up the lock file
    if (lfp < 0) {
        std::cerr << "Error opening lock file!" << std::endl;
        //syslog(LOG_ERR, "Error opening lock file!");
        exit(1);
    }

    gst_init (&argc, &argv);
    eventLoop = g_main_loop_new(NULL, FALSE);

    if(lockf(lfp, F_TEST, 0) < 0) { //Test to see if this is the only instance running
        //There is another process running already
        if(makeDaemon) {
            std::cerr << "There is another process running already that has locked the lockfile! Unable to start the daemon! Exiting..." << std::endl;
            exit(-1);
        }
        else {
            openlog("imitate", LOG_NDELAY | LOG_PID | LOG_CONS, LOG_USER);
#ifdef ENABLE_DEBUG
            std::cout << "Debug logging has been enabled" << std::endl;
            setlogmask(LOG_DEBUG);
#else
            setlogmask(LOG_WARNING);
#endif
            readConfiguration();
            doLoop();
        }
    }
    else {

        // This is the only process running, make a new instance or make a new daemon
        if(makeDaemon) {
            openlog("imitate", LOG_NDELAY | LOG_PID | LOG_CONS, LOG_USER);
#ifdef ENABLE_DEBUG
            std::cout << "Debug logging has been enabled" << std::endl;
            setlogmask(LOG_DEBUG);
#else
            setlogmask(LOG_WARNING);
#endif
            readConfiguration();
            daemonize();
        }
        else {
            //Lock the lock file and then continue
            if (lockf(lfp, F_TLOCK, 0) < 0) {
                std::cerr << "Failed to lock the lock file! Make sure this program isn't already running!" << std::endl;
                exit(-1);
            }

            openlog("imitate", LOG_NDELAY | LOG_PID | LOG_CONS, LOG_USER);
#ifdef ENABLE_DEBUG
            std::cout << "Debug logging has been enabled" << std::endl;
            setlogmask(LOG_UPTO(LOG_DEBUG));
#else
            setlogmask(LOG_UPTO(LOG_WARNING));
#endif
            readConfiguration();
            doLoop();
        }
    }
    g_key_file_free(configFile);
    return 0;
}
