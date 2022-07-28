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
#include <NvJpegEncoder.h>
#include <EGLStream/NV/ImageNativeBuffer.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

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
        _doExecute(true)
{}

ConsumerThread::~ConsumerThread() {
    if (_jpegEncoder)
        delete _jpegEncoder;
    if (_outputBuffer)
        delete[] _outputBuffer;
    if (_dmabuf != -1)
        NvBufferDestroy(_dmabuf);
}

bool ConsumerThread::threadInitialize() {

    bool errorOccurred = false;

    /* Create the image sub-directory */
    if (!errorOccurred) {
        consumerLog("Creating the image sub-directory...");
        std::string subDirName(_options.directory);
        subDirName.append("/cam" + std::to_string(_id));
        if (mkdir(subDirName.c_str(), MKDIR_MODE) != 0) {
            consumerLog("Failed to create image sub-directory!");
            errorOccurred = true;
        }
    }

    /* Create the frame consumer */
    if (!errorOccurred) {
        consumerLog("Creating the frame consumer...");
        _consumer.reset(FrameConsumer::create(_stream));
        if (!_consumer || !_consumer.get()) {
            consumerLog("Failed to create frame consumer!");
            errorOccurred = true;
        }
    }

    /* Allocate memory for JPEG encoded images */
    if (!errorOccurred) {
        consumerLog("Creating the encoder output buffer...");
        _outputBufferSize = getJPEGSize(_options.captureWidth, _options.captureHeight);
        _outputBuffer = new unsigned char[_outputBufferSize];
        if (!_outputBuffer) {
            consumerLog("Failed to allocate buffer memory!");
            errorOccurred = true;
        }
    }

    /* Create encoder with name jpegenc */
    if (!errorOccurred) {
        consumerLog("Creating the encoder...");
        _jpegEncoder = NvJPEGEncoder::createJPEGEncoder("jpegenc");
        if (!_jpegEncoder) {
            consumerLog("Failed to create JPEGEncoder!");
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
    consumerLog("Waiting until producer is connected...");
    if (iEglOutputStream->waitUntilConnected() != STATUS_OK) {
        consumerLog("Stream failed to connect! Exiting...");
        errorOccurred = true;
    } else
        consumerLog("Producer has connected! Continuing...");

    /* Repeatedly save frames until a shutdown is requested from outside the class */
    uint64_t index = 1;
    UniqueObj<Frame> frame;
    IFrame *iFrame = NULL;
    NV::IImageNativeBuffer *iNativeBuffer = NULL;
    bool wroteFirst = false;
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
                consumerLog("An error occurred while retrieving the image buffer interface! Exiting...");
                errorOccurred = true;
            }

            /* If we don't already have a buffer, create one from this image */
            if (!errorOccurred) {
                if (_dmabuf == -1) {
                    _dmabuf = iNativeBuffer->createNvBuffer(iEglOutputStream->getResolution(),
                                                            NvBufferColorFormat_YUV420,
                                                            NvBufferLayout_BlockLinear);
                    if (_dmabuf == -1) {
                        consumerLog("An error occurred while creating the NvBuffer! Exiting...");
                        errorOccurred = true;
                    }
                } else if (iNativeBuffer->copyToNvBuffer(_dmabuf) != STATUS_OK) {
                    consumerLog("An error occurred while copying to the NvBuffer! Exiting...");
                    errorOccurred = true;
                }
            }

            /* Process frame. */
            if (!errorOccurred) {
                if (processV4L2Fd(_dmabuf, index++)) {
                    if (!wroteFirst) {
                        consumerLog("First image successfully written! This message will not be shown for any subsequent images.");
                        wroteFirst = true;
                    }
                } else {
                    consumerLog("An error occurred while writing the JPEG image! Exiting...");
                    errorOccurred = true;
                }
            }

            /* Exit if any previous operations raised errors */
            if (errorOccurred)
                break;
        }
    }

    if (!errorOccurred)
        consumerLog("Process completed successfully, requesting shutdown...");
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

/* Standard method for formatted printing and logging */
void ConsumerThread::consumerLog(const char *s) {

    // format the string
    std::stringstream ss;
    ss << "CONSUMER " << _id << " [" << time(0) << "]: " << s;

    // write to STDOUT
    std::cout << ss.str() << std::endl;

    // write to log file
    std::string logname(_options.directory);
    logname += "/log.txt";
    std::ofstream logfile(logname, std::ios_base::app);
    if (logfile)
        logfile << ss.str() << std::endl;
}
