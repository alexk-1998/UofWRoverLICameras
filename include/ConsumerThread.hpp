/*******************************************************************************
 * Consumer thread:
 *   Creates an EGLStream::FrameConsumer object to read frames from the
 *   OutputStream, then creates/populates an NvBuffer (dmabuf) from the frames
 *   to be processed by processV4L2Fd.
 ******************************************************************************/

#pragma once

#include "Thread.h"
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>

class Options;
class NvJPEGEncoder;

class ConsumerThread : public ArgusSamples::Thread {

    public:
        explicit ConsumerThread(Argus::OutputStream *stream, uint32_t id, const Options& options, bool *doRun);
        virtual ~ConsumerThread();

    protected:
        virtual bool threadInitialize();
        virtual bool threadExecute();
        virtual bool threadShutdown();

    private:
        bool processV4L2Fd(int32_t fd, uint64_t frameNumber);
        uint32_t getJPEGSize(uint32_t width, uint32_t height);
        void consumerPrint(const char *s);

        Argus::OutputStream* _stream;
        Argus::UniqueObj<EGLStream::FrameConsumer> _consumer;
        int _dmabuf;
        NvJPEGEncoder *_jpegEncoder;
        unsigned char *_outputBuffer;
        uint32_t _outputBufferSize;
        uint32_t _id;
        const Options& _options;
        bool *_doRun;
};
