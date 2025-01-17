#include <iostream>
#include <stdlib.h>
#include <time.h>

#include "Exceptions.h"
#include "TrakStarController.h"

using namespace SmartScan;

TrakStarController::TrakStarController(bool useMockData) : mUseMockData { useMockData }
{
	if (mUseMockData) {
		// Initialize random seed.
		srand(time(NULL));
	}
}

TrakStarController::~TrakStarController()
{
	this->StopTransmit();
}

void TrakStarController::Init()
{
	if (mUseMockData) {
		return;
	}

	int errorCode = InitializeBIRDSystem();
	ErrorHandler(errorCode);

	errorCode = GetBIRDSystemConfiguration(&ATC3DG.m_config);
	ErrorHandler(errorCode);

	for (int i = 0; i < ATC3DG.m_config.numberSensors; i++) {
		pSensor.push_back(CSensor());
		errorCode = GetSensorConfiguration(i, &pSensor[i].m_config);
		ErrorHandler(errorCode);
	}

	for (int i = 0; i < ATC3DG.m_config.numberTransmitters; i++) {
		pXmtr.push_back(CXmtr());
		errorCode = GetTransmitterConfiguration(i, &pXmtr[i].m_config);
		ErrorHandler(errorCode);
	}
}

void TrakStarController::StopTransmit()
{
	if (mUseMockData) {
		return;
	}

	short int id = -1;
	int errorCode = SetSystemParameter(SELECT_TRANSMITTER, &id, sizeof(id));
	ErrorHandler(errorCode);
}

void TrakStarController::SelectTransmitter(short int id)
{
	int errorCode = SetSystemParameter(SELECT_TRANSMITTER, &id, sizeof(id));
	ErrorHandler(errorCode);
	errorCode = GetTransmitterStatus(id);
	DeviceStatusHandler(errorCode);
}

void TrakStarController::SetPowerlineFrequency(double freq)
{
	int errorCode = SetSystemParameter(POWER_LINE_FREQUENCY, &freq, sizeof(freq));
	ErrorHandler(errorCode);
}

void TrakStarController::SetMeasurementRate(double freq)
{
	int errorCode = SetSystemParameter(MEASUREMENT_RATE, &freq, sizeof(freq));
	ErrorHandler(errorCode);
}

void TrakStarController::SetMaxRange(double range)
{
	int errorCode = SetSystemParameter(MAXIMUM_RANGE, &range, sizeof(range));
	ErrorHandler(errorCode);
}

void TrakStarController::SetMetric()
{
	BOOL isTrue = true;
	int errorCode = SetSystemParameter(METRIC, &isTrue, sizeof(isTrue));
	ErrorHandler(errorCode);
}

void TrakStarController::SetReferenceFrame(short int id, double angles[3])
{
	BOOL isTrue = true;
	DOUBLE_ANGLES_RECORD record;

	record.a = angles[0];
	record.e = angles[1];
	record.r = angles[2];

	int errorCode = SetTransmitterParameter(id, XYZ_REFERENCE_FRAME, &record, sizeof(record));
	ErrorHandler(errorCode);
	errorCode = SetTransmitterParameter(id, REFERENCE_FRAME, &isTrue, sizeof(isTrue));
	ErrorHandler(errorCode);
}

void TrakStarController::SetSensorFormat()
{
	for (int i = 0; i < ATC3DG.m_config.numberSensors; i++)	{
		DATA_FORMAT_TYPE type = DOUBLE_POSITION_ANGLES_TIME_Q_BUTTON;
		int errorCode = SetSensorParameter(i, DATA_FORMAT, &type, sizeof(type));
		ErrorHandler(errorCode);
	}
}

void TrakStarController::SetRefSensorFormat(int id)
{
	DATA_FORMAT_TYPE type = DOUBLE_POSITION_MATRIX;
	int errorCode = SetSensorParameter(id, DATA_FORMAT, &type, sizeof(type));
	ErrorHandler(errorCode);
}

void TrakStarController::SetSensorZOffset(int id, double offset)
{
	DOUBLE_POSITION_RECORD record;

	record.x = 0.0;
	record.y = 0.0;
	record.z = offset * toInch;

	int errorCode = SetSensorParameter(id, SENSOR_OFFSET, &record, sizeof(record));
	ErrorHandler(errorCode);
}

const int TrakStarController::NumAttachedBoards() const
{
	if (mUseMockData) {
		return 1;
	}
	
	return ATC3DG.m_config.numberBoards;
}

const int TrakStarController::NumAttachedTransmitters() const
{
	if (mUseMockData) {
		return 1;
	}

	return pXmtr.size();
}

std::vector<int> TrakStarController::GetAttachedPorts() const
{
	std::vector<int> attachedSensors;

	if (!mUseMockData) {
    	for (int i = 0; i < ATC3DG.m_config.numberSensors; i++) {
    	    if(pSensor[i].m_config.attached) {
    	        attachedSensors.push_back(i);
    	    }
    	}
	}
	else {
		for (int i = 0; i < 3; i++) {
			attachedSensors.push_back(i);
		}
	}

	return attachedSensors;
}

std::vector<int> TrakStarController::GetAttachedSerials() const
{
	std::vector<int> attachedSensors;

	if (!mUseMockData) {
    	for (int i = 0; i < ATC3DG.m_config.numberSensors; i++) {
    	    if (pSensor[i].m_config.attached) {
    	        attachedSensors.push_back(pSensor[i].m_config.serialNumber);
    	    }
    	}
	}
	else {
		for (int i = 0; i < 3; i++) {
			attachedSensors.push_back(i);
		}
	}

	return attachedSensors;
}

/* Measurements: 235*/
Point3 TrakStarController::GetRecord(int id)
{
	// When in mock mode, return a random value on a sphere.
	if (mUseMockData) {
		//return GetMockRecord();
		return GetMockRecordFromFile(id);
	}

	// Only report the data if everything is okay.
	// Device status handler for sensors 
	unsigned int status = GetSensorStatus(id);
	static int lastDeviceStatus;

	try	{
		DeviceStatusHandler(status);
	}
	catch (ex_trakStar e) {
		if (status != lastDeviceStatus)	{
			std::cerr << e.what() << std::endl;
		}
		lastDeviceStatus = status;
		return Point3();
	}
    
	DOUBLE_POSITION_ANGLES_TIME_Q_BUTTON_RECORD record;
	int errorCode = GetAsynchronousRecord(id, &record, sizeof(record));
	static int lastErrorCode;

	try	{
		ErrorHandler(errorCode);
	}
	catch (ex_trakStar e) {
		if (errorCode != lastErrorCode)	{
			std::cerr << e.what() << std::endl;
		}
		return Point3();
	}
    lastErrorCode = errorCode;

	return Point3(record.x, record.y, record.z, record.r, record.e, record.a, record.quality, record.button);
}

Point3Ref TrakStarController::GetRefRecord(int id)
{
	// When in mock mode, return a random value on a sphere.
	if (mUseMockData) {
		//return GetMockRecord();
		return Point3Ref();
	}

	// Only report the data if everything is okay.
	// Device status handler for sensors 
	unsigned int status = GetSensorStatus(id);
	static int lastDeviceStatus;

	try	{
		DeviceStatusHandler(status);
	}
	catch (ex_trakStar e) {
		if (status != lastDeviceStatus)	{
			std::cerr << e.what() << std::endl;
		}
		lastDeviceStatus = status;
		return Point3Ref();
	}
    
	DOUBLE_POSITION_MATRIX_RECORD record;
	int errorCode = GetAsynchronousRecord(id, &record, sizeof(record));
	static int lastErrorCode;

	try	{
		ErrorHandler(errorCode);
	}
	catch (ex_trakStar e) {
		if (errorCode != lastErrorCode)	{
			std::cerr << e.what() << std::endl;
		}
		return Point3Ref();
	}
    lastErrorCode = errorCode;

	return Point3Ref(record.x, record.y, record.z, record.s);
}

Point3 TrakStarController::GetMockRecord()
{
	double radius = 100;
	int randomMax = 10;
	double randomMaxRadius = 200;

	//return Point3();

	if (mPrevMockRecord.x == 0 && mPrevMockRecord.y == 0 && mPrevMockRecord.z == 0)	{
		// First mock value.
		mPrevMockRecord = Point3(radius, 0, 0);
		return mPrevMockRecord;
	}
	else {
		// Add some random noise to the previous mockRecord.
		mPrevMockRecord = Point3(mPrevMockRecord.x + ((rand() % randomMax) - randomMax / 2), mPrevMockRecord.y + ((rand() % randomMax) - randomMax / 2), mPrevMockRecord.z + ((rand() % randomMax) - randomMax / 2));
		return mPrevMockRecord;
	}
}

Point3 TrakStarController::GetMockRecordFromFile(int sensorId)
{
	// Open the file if not already open.
	if (!s0MockDataFile.is_open()) {
		s0MockDataFile.open(s0MockDataFilePath, std::ifstream::in);
	}
	if (!s1MockDataFile.is_open()) {
		s1MockDataFile.open(s1MockDataFilePath, std::ifstream::in);
	}
	if (!s2MockDataFile.is_open()) {
		s2MockDataFile.open(s2MockDataFilePath, std::ifstream::in);
	}

	// Get the next line from the file.
	Point3 newPoint;
	char line[512];
	if (sensorId == 0 && !s0MockDataFile.getline(line, 512)) {
		// No more lines to read.
		// Close the file.
		s0MockDataFile.close();
		// Try again.
		return GetMockRecordFromFile(sensorId);
	}
	else if (sensorId == 1 && !s1MockDataFile.getline(line, 512)) {
		// No more lines to read.
		// Close the file.
		s1MockDataFile.close();
		// Try again.
		return GetMockRecordFromFile(sensorId);
	}
	else if (sensorId == 2 && !s2MockDataFile.getline(line, 512)) {
		//no more lines to read
		//close the file
		s2MockDataFile.close();
		//try again:
		return GetMockRecordFromFile(sensorId);
	}
	//parse the coorindates from the line:
	for (int c = 5; c >= 0; c--) {
		//find the last comma
		char* found = line;
		if (c) {
			found = strrchr(line, ',');
			++found;
		}

		//parse the number after that comma:
		float f = std::atof(found);

		//delete the number from the line
		if (c) {
			*(--found) = '\0';
		}

		//add it to the point:
		switch (c) {
			case 0:
				newPoint.x = f;
				break;
			case 1:
				newPoint.y = f;
				break;
			case 2:
				newPoint.z = f;
				break;
			case 3:
				newPoint.r.x = f;
				break;
			case 4:
				newPoint.r.y = f;
				break;
			case 5:
				newPoint.r.z = f;
				break;
		}
	}
	newPoint.button = 1;
	return newPoint;
}

void TrakStarController::DeviceStatusHandler(int deviceStatus)
{
	switch (deviceStatus & ~GLOBAL_ERROR) {
		case NOT_ATTACHED:
			throw ex_trakStar("[WARNING] No physical device attached to this device channel", __func__, __FILE__);
			break;
		case SATURATED:
			throw ex_trakStar("[WARNING] Sensor currently staturated", __func__, __FILE__);
			break;
		case BAD_EEPROM:
			throw ex_trakStar("[WARNING] PCB or attached device has a corrupt or unresponsive EEprom", __func__, __FILE__);
			break;
		case HARDWARE:
			throw ex_trakStar("[WARNING] Unspecified hardware fault condition is preventing normal operation of this device channel, board or the system", __func__, __FILE__);
			break;
		case NON_EXISTENT:
			throw ex_trakStar("[WARNING] The device ID used to obtain this status word is invalid. This device channel or board does not exist in the system", __func__, __FILE__);
			break;
		case UNINITIALIZED:
			throw ex_trakStar("[WARNING] The system has not been initialized yet. The system must be initialized at least once before any other commands can be used", __func__, __FILE__);
			break;
		case NO_TRANSMITTER_RUNNING:
			throw ex_trakStar("[WARNING] An attempt was made to call Smart Scan when no transmitter was running", __func__, __FILE__);
			break;
		case BAD_12V:
			throw ex_trakStar("[WARNING] N/A for the 3DG systems", __func__, __FILE__);
			break;
		case CPU_TIMEOUT:
			throw ex_trakStar("[WARNING] N/A for the 3DG systems", __func__, __FILE__);
			break;
		case INVALID_DEVICE:
			throw ex_trakStar("[WARNING] N/A for the 3DG systems", __func__, __FILE__);
			break;
		case NO_TRANSMITTER_ATTACHED:
			throw ex_trakStar("[WARNING] A transmitter is not attached to the tracking system", __func__, __FILE__);
			break;
		case OUT_OF_MOTIONBOX:
			throw ex_trakStar("[WARNING] The sensor has exceeded the maximum range an the position has been clamped to the maximum range", __func__, __FILE__);
			break;
		case ALGORITHM_INITIALIZING:
			throw ex_trakStar("[WARNING] The sensor has not acquired enough raw magnetic data to compute an accurate P&0 solution", __func__, __FILE__);
			break;
		default:
			break;
	}
}

void TrakStarController::ErrorHandler(int error)
{
	char			buffer[1024];
	char* pBuffer = &buffer[0];
	int				numberBytes;

	while (error != BIRD_ERROR_SUCCESS)
	{
		error = GetErrorText(error, pBuffer, sizeof(buffer), SIMPLE_MESSAGE);
		numberBytes = strlen(buffer);

		//printf("%s", buffer);
		throw ex_trakStar(buffer, __func__, __FILE__);
	}
}

const std::string TrakStarController::GetErrorString(int error)
{
	char			buffer[1024];
	std::string errorString;

	if (error != BIRD_ERROR_SUCCESS)
	{
		error = GetErrorText(error, buffer, sizeof(buffer), SIMPLE_MESSAGE);
		errorString = buffer;
		return errorString;
	}
}