/***************************************************************************//**
********************************************************************************
**
** @file AcquisitionThread.cpp
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
** @details Implementation file for the AcquisitionThread callable
**
********************************************************************************
*******************************************************************************/

#include"AcquisitionThread.h"
// includes for C system headers
// includes for C++ system headers
// includes from other libraries
// includes from ORCHID

namespace Threads
{

enum {MaxInteruptChecks = 50};

void AcquisitionThread::operator ()()
{
    //run the event loop
    while(this->notTerminated)
    {
        switch(this->controller->getCurrentState())
        {
        case InterThread::AcquisitionThreadState::Terminate:
            BOOST_LOG_SEV(lg, Information) << "ACQ Thread: Terminating";
            this->notTerminated = false;
            break;
        case InterThread::AcquisitionThreadState::Stopped:
            BOOST_LOG_SEV(lg, Information) << "ACQ Thread: Stopped and waiting for instructions";
            this->controller->waitForChange();
            break;
        case InterThread::AcquisitionThreadState::Acquiring:
            BOOST_LOG_SEV(lg, Information) << "ACQ Thread: Starting Acquisition";
            this->controller->acknowledgeStart();
            this->doAcquisitionLoop();
            break;
        }
    }
    //if we are here then we are exiting
    this->controller->acknowledgeTerminate();
}


void AcquisitionThread::doAcquisitionLoop()
{
    this->startAcquisition();
    Utility::ToProcessingBuffer* currentBuffer;
    while(this->controller->getCurrentState() == InterThread::AcquisitionThreadState::Acquiring)
    {
        if(!(this->dataOutputQueue->producerPop(currentBuffer)))
        {
            //we were unable to obtain a buffer this probably happened because were told to keep awake while waiting
            //make certain this is the case by jumping to the next loop
            continue;
        }
        //we have the buffer prepped and ready for the first read, so go about things
        currentBuffer->info.startChannel = this->firstChannel;
        currentBuffer->info.boardNumber = this->modNumber;
        int loopCount = 0;
        currentBuffer->sizeOfData = this->digitizer->getData(currentBuffer->dataBuffer);
        while((loopCount < MaxInteruptChecks) && (currentBuffer->sizeOfData == 0))
        {
            ++loopCount;
            currentBuffer->sizeOfData = this->digitizer->getData(currentBuffer->dataBuffer);
        }
        
        if(currentBuffer->sizeOfData != 0)
        {
            //if we have a filled buffer, there must be space for it to be pushed
            this->dataOutputQueue->producerPush(currentBuffer);
            currentBuffer = nullptr;
        }
        else
        {
            //the buffer is still empty, maybe something is wrong
            this->dataOutputQueue->consumerPush(currentBuffer);
            BOOST_LOG_SEV(lg, Information) << "ACQ Thread: Did not get data after 50 loops";
        }
    }
    this->endAcquisition();
}

void AcquisitionThread::startAcquisition()
{
    //start acquisition
    this->digitizer->startAcquisition();
}

void AcquisitionThread::endAcquisition()
{
    this->digitizer->stopAcquisition();
    //now get the final data
    this->doFinalRead();
}

void AcquisitionThread::doFinalRead()
{
    Utility::ToProcessingBuffer* currentBuffer;
    this->dataOutputQueue->producerPop(currentBuffer);
    currentBuffer->info.startChannel = this->firstChannel;
    currentBuffer->info.boardNumber = this->modNumber;
    BOOST_LOG_SEV(lg, Information) << "ACQ Thread: Performing final flush";
    currentBuffer->sizeOfData = this->digitizer->performFinalReadout(currentBuffer->dataBuffer);
    if(currentBuffer->sizeOfData != 0)
    {
        //if we have a buffer, there must be space for it to be pushed
        BOOST_LOG_SEV(lg, Information) << "ACQ Thread: Final flush gave data, pushing onto queue to processing";
        this->dataOutputQueue->producerPush(currentBuffer);
        currentBuffer = nullptr;
    }
    else
    {
         //since the buffer is empty, dump it back on to the end of the consumer queue
        BOOST_LOG_SEV(lg, Information) << "ACQ Thread: Final flush gave no data";
        this->dataOutputQueue->consumerPush(currentBuffer);
        currentBuffer = nullptr;
    }
}

}
