/*
 * StreamCapture.cpp
 * 
 * Provides the executable entrypoint.
 */

#include "App.hpp"

int main(int argc, char * argv[]) {
    App app;
    if (!app.run(argc, argv))
        return 1;
    return 0;
}
