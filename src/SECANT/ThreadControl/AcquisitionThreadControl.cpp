/***************************************************************************//**
********************************************************************************
**
** @file DigitizerThreadControl.h
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
** @details Implementation file for the AcquisitionThreadControl class
**
********************************************************************************
*******************************************************************************/
#include"AcquisitionThreadControl.h"
// includes for C system headers
// includes for C++ system headers
// includes from other libraries
// includes from ORCHID

namespace Secant
{

namespace InterThreadCtrl
{

void AcquisitionThreadControl::waitForChange()
{
    boost::unique_lock<boost::mutex> waitLock(waitMutex);
    acknowledgeStop();
    while(this->acqState.load() == AcquisitionThreadState::Stopped)
    {
        acqThreadWaitCondition.wait(waitLock);
    }
    //the lock will release on deconstruction
}

}
}
