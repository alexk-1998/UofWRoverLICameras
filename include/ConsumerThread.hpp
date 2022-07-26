/*
 * ConsumerThread.hpp
 *
 * Creates an EGLStream::FrameConsumer object to read frames from the
 * OutputStream, then creates/populates an NvBuffer (dmabuf) from the frames
 * to be processed by processV4L2Fd, which saves each frame as a JPEG image.
 * Note that for ThreadExecute to terminate, stopExecute must first be 
 * called on the object.
 */

#pragma once

#include "Thread.h"
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>

class Options;
class Logger;
class NvJPEGEncoder;

class ConsumerThread : public ArgusSamples::Thread {

    public:
        explicit ConsumerThread(Argus::OutputStream *stream, uint32_t id, const Options& options);
        virtual ~ConsumerThread();

        void stopExecute();
        bool isExecuting();

    protected:
        virtual bool threadInitialize();
        virtual bool threadExecute();
        virtual bool threadShutdown();

    private:
        bool processV4L2Fd(int32_t fd, uint64_t frameNumber);
        uint32_t getJPEGSize(uint32_t width, uint32_t height);
        void consumerLog(const char *s);

        Argus::OutputStream* _stream;
        Argus::UniqueObj<EGLStream::FrameConsumer> _consumer;
        int _dmabuf;
        NvJPEGEncoder *_jpegEncoder;
        unsigned char *_outputBuffer;
        uint32_t _outputBufferSize;
        uint32_t _id;
        const Options& _options;
        Logger *_logger;
        bool _doExecute;
};
