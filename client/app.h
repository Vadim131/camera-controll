
/* This is for compatibility with C
 * first ifdef part for only app.cpp
 */

#ifdef __cplusplus

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <queue>

// Save
#include "tiffio.h"

// QSI Camera
#include "qsiapi.h"

struct CameraPhotoTask {
    bool m_status = false;
    double m_exposureTime;
    bool m_light;
    std::string m_dir;
    std::string m_pushTime;
    std::string m_startTime;
};

class Camera: public QSICamera {
    double m_exposureTime, m_minExposureTime, m_maxExposureTime;
    std::atomic<bool> m_doPhoto, m_doTransferring;
    std::atomic<bool> stop_flag;;
    struct timespec start, end;
    std::condition_variable readyToRun;
    std::mutex queue_mutex;
    std::queue<CameraPhotoTask> queueTask;
    CameraPhotoTask popTask();
    CameraPhotoTask m_currentTask;
    std::thread photoWorker;
    void photoWorkerLoop();
    bool makePhoto(double exposureTime, bool light = true, std::string dir = "pics");

public:
    Camera();
    ~Camera();
    bool Connect();
    bool Disconnect();
    bool ChangeShutterMode(bool isOpen = false);
    bool SetExposureTime(double& value);
    bool PushTakeNPhoto(double exposureTime, int nPhoto, std::string dir = "pics", bool light = true);
    bool StopPhoto();
    bool SaveImage(unsigned short* image, int cols, int rows, std::string dir = "pics");
    double GetMinExposureTime() {return m_minExposureTime;};
    double GetMaxExposureTime() {return m_maxExposureTime;};
    int WriteTIFF(unsigned short* buffer, int cols, int rows, char* filename);
    void AdjustImage(unsigned short* buffer, int x, int y, unsigned char* out);
    double GetExposureTime() {return m_exposureTime;};
    bool DoPhoto() {return m_doPhoto;};
    bool DoTransferring() {return m_doTransferring;};
    struct timespec GetTaskStartTime() {return start;};
    struct timespec GetTaskPreliminaryEndTime() {return end;};
};

#endif

/*
 * Second part is used for c-code. It only one function, because it is callback which should call c++ func
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief: handle commands from server by calling c++ camera's API functions, and add new msg with answer to queue
 * @param command: it goes from server and can be one of this several types:
 *                     1) connect
 *                     2) disconnect
 *                     3) set + params (example 'set 10 10 off' )
 *                     4) phototask + params
 *                     5) cancel
 * @return int (bool) 0 - fail  or 1 - success cause it goes to c-func. this value is usually send to server
 */
void handle_server_command(const char* command, size_t len);

/**
 * @brief add new msg with status to queue
 * @return int (bool) 0 -fail or 1 -success
 */
void get_camera_status(void);

#ifdef __cplusplus
    }
#endif

