/***************************************************************************//**
********************************************************************************
**
** @file AcquisitionThreadControl.h
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
** @details Definition file for the AcquisitionThreadControl class
**
********************************************************************************
*******************************************************************************/
#ifndef ORCHID_SRC_INTERTHREADCOMM_CONTROL_DIGITIZERTHREADCONTROL_H
#define ORCHID_SRC_INTERTHREADCOMM_CONTROL_DIGITIZERTHREADCONTROL_H
// includes for C system headers
// includes for C++ system headers
#include<atomic>
// includes from other libraries
#include<boost/thread.hpp>
// includes from ORCHID

namespace InterThread
{

enum class AcquisitionThreadState : char {Acquiring, Stopped, Terminate};

class AcquisitionThreadControl
{
public:
    AcquisitionThreadControl():
        acqState(AcquisitionThreadState::Stopped), termAckCount(0),
        stopAckCount(0), startAckCount(0) {}
    
    //functions to be accessed by the acquisition threads
    AcquisitionThreadState getCurrentState(){return acqState.load();}
    void waitForChange();
    void acknowledgeTerminate(){termAckCount.fetch_add(1);}
    void acknowledgeStart(){startAckCount.fetch_add(1);}

    //functions to be accessed by the UI thread
    void setToAcquiring()
    {
        //enter an artificial block to create and destroy lock before we notify
        {
            boost::unique_lock<boost::mutex> lock(this->waitMutex);
            if(acqState.load() != AcquisitionThreadState::Acquiring) startAckCount.store(0);
            acqState.store(AcquisitionThreadState::Acquiring);
        }
        acqThreadWaitCondition.notify_all();
    }
    void setToStopped()
    {
        //enter an artificial block to create and destroy lock before we notify
        {
            boost::unique_lock<boost::mutex> lock(this->waitMutex);
            if(acqState.load() != AcquisitionThreadState::Stopped) stopAckCount.store(0);
            acqState.store(AcquisitionThreadState::Stopped);
        }
        acqThreadWaitCondition.notify_all();
    }
    void setToTerminate()
    {
        //enter an artificial block to create and destroy lock before we notify
        {
            boost::unique_lock<boost::mutex> lock(this->waitMutex);
            if(acqState.load() != AcquisitionThreadState::Terminate) termAckCount.store(0);
            acqState.store(AcquisitionThreadState::Terminate);
        }
        acqThreadWaitCondition.notify_all();
    }
    
    int getThreadsStopped(){return stopAckCount.load();}
    int getThreadsStarted(){return startAckCount.load();}
    int getThreadsTerminated(){return termAckCount.load();}
    
private:
    //called by wait for change
    void acknowledgeStop(){stopAckCount.fetch_add(1);}
    
    std::atomic<AcquisitionThreadState> acqState;
    std::atomic_uint termAckCount;
    std::atomic_uint stopAckCount;
    std::atomic_uint startAckCount;
    
    boost::mutex waitMutex;
    boost::condition_variable acqThreadWaitCondition;
    
};

}

#endif //ORCHID_SRC_INTERTHREADCOMM_CONTROL_DIGITIZERTHREADCONTROL_H
