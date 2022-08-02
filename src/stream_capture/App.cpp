/*
 * App.cpp
 *
 * Opens the Argus camera driver, creates an output stream and consumer thread
 * for each device, then performs repeating capture requests for a variable amount of time
 * before closing the producer and Argus driver. The run() method of this class is meant to
 * mimic the execute() method for producer threads in the Jetson multimedia API examples.
 */

#include "App.hpp"

#include "ConsumerThread.hpp"
#include "Options.hpp"
#include "Logger.hpp"
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <sys/stat.h>
#include <signal.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

using namespace Argus;
using namespace EGLStream;

#define MKDIR_MODE 0777

bool App::_doRun = true;
App::App() :
    _options(new Options)
{}

App::~App() {
    delete _options;
}

bool App::run(int argc, char * argv[]) {

    /* Repeatedly use this value for error handling */
    bool errorOccurred = false;

    /* Create the logger object, don't log until directory path is set in options */
    Logger *logger = NULL;
    if (!errorOccurred) {
        logger = new Logger("PRODUCER", "");
        if (!logger) {
            std::cout << "An error occurred while creating the Logger object! Exiting..." << std::endl;
            errorOccurred = true;
        }
    }

    /* Verify the Options object was successfully created */
    if (!errorOccurred) {
        std::cout << "Verifying the command line options..." << std::endl;
        if (!_options) {
            std::cout << "An error occurred while creating the Options object! Exiting..." << std::endl;
            Options::printHelp();
            errorOccurred = true;
        } else if (!_options->parse(argc, argv)) {
            std::cout << "An error occurred while verifying the command line options! Exiting..." << std::endl;
            Options::printHelp();
            errorOccurred = true;
        }
    }

    /* Search for available usb device and pre-append options directory to reflect changes
     * Eg. If we choose directory "bar" and a device is found mounted at /media/nvidia/foo/
     * the resultant path should be /media/nvidia/foo/bar/
     * Eg. If we choose directory "bar" and no device is found
     * the resultant path should be ./bar/
     */
    if (!errorOccurred) {
        std::cout << "Searching for first available USB volume...\n";
        std::vector<std::string> devicePaths = getAvailableDevices();
        std::string path("");
        for (std::string devicePath : devicePaths) {
            path = devicePath;
            break;             // just use the first device
        }
        // fall back to system since no path is set
        if (path.size() == 0)
            std::cout << "USB device not found, falling back to saving on system memory...\n";
        else
            std::cout << "Using USB device mounted at: " << path << "\n";

        // pre-append the resultant path to the chosen directory name
        strncat(&path[0], _options->directory, FILENAME_MAX);
        strncpy(_options->directory, path.data(), FILENAME_MAX);
    }
    logger->setDirectory(_options->directory);

    /* Register signal callback to various signals (ctrl+c, ctrl+d, etc...)
       Lazy OR means lines are executed if errorOccurred is not already true */
    errorOccurred = errorOccurred || signal(SIGHUP, signalCallback) == SIG_ERR;
    errorOccurred = errorOccurred || signal(SIGINT, signalCallback) == SIG_ERR;
    errorOccurred = errorOccurred || signal(SIGQUIT, signalCallback) == SIG_ERR;
    errorOccurred = errorOccurred || signal(SIGTERM, signalCallback) == SIG_ERR;
    
    /* Create base directory for saving images, sub-directories are handled by consumers */
    if (!errorOccurred) {
        logger->log("Creating base output directory...");
        if (mkdir(_options->directory, MKDIR_MODE) != 0) {
            logger->error("An error occured while creating the file structure, is the device/system full? Exiting...");
            errorOccurred = true;
        }
    }

    /* Create the CameraProvider object and get the core interface */
    UniqueObj<CameraProvider> cameraProvider;
    ICameraProvider *iCameraProvider = NULL;
    if (!errorOccurred) {
        logger->log("Getting the camera provider...");
        cameraProvider.reset(CameraProvider::create());
        iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
        if (!iCameraProvider) {
            logger->error("An error occured while creating the camera provider! Exiting...");
            errorOccurred = true;
        }
    }

    /* Get the camera devices */
    std::vector<CameraDevice*> cameraDevices;
    if (!errorOccurred) {
        logger->log("Getting the camera devices...");
        iCameraProvider->getCameraDevices(&cameraDevices);
        if (cameraDevices.size() == 0) {
            logger->error("No cameras available! Exiting...");
            errorOccurred = true;
        }
    }
    uint8_t numCameras = cameraDevices.size();

    /* Create the capture session using the first device and get the core interface */
    UniqueObj<CaptureSession> captureSessions[numCameras];
    ICaptureSession *iCaptureSessions[numCameras];
    if (!errorOccurred) {
        logger->log("Creating the capture sessions...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            Argus::Status status;
            captureSessions[i].reset(iCameraProvider->createCaptureSession(cameraDevices[i], &status));
            iCaptureSessions[i] = interface_cast<ICaptureSession>(captureSessions[i]);
            if (status == STATUS_UNAVAILABLE) {
                logger->error("Camera device unavailable, try rebooting. Exiting...");
                errorOccurred = true;
            } else if (!iCaptureSessions[i]) {
                logger->error("Failed to get ICaptureSession interface! Exiting...");
                errorOccurred = true;
            }
        }
    }

    /* Verify the selected sensor mode */
    ICameraProperties *iCameraProperties = NULL;
    std::vector<SensorMode*> sensorModes;
    SensorMode *sensorMode = NULL;
    ISensorMode *iSensorMode = NULL;
    if (!errorOccurred) {
        logger->log("Verifying the selected sensor mode...");
        iCameraProperties = interface_cast<ICameraProperties>(cameraDevices[0]);
        if (!iCameraProperties) {
            logger->error("Failed to get ICameraProperties interface! Exiting...");
            errorOccurred = true;
        } else {
            iCameraProperties->getBasicSensorModes(&sensorModes);
            if (sensorModes.size() == 0) {
                logger->error("Failed to get sensor modes! Exiting...");
                errorOccurred = true;
            } else if (_options->captureMode > sensorModes.size()) {
                logger->log("Unable to set selected sensor mode, setting to default...");
                _options->captureMode = CAPTURE_MODE_0;
            } else {
                sensorMode = sensorModes[_options->captureMode];
                iSensorMode = interface_cast<ISensorMode>(sensorMode);
                if (!iSensorMode) {
                    logger->error("Failed to get ISensorMode interface! Exiting...");
                    errorOccurred = true;
                } else {
                    _options->captureResolution = iSensorMode->getResolution();
                }
            }
        }
    }

    /* Write the options object to file */
    if (!errorOccurred) {
        logger->log("Writing the command line options to a file...");
        _options->write();
    }

    /* Initialize the settings of output stream */
    UniqueObj<OutputStream> captureStreams[numCameras];
    if (!errorOccurred) {
        logger->log("Creating the output streams...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            UniqueObj<OutputStreamSettings> streamSettings(iCaptureSessions[i]->createOutputStreamSettings(STREAM_TYPE_EGL));
            IEGLOutputStreamSettings *iEglStreamSettings = interface_cast<IEGLOutputStreamSettings>(streamSettings);
            if (!iEglStreamSettings) {
                logger->error("Failed to get IEGLOutputStreamSettings interface! Exiting...");
                errorOccurred = true;
            } else {
                iEglStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
                iEglStreamSettings->setEGLDisplay(EGL_NO_DISPLAY);
                iEglStreamSettings->setResolution(iSensorMode->getResolution());
                captureStreams[i] = (UniqueObj<OutputStream>) iCaptureSessions[i]->createOutputStream(streamSettings.get());
                if (!captureStreams[i]) {
                    logger->error("Failed to create capture stream! Exiting...");
                    errorOccurred = true;
                }
            }
        }
    }

    /* Launch the threads to consume frames from the OutputStream */
    ConsumerThread *consumers[numCameras];
    uint8_t numThreadsCreated = 0;
    uint8_t numThreadsInitialized = 0;
    if (!errorOccurred) {
        logger->log("Launching consumer threads...");
        for (uint8_t i = 0; i < numCameras  && !errorOccurred; i++) {
            consumers[i] = new ConsumerThread(captureStreams[i].get(), i, *_options);
            numThreadsCreated = i + 1;
            if (!consumers[i]) {
                logger->error("Failed to create consumer thread! Exiting...");
                errorOccurred = true;
            } else if (!consumers[i]->initialize()) {
                logger->error("Failed to initialize consumer thread! Exiting...");
                errorOccurred = true;
            } else {
                numThreadsInitialized = i + 1;
            }
        }
    }

    /* Wait until the consumer thread is connected to the stream */
    if (!errorOccurred) {
        logger->log("Waiting for the consumer threads...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            if (!consumers[i]->waitRunning()) {
                logger->error("Failed to start consumer thread! Exiting...");
                errorOccurred = true;
            }
        }
    }

    /* Create capture request and enable its output stream */
    UniqueObj<Request> requests[numCameras];
    if (!errorOccurred) {
        logger->log("Creating capture requests and enabling output streams...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            requests[i].reset(iCaptureSessions[i]->createRequest());
            IRequest *iRequest = interface_cast<IRequest>(requests[i]);
            if (!iRequest) {
                logger->error("Failed to get request interface! Exiting...");
                errorOccurred = true;
            } else {
                ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(iRequest->getSourceSettings());
                if (!iSourceSettings) {
                    logger->error("Failed to get source settings interface! Exiting...");
                    errorOccurred = true;
                } else {
                    iSourceSettings->setSensorMode(sensorMode);
                    iSourceSettings->setFrameDurationRange(iSensorMode->getFrameDurationRange());
                    iRequest->enableOutputStream(captureStreams[i].get());
                }
            }
        }
    }

    /* Submit capture requests. */
    uint8_t numSuccessfulRequests = 0;
    if (!errorOccurred) {
        logger->log("Starting repeat capture requests...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            if (iCaptureSessions[i] && iCaptureSessions[i]->repeat(requests[i].get()) != STATUS_OK) {
                logger->error("Failed to start repeat capture requests! Exiting...");
                errorOccurred = true;
            } else {
                numSuccessfulRequests = i + 1;
            }
        }
    }

    if (!errorOccurred) {

        /* Wait for captureTime seconds or until SIGINT is received. */
        if (_options->captureTime > 0) {
            time_t start = time(0);
            while (_doRun && time(0) - start < _options->captureTime) {
                sleep(1);
                for (int i = 0; i < numCameras; i++) {
                    if (!consumers[i]->isExecuting())
                        _doRun = false;
                }
            }
        }

        /* Wait until SIGINT is received. */
        else {
            while (_doRun) {
                sleep(1);
                for (int i = 0; i < numCameras; i++) {
                    if (!consumers[i]->isExecuting())
                        _doRun = false;
                }
            }
        }

        /* Start stop process for threads */
        for (uint8_t i = 0; i < numCameras; i++)
            if (consumers[i])
                consumers[i]->stopExecute();
        sleep(1);
    }

    /* Stop the repeating request. */
    if (!errorOccurred)
        logger->log("Stopping repeat capture requests...");
    for (uint8_t i = 0; i < numSuccessfulRequests; i++) {
        if (iCaptureSessions[i]) {
            iCaptureSessions[i]->stopRepeat();
        }
    }

    /* Wait until the requests have been fulfilled */
    if (!errorOccurred)
        logger->log("Finishing remaining capture requests...");
    for (uint8_t i = 0; i < numSuccessfulRequests; i++)
        if (iCaptureSessions[i])
            iCaptureSessions[i]->waitForIdle();

    /* Destroy the output streams. */
    if (!errorOccurred)
        logger->log("Destroying the output streams...");
    for (uint8_t i = 0; i < numCameras; i++)
        if (captureStreams[i])
            captureStreams[i].reset();

    /* Wait for the consumer thread to complete. */
    if (!errorOccurred)
        logger->log("Waiting for consumers to terminate...");
    for (uint8_t i = 0; i < numThreadsInitialized; i++) {
        if (consumers[i])
            consumers[i]->shutdown();
    }
    for (uint8_t i = 0; i < numThreadsCreated; i++)
        delete consumers[i];

    if (!errorOccurred)
        logger->log("Process has completed successfully, exiting...");
    return !errorOccurred;
}

/* Sets the static variable _doRun to false to exit the infinite loop in run() */
void App::signalCallback(int signum) {
    _doRun = false;
}

/* Parse the output of lsblk to return the mount path of any removable block device
 * lsblk output takes the form:
 * NAME         MAJ:MIN RM   SIZE RO TYPE MOUNTPOINT
 * loop0          7:0    0    16M  1 loop 
 * sda            8:0    1  57.6G  0 disk /media/nvidia/$DEVICE
 * mmcblk0      179:0    0  29.1G  0 disk 
 * ...
 */
std::vector<std::string> App::getAvailableDevices() {

    // resultant strings, remains empty if pipe fails
    std::vector<std::string> result;

    // avoid magic numbers
    enum HEADER_INDEX {NAME, MAJ_MIN, RM, SIZE, RO, TYPE, MOUNTPOINT};

    // stores one line from the lsblk result
    size_t bufferSize = 256;
    char buffer[bufferSize];
    std::stringstream ss;
    std::string str;
    bool valid;

    // open the pipe and repeatedly parse lines...
    FILE* pipe = popen("lsblk", "r");
    if (pipe) {
        while (fgets(&buffer[0], bufferSize, pipe)) {

            // read buffer into string stream
            ss.str(buffer);
            valid = true;
            for (int i = 0; i <= MOUNTPOINT; i++) {

                // skip if invalid condition is met in previous iteration or ss is empty
                if (!valid || ss.rdbuf()->in_avail() == 0)
                    break;

                // validate the next token
                ss >> str;
                switch (i) {

                    // check if device is removable
                    case RM:
                        if (str != "1")
                            valid = false;
                        break;

                    // check if device is read-only
                    case RO:
                        if (str != "0")
                            valid = false;
                        break;

                    // check if device is a disk block/partition
                    case TYPE:
                        if (str != "disk" && str != "part")
                            valid = false;
                        break;

                    // get mount point path
                    case MOUNTPOINT:
                        if (valid && str.size() > 1)
                            result.push_back(str + "/");
                        break;

                    default:
                        break;
                }
            }
        }
        pclose(pipe);
    }
    return result;
}
