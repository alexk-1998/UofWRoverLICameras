/*
 * Options.hpp
 *
 * This class provides a singular object for passing command line options to objects in the main executable.
 * Methods are provided for parsing the command line input and help output.
 */

#pragma once

#include "Argus/Argus.h"

#define CAPTURE_MODE_0 0

class Options {

    public:
        Options();
        ~Options();

        /* Static class methods */
        static void printHelp();
        bool parse(int argc, char * argv[]);
        void write();

        /* Class fields, public to reduce overhead */
        char *directory;
        int captureMode;
        Argus::Size2D<uint32_t> captureResolution;
        int captureTime;
        int profile;
        int saveEvery;
};
