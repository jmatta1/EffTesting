/***************************************************************************//**
********************************************************************************
**
** @file Digitizer.h
** @author James Till Matta
** @date 27 June, 2016
** @brief
**
** @copyright Copyright (C) 2016 James Till Matta
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
**
** @details Definition file for the Digitizer class
**
********************************************************************************
*******************************************************************************/
#ifndef ORCHID_SRC_HARDWARE_DIGITIZER_DIGITIZER_H
#define ORCHID_SRC_HARDWARE_DIGITIZER_DIGITIZER_H
// includes for C system headers
// includes for C++ system headers
// includes from other libraries
#include<CAENComm.h>
// includes from ORCHID
#include"InputLib/Blocks/DigitizerModuleData.h"
#include"InputLib/Blocks/DigitizerChannelData.h"
#include"Utility/OrchidLogger.h"

namespace Digitizer
{

enum class InternalPulserRate {Off, OneKHz, TenKHz, HundKHz, OneMHz};
struct PulserSetting
{
public:
    void setOverallRate(unsigned oneKHzCt, unsigned tenKHzCt, unsigned hundKHzCt, unsigned oneMHzCt)
    {
        InternalPulserRate rateList[5] = {InternalPulserRate::Off, InternalPulserRate::OneMHz, 
                                          InternalPulserRate::HundKHz, InternalPulserRate::TenKHz,
                                          InternalPulserRate::OneKHz};
        unsigned counts[5];
        counts[4] = oneKHzCt;
        counts[3] = tenKHzCt;
        counts[2] = hundKHzCt;
        counts[1] = oneMHzCt;
        counts[0] = (16 - (oneKHzCt + tenKHzCt + hundKHzCt + oneMHzCt));
        
        unsigned baseIndex = 0;
        for(int i=0; i<5; ++i)
        {
            unsigned limit = baseIndex + counts[i];
            for(int j=baseIndex; j<limit; ++j)
            {
                setChannel(j,rateList[i]);
            }
            baseIndex = limit;
        }
    }

    std::array<unsigned int, 16> rates;
    std::array<bool, 16> active;
private:
    void setChannel(unsigned chan, InternalPulserRate rate)
    {
        rates[chan] = lookupRateValue(rate);
        active[chan] = lookupChannelStatus(rate);
    }

    unsigned int lookupRateValue(InternalPulserRate rate)
    {
        switch(rate)
        {
        case InternalPulserRate::Off:
            return 0;
        case InternalPulserRate::OneKHz:
            return 0;
        case InternalPulserRate::TenKHz:
            return 1;
        case InternalPulserRate::HundKHz:
            return 2;
        case InternalPulserRate::OneMHz:
            return 3;
        }
    }
    bool lookupChannelStatus(InternalPulserRate rate)
    {
        if(rate == InternalPulserRate::Off)
        {
            return false;
        }
        else
        {
            return true;
        }
    }
};

//Todo: add some mechanism to reduce the number of individual channel reads
//  while acquiring data, maybe use the number of aggregates to trigger an IRQ
//  as a bare minimum to wait for?
class Vx1730Digitizer
{
public:
    Vx1730Digitizer(int modNum, InputParser::DigitizerModuleData* modData,
                    InputParser::DigitizerChannelData* chanData);
    ~Vx1730Digitizer();
    
    //this takes a little while as the system has to not only set all the
    //parameters, it then needs to wait a little while, run the calibration,
    //and then wait a little more time
    void setupDigitizer();
    
    //puts the digitizer in running mode
    void startAcquisition();
    //takes the digitizer out of running mode
    void stopAcquisition();
    
    void setupPulsing(PulserSetting& pulseSetting);
    
    //checks if there is an event ready, if yes it grabs the event, if false
    //it waits on an interrupt from the digitizer
    unsigned int getData(unsigned int* buffer);
    
    //is used after a forcible data flush at the end of acquisition
    unsigned int performFinalReadout(unsigned int* buffer);
    
    //give the max possible size of a buffer in bytes so that they can be pre-
    //allocated for the queueing system
    int getSizeOfReadBufferIn32BitInts(){return maxSizeOfBoardAggregateBlock;}
    
    //return the channel 0 index
    int getModuleStartChannel(){return channelStartInd;}
    //return the channel 0 index
    int getModuleNumber(){return moduleNumber;}
private:
    void writeErrorAndThrow(CAENComm_ErrorCode errVal);

    //waits for the digitizer to raise and interrupt about data full.
    //once an interrupt is raised, it reads the event size value to find out
    //how much it needs to read, then it iteratively reads data that data into
    //the given buffer
    unsigned int waitForInterruptToReadData(unsigned int* buffer);
    unsigned int readImpromptuDataAvailable(unsigned int* buffer);
    unsigned int readInterruptDataAvailable(unsigned int* buffer);
    
    void writeCommonRegisterData();
    void writeGroupRegisterData();
    void writeIndividualRegisterData();
    void calculateMaximumSizes();
    
    //functions to calculate register values for common registers
    unsigned int calculateBoardConfigRegVal();
    void calculateAcqCtrlRegBase();
    unsigned int calculateGlblTrigMaskRegVal();
    unsigned int calculateTrigOutEnableMaskRegVal();
    unsigned int calculateChanEnMaskRegVal();
    unsigned int calculateLocalTrgManagementRegVal(int ind);
    unsigned int calculateTriggerValidationMask(int ind);
    
    //functions to calculate register values for individual registers
    unsigned int calculateCfdRegSettings(int i);
    unsigned int calculateDppAlgCtrlRegVal(int i);
    
    int moduleNumber;
    int channelStartInd;
    int numChannel;
    int digitizerHandle;
    int eventsPerInterrupt;
    bool acqRunning;
    bool digitizerOpen;
    InputParser::DigitizerModuleData* moduleData;
    InputParser::DigitizerChannelData* channelData;
    //arrays to handle multireads and multi writes
    unsigned int* addrArray;
    unsigned int* dataArray;
    unsigned int* rdbkArray;
    CAENComm_ErrorCode* cycleErrsArray;
    int arraySize;
    
    unsigned long long interuptWaitAttempts = 0;
    unsigned long long interuptTimeouts = 0;
    
    //variables to hold sizes of parts of the readout (in 32 bit ints
    int sizePerEvent[8];
    int sizePerChanPairAggregate[8];
    int maxSizeOfBoardAggregate;
    int maxSizeOfBoardAggregateBlock;
    int maxBufferFillForAnotherRead;
    //variables to hold persistent values for use later
    unsigned int acquisitionCtrlRegBase;
    
    boost::log::sources::severity_logger_mt<LogSeverity>& lg;
    
};

}

#endif //ORCHID_SRC_HARDWARE_DIGITIZER_DIGITIZER_H
