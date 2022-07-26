/*
 * App.hpp
 *
 * Opens the Argus camera driver, creates an output stream and consumer thread
 * for each device, then performs repeating capture requests for a variable amount of time
 * before closing the producer and Argus driver. The run() method of this class is meant to
 * mimic the execute() method for producer threads in the Jetson multimedia API examples.
 */

#pragma once

#include <string>

class Options;

class App {

    public:
        App();
        ~App();

        bool run(int argc, char * argv[]);

    private:
        static void signalCallback(int signum);
        static std::string getAvailableDevice();

        Options *_options;
        static bool _doRun;
};
