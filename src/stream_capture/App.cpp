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
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <sys/stat.h>
#include <signal.h>

#include <iostream>

using namespace Argus;
using namespace EGLStream;

#define MKDIR_MODE 0777
#define WHITESPACE " \f\n\r\t\v"
#define USB_MOUNT_PATH "/media/nvidia/"

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

    /* Register signal callback to various signals (ctrl+c, ctrl+d, etc...) */
    signal(SIGHUP, signalCallback);
    signal(SIGINT, signalCallback);
    signal(SIGQUIT, signalCallback);
    signal(SIGTERM, signalCallback);

    /* Verify the Options object was successfully created */
    if (!errorOccurred) {
        producerPrint("Verifying the command line options...");
        if (!_options) {
            producerPrint("An error occurred while creating the Options object! Exiting...");
            Options::printHelp();
            errorOccurred = true;
        } else if (!_options->parse(argc, argv)) {
            producerPrint("An error occurred while verifying the command line options! Exiting...");
            Options::printHelp();
            errorOccurred = true;
        }
    }

    /* Search for available usb device and pre-append options directory to reflect changes */
    if (!errorOccurred) {
        producerPrint("Searching for available usb volumes...");
        std::vector<std::string> devicePaths = getUSBMountPaths();
        std::string path("");
        if (devicePaths.empty()) {
            producerPrint("Device not found, falling back to saving on system memory...");
        } else {
            producerPrint(std::string("Using USB device mounted at: " + devicePaths[0]).c_str());
            path = devicePaths[0];
        }
        // pre-append the device path to the chosen directory name
        strncat(&path[0], _options->directory, FILENAME_MAX);
        strncpy(_options->directory, path.data(), FILENAME_MAX);
    }
    
    /* Create base directory for saving images, sub-directories are handled by consumers */
    if (!errorOccurred) {
        producerPrint("Creating base output directory...");
        if (mkdir(_options->directory, MKDIR_MODE) != 0) {
            producerPrint("An error occured while creating the file structure! Exiting...");
            errorOccurred = true;
        }
    }

    /* Write the options object to file */
    if (!errorOccurred) {
        producerPrint("Writing the command line options to a file...");
        _options->write();
    }

    /* Create the CameraProvider object and get the core interface */
    UniqueObj<CameraProvider> cameraProvider;
    ICameraProvider *iCameraProvider = NULL;
    if (!errorOccurred) {
        producerPrint("Getting the camera provider...");
        cameraProvider.reset(CameraProvider::create());
        iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
        if (!iCameraProvider) {
            producerPrint("An error occured while creating the camera provider! Exiting...");
            errorOccurred = true;
        }
    }

    /* Get the camera devices */
    std::vector<CameraDevice*> cameraDevices;
    if (!errorOccurred) {
        producerPrint("Getting the camera devices...");
        iCameraProvider->getCameraDevices(&cameraDevices);
        if (cameraDevices.size() == 0) {
            producerPrint("No cameras available! Exiting...");
            errorOccurred = true;
        }
    }
    uint8_t numCameras = cameraDevices.size();

    /* Create the capture session using the first device and get the core interface */
    UniqueObj<CaptureSession> captureSessions[numCameras];
    ICaptureSession *iCaptureSessions[numCameras];
    if (!errorOccurred) {
        producerPrint("Creating the capture sessions...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            Argus::Status status;
            captureSessions[i].reset(iCameraProvider->createCaptureSession(cameraDevices[i], &status));
            iCaptureSessions[i] = interface_cast<ICaptureSession>(captureSessions[i]);
            if (status == STATUS_UNAVAILABLE) {
                producerPrint("Camera device unavailable, try rebooting. Exiting...");
                errorOccurred = true;
            } else if (!iCaptureSessions[i]) {
                producerPrint("Failed to get ICaptureSession interface! Exiting...");
                errorOccurred = true;
            }
        }
    }

    /* Verify the selected sensor mode */
    ICameraProperties *iCameraProperties = NULL;
    ISensorMode *iSensorMode = NULL;
    std::vector<SensorMode*> sensorModes;
    SensorMode *sensorMode = NULL;
    if (!errorOccurred) {
        producerPrint("Verifying the selected sensor mode...");
        iCameraProperties = interface_cast<ICameraProperties>(cameraDevices[0]);
        if (!iCameraProperties) {
            producerPrint("Failed to get ICameraProperties interface! Exiting...");
            errorOccurred = true;
        } else {
            iCameraProperties->getBasicSensorModes(&sensorModes);
            if (sensorModes.size() == 0) {
                producerPrint("Failed to get sensor modes! Exiting...");
                errorOccurred = true;
            } else if (_options->captureMode > sensorModes.size()) {
                producerPrint("Unable to set selected sensor mode, setting to default...");
                _options->setCaptureMode(CAPTURE_MODE_0);
            } else {
                sensorMode = sensorModes[_options->captureMode];
            }
        }
    }

    /* Initialize the settings of output stream */
    UniqueObj<OutputStream> captureStreams[numCameras];
    if (!errorOccurred) {
        producerPrint("Creating the output streams...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            UniqueObj<OutputStreamSettings> streamSettings(iCaptureSessions[i]->createOutputStreamSettings(STREAM_TYPE_EGL));
            IEGLOutputStreamSettings *iEglStreamSettings = interface_cast<IEGLOutputStreamSettings>(streamSettings);
            if (!iEglStreamSettings) {
                producerPrint("Failed to get IEGLOutputStreamSettings interface! Exiting...");
                errorOccurred = true;
            } else {
                iEglStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
                iEglStreamSettings->setEGLDisplay(EGL_NO_DISPLAY);
                iEglStreamSettings->setResolution(Size2D<uint32_t>(_options->captureWidth, _options->captureHeight));
                captureStreams[i] = (UniqueObj<OutputStream>) iCaptureSessions[i]->createOutputStream(streamSettings.get());
                if (!captureStreams[i]) {
                    producerPrint("Failed to create capture stream! Exiting...");
                    errorOccurred = true;
                }
            }
        }
    }

    /* Launch the FrameConsumer thread to consume frames from the OutputStream */
    ConsumerThread *consumers[numCameras];
    uint8_t numThreadsCreated = 0;
    uint8_t numThreadsInitialized = 0;
    if (!errorOccurred) {
        producerPrint("Launching consumer threads...");
        for (uint8_t i = 0; i < numCameras  && !errorOccurred; i++) {
            consumers[i] = new ConsumerThread(captureStreams[i].get(), i, *_options, &_doRun);
            numThreadsCreated = i + 1;
            if (!consumers[i]) {
                producerPrint("Failed to create consumer thread! Exiting...");
                errorOccurred = true;
            } else if (!consumers[i]->initialize()) {
                producerPrint("Failed to initialize consumer thread! Exiting...");
                errorOccurred = true;
            } else {
                numThreadsInitialized = i + 1;
            }
        }
    }

    /* Wait until the consumer thread is connected to the stream */
    if (!errorOccurred) {
        producerPrint("Waiting for the consumer threads...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            if (!consumers[i]->waitRunning()) {
                producerPrint("Failed to start consumer thread! Exiting...");
                errorOccurred = true;
            }
        }
    }

    /* Create capture request and enable its output stream */
    UniqueObj<Request> requests[numCameras];
    if (!errorOccurred) {
        producerPrint("Creating capture requests and enabling output streams...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            requests[i].reset(iCaptureSessions[i]->createRequest());
            IRequest *iRequest = interface_cast<IRequest>(requests[i]);
            if (!iRequest) {
                producerPrint("Failed to get request interface! Exiting...");
                errorOccurred = true;
            } else {
                ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(iRequest->getSourceSettings());
                if (!iSourceSettings) {
                    producerPrint("Failed to get source settings interface! Exiting...");
                    errorOccurred = true;
                } else {
                    iSourceSettings->setSensorMode(sensorMode);
                    iRequest->enableOutputStream(captureStreams[i].get());
                }
            }
        }
    }

    /* Submit capture requests. */
    uint8_t numSuccessfulRequests = 0;
    if (!errorOccurred) {
        producerPrint("Starting repeat capture requests...");
        for (uint8_t i = 0; i < numCameras && !errorOccurred; i++) {
            if (iCaptureSessions[i] && iCaptureSessions[i]->repeat(requests[i].get()) != STATUS_OK) {
                producerPrint("Failed to start repeat capture requests! Exiting...");
                errorOccurred = true;
            } else {
                numSuccessfulRequests = i + 1;
            }
        }
    }

    /* Wait for captureTime seconds or until SIGINT is received. */
    if (!errorOccurred) {
        if (_options->captureTime > 0)
            sleep(_options->captureTime);
        else
            while (_doRun)
                sleep(1);
    }

    /* Stop the repeating request. */
    if (!errorOccurred)
        producerPrint("Stopping repeat capture requests...");
    for (uint8_t i = 0; i < numSuccessfulRequests; i++) {
        if (iCaptureSessions[i]) {
            iCaptureSessions[i]->stopRepeat();
        }
    }

    /* Wait until the requests have been fulfilled */
    if (!errorOccurred)
        producerPrint("Finishing remaining capture requests...");
    for (uint8_t i = 0; i < numSuccessfulRequests; i++)
        if (iCaptureSessions[i])
            iCaptureSessions[i]->waitForIdle();

    /* Destroy the output streams. */
    if (!errorOccurred)
        producerPrint("Destroying the output streams...");
    for (uint8_t i = 0; i < numCameras; i++)
        if (captureStreams[i])
            captureStreams[i].reset();

    /* Wait for the consumer thread to complete. */
    if (!errorOccurred)
        producerPrint("Waiting for consumers to terminate...");
    for (uint8_t i = 0; i < numThreadsInitialized; i++) {
        if (consumers[i])
            consumers[i]->shutdown();
    }
    for (uint8_t i = 0; i < numThreadsCreated; i++)
        delete consumers[i];

    if (!errorOccurred)
        producerPrint("Process has completed successfully, exiting...");
    return !errorOccurred;
}

/* Standard method for formatted printing */
void App::producerPrint(const char *s) {
    printf("[PRODUCER]: %s\n", s);
}

/* Sets the static variable _doRun to false to exit the infinite loop in run() */
void App::signalCallback(int signum) {
    _doRun = false;
}

/* Use system calls to return the names of any device listed under USB_MOUNT_PATH */
std::vector<std::string> App::getUSBMountPaths() {
    std::vector<std::string> result;
    size_t bufferSize = 256;
    char buffer[bufferSize];
    std::string cmd("ls ");
    cmd += USB_MOUNT_PATH;
    FILE* pipe = popen(cmd.c_str(), "r");
    int i = 0;
    if (pipe) {
        while (fgets(&buffer[0], bufferSize, pipe) != NULL) {
            rtrim(buffer, bufferSize);

            result.push_back(USB_MOUNT_PATH);
            result[i] += buffer;
            result[i] += "/";
            i++;
        }
        pclose(pipe);
    }
    return result;
}

/* Trim a string from the right to remove control characters */
void App::rtrim(char *str, size_t n) {
    if (str) {
        for (size_t i = 0; i < n; i++) {
            if (iscntrl(*(str + i))) {
                memset(str + i, 0, n - i);
                break;
            }
        }
    }
}
