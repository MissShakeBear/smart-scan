#include <iomanip>
#include <ctime>
#include <cstdio>

#include "DataAcquisition.h"
#include "Exceptions.h"

using namespace SmartScan;

DataAcqConfig::DataAcqConfig() 
{

}

DataAcqConfig::DataAcqConfig(short int transmitterID, double measurementRate, double powerLineFrequency, double maximumRange, int refSensorSerial, double frameRotations[3])
	: transmitterID { transmitterID }, measurementRate { measurementRate }, powerLineFrequency { powerLineFrequency }, maximumRange { maximumRange }, refSensorSerial { refSensorSerial }
{
	for (int i = 0; i < 3; i ++) {
		this->frameRotations[i] = frameRotations[i]; // Copy the array.
	}
}

DataAcq::DataAcq(bool useMockData) : mUseMockData { useMockData }, mTSCtrl(useMockData)
{

}

DataAcq::~DataAcq()
{
    // Delete all raw data when this object is removed.
	this->Stop(true);
}

void DataAcq::Init()
{
	// Skip initalization of the TrakStar device when in Mock mode.
	if (!mUseMockData) {
		mTSCtrl.Init();
		mTSCtrl.SelectTransmitter(mConfig.transmitterID);
		mTSCtrl.SetPowerlineFrequency(mConfig.powerLineFrequency);
		mTSCtrl.SetMeasurementRate(mConfig.measurementRate);
		mTSCtrl.SetMaxRange(mConfig.maximumRange);
		mTSCtrl.SetMetric();
		mTSCtrl.SetReferenceFrame(mConfig.transmitterID, mConfig.frameRotations);
		mTSCtrl.SetSensorFormat();
	}

	// Get sensor info from TrakStar object.
	mPortNumBuff = mTSCtrl.GetAttachedPorts();
	mSerialBuff = mTSCtrl.GetAttachedSerials();

    // Remove reference sensor from sensor vector, because it is special.
	if (mConfig.refSensorSerial >= 0) {
		bool foundSensor = false;
		for(int i = 0; i < mSerialBuff.size(); i++) {
			if (mSerialBuff[i] == mConfig.refSensorSerial) {
				refSensorPort = mPortNumBuff[i];
				mSerialBuff.erase(mSerialBuff.cbegin()+i);
				mPortNumBuff.erase(mPortNumBuff.cbegin()+i);
				foundSensor = true;
			}
		}

		// Throw an error if the user specified a reference sensor serial number that is not attached.
		if (!foundSensor) {
			throw ex_acq("Could not find the reference sensor specified.", __func__, __FILE__);
		}
		// Set the reference sensor to use rotation matrices instead of Euler angles.
		if (!mUseMockData) {
			mTSCtrl.SetRefSensorFormat(refSensorPort);
		}
	}

    // Initialize raw data buffer.
	for (int i = 0; i < mPortNumBuff.size(); i++) {
		mRawBuff.push_back(std::vector<Point3>());
	}
}

void DataAcq::Init(DataAcqConfig acquisitionConfig)
{
	this->mConfig = acquisitionConfig;
	this->Init();
}

void DataAcq::SetZOffset(int serialNumber, double offset)
{
	mTSCtrl.SetSensorZOffset(findPortNum(serialNumber), offset);
}

void DataAcq::Start()
{
	// Check whether trak star controller has been initialised.
	if (!mRawBuff.size()) {
		throw ex_acq("Data acquisition is not initialized.", __func__, __FILE__);
	}

	// Check if the DataAcquisition thread is already running.
	if (this->mRunning)	{
		return;
	}

    // Create a new DataAcquisition thread.
	try	{
		this->pAcquisitionThread = std::make_unique<std::thread>(&DataAcq::DataAcquisition, this);
	}
	catch (...)	{
		throw ex_acq("Unnable to start data-acquisition thread.", __func__, __FILE__);
	}

	// Let it gooooo, let it gooo.
	this->pAcquisitionThread->detach();

	mRunning = true;
}

void DataAcq::Stop(bool clearData)
{
	// Wait a bit for the other threads to finish.
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (clearData) {
		for (int i = 0; i < mRawBuff.size(); i++) {
			mRawBuff.at(i).clear();
		}
	}

	mRunning = false;
}

const bool DataAcq::IsRunning() const
{
	return mRunning;
}

const std::vector<std::vector<Point3>>* DataAcq::getRawBuffer()
{
	return &mRawBuff;
}

const int DataAcq::NumAttachedBoards() const
{
	return mTSCtrl.NumAttachedBoards();
} 

const int DataAcq::NumAttachedTransmitters() const
{
	return mTSCtrl.NumAttachedTransmitters();
} 

const int DataAcq::NumAttachedSensors(bool includeRef) const
{
	// Add + 1 since the reference sensor is removed from the sensor list.
	if (includeRef && refSensorPort > -1) {
		return mPortNumBuff.size() + 1;
	}
	return mPortNumBuff.size();
} 

void DataAcq::RegisterRawDataCallback(std::function<void(const std::vector<Point3>&)> callback)
{
	mRawDataCallback = callback;
}

Point3 DataAcq::getSingleSample(int sensorSerial, bool raw)
{
	// Check whether trak star controller has been initialised.
	if (!mRawBuff.size()) {
		throw ex_acq("Data acquisition is not initialized.", __func__, __FILE__);
	}

	// Create empty reference point for later use.
	Point3Ref refMatrix;

	// Make Point3 obj to get the position info of the trackStar device.
	Point3 rawPoint = mTSCtrl.GetRecord(findPortNum(sensorSerial)); 

	// Check if raw data is requested.
	if (raw) {
		//this->angleCorrect(&rawPoint);
	}
	// Check if a reference sensor is defined.
	else if (refSensorPort > -1) {
		refMatrix = mTSCtrl.GetRefRecord(refSensorPort);
		referenceCorrect(&refMatrix, &rawPoint);
	}

	return rawPoint;
}

int DataAcq::findPortNum(int serialNumber)
{
	// Initialize to an irrealistic number.
	int portNum = -1;

    for (int i = 0; i < mSerialBuff.size(); i++) {
        if(mSerialBuff[i] == serialNumber) {
			portNum = mPortNumBuff[i];
            break;	// Break if serial number has been found.  
        }
    }

	// Throw an error if the serial number cannot be found.
	if (portNum == -1) {
		throw ex_acq("Could not find requested sensor serial number.", __func__, __FILE__);
	}

    return portNum;
}

int DataAcq::findBuffNum(int serialNumber)
{
	// Initialize to an irrealistic number.
	int bufNum = -1;

    for (int i = 0; i < mSerialBuff.size(); i++) {
        if(mSerialBuff[i] == serialNumber) {
			bufNum = i;
            break; // Break if serial number has been found.
        }
    }

	// Throw an error if the serial number cannot be found.
	if (bufNum == -1) {
		throw ex_acq("Could not find requested sensor serial number.", __func__, __FILE__);
	}

    return bufNum;
}

void DataAcq::DataAcquisition()
{
    // Store the current time.
	double time = 0;
	auto startSampling = std::chrono::steady_clock::now();
	std::chrono::time_point<std::chrono::steady_clock> endSampleTime = startSampling;

	// Create empty reference point for later use.
	Point3Ref refMatrix;

	while (mRunning) {
		// Store time and calculate the elapsed time since last sample.
		auto startSampleTime = std::chrono::steady_clock::now();
		std::chrono::duration<double> elapsedTime = startSampleTime - endSampleTime;

		if (elapsedTime.count() >= 1 / mConfig.measurementRate) {
            time += elapsedTime.count();

			// Check if a reference sensor is defined.
			if (refSensorPort > -1) {
				refMatrix = mTSCtrl.GetRefRecord(refSensorPort);
			}

            for (int i = 0; i < mPortNumBuff.size(); i++) {
                // Make Point3 obj to get the position info of the trackStar device
				Point3 raw = mTSCtrl.GetRecord(mPortNumBuff[i]); 

				// Check and store the buttonstate
				button_obj.UpdateButtonState(raw.button); 
				raw.buttonState = button_obj.GetButtonState();

				// Add total measurement time to point3.
				raw.time = time;

				// Correct point for reference sensor
				if (refSensorPort > -1) {
					referenceCorrect(&refMatrix, &raw);
				}

				mRawBuff[i].push_back(raw);
		    }			

			// Print the acquired data
			if (mRawDataCallback) {
				std::vector<Point3> sampleRow;
				for (int i = 0; i < mRawBuff.size(); i++) {
					sampleRow.push_back(mRawBuff.at(i).back());
				}
				mRawDataCallback(sampleRow);
			}

			// Store current time and calculate duration of the samples
			endSampleTime = std::chrono::steady_clock::now();
		}
    }
}

void DataAcq::angleCorrect(Point3* sensorPoint)
{
	// Use the azimuth to calculate the rotation around the z-axis.
	double x_new = sensorPoint->x * cos(sensorPoint->r.z * toRad) + sensorPoint->y * sin(sensorPoint->r.z * toRad);
	double y_new = sensorPoint->y * cos(sensorPoint->r.z * toRad) - sensorPoint->x * sin(sensorPoint->r.z * toRad);

	// Use the elevation to calculate the rotation around the y-axis.
	sensorPoint->x = x_new * cos(sensorPoint->r.y * toRad) - sensorPoint->z * sin(sensorPoint->r.y * toRad);
	double z_new = sensorPoint->z * cos(sensorPoint->r.y * toRad) + x_new * sin(sensorPoint->r.y * toRad);

	// Use the roll difference to calculate the rotation around the x-axis.
	sensorPoint->y = y_new * cos(sensorPoint->r.x * toRad) + z_new * sin(sensorPoint->r.x * toRad);
	sensorPoint->z = z_new * cos(sensorPoint->r.x * toRad) - y_new * sin(sensorPoint->r.x * toRad);
}

void DataAcq::referenceCorrect(Point3Ref* refPoint, Point3* sensorPoint)
{
	// Check the orientation of the current point.
	sensorPoint->x = sensorPoint->x - refPoint->x;
	sensorPoint->y = sensorPoint->y - refPoint->y;
	sensorPoint->z = sensorPoint->z - refPoint->z;

	// Multiply current point with reference sensor rotation matrix.
	double x_new = sensorPoint->x*refPoint->m[0][0]+sensorPoint->y*refPoint->m[1][0]+sensorPoint->z*refPoint->m[2][0];
	double y_new = sensorPoint->x*refPoint->m[0][1]+sensorPoint->y*refPoint->m[1][1]+sensorPoint->z*refPoint->m[2][1];
	double z_new = sensorPoint->x*refPoint->m[0][2]+sensorPoint->y*refPoint->m[1][2]+sensorPoint->z*refPoint->m[2][2];

	// Store result.
	sensorPoint->x = x_new;
	sensorPoint->y = y_new;
	sensorPoint->z = z_new;
}