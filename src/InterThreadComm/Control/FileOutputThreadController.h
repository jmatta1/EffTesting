/***************************************************************************//**
********************************************************************************
**
** @file FileOutputThreadController.h
** @author James Till Matta
** @date 08 June, 2016
** @brief
**
** @copyright Copyright (C) 2016 James Till Matta
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
**
** @details Holds the definition of the file output thread controller class
**
********************************************************************************
*******************************************************************************/
#ifndef ORCHID_SRC_INTERTHREADCOMM_CONTROL_FILEOUTPUTTHREADCONTROLLER_H
#define ORCHID_SRC_INTERTHREADCOMM_CONTROL_FILEOUTPUTTHREADCONTROLLER_H

// includes for C system headers
// includes for C++ system headers
#include<atomic>
#include<string>
// includes from other libraries
#include<boost/thread.hpp>
// includes from ORCHID
#include"Utility/OrchidLogger.h"

namespace InterThread
{

enum class FileOutputThreadState : char {Terminate, NewRunParams, Waiting, Writing};

class FileOutputThreadController
{
public:
    FileOutputThreadController():
        currentState(FileOutputThreadState::Waiting),
        priorState(FileOutputThreadState::Waiting),
        fileThreadWaiting(false), threadDone(false){}
    ~FileOutputThreadController(){}
    
    //functions for the file thread to access
    FileOutputThreadState getCurrentState(){return this->currentState.load();}
    void waitForNewState();
    void setThreadDone(){this->threadDone.store(true);}
    void getNewRunParams(std::string& rTitle, int& rNum)
    {
        rTitle=this->runTitle;
        rNum=this->runNumber;
        this->currentState.store(this->priorState.load());
    }
    
    //files for the UI thread
    void setToTerminate()
    {
        {
            boost::unique_lock<boost::mutex> waitLock(this->waitMutex);
            this->currentState.store(FileOutputThreadState::Terminate);
        }
        if(this->fileThreadWaiting.load()) this->waitCondition.notify_all();
    }
    bool setNewRunParameters(const std::string& rTitle, int runNum)
    {
        //check to make certain we have not already set a title change that has
        //not yet been acknowledged, if that is the case then we cannot change
        //parameters yet
        if(FileOutputThreadState::NewRunParams == this->currentState.load()) return false;
        {
            boost::unique_lock<boost::mutex> waitLock(this->waitMutex);
            //change the parameter *then* change the state
            //this way the file output thread does not check the parameters
            //while we are modifying them
            this->runTitle = rTitle;
            this->runNumber = runNum;
            this->priorState.store(this->currentState.load());
            this->currentState.store(FileOutputThreadState::NewRunParams);
            if(this->fileThreadWaiting.load()) this->waitCondition.notify_all();
        }
        return true;
    }
    void setToWaiting()
    {
        BOOST_LOG_SEV(OrchidLog::get(), Information) << "FO CTRL: Set to waiting";
        {
            boost::unique_lock<boost::mutex> waitLock(this->waitMutex);
            this->currentState.store(FileOutputThreadState::Waiting);
        }
        if(this->fileThreadWaiting.load()) this->waitCondition.notify_all();
    }
    void setToWriting()
    {
        BOOST_LOG_SEV(OrchidLog::get(), Information) << "FO CTRL: Set to writing";
        {
            boost::unique_lock<boost::mutex> waitLock(this->waitMutex);
            this->currentState.store(FileOutputThreadState::Writing);
        }
        if(this->fileThreadWaiting.load()) this->waitCondition.notify_all();
    }
    void uiGetRunParams(std::string& rTitle, int& rNum)
    {
        rTitle=this->runTitle;
        rNum=this->runNumber;
    }

    bool isDone(){return this->threadDone.load();}
    bool isWaiting(){return this->fileThreadWaiting.load();}
private:
    //state variable
    std::atomic<FileOutputThreadState> currentState;
    //holds the state of the system before we set NewRunParams
    std::atomic<FileOutputThreadState> priorState;
    
    //variables for waiting to wake up
    boost::mutex waitMutex;
    boost::condition_variable waitCondition;
    std::atomic_bool fileThreadWaiting;
    
    //termination checking
    std::atomic_bool threadDone;
    
    //run parameters, namely run title and run number while run number coule be
    //an atomic variable, run title must be a simple string. To protect things
    //properly we do the following: Functions that change the run number/run
    //title, do not do anything to notify the file thread *until* the changes
    //are made. In this way the UI thread is done modifying the variables before
    //the File output thread knows to check for new parameter
    std::string runTitle;
    int runNumber;
};

}

#endif //ORCHID_SRC_INTERTHREADCOMM_CONTROL_FILEOUTPUTTHREADCONTROLLER_H
