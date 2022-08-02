/*
 * StreamPreview.cpp
 *
 * This is a re-worked version of the 'multi_camera' example from the
 * Jetson multimedia API samples. This will provide an indefinite stream
 * of composited images from each of the 6 cameras displayed on a simple
 * OpenCV window for interactivity.
 */

#include "Error.h"
#include "Thread.h"

#include <Argus/Argus.h>
#include <EGLGlobal.h>
#include <EGLStream/EGLStream.h>
#include <EGLStream/NV/ImageNativeBuffer.h>

#include <nvbuf_utils.h>
#include <NvEglRenderer.h>

#include <stdio.h>
#include <stdlib.h>

#include "opencv2/opencv.hpp"
#include "opencv2/highgui.hpp"

using namespace Argus;
using namespace EGLStream;

/* Constants */
static const uint32_t            MAX_CAMERA_NUM = 6;
static const uint32_t            DEFAULT_FPS = 38;
static const int32_t             CELL_WIDTH = 400;
static const int32_t             CELL_HEIGHT = 300;
static const int32_t             CELL_SPACING = 2;
static const Size2D<uint32_t>    STREAM_SIZE(3 * CELL_WIDTH + 4 * CELL_SPACING, 2 * CELL_HEIGHT + 3 * CELL_SPACING);

/* Globals */
UniqueObj<CameraProvider>       g_cameraProvider;
bool                            g_doStream = true;

/* Debug print macros */
#define PRODUCER_PRINT(...) printf("PRODUCER: " __VA_ARGS__)
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)

namespace ArgusSamples {

/* A utility class to hold all resources of one capture session */
class CaptureHolder : public Destructable {

    public:
        explicit CaptureHolder();
        virtual ~CaptureHolder();

        bool initialize(CameraDevice *device);

        CaptureSession* getSession() const {
            return m_captureSession.get();
        }

        OutputStream* getStream() const {
            return m_outputStream.get();
        }

        Request* getRequest() const {
            return m_request.get();
        }

        virtual void destroy() {
            delete this;
        }

    private:
        UniqueObj<CaptureSession> m_captureSession;
        UniqueObj<OutputStream> m_outputStream;
        UniqueObj<Request> m_request;
};

CaptureHolder::CaptureHolder() {}

CaptureHolder::~CaptureHolder() {
    /* Destroy the output stream */
    m_outputStream.reset();
}

bool CaptureHolder::initialize(CameraDevice *device) {

    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(g_cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to get ICameraProvider interface");

    /* Create the capture session using the first device and get the core interface */
    m_captureSession.reset(iCameraProvider->createCaptureSession(device));
    ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(m_captureSession);
    IEventProvider *iEventProvider = interface_cast<IEventProvider>(m_captureSession);
    if (!iCaptureSession || !iEventProvider)
        ORIGINATE_ERROR("Failed to create CaptureSession");

    /* Create the OutputStream */
    UniqueObj<OutputStreamSettings> streamSettings(
        iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iEglStreamSettings =
        interface_cast<IEGLOutputStreamSettings>(streamSettings);
    if (!iEglStreamSettings)
        ORIGINATE_ERROR("Failed to create EglOutputStreamSettings");

    iEglStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
    iEglStreamSettings->setEGLDisplay(EGL_NO_DISPLAY);
    iEglStreamSettings->setResolution(STREAM_SIZE);

    m_outputStream.reset(iCaptureSession->createOutputStream(streamSettings.get()));

    /* Create capture request and enable the output stream */
    m_request.reset(iCaptureSession->createRequest());
    IRequest *iRequest = interface_cast<IRequest>(m_request);
    if (!iRequest)
        ORIGINATE_ERROR("Failed to create Request");
    iRequest->enableOutputStream(m_outputStream.get());

    ISourceSettings *iSourceSettings =
            interface_cast<ISourceSettings>(iRequest->getSourceSettings());
    if (!iSourceSettings)
        ORIGINATE_ERROR("Failed to get ISourceSettings interface");
    iSourceSettings->setFrameDurationRange(Range<uint64_t>(1e9/DEFAULT_FPS));

    return true;
}


/*
 * Argus Consumer Thread:
 * This is the thread acquires buffers from each stream and composite them to
 * one frame. Finally it renders the composited frame through EGLRenderer.
 */
class ConsumerThread : public Thread {

    public:
        explicit ConsumerThread(std::vector<OutputStream*> &streams) :
            m_streams(streams),
            m_compositedFrame(0)
        {}
        virtual ~ConsumerThread();

    protected:
        /** @name Thread methods */
        /**@{*/
        virtual bool threadInitialize();
        virtual bool threadExecute();
        virtual bool threadShutdown();
        /**@}*/

        std::vector<OutputStream*> &m_streams;
        UniqueObj<FrameConsumer> m_consumers[MAX_CAMERA_NUM];
        int m_dmabufs[MAX_CAMERA_NUM];
        NvBufferCompositeParams m_compositeParam;
        int m_compositedFrame;
};

ConsumerThread::~ConsumerThread() {

    if (m_compositedFrame)
        NvBufferDestroy(m_compositedFrame);

    for (uint32_t i = 0; i < m_streams.size(); i++)
        if (m_dmabufs[i])
            NvBufferDestroy(m_dmabufs[i]);
}

bool ConsumerThread::threadInitialize() {

    NvBufferRect dstCompRect[6];
    NvBufferCreateParams input_params = {0};

    // -------------------------
    // |  -----  -----  -----  |
    // |  | 0 |  | 2 |  | 4 |  |
    // |  -----  -----  -----  |
    // |                       |
    // |  -----  -----  -----  |
    // |  | 1 |  | 3 |  | 5 |  |
    // |  -----  -----  -----  |
    // -------------------------

    for (uint8_t i = 0; i < MAX_CAMERA_NUM / 2; i++) {
        // top row
        dstCompRect[i].top  = CELL_SPACING;
        dstCompRect[i].left = (i + 1) * CELL_SPACING + i * CELL_WIDTH;
        dstCompRect[i].width = CELL_WIDTH;
        dstCompRect[i].height = CELL_HEIGHT;
        // bottom row
        dstCompRect[i + MAX_CAMERA_NUM / 2].top  = 2 * CELL_SPACING + CELL_HEIGHT;
        dstCompRect[i + MAX_CAMERA_NUM / 2].left = dstCompRect[i].left;
        dstCompRect[i + MAX_CAMERA_NUM / 2].width = CELL_WIDTH;
        dstCompRect[i + MAX_CAMERA_NUM / 2].height = CELL_HEIGHT;
    }

    /* Allocate composited buffer */
    //input_params.payloadType = NvBufferPayload_SurfArray;
    input_params.width = STREAM_SIZE.width();
    input_params.height = STREAM_SIZE.height();
    input_params.layout = NvBufferLayout_Pitch;
    input_params.colorFormat = NvBufferColorFormat_ABGR32;
    input_params.nvbuf_tag = NvBufferTag_VIDEO_CONVERT;
    NvBufferCreateEx (&m_compositedFrame, &input_params);
    if (!m_compositedFrame)
        ORIGINATE_ERROR("Failed to allocate composited buffer");

    /* Initialize composite parameters */
    memset(&m_compositeParam, 0, sizeof(m_compositeParam));
    m_compositeParam.composite_flag = NVBUFFER_COMPOSITE;
    m_compositeParam.input_buf_count = m_streams.size();
    memcpy(m_compositeParam.dst_comp_rect, dstCompRect,
                sizeof(NvBufferRect) * m_compositeParam.input_buf_count);
    for (uint32_t i = 0; i < 6; i++) {
        m_compositeParam.dst_comp_rect_alpha[i] = 1.0f;
        m_compositeParam.src_comp_rect[i].top = 0;
        m_compositeParam.src_comp_rect[i].left = 0;
        m_compositeParam.src_comp_rect[i].width = STREAM_SIZE.width();
        m_compositeParam.src_comp_rect[i].height = STREAM_SIZE.height();
    }

    /* Initialize buffer handles. Buffer will be created by FrameConsumer */
    memset(m_dmabufs, 0, sizeof(m_dmabufs));

    /* Create the FrameConsumer */
    for (uint32_t i = 0; i < m_streams.size(); i++)
        m_consumers[i].reset(FrameConsumer::create(m_streams[i]));

    return true;
}

bool ConsumerThread::threadExecute() {

    IEGLOutputStream *iEglOutputStreams[MAX_CAMERA_NUM];
    IFrameConsumer *iFrameConsumers[MAX_CAMERA_NUM];

    for (uint32_t i = 0; i < m_streams.size(); i++) {
        iEglOutputStreams[i] = interface_cast<IEGLOutputStream>(m_streams[i]);
        iFrameConsumers[i] = interface_cast<IFrameConsumer>(m_consumers[i]);
        if (!iFrameConsumers[i])
            ORIGINATE_ERROR("Failed to get IFrameConsumer interface");

        /* Wait until the producer has connected to the stream */
        CONSUMER_PRINT("Waiting until producer is connected...\n");
        if (iEglOutputStreams[i]->waitUntilConnected() != STATUS_OK)
            ORIGINATE_ERROR("Stream failed to connect.");
        CONSUMER_PRINT("Producer has connected; continuing.\n");
    }

    /* Create a window for displaying the stream */
    char winName[] = "Stream Preview";
    cv::namedWindow(winName, cv::WINDOW_NORMAL);
    cv::resizeWindow(winName, STREAM_SIZE.width(), STREAM_SIZE.height());

    while (g_doStream) {
        for (uint32_t i = 0; i < m_streams.size(); i++) {
            /* Acquire a frame */
            UniqueObj<Frame> frame(iFrameConsumers[i]->acquireFrame());
            IFrame *iFrame = interface_cast<IFrame>(frame);
            if (!iFrame)
                break;

            /* Get the IImageNativeBuffer extension interface */
            NV::IImageNativeBuffer *iNativeBuffer =
                interface_cast<NV::IImageNativeBuffer>(iFrame->getImage());
            if (!iNativeBuffer)
                ORIGINATE_ERROR("IImageNativeBuffer not supported by Image.");

            /* If we don't already have a buffer, create one from this image.
               Otherwise, just blit to our buffer */
            if (!m_dmabufs[i]) {
                m_dmabufs[i] = iNativeBuffer->createNvBuffer(iEglOutputStreams[i]->getResolution(),
                                                             NvBufferColorFormat_ABGR32,
                                                             NvBufferLayout_Pitch);
                if (!m_dmabufs[i])
                    CONSUMER_PRINT("\tFailed to create NvBuffer\n");
            
            } else if (iNativeBuffer->copyToNvBuffer(m_dmabufs[i]) != STATUS_OK) {
                ORIGINATE_ERROR("Failed to copy frame to NvBuffer.");
            }
        }

        /* Composite and display the image */
        if (m_streams.size() > 1) {

            /* Create composite image */
            NvBufferComposite(m_dmabufs, m_compositedFrame, &m_compositeParam);

            /* Convert NvBuffer to cv::Mat */
            void *pdata = NULL;
            NvBufferMemMap(m_compositedFrame, 0, NvBufferMem_Read, &pdata);
            NvBufferMemSyncForCpu(m_compositedFrame, 0, &pdata);
            NvBufferParams params;
            NvBufferGetParams(m_compositedFrame, &params);
            cv::Mat imgbuf = cv::Mat(STREAM_SIZE.height(),
                                    STREAM_SIZE.width(),
                                    CV_8UC4, pdata, params.pitch[0]);
            cv::Mat display_img; 
            cvtColor(imgbuf, display_img, cv::COLOR_RGBA2BGR);
            NvBufferMemUnMap(m_compositedFrame, 0, &pdata);

            /* Display the image, check for exit button press */
            cv::imshow(winName, display_img);
            cv::waitKey(1);
            g_doStream = cv::getWindowProperty(winName, cv::WND_PROP_AUTOSIZE) != -1;
        }
    }

    /* Destroy the window, if it exists */
    if (cv::getWindowProperty(winName, cv::WND_PROP_VISIBLE) != -1)
        cv::destroyWindow(winName);

    CONSUMER_PRINT("Done.\n");

    requestShutdown();

    return true;
}

bool ConsumerThread::threadShutdown() {
    return true;
}


/*
 * Argus Producer Thread:
 * Open the Argus camera driver and detect how many camera devices available.
 * Create one OutputStream for each camera device. Launch consumer thread
 * and then submit repeat capture requests.
 */
static bool execute() {

    /* Initialize the Argus camera provider */
    g_cameraProvider = UniqueObj<CameraProvider>(CameraProvider::create());
    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(g_cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to get ICameraProvider interface");
    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());

    /* Get the camera devices */
    std::vector<CameraDevice*> cameraDevices;
    iCameraProvider->getCameraDevices(&cameraDevices);
    if (cameraDevices.size() == 0)
        ORIGINATE_ERROR("No cameras available");

    UniqueObj<CaptureHolder> captureHolders[MAX_CAMERA_NUM];
    uint32_t streamCount = cameraDevices.size() < MAX_CAMERA_NUM ?
            cameraDevices.size() : MAX_CAMERA_NUM;
    for (uint32_t i = 0; i < streamCount; i++) {
        captureHolders[i].reset(new CaptureHolder);
        if (!captureHolders[i].get()->initialize(cameraDevices[i]))
            ORIGINATE_ERROR("Failed to initialize Camera session %d", i);
    }

    std::vector<OutputStream*> streams;
    for (uint32_t i = 0; i < streamCount; i++)
        streams.push_back(captureHolders[i].get()->getStream());

    /* Start the rendering thread */
    ConsumerThread consumerThread(streams);
    PROPAGATE_ERROR(consumerThread.initialize());
    PROPAGATE_ERROR(consumerThread.waitRunning());

    /* Submit capture requests */
    for (uint32_t j = 0; j < streamCount; j++) {
        ICaptureSession *iCaptureSession =
                interface_cast<ICaptureSession>(captureHolders[j].get()->getSession());
        Request *request = captureHolders[j].get()->getRequest();
        Argus::Status status = iCaptureSession->repeat(request);
        if (status != STATUS_OK)
            ORIGINATE_ERROR("Failed to submit capture request");
    }

    /* Loop until g_doStream becomes false from signal handler or exit button pressed */
    while (g_doStream) {}

    /* Stop repeating requests */
    for (uint32_t i = 0; i < streamCount; i++) {
        ICaptureSession *iCaptureSession =
            interface_cast<ICaptureSession>(captureHolders[i].get()->getSession());
        iCaptureSession->stopRepeat();
    }

    /* Wait for idle */
    for (uint32_t i = 0; i < streamCount; i++) {
        ICaptureSession *iCaptureSession =
            interface_cast<ICaptureSession>(captureHolders[i].get()->getSession());
        iCaptureSession->waitForIdle();
    }

    /* Destroy the capture resources */
    for (uint32_t i = 0; i < streamCount; i++) {
        captureHolders[i].reset();
    }

    /* Wait for the rendering thread to complete */
    PROPAGATE_ERROR(consumerThread.shutdown());

    /* Shut down Argus */
    g_cameraProvider.reset();

    return true;
}

}; /* namespace ArgusSamples */

int main(int argc, char * argv[]) {
    if (!ArgusSamples::execute())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
