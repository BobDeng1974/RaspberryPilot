/******************************************************************************
The altHold.c in RaspberryPilot project is placed under the MIT license

Copyright (c) 2016 jellyice1986 (Tung-Cheng Wu)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#include "commonLib.h"
#include "flyControler.h"
#include "pid.h"
#include "motorControl.h"
#include "attitudeUpdate.h"
#if defined(ALTHOLD_MODULE_MS5611)
#include "ms5611.h"
#elif defined(ALTHOLD_MODULE_SRF02)
#include "srf02.h"
#elif defined(ALTHOLD_MODULE_VL53L0X)
#include "vl53l0x.h"
#endif
#include "altHold.h"

#define ALTHOLD_UPDATE_PERIOD 100000

static float aslRaw = 0.f;
static float targetAlt = 0;
static float altholdSpeed = 0;
static bool altHoldIsReady = false;
static bool enableAltHold = false;
static bool altholdIsUpdate = false;
static unsigned int maxAlt = 50; 		//cm
static pthread_t altHoldThreadId;
static pthread_mutex_t altHoldIsUpdateMutex;

static void setAltHoldIsReady(bool v);
static void setMaxAlt(unsigned int v);
static unsigned int getMaxAlt();
static void *altHoldUpdate(void *arg);

/**
 * init althold
 *
 * @param
 * 		void
 *
 * @return
 *		void
 *
 */
bool initAltHold() {

	setAltHoldIsReady(false);

#if defined(ALTHOLD_MODULE_MS5611)

	if(!ms5611Init()){
		_DEBUG(DEBUG_NORMAL, "MS5611 Init failed\n");
		return	false;
	}
	
	setMaxAlt(99999999); 	//cm

#elif defined(ALTHOLD_MODULE_SRF02)

	if(!srf02Init()){
		_DEBUG(DEBUG_NORMAL, "SRF02 Init failed\n");
		return	false;
	}

	setMaxAlt(200); 	//cm

#elif defined(ALTHOLD_MODULE_VL53L0X)
	if (!vl53l0xInit()) {
		_DEBUG(DEBUG_NORMAL, "vl53l0x Init failed\n");
		return false;
	}

	setMaxAlt(140);  //cm	
	
#else
	return false;
#endif

	if (pthread_mutex_init(&altHoldIsUpdateMutex, NULL) != 0) {
		_ERROR("(%s-%d) altHoldIsUpdateMutex init failed\n", __func__,
				__LINE__);
		return false;
	}

	if (pthread_create(&altHoldThreadId, NULL, altHoldUpdate, 0)) {
		_DEBUG(DEBUG_NORMAL, "altHold thread create failed\n");
		return false;
	} else {
		_DEBUG(DEBUG_NORMAL, "start altHold thread...\n");
	}
	
	setAltHoldIsReady(true);

	return true;
}

/**
 * get the flag to indicate whether enable althold or not
 *
 * @param
 * 		void
 *
 * @return
 *		altHold is ready or not
 *
 */
bool getEnableAltHold() {
	return enableAltHold;
}

/**
 * set the flag to indicate whether enable althold or not
 *
 * @param v
 * 		altHold is ready or not
 *
 * @return
 *		void
 *
 */
void setEnableAltHold(bool v) {
	enableAltHold = v;
}

/**
 * get the flag to indicate whether althold is ready or not
 *
 * @param
 * 		void
 *
 * @return
 *		altHold is ready or not
 *
 */
bool getAltHoldIsReady() {
	return altHoldIsReady;
}

/**
 * set the flag to indicate whether althold is ready or not
 *
 * @param v
 * 		altHold is ready or not
 *
 * @return
 *		void
 *
 */
void setAltHoldIsReady(bool v) {
	altHoldIsReady = v;
}

/**
 * set the maximum value of altitude
 *
 * @param v
 * 		maximum value of altitude
 *
 * @return
 *		void
 *
 */
void setMaxAlt(unsigned int v) {
	maxAlt = v;
}

/**
 * get the maximum value of altitude
 *
 * @param
 * 		void
 *
 * @return
 *		maximum value of altitude
 *
 */
unsigned int getMaxAlt() {
	return maxAlt;
}

/**
 * get current altitude
 *
 * @param
 * 		void
 *
 * @return
 *		altitude (cm)
 *
 */
float getCurrentAltHoldAltitude() {
	return aslRaw;
}

/**
 *  check whether update AltHold info or not
 *
 * @param
 * 		void
 *
 * @return
 *		true or false
 *
 */
bool updateAltHold() {

	bool ret = false;

	pthread_mutex_lock(&altHoldIsUpdateMutex);
	if (altholdIsUpdate) {
		altholdIsUpdate = false;
		ret = true;
	} else {
		ret = false;
	}
	pthread_mutex_unlock(&altHoldIsUpdateMutex);

	return ret;
}

/**
 * get target altitude
 *
 * @param
 * 		void
 *
 * @return
 *		target altitude
 *
 */
float getTargetAlt(){
	return targetAlt;
}

/**
 * get altitude speed
 *
 * @param
 * 		void
 *
 * @return
 *		altitude speed
 *
 */
float getAltholdSpeed(){
	return altholdSpeed;
}


/**
 * update target altitude
 *
 * @param
 * 		current throttle
 *
 * @return
 *		void
 *
 */
void updateTargetAltitude(float throttle){

	static float lastThrottle=0.f;
	static struct timeval last_tv;
	struct timeval tv;

	gettimeofday(&tv,NULL);
	
	if((throttle != 0.f) && ((throttle <= lastThrottle + 0.03f) && (throttle >= lastThrottle - 0.03f))){
		if(GET_SEC_TIMEDIFF(tv,last_tv)>=3.f){
			
			//_DEBUG(DEBUG_NORMAL, "Target Altitude is %f lastThrottle=%f\n",targetAlt,lastThrottle);
			return;
		}

	}else{
		lastThrottle = throttle;
		UPDATE_LAST_TIME(tv,last_tv);
	}
	
	targetAlt = getCurrentAltHoldAltitude();

}

/**
 *  AltHold thread, updates altitude and vertical speed
 *
 * @param arg
 * 		arg
 *
 * @return
 *		pointer
 *
 */
void *altHoldUpdate(void *arg) {

	unsigned short data = 0;
	unsigned long interval=0;
	bool result = false;
	struct timeval tv;
	struct timeval tv2;

	while (!getLeaveFlyControlerFlag()&&getAltHoldIsReady()) {

		gettimeofday(&tv,NULL);
	
		if(TIME_IS_UPDATED(tv2)){

#if defined(ALTHOLD_MODULE_MS5611)
			result = ms5611GetMeasurementData(&data);
#elif defined(ALTHOLD_MODULE_SRF02)
			result = srf02GetMeasurementData(&data);
#elif defined(ALTHOLD_MODULE_VL53L0X)
			result = vl53l0xGetMeasurementData(&data);
#else
			result = false;
#endif

			if (result && data <= getMaxAlt()) {
					
				interval = GET_USEC_TIMEDIFF(tv,tv2);			
							
				//_DEBUG(DEBUG_NORMAL,"duration=%ld us\n",interval);	
						
				altholdSpeed = getVerticalAcceleration();
				aslRaw=(float)data;
				
				if(interval>=ALTHOLD_UPDATE_PERIOD){
					
					pthread_mutex_lock(&altHoldIsUpdateMutex);
					altholdIsUpdate = true;
					pthread_mutex_unlock(&altHoldIsUpdateMutex);
					UPDATE_LAST_TIME(tv,tv2);
					
				}

				_DEBUG_HOVER(DEBUG_HOVER_RAW_ALTITUDE, "(%s-%d) aslRaw=%.3f\n",
						__func__, __LINE__, aslRaw);
				_DEBUG_HOVER(DEBUG_HOVER_SPEED, "(%s-%d) altholdSpeed=%.3f\n",
						__func__, __LINE__, altholdSpeed);
			
			} else {
					usleep(5000);
			}
			
		}else{
			UPDATE_LAST_TIME(tv,tv2);
		}
	}

	pthread_exit((void *) 0);

}


