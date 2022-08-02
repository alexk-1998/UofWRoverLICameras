/*
 * ConsumerThread.cpp
 *
 * Creates an EGLStream::FrameConsumer object to read frames from the
 * OutputStream, then creates/populates an NvBuffer (dmabuf) from the frames
 * to be processed by processV4L2Fd, which saves each frame as a JPEG image.
 * Note that for ThreadExecute to terminate, stopExecute must first be 
 * called on the object.
 */

#include "ConsumerThread.hpp"

#include "Options.hpp"
#include "Logger.hpp"
#include <NvJpegEncoder.h>
#include <EGLStream/NV/ImageNativeBuffer.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <chrono>

using namespace Argus;
using namespace EGLStream;

#define MKDIR_MODE 0777

ConsumerThread::ConsumerThread(OutputStream *stream, uint32_t id, const Options& options) :
        _stream(stream),
        _dmabuf(-1),
        _jpegEncoder(NULL),
        _outputBuffer(NULL),
        _outputBufferSize(0),
        _id(id),
        _options(options),
        _logger(NULL),
        _doExecute(true)
{}

ConsumerThread::~ConsumerThread() {
    if (_jpegEncoder)
        delete _jpegEncoder;
    if (_outputBuffer)
        delete[] _outputBuffer;
    if (_dmabuf != -1)
        NvBufferDestroy(_dmabuf);
    if (_logger)
        delete _logger;
}

bool ConsumerThread::threadInitialize() {

    bool errorOccurred = false;

    /* Create the logger */
    if (!errorOccurred) {
        std::stringstream ss;
        ss << "CONSUMER " << std::to_string(_id);
        _logger = new Logger(ss.str(), _options.directory);
        if (!_logger)
            errorOccurred = true;
        else
            _logger->log("Logger successfully created!");
    }

    /* Create the image sub-directory */
    if (!errorOccurred) {
        _logger->log("Creating the image sub-directory...");
        std::stringstream ss;
        ss << _options.directory << "/cam" << std::to_string(_id);
        if (mkdir(ss.str().c_str(), MKDIR_MODE) != 0) {
            _logger->error("Failed to create image sub-directory!");
            errorOccurred = true;
        }
    }

    /* Create the frame consumer */
    if (!errorOccurred) {
        _logger->log("Creating the frame consumer...");
        _consumer.reset(FrameConsumer::create(_stream));
        if (!_consumer && !_consumer.get()) {
            _logger->error("Failed to create frame consumer!");
            errorOccurred = true;
        }
    }

    /* Allocate memory for JPEG encoded images */
    if (!errorOccurred) {
        _logger->log("Creating the encoder output buffer...");
        _outputBufferSize = getJPEGSize(_options.captureWidth, _options.captureHeight);
        _outputBuffer = new unsigned char[_outputBufferSize];
        if (!_outputBuffer) {
            _logger->error("Failed to allocate buffer memory!");
            errorOccurred = true;
        }
    }

    /* Create encoder with name jpegenc */
    if (!errorOccurred) {
        _logger->log("Creating the encoder...");
        _jpegEncoder = NvJPEGEncoder::createJPEGEncoder("jpegenc");
        if (!_jpegEncoder) {
            _logger->error("Failed to create JPEGEncoder!");
            errorOccurred = true;
        } else if (_options.profile)
            _jpegEncoder->enableProfiling();
    }

    return !errorOccurred;
}

bool ConsumerThread::threadExecute() {

    IEGLOutputStream *iEglOutputStream = interface_cast<IEGLOutputStream>(_stream);
    IFrameConsumer *iFrameConsumer = interface_cast<IFrameConsumer>(_consumer);
    bool errorOccurred = false;

    /* Wait until the producer has connected to the stream. */
    _logger->log("Waiting until producer is connected...");
    if (iEglOutputStream->waitUntilConnected() != STATUS_OK) {
        _logger->error("Stream failed to connect! Exiting...");
        errorOccurred = true;
    } else
        _logger->log("Producer has connected! Continuing...");

    /* Repeatedly save frames until a shutdown is requested from outside the class */
    uint64_t index = 1;
    UniqueObj<Frame> frame;
    IFrame *iFrame = NULL;
    NV::IImageNativeBuffer *iNativeBuffer = NULL;
    bool wroteFirst = false;
    auto start = std::chrono::steady_clock::now();
    while (!errorOccurred && _doExecute) {

        /* Acquire a frame, null when stream ends */
        frame.reset(iFrameConsumer->acquireFrame());
        iFrame = interface_cast<IFrame>(frame);
        if (!iFrame)
            break;

        if (iFrame->getNumber() % _options.saveEvery == 0) {

            /* Get the IImageNativeBuffer extension interface */
            iNativeBuffer = interface_cast<NV::IImageNativeBuffer>(iFrame->getImage());
            if (!iNativeBuffer) {
                _logger->error("An error occurred while retrieving the image buffer interface! Exiting...");
                errorOccurred = true;
            }

            /* If we don't already have a buffer, create one from this image */
            if (!errorOccurred) {
                if (_dmabuf == -1) {
                    _dmabuf = iNativeBuffer->createNvBuffer(iEglOutputStream->getResolution(),
                                                            NvBufferColorFormat_YUV420,
                                                            NvBufferLayout_BlockLinear);
                    if (_dmabuf == -1) {
                        _logger->error("An error occurred while creating the NvBuffer! Exiting...");
                        errorOccurred = true;
                    }
                } else if (iNativeBuffer->copyToNvBuffer(_dmabuf) != STATUS_OK) {
                    _logger->error("An error occurred while copying to the NvBuffer! Exiting...");
                    errorOccurred = true;
                }
            }

            /* Process frame. */
            if (!errorOccurred) {
                if (processV4L2Fd(_dmabuf, index++)) {
                    if (!wroteFirst) {
                        _logger->log("First image successfully written! This message will not be shown for any subsequent images.");
                        wroteFirst = true;
                    }
                } else {
                    // device is probably full, don't actually set the error status, just exit
                    _logger->log("An error occurred while writing the JPEG image, is the device/system full? Exiting...");
                    _doExecute = false;
                }
            }

            /* Exit if any previous operations raised errors */
            if (errorOccurred)
                break;
        }
    }
    auto stop = std::chrono::steady_clock::now();

    _doExecute = false;

    /* Calculate and display effective fps */
    if (_options.profile) {
        auto elapsed = (stop - start).count();
        double fps = (double) index / (double) elapsed;
        std::stringstream ss;
        ss << "Images processed: " << std::to_string(index-1);
        ss << "Time elapsed: " << std::to_string(elapsed);
        ss << "Effective fps: " << std::to_string(fps);
        _logger->log(ss.str());
    }        

    if (!errorOccurred)
        _logger->log("Process completed successfully, requesting shutdown...");
    requestShutdown();
    return !errorOccurred;
}

bool ConsumerThread::threadShutdown() {
    /* Display encoder stats (frames processed, latency, ...) */
    if (_options.profile)
        _jpegEncoder->printProfilingStats();
    return true;
}

/* Used to stop infinite loop in execute */
void ConsumerThread::stopExecute() {
    _doExecute = false;
}

/* Used to check if the thread should be killed */
bool ConsumerThread::isExecuting() {
    return _doExecute;
}

/* JPEG encode the passed file descriptor, return bool indicating successful file writing */
bool ConsumerThread::processV4L2Fd(int32_t fd, uint64_t index) {
    
    bool success = false;

    /* Create a file with name filename */
    char filename[FILENAME_MAX];
    sprintf(filename, "%s/cam%u/image%06lu.jpg", _options.directory, _id, index);
    std::ofstream *outputFile = new std::ofstream(filename);

    /* Write the image to the file */
    if (outputFile) {
        unsigned long size = _outputBufferSize;
        if (_jpegEncoder->encodeFromFd(fd, JCS_YCbCr, &_outputBuffer, size) == 0) {
            outputFile->write((char *) _outputBuffer, size);
            success = outputFile->good();
            outputFile->close();
        }
    }
    delete outputFile;
    return success;
}

/* Returns the buffer size, in bytes, of an encoded JPEG image with the same width and height as the passed fields */
uint32_t ConsumerThread::getJPEGSize(uint32_t width, uint32_t height) {
    return width * height * 3 / 2;
}
