/**
 * main.cpp
 * 
 * Provides the executable entrypoint.
 */

#include "App.hpp"
#include <iostream>

int main(int argc, char * argv[]) {
    App app;
    if (!app.run(argc, argv))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
