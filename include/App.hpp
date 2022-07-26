/**
 * App (Equivalent to Producer Thread in samples):
 *   Opens the Argus camera driver, creates an output stream and consumer thread
 *   for each device, then performs repeating capture requests for a variable amount of time
 *   before closing the producer and Argus driver.
 *
 */

#include <string>
#include <vector>

class Options;

class App {

    public:
        App();
        ~App();

        bool run(int argc, char * argv[]);

    private:
        void producerPrint(const char *s);
        static void signalCallback(int signum);
        static std::vector<std::string> getUSBMountPaths();
        static void rtrim(char *str, size_t n);

        Options *_options;
        static bool _doRun;
};
