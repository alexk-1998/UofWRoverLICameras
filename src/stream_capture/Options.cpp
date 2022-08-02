/*
 * Options.cpp
 *
 * This class provides a singular object for passing command line options to objects in the main executable.
 * Methods are provided for parsing the command line input and help output.
 */

#include "Options.hpp"
#include <iostream>
#include <getopt.h>
#include <chrono>
#include <unistd.h>
#include <string.h>
#include <fstream>

using namespace std;

#define MKDIR_MODE 0777

#define DEFAULT_PROFILE false
#define DEFAULT_CAPTURE_TIME 0U
#define DEFAULT_SAVE_EVERY 1U

/* 2048x1554 @ 38 FPS */
#define CAPTURE_WIDTH_0 2048U
#define CAPTURE_HEIGHT_0 1554U
#define CAPTURE_FPS_0 38U

/* 1936x1106 @ 30 FPS */
#define CAPTURE_MODE_1 1U
#define CAPTURE_WIDTH_1 1936U
#define CAPTURE_HEIGHT_1 1106U
#define CAPTURE_FPS_1 30U

/* Default constructor, initialize to all default options then parse the input */
Options::Options() :
    profile(DEFAULT_PROFILE),
    captureTime(DEFAULT_CAPTURE_TIME),
    saveEvery(DEFAULT_SAVE_EVERY),
    directory(NULL),
    captureMode(CAPTURE_MODE_0),
    captureResolution(0)
{
    /* Assign time since epoch */
    directory = new char[FILENAME_MAX];
    if (directory) {
        memset(directory, 0, FILENAME_MAX);
        snprintf(directory, FILENAME_MAX, "%ld", (long) time(0));
    }
}

/* Default destructor, do nothing since there are no heap-allocated member fields */
Options::~Options() {
    delete[] directory;
}

/* Display the following text when the executable is called with improper parameters or if the -h flag is passed */
void Options::printHelp() {
    cout << "Usage:" << endl
         << "./main [OPTIONS]" << endl
         << endl
         << "Optional Arguments:" << endl
         << endl << "  --capture-mode\t-m\t<0 or 1>\tSensor mode for the IMX265 cameras. [Default: 0]" << endl
         << "Mode " << CAPTURE_MODE_0 << ": " << CAPTURE_WIDTH_0 << "x" << CAPTURE_HEIGHT_0 << " @ " << CAPTURE_FPS_0 << "fps" << endl
         << "Mode " << CAPTURE_MODE_1 << ": " << CAPTURE_WIDTH_1 << "x" << CAPTURE_HEIGHT_1 << " @ " << CAPTURE_FPS_1 << "fps" << endl
         << endl << "  --root-directory\t-r\t<str>\t\tRoot path of the image directory for storing all images. [Default: system time]" << endl
         << "Creates a file structure of the form:" << endl
         << "root" << endl
         << "  cam0" << endl
         << "      image000000.jpg" << endl
         << "      image000001.jpg" << endl
         << "      ..." << endl
         << "  cam1" << endl
         << "  ..." << endl
         << "  options.txt" << endl
         << endl << "  --save-every\t\t-s\t<1-inf>\t\tSave every s frames from the stream. [Default: 1]" << endl
         << "Default will save every frame, if s == 2 then every second frame is saved, etc." << endl
         << endl << "  --capture-time\t-t\t<0-inf>\t\tRecording time in seconds. [Default: 0]" << endl
         << "Passing 0 requires the process be killed from an external signal (ctrl+c)." << endl
         << endl << "  --profile\t\t-p\tNone\t\tEnable encoder profiling." << endl
         << endl << "  --help\t\t-h\tNone\t\tPrint this help." << endl
         << endl;
}

/* Parse all command line arguments and validate the inputs */
bool Options::parse(int argc, char *argv[]) {

    int valid;

    static struct option long_options[] = {
        /* These options set a flag. */
        {"profile", no_argument, &profile, 1},
        {"help", no_argument, &valid, 0},
        /* These options don’t set a flag. We distinguish them by their indices. */
        {"root-directory", required_argument, NULL, 'd'},
        {"capture-mode",  required_argument, NULL, 'm'},
        {"save-every",  required_argument, NULL, 's'},
        {"capture-time", required_argument, NULL, 't'},
        {NULL, 0, NULL, 0}
    };

    int c;
    while (valid && (c = getopt_long(argc, argv, "d:m:s:t:ph", long_options, NULL)) != -1) {
        switch (c) {

            /* Do nothing */
            case 0:
                break;

            /* Copy and validate passed folder name */
            case 'd':
                if (strlen(optarg) == 0 || !isalnum(optarg[0])) {
                    cout << "Invalid directory name, must begin with at least one alphanumeric character" << endl;
                    valid = false;
                } else {
                    strncpy(directory, optarg, FILENAME_MAX);
                }
                break;

            /* Copy and validate passed sensor mode */
            case 'm':
                captureMode = atoi(optarg);
                if (captureMode != CAPTURE_MODE_0 && captureMode != CAPTURE_MODE_1) {
                    cout << "Invalid sensor mode, expected " << CAPTURE_MODE_0 << " or " << CAPTURE_MODE_1 << endl;
                    valid = false;
                }
                if (captureMode == CAPTURE_MODE_1) {
                    cout << "The chosen sensor mode is temporarily disabled, use the default mode." << endl;
                    valid = false;     
                }
                break;

            /* Get the time to capture */
            case 't':
                captureTime = atoi(optarg);
                if (captureTime < 0) {
                    cout << "Invalid capture time, expected >= 0" << endl;
                    valid = false;
                }
                break;

            /* Get the frame saving frequency */
            case 's':
                saveEvery = atoi(optarg);
                if (saveEvery < 1) {
                    cout << "Invalid save rate, expected >= 1" << endl;
                    valid = false;
                }
                break;

            /* Enable debugging mode (verbose output and encoder profiling) */
            case 'p':
                profile = !DEFAULT_PROFILE;
                break;
        
            /* Show the help message */
            case 'h':
                valid = false;
                break;

            /* Ignore anything else */
            default:
                break;
        }
    }
    return valid;
}

/* Write to a file options.txt in the root directory */
void Options::write() {

    /* Create the file */
    string filename(directory);
    filename += "/options.txt";
    ofstream outputFile;

    /* Write */
    outputFile.open(filename);
    outputFile << "Root directory: " << directory << endl;
    outputFile << "Capture mode: " << captureMode << endl;
    if (captureTime == 0)
        outputFile << "Capture time: inf" << endl;
    else
        outputFile << "Capture time: " << captureTime << endl;
    outputFile << "Profile: " << (bool) profile << endl;
    outputFile << "Save every: " << saveEvery << endl;
    outputFile.close();
}
