/*
 * Logger.cpp
 *
 * This class provides a singular object for handling logging operations.
 * Use Logger.log for low-priority i/o and Logger.error for high-priority.
 */

#include "Logger.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>

#define DEFAULT_FILENAME "log.txt"
#define DEFAULT_VERBOSE false

using namespace std;

Logger::Logger(string name, string directory, string filename) :
    _name(name),
    _directory(directory),
    _filename(filename),
    _verbose(DEFAULT_VERBOSE)
{}

Logger::Logger(const char *name, const char *directory, const char *filename) :
    _name(name),
    _directory(directory),
    _filename(filename),
    _verbose(DEFAULT_VERBOSE)
{}

Logger::Logger(string name, string directory) :
    _name(name),
    _directory(directory),
    _filename(DEFAULT_FILENAME),
    _verbose(DEFAULT_VERBOSE)
{}

Logger::Logger(const char *name, const char *directory) :
    _name(name),
    _directory(directory),
    _filename(DEFAULT_FILENAME),
    _verbose(DEFAULT_VERBOSE)
{}

Logger::~Logger() {}

/* Standard method for formatted printing and logging */
void Logger::log(string s, bool verbose) {

    // format the string
    stringstream ss;
    ss << _name << " [" << time(0) << "]: " << s;

    // write to STDOUT
    if (_verbose || verbose)
        cout << ss.str() << "\n";

    // write to log file
    string logname(_directory);
    logname = logname + "/" + _filename;
    ofstream logfile(logname, ios_base::app);
    if (logfile)
        logfile << ss.str() << "\n";
}

/* Standard method for formatted printing and logging */
void Logger::error(string s) {

    // format the string
    stringstream ss;
    ss << "ERROR: " << _name << " [" << time(0) << "]: " << s;

    // write to STDOUT
    cout << ss.str() << "\n";

    // write to log file
    string logname(_directory);
    logname = logname + "/" + _filename;
    ofstream logfile(logname, ios_base::app);
    if (logfile)
        logfile << ss.str() << "\n";
}

/* Wrapper around std::string method */
void Logger::log(const char *s, bool verbose) {
    log(string(s), verbose);
}

/* Wrapper around std::string method */
void Logger::error(const char *s) {
    error(string(s));
}

/* Update the value stored in _directory */
void Logger::setDirectory(string directory) {
    _directory = directory;
}

/* Wrapper around std::string method */
void Logger::setDirectory(const char *directory) {
    setDirectory(string(directory));
}

/* Enable verbose logging */
void Logger::enableVerbose() {
    _verbose = true;
}

/* Disable verbose logging */
void Logger::disableVerbose() {
    _verbose = false;
}
