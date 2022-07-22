#pragma once

#include <string>

#define CAPTURE_MODE_0 0

class Options {

    public:
        /* Constructors */
        Options();

        /* Destructors */
        ~Options();

        /* Static class methods */
        static void printHelp();
        bool parse(int argc, char * argv[]);
        bool setCaptureMode(int mode);
        void write();

        /* Class fields, public to reduce overhead */
        char *directory;
        int captureMode;
        int captureWidth;
        int captureHeight;
        int captureFPS;
        int captureTime;
        int profile;
        int saveEvery;
};
