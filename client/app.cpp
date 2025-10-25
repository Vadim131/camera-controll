// Project headres
#include "app.h"


const float MIN_TEMP = 0, MAX_TEMP = 50;
const int TIME = 10;

std::string getCurrentTimeAsString()
{
    auto now = std::chrono::system_clock::now();
    std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm* localTime = std::localtime(&currentTime);
    std::stringstream ss;
    ss << std::put_time(localTime, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

Camera::Camera() : QSICamera()
{
    m_doPhoto = false;
    m_doTransferring = false;
    stop_flag = false;
}

Camera::~Camera()
{
    stop_flag = true;
    if (photoWorker.joinable())
    {
	readyToRun.notify_one();
        photoWorker.join();
    }
}

bool Camera::Connect()
{
    std::cout << "Connect .. " << std::endl;
    try
    {
	bool isMain;
	long adu;
	double eADU;
	double fwc;
	QSICamera::CameraState state;

        std::string serial("");
        //std::string * namea
        std::string desc("");
        std::string info("");
        std::string modelNumber("");
        get_DriverInfo(info);
	std::cout << "qsiapitest version: " << info << "\n";

        //Discover the connected cameras
        int iNumFound;
        std::string camSerial[QSICamera::MAXCAMERAS];// = "";
        std::string camDesc[QSICamera::MAXCAMERAS];// = "";

        get_AvailableCameras(camSerial, camDesc, iNumFound);

        std::cout << "Available cameras ("<< iNumFound << "): " << std::endl;
        for (int i = 0; i < iNumFound; i++)
            std::cout << camSerial[i] << ":" << camDesc[i] << "\n";
		
        // Get the serial number of the selected camera in the setup dialog box
        get_SelectCamera(serial);
        if (serial.length() !=0 )
            std::cout << "Saved selected camera serial number is " << serial << "\n";
        put_SelectCamera(serial);

        get_IsMainCamera(&isMain);
        put_IsMainCamera(true);

        // Connect to the selected camera and retrieve camera parameters
        std::cout << "Try to connect to camera...\n";
        put_Connected(true);
        std::cout << "Camera connected. \n";
        get_SerialNumber(serial);
        std::cout << "Serial Number: " + serial + "\n";

        // Get Model Number
        get_ModelNumber(modelNumber);
        std::cout << modelNumber << "\n";

	// This app works only with 6 series
        if (modelNumber.substr(0,1) != "6")
	    exit(1);

        // Get the camera state
        // It should be idle at this point
        get_CameraState(&state);
	if (state == QSICamera::CameraError)
	{
            std::cout << "--- Camera is in Error state at the init time. Better to reboot camera. ---" << std::endl;
            exit(1);
	}

        put_SoundEnabled(true);
        put_LEDEnabled(true);

        // Get Camera Description
        get_Description(desc);
        std:: cout << desc << "\n";
	
	bool hasShutter;
	get_HasShutter(&hasShutter);
	if (!hasShutter)
	{
	    std::cout << "No shutter. This app works only with camera having the shutter" << std::endl;
	    exit(1);
	}

	std::cout << "Test shutter. Sometimes it get stuck.\n" << "If the camera beeps, that is the error. Reboot" << std::endl;
	//put_ManualShutterOpen(false);
        //put_ManualShutterOpen(false);
	//put_ManualShutterMode(false);

	//put_ManualShutterMode(true);
	//put_ManualShutterOpen(true);
        //put_ManualShutterOpen(false);
	//put_ManualShutterMode(false);

	put_ReadoutSpeed(QSICamera::HighImageQuality);
	put_ShutterPriority(QSICamera::ShutterPriorityElectronic);

        long maxX, maxY;
        get_CameraXSize(&maxX);
        get_CameraYSize(&maxY);
	std::cout << "Image size: " << maxX << " x " << maxY << std::endl;
        put_StartX(0);
        put_StartY(0);
        put_NumX(maxX);
        put_NumY(maxY);
        put_BinX(1);
        put_BinY(1);

	// Query various camera parameters
	get_ElectronsPerADU(&eADU);
	std::cout << "Electrons per adu: " << eADU << "\n";
	get_FullWellCapacity(&fwc);
	std::cout << "FWC: " << fwc << "\n";
	get_MaxADU(&adu);
	std::cout << "Max. ADU: " << adu << "\n";
	
	get_MinExposureTime(&m_minExposureTime);
	std::cout << "Min. Exposure Time: " << m_minExposureTime << " sec\n";
	get_MaxExposureTime(&m_maxExposureTime);
	std::cout << "Max. Exposure Time: " << m_maxExposureTime << " sec\n";
	m_exposureTime = 0.03;
	std::cout << "Set Standard Exposure Time: " << m_exposureTime << " sec\n";
	
	//put_ManualShutterMode(true);
	//put_ManualShutterOpen(true);

	std::string lastError;
	get_LastError(lastError);
	std::cout << "Last Error: " << lastError << std::endl;

	bool canSetTemp;
	get_CanSetCCDTemperature(&canSetTemp);
	std::cout << "Can set temp? " << canSetTemp << "\n";
	bool coolerOn;
	int result = get_CoolerOn(&coolerOn);
	if (result == 0 && coolerOn)
	    put_CoolerOn(false);

    	put_SetCCDTemperature(10);
    }
    catch (std::runtime_error &err)
    {
        std::string text = err.what();
	std::cout << text << "\n";
	std::string last("");
	get_LastError(last);
	std::cout << last << "\n";
	std::cout << "exiting with errors\n";
	return false;
    }

    m_doPhoto = false;
    m_doTransferring = false;
    stop_flag = false;
    photoWorker = std::thread(&Camera::photoWorkerLoop, this);
    photoWorker.detach();

    return true;
}

void Camera::photoWorkerLoop()
{
    while (!stop_flag)
    {
        CameraPhotoTask task = popTask();
	if (!task.m_status)
	{
	    std::cout << "PhotoWorker gets a bad task...\n";;
            continue;
	}

	task.m_startTime = getCurrentTimeAsString();
	m_currentTask = task;
	makePhoto(task.m_exposureTime, task.m_light, task.m_dir);
    }

    std::cout << "Photo thread is stopped...\n";
}

bool Camera::PushTakeNPhoto(double exposureTime, int nPhoto, std::string dir, bool light)
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    for (int i = 0; i < nPhoto; i++)
    {
	CameraPhotoTask task;
	task.m_status = true;
	task.m_exposureTime = exposureTime;
	task.m_dir = dir;
	task.m_light = light;
	task.m_pushTime = getCurrentTimeAsString();
        queueTask.push(task);
    }

    readyToRun.notify_one();
    return true;
}

bool Camera::StopPhoto()
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    std::cout << "Start to clear queue...\n";
    while (!queueTask.empty())
        queueTask.pop();

    try
    {
        std::cout << "Try to stop...\n";
        bool canAbort;
        get_CanAbortExposure(&canAbort);
        if (canAbort)
        {
            AbortExposure();
            m_doPhoto = false;
            m_doTransferring = false;
        }
    }
    catch (std::runtime_error& err)
    {
        std::string text = err.what();
	std::cout << text << "\n";
	std::string last("");
	get_LastError(last);
	std::cout << last << "\n";
	std::cout << "exiting with errors\n";
	return false;
    }

    return true;
}

CameraPhotoTask Camera::popTask()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        readyToRun.wait(lock, [this](){ return !queueTask.empty() || stop_flag; });
    }
    
    if (stop_flag)
        return CameraPhotoTask();

    CameraPhotoTask task = queueTask.front();
    queueTask.pop();
    return task;
}

bool Camera::makePhoto(double exposureTime, bool light, std::string dir)
{
    m_doPhoto = true;
    m_exposureTime = exposureTime;

    int x, y, z;

    QSICamera::CameraState state;
    get_CameraState(&state);
    std::cout << "Camera state: " << state << "...\n";

    clock_gettime(CLOCK_REALTIME, &start);
    end.tv_sec = start.tv_sec + exposureTime;
    QSICamera::ReadoutSpeed readout;
    get_ReadoutSpeed(readout);
    end.tv_sec += readout == 0 ? 13 : 3;

    std::cout << "Starting exposure  with " << m_exposureTime << "s exposure time" << " ...\n";
    std::cout << "Photo is light: " << light << " ...\n";
    bool result = false;
    try
    {
        result = StartExposure(m_exposureTime, light);
        if (result != 0) 
        {
            std::cout << "StartExposure error \n";
            std::string last("");
            get_LastError(last);
            std::cout << last << "\n";
            return false;
        }
        std::cout << "Start Exposure Complete.  \nWaiting for Image Ready...\n";
    }
    catch (std::runtime_error &err)
    {
        std::string text = err.what();
	std::cout << text << "\n";
	std::string last("");
	get_LastError(last);
	std::cout << last << "\n";
	std::cout << "exiting with errors\n";
	return false;
    }

    //usleep(m_exposureTime);

    bool imageReady = false;
    result = get_ImageReady(&imageReady);
    while(!imageReady)
    {
	try
	{
            result = get_ImageReady(&imageReady);
	}
        catch (std::runtime_error &err)
        {
    	    std::string text = err.what();
    	    std::cout << text << "\n";
    	    std::string last("");
    	    get_LastError(last);
    	    std::cout << last << "\n";
	    return false;
	}

        if (result != 0) 
        {
            std::cout << "get_ImageReady error \n";
            std::string last("");
            get_LastError(last);
            std::cout << last << "\n";
	    return false;
	}
    }
    if (!m_doPhoto) // The exposre was aborted
    {
        std::cout << "Stopped...\n";
        return false;
    }

    std::cout << "Image Ready...\n";

    m_doTransferring = true;

    result = get_ImageArraySize(x, y, z);
    if (result != 0) 
    {
        std::cout << "get_ImageArraySize error \n";
        std::string last("");
        get_LastError(last);
        std::cout << last << "\n";
        return false;
    }
    std::cout << "Image Size " << x << " x " << y << " " << x * y << " Pixels...\n";

    struct timespec startR, finishR;
    clock_gettime(CLOCK_REALTIME, &startR);

    unsigned short* image = new unsigned short[x * y];
    // Retrieve the pending image from the camera
    result = get_ImageArray(image);
    if (result != 0) 
    {
        std::cout << "get_ImageArray error \n";
        std::string last("");
        get_LastError(last);
        std::cout << last << "\n";
        delete [] image;
        return false;
    }
    clock_gettime(CLOCK_REALTIME, &finishR);
    printf("Read time %.9f sec\n", (finishR.tv_sec - startR.tv_sec) + (finishR.tv_nsec - startR.tv_nsec) * 1E-9);

    m_doTransferring = false;
    m_doPhoto = false;
    m_currentTask.m_status = false;

    std::cout << image[100] << " " << image[667] << std::endl;
    
    bool flag = SaveImage(image, x, y, dir);

    char filename[256] = "";
    sprintf(filename, "qsiimage%d.tif", 1);
    WriteTIFF(image, x, y, filename);
    delete [] image;

    return flag;
}

int Camera::WriteTIFF(unsigned short* buffer, int cols, int rows, char* filename)
{
	TIFF *image;
	unsigned char *out;
	out = new unsigned char[cols*rows];

	AdjustImage(buffer, cols, rows, out);

	// Open the TIFF file
	if((image = TIFFOpen(filename, "w")) == NULL)
	{
		printf("Could not open output.tif for writing\n");
		exit(42);
	}
	
	// We need to set some values for basic tags before we can add any data
	TIFFSetField(image, TIFFTAG_IMAGEWIDTH, cols);
	TIFFSetField(image, TIFFTAG_IMAGELENGTH, rows);
	TIFFSetField(image, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(image, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField(image, TIFFTAG_ROWSPERSTRIP, 1);
	
	TIFFSetField(image, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField(image, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
	TIFFSetField(image, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
	TIFFSetField(image, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	
	TIFFSetField(image, TIFFTAG_XRESOLUTION, 150.0);
	TIFFSetField(image, TIFFTAG_YRESOLUTION, 150.0);
	TIFFSetField(image, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
	
	// Write the information to the file
	for (int y = 0; y < rows; y++)
	{
		TIFFWriteScanline(image, &out[cols*y], y);
	}
	
	// Close the file
	TIFFClose(image);
	delete[] (out);
	return 0;
}

void Camera::AdjustImage(unsigned short * buffer, int x, int y, unsigned char * out)
{
	//
	// adjust the image to better display and
	// covert to a byte array
	//
	// Compute the average pixel value and the standard deviation
	double avg = 0;
	double total = 0;
	double deltaSquared = 0;
	double std = 0;

	for (int j = 0; j < y; j++)
		for (int i = 0; i < x; i++)
			total += (double)buffer[((j * x) + i)];

	avg = total / (x * y);

	for (int j = 0; j < y; j++)
		for (int i = 0; i < x; i++)
			deltaSquared += std::pow((avg - buffer[((j * x) + i)]), 2);

	std = std::sqrt(deltaSquared / ((x * y) - 1));
	
	// re-scale scale pixels to three standard deviations for display
	double minVal = avg - std*3;
	if (minVal < 0) minVal = 0;
	double maxVal = avg + std*3;
	if (maxVal > 65535) maxVal = 65535;
	double range = maxVal - minVal;
	if (range == 0)
		range = 1;
	double spread = 65535 / range;
	//
	// Copy image to bitmap for display and scale during the copy
	//
	int pix;
	double pl;
	unsigned char level;
	
	for (int j = 0; j < y; j++)
	{
		for (int i = 0; i < x; i++)
		{
			pix = ((j * x) + i);
			pl = (double)buffer[pix];
			// Spread out pixel values for better viewing
			pl = (pl - minVal) * spread;
			// Scale pixel value
			pl = (pl*255)/65535;
			if (pl > 255) pl = 255;
			//
			level = (unsigned char)pl;
			out[pix] = level;
		}
	}
	return;
}

bool Camera::ChangeShutterMode(bool isOpen)
{
    put_ManualShutterMode(true);
    put_ManualShutterOpen(isOpen);
    put_ManualShutterMode(false);
    std::cout << "Shutter is " << (isOpen ? "open" : "close") << std::endl;
    return true;
}

bool Camera::SetExposureTime(double& value)
{
    if (value > m_maxExposureTime)
        value = m_maxExposureTime;

    if (value < m_minExposureTime)
        value = m_minExposureTime;

    m_exposureTime = value;
    return true;
}

bool Camera::Disconnect()
{
    try
    {
        put_FanMode(QSICamera::fanFull);
	bool coolerOn;
	int result = get_CoolerOn(&coolerOn);
	if (result == 0 && coolerOn)
	    put_CoolerOn(false);

	ChangeShutterMode(false);
        put_Connected(false);
        std::cout << "Camera disconnected. \n";
    }
    catch (std::runtime_error &err)
    {
	std::string text = err.what();
	std::cout << text << "\n";
	std::string last("");
	get_LastError(last);
	std::cout << last << "\n";
	std::cout << "exiting with errors\n";
	return false;
    }

    stop_flag = true;
    if (photoWorker.joinable())
    {
	readyToRun.notify_one();
        photoWorker.join();
    }

    return true;
}

bool Camera::SaveImage(unsigned short* image, int cols, int rows, std::string dir)
{
    std::string last_time;
    get_LastExposureStartTime(last_time);
    std::string filename = dir + "/photo_" + last_time + ".dat";
    std::cout << "Wrtie objects to " << filename << std::endl;

    std::ofstream fout(filename, std::ios::binary);
    if (!fout.is_open())
        return false;

    struct timespec start, finish;
    clock_gettime(CLOCK_REALTIME, &start);

    fout << "date " << last_time << std::endl;

    fout << "exposureTime " << m_exposureTime << std::endl;

    fout << "shutterPriority ";
    QSICamera::ShutterPriority priority;
    get_ShutterPriority(&priority);
    if (priority == 0)
        fout << "ShutterPriorityMechanical" << std::endl;
    else
        fout << "ShutterPriorityElectronic" << std::endl;

    fout << "readoutSpeed ";
    QSICamera::ReadoutSpeed readout;
    get_ReadoutSpeed(readout);
    if (readout == 0)
        fout << "HighImageQuality" << std::endl;
    else
        fout << "FastReadout" << std::endl;

    fout << "gain ";
    QSICamera::CameraGain gain;
    get_CameraGain(&gain);
    if (gain == 0)
        fout << "HighGain" << std::endl;
    else if (gain == 1)
        fout << "LowGain" << std::endl;
    else
        fout << "AutoGain" << std::endl;
    
    double eADU;
    get_ElectronsPerADU(&eADU);
    fout << "ePerADU " << eADU << std::endl;

    double ccdTemp;
    get_CCDTemperature(&ccdTemp);
    fout << "ccdTemp " << ccdTemp << std::endl;

    fout << "xSize " << cols << std::endl;
    fout << "ySize " << rows << std::endl;

    fout.write((char const*)image, sizeof(image[0]) * cols * rows);

    fout.close();

    std::cout << "Finish saving" << std::endl;
    clock_gettime(CLOCK_REALTIME, &finish);
    printf("Converting time %.9f sec\n", (finish.tv_sec - start.tv_sec) + (finish.tv_nsec - start.tv_nsec) * 1E-9);

    return true;
}


int handle_server_command(const char *command) {
	return 0;
}

