/*
 * Logger.hpp
 *
 * This class provides a singular object for handling logging operations.
 * Use Logger.log for low-priority i/o and Logger.error for high-priority.
 */

#pragma once

#include <string>

class Logger {

    public:
        Logger(std::string name, std::string directory, std::string filename);
        Logger(const char *name, const char *directory, const char *filename);
        Logger(std::string name, std::string directory);
        Logger(const char *name, const char *directory);
        ~Logger();

        void log(std::string s, bool verbose = false);
        void log(const char *s, bool verbose = false);

        void error(std::string s);
        void error(const char *s);

        void setDirectory(std::string directory);
        void setDirectory(const char *directory);

        void enableVerbose();
        void disableVerbose();

    private:
        std::string _name;
        std::string _directory;
        std::string _filename;
        bool _verbose;
};
