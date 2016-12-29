/***************************************************************************//**
********************************************************************************
**
** @file FileOutputThread.cpp
** @author James Till Matta
** @date 09 June, 2016
** @brief
**
** @copyright Copyright (C) 2016 James Till Matta
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
**
** @details Holds the implementation of the callable class that will be used to
** build the thread that handles taking events, buffering those events, and
** and writing those events
**
********************************************************************************
*******************************************************************************/

#include"FileOutputThread.h"
// includes for C system headers
// includes for C++ system headers
#include<sstream>
#include<iomanip>
#include<algorithm>
// includes from other libraries
#define BOOST_FILESYSTEM_NO_DEPRECATED
#include<boost/filesystem.hpp>
#include<boost/crc.hpp>
// includes from ORCHID
#include"Utility/OrchidConfig.h"
#include"Events/EventInterface.h"

namespace Threads
{

static const int bufferCountOutputDivisor = 111;

static const boost::posix_time::ptime epoch(boost::posix_time::time_from_string("1970-01-01 00:00:00.000"));

FileOutputThread::FileOutputThread(Utility::ToFileMultiQueue* queueInput,
                                   InterThread::FileData* fileDat,
                                   InterThread::FileOutputThreadController* fileThreadCtrl,
                                   InputParser::GeneralBlock* generalBlock):
    outFile(nullptr), evBufSize(2048), eventBuffer(nullptr), 
    outDirectory(generalBlock->baseOutputDirectory), currentFileName(""),
    runNumber(0), sequenceNumber(0), buffInd(0), bufferNumber(0), eventCount(0),
    inputQueues(queueInput), fileData(fileDat),
    fileThreadController(fileThreadCtrl), notTerminated(true), lg(OrchidLog::get())
{
    //make directories and prep file name
    this->prepNewRunFolder();
    //load the queue with buffers of the appropriate size
    for(int i=0; i<BufferCount ; ++i)
    {
        char* allocBuffer = new (std::nothrow) char[BufferSizeInBytes];
        if(allocBuffer == nullptr)
        {
            BOOST_LOG_SEV(lg, Critical) << "FO Thread: Error In File Write Buffer Allocation";
            throw std::runtime_error("Error In File Write Buffer Allocation");
        }
        //technically this thread should not push, only pop, but I think that it
        //is legal in this case since concurrent use of the queue has no hope of 
        //start until after this loop
        this->bufferQueue.push(allocBuffer);
    }
    //pop a buffer from the queue to serve as what we are filling now
    this->getNextBuffer();
    //make the asynchronous output file uninitialized
    this->outFile = new AsyncIO::AsyncOutFile<BufferQueue>(&(this->bufferQueue));
    //this->outFile = new AsyncIO::AsyncOutFile<BufferQueue>(this->currentFileName, &(this->bufferQueue));
    //write the file header
    //this->writeFileHeader();
    //allocate the event buffer to be 2kB
    this->eventBuffer = new char[evBufSize];
    //this->fileData->setFileNameAndRunTitle(this->currentFileName,this->currentRunTitle);
    //this->fileData->setRunNumber(this->runNumber);
    //this->fileData->setSequenceNumber(this->sequenceNumber);
    //this->fileData->setSize(0);
    this->fileData->setInitState('u');
}

FileOutputThread::~FileOutputThread()
{
    //delete the file stream
    delete this->outFile;
    //delete the buffer that we have popped from the bufferqueue to write to
    delete this->currentBuffer;
    char* allocBuffer;
    //pop each of the buffers in the queue and delete them
    while(this->bufferQueue.pop(allocBuffer))
    {
        delete[] allocBuffer;
    }
    //delete the temporary buffer
    delete[] this->eventBuffer;
}

void FileOutputThread::prepNewRunFolder()
{
    BOOST_LOG_SEV(lg, Information) << "FO Thread: Preparing new folder";
    if(this->outDirectory.back() == '/')
    {
        this->outDirectory.erase(this->outDirectory.size()-1, 1); 
    }
    boost::filesystem::path writePath(this->outDirectory);
    boost::system::error_code mkDirErr;
    boost::filesystem::create_directories(writePath, mkDirErr);
    if(!boost::filesystem::is_directory(writePath))
    {
        BOOST_LOG_SEV(lg, Critical) << "FO Thread: Could not create data directory in file thread";
        throw std::runtime_error("Could not create data directory in file thread");
    }
    writePath.append(this->currentRunTitle);
    boost::filesystem::create_directories(writePath, mkDirErr);
    if(!boost::filesystem::is_directory(writePath))
    {
        BOOST_LOG_SEV(lg, Critical) << "FO Thread: Could not create run directory in file thread";
        throw std::runtime_error("Could not create run directory in file thread");
    }
    if(!boost::filesystem::is_directory(writePath, mkDirErr))
    {
        BOOST_LOG_SEV(lg, Critical) << "FO Thread: No run directory detected despite creation";
        throw std::runtime_error("No run directory detected despite creation");
    }
    this->writeDirectory = writePath.native();
    BOOST_LOG_SEV(lg, Information) << "FO Thread: Made new folder at: " << this->writeDirectory;
    this->buildFileName();
}

void FileOutputThread::buildFileName()
{
    BOOST_LOG_SEV(lg, Information) << "FO Thread: Starting new file";
    bool buildFlag = true;
    while(buildFlag)
    {
        std::ostringstream builder;
        builder << this->writeDirectory;
        if(this->writeDirectory.back()!=boost::filesystem::path::preferred_separator)
        {
            builder << boost::filesystem::path::preferred_separator;
        }
        builder << this->currentRunTitle << "_";
        builder << std::setw(4) << std::setfill('0') << this->runNumber << ".dat.";
        builder << std::setw(4) << std::setfill('0') << this->sequenceNumber ;
        this->currentFileName = builder.str();
        //check for file existence
        if ( !boost::filesystem::exists( this->currentFileName ) )
        {
            buildFlag = false;
        }
        else
        {
            BOOST_LOG_SEV(lg, Information) << "FO Thread: " << this->currentFileName << ": File already exists, incrementing sequence number";
            this->sequenceNumber += 1;
            this->fileData->incrementSequenceNumber();
        }
    }
    BOOST_LOG_SEV(lg, Information) << "FO Thread: New file name is: " << this->currentFileName;
}

void FileOutputThread::operator()()
{
    //run the event loop
    while(this->notTerminated)
    {
        switch(this->fileThreadController->getCurrentState())
        {
        case InterThread::FileOutputThreadState::Terminate:
            BOOST_LOG_SEV(lg, Information) << "FO Thread: Preparing To Terminate";
            this->notTerminated = false;
            this->outFile->closeAndTerminate();
            BOOST_LOG_SEV(lg, Information) << "FO Thread: Terminating";
            break;
        case InterThread::FileOutputThreadState::Waiting:
            BOOST_LOG_SEV(lg, Information) << "FO Thread: Waiting";
            this->fileThreadController->waitForNewState();
            break;
        case InterThread::FileOutputThreadState::NewRunParams:
            BOOST_LOG_SEV(lg, Information) << "FO Thread: New Parameters";
            if(this->grabNewRunParameters())
            {
                this->fileData->setInitState('i');
            }
            break;
        case InterThread::FileOutputThreadState::Writing:
            BOOST_LOG_SEV(lg, Information) << "FO Thread: Writing";
            this->fileThreadController->acknowledgeStart();
            this->doWriteLoop();
            this->emptyWriteQueueBeforeChange();
            this->outFile->closeFile();
            this->fileData->setInitState('n');
            BOOST_LOG_SEV(lg, Information) << "FO Thread: leaving write state";
            break;
        }
    }
    //if we are here then we are exiting
    this->fileThreadController->setThreadDone();
}

bool FileOutputThread::grabNewRunParameters()
{
    std::string tempRunTitle;
    int tempRunNumber;
    this->fileThreadController->getNewRunParams(tempRunTitle, tempRunNumber);
    if(tempRunTitle == this->currentRunTitle && tempRunNumber == this->runNumber)
    {
        //everything is the same, do nothing, and do not change init state
        return false;
    }
    else if(tempRunTitle == this->currentRunTitle && tempRunNumber != this->runNumber)
    {
        //the run number has changed, we do not need to prep a new run folder,
        //we merely need to build a new file name and make a new file
        this->outFile->closeFile();
        this->runNumber = tempRunNumber;
        this->sequenceNumber = 0;
        this->buildFileName();
    }
    else
    {
        //either the run title or both the run title and number have changed
        //prep a new folder and build a new run title then set a new file
        this->outFile->closeFile();
        this->runNumber = tempRunNumber;
        this->currentRunTitle = tempRunTitle;
        this->sequenceNumber = 0;
        this->prepNewRunFolder();
    }
    //now make a new file
    this->outFile->newFile(this->currentFileName);
    this->writeFileHeader();
    this->fileData->setFileNameAndRunTitle(this->currentFileName,this->currentRunTitle);
    this->fileData->setRunNumber(this->runNumber);
    this->fileData->setSequenceNumber(this->sequenceNumber);
    this->fileData->setSize(0);
    this->bufferNumber = 0;
    return true;
}

void FileOutputThread::writeFileHeader()
{
    //directly manipulate the buffer to contain the header and send it to disk
    reinterpret_cast<unsigned long long*>(&(this->currentBuffer[this->buffInd]))[0] = 0x0102040810204080ULL;
    this->buffInd += 8;
    reinterpret_cast<unsigned long*>(&(this->currentBuffer[this->buffInd]))[0] = 0x00000001;
    this->buffInd += 4;
    reinterpret_cast<unsigned short*>(&(this->currentBuffer[this->buffInd]))[0] = ORCHID_MAJOR_VERSION;
    this->buffInd += 2;
    reinterpret_cast<unsigned short*>(&(this->currentBuffer[this->buffInd]))[0] = ORCHID_MINOR_VERSION;
    this->buffInd += 2;
    reinterpret_cast<unsigned short*>(&(this->currentBuffer[this->buffInd]))[0] = ORCHID_PATCH_VERSION;
    this->buffInd += 2;
    this->currFileTime = boost::posix_time::microsec_clock::universal_time();
    boost::posix_time::time_duration epochTime = (this->currFileTime - epoch);
    reinterpret_cast<long long*>(&(this->currentBuffer[this->buffInd]))[0] = epochTime.total_microseconds();
    this->buffInd += 8;
    std::string fileTimeString = boost::posix_time::to_iso_extended_string(boost::posix_time::microsec_clock::local_time());
    int fTimeLength = fileTimeString.size();
    std::copy(fileTimeString.c_str(), fileTimeString.c_str() + fTimeLength, &this->currentBuffer[this->buffInd]);
    this->buffInd += fTimeLength;
    std::fill_n(reinterpret_cast<char*>(&(this->currentBuffer[this->buffInd])), 30-fTimeLength, 0);
    this->buffInd += (30-fTimeLength);
    if(this->currentRunTitle.size() < 100)
    {
        std::copy(this->currentRunTitle.c_str(), this->currentRunTitle.c_str() + this->currentRunTitle.size(), &this->currentBuffer[this->buffInd]);
        this->buffInd += this->currentRunTitle.size();
        std::fill_n(&this->currentBuffer[this->buffInd], 100-this->currentRunTitle.size(), '\0');
        this->buffInd += (100-this->currentRunTitle.size());
    }
    else
    {
        std::copy(this->currentRunTitle.c_str(), this->currentRunTitle.c_str() + 99, &this->currentBuffer[this->buffInd]);
        this->buffInd += 99;
        this->currentBuffer[this->buffInd] = '\0';
        this->buffInd += 1;
    }
    reinterpret_cast<unsigned long*>(&(this->currentBuffer[this->buffInd]))[0] = this->runNumber;
    this->buffInd += 4;
    reinterpret_cast<unsigned long*>(&(this->currentBuffer[this->buffInd]))[0] = this->sequenceNumber;
    this->buffInd += 4;
    //reserve 3924 bytes in the buffer header for whatever
    std::fill_n(reinterpret_cast<unsigned int*>(&this->currentBuffer[this->buffInd]), 981, 0);
    this->buffInd += 3924;
    reinterpret_cast<unsigned long long*>(&(this->currentBuffer[this->buffInd]))[0] = 0xF0F0F0F0F0F0F0F0ULL;
    this->buffInd += 8;
    //send this 4kB header to the file
    this->writeBufferToDisk(this->buffInd);
    //now that we have written the file header we want to write a buffer header
    this->writeBufferHeader();
}

void FileOutputThread::doWriteLoop()
{
    bool gotData = false;
    while(this->fileThreadController->getCurrentState() == InterThread::FileOutputThreadState::Writing)
    {
        //check for data
        Events::EventInterface* event;
        if(this->inputQueues->tryConsumerPop<Utility::ProcessingQueueIndex>(event))
        {
            gotData = true;
            int eventSize = event->getSizeOfBinaryRepresentation();
            //loop until the intermediate buffer is big enough
            while(eventSize > this->evBufSize)
            {
                this->doubleEventBuffer();
            }
            //get the event now that our buffer is big enough
            event->getBinaryRepresentation(this->eventBuffer);
            //pass the empty event back to the queue
            this->inputQueues->consumerPush<Utility::ProcessingQueueIndex>(event);
            //now write the data into the file buffer
            this->transferData(eventSize);
            
        }
        if(this->inputQueues->tryConsumerPop<Utility::SlowControlsQueueIndex>(event))
        {
            gotData = true;
            int eventSize = event->getSizeOfBinaryRepresentation();
            //loop until we double the event buffer past the size of the event
            while(eventSize > this->evBufSize)
            {
                //we need to double the size of the event buffer
                this->doubleEventBuffer();
            }
            //now that we know that we have enough size
            event->getBinaryRepresentation(this->eventBuffer);
            //now that we are done with the event pass it back to the return queue
            this->inputQueues->consumerPush<Utility::SlowControlsQueueIndex>(event);
            //now that we have the data, write it to our large buffer
            this->transferData(eventSize);
        }
        if(!gotData)
        {
            this->inputQueues->consumerWaitForData();
        }
        gotData = false;
    }
}

void FileOutputThread::emptyWriteQueueBeforeChange()
{//this function is used to run until the consumer queue is empty, then stop
    //for a state change
    bool outOfData = false;
    while(!outOfData)
    {
        outOfData = true;
        //check for data
        Events::EventInterface* event;
        if(this->inputQueues->tryConsumerPop<Utility::ProcessingQueueIndex>(event))
        {
            outOfData = false;
            int eventSize = event->getSizeOfBinaryRepresentation();
            //loop until the intermediate buffer is big enough
            while(eventSize > this->evBufSize)
            {
                this->doubleEventBuffer();
            }
            //get the event now that our buffer is big enough
            event->getBinaryRepresentation(this->eventBuffer);
            //pass the empty event back to the queue
            this->inputQueues->consumerPush<Utility::ProcessingQueueIndex>(event);
            //now write the data into the file buffer
            this->transferData(eventSize);
            
        }
        if(this->inputQueues->tryConsumerPop<Utility::SlowControlsQueueIndex>(event))
        {
            outOfData = false;
            int eventSize = event->getSizeOfBinaryRepresentation();
            //loop until we double the event buffer past the size of the event
            while(eventSize > this->evBufSize)
            {
                //we need to double the size of the event buffer
                this->doubleEventBuffer();
            }
            //now that we know that we have enough size
            event->getBinaryRepresentation(this->eventBuffer);
            //now that we are done with the event pass it back to the return queue
            this->inputQueues->consumerPush<Utility::SlowControlsQueueIndex>(event);
            //now that we have the data, write it to our large buffer
            this->transferData(eventSize);
        }
    }
    //the queues are empty, so now dump the last buffer and be done with it
    this->finalizeDataBuffer();
}

void FileOutputThread::doubleEventBuffer()
{
    delete[] this->eventBuffer;
    this->evBufSize *= 2;
    this->eventBuffer = new char[this->evBufSize];
}

void FileOutputThread::transferData(int eventSize)
{
    //TODO implement compression when we write data to the buffer
    if(eventSize < (BufferSizeInBytes - this->buffInd))
    {//we have enough space, write the event to the buffer
        std::copy(this->eventBuffer, (this->eventBuffer + eventSize), &(this->currentBuffer[this->buffInd]));
        ++(this->eventCount);
        this->buffInd += eventSize;
    }
    else
    {//otherwise, write the buffer to disk and write this event to the next buffer
        this->finalizeDataBuffer();//this finalizes the event and writes the buffer to disk
        if((this->bufferNumber % bufferCountOutputDivisor) ==0)
        {
            BOOST_LOG_SEV(lg, Information) << "FO Thread: Output Buffer # " << this->bufferNumber;
        }
        //here we make certain that we are not at capacity
        if (this->bufferNumber >= MaxBuffersPerFile)
        {
            this->outFile->closeFile();
            //increment sequence number and set up the new file
            ++(this->sequenceNumber);
            this->fileData->incrementSequenceNumber();
            this->buildFileName();
            this->fileData->setFileName(this->currentFileName);
            this->fileData->setSize(0);
            this->outFile->newFile(this->currentFileName);
            //reset the buffer number to 0 now that we have a new file
            this->bufferNumber = 0;
            this->writeFileHeader();
        }
        //if we are here then we are either starting a fresh buffer in the same
        //file or we are starting a new file and have already written the file
        //header
        this->writeBufferHeader();
        //now write the data to the fresh new buffer
        std::copy(this->eventBuffer, (this->eventBuffer + eventSize), &(this->currentBuffer[this->buffInd]));
        ++(this->eventCount);
        this->buffInd += eventSize;
    }
}

void FileOutputThread::finalizeDataBuffer()
{
    //first we load the last of the buffer with zeros
    std::fill_n(&(this->currentBuffer[this->buffInd]), BufferSizeInBytes - this->buffInd, 0);
    //here we go back to the beginning of the buffer and load the number of events into the header
    reinterpret_cast<unsigned int*>(&(this->currentBuffer[8]))[0] = this->eventCount;
    //then we write the time this buffer is finalized
    this->currFileTime = boost::posix_time::microsec_clock::universal_time();
    boost::posix_time::time_duration epochTime = (this->currFileTime - epoch);
    reinterpret_cast<long long*>(&(this->currentBuffer[24]))[0] = epochTime.total_microseconds();
    //now skip past the first 32 bytes of the buffer and start writing the 4 byte 32 bit crcs
    unsigned int writeInd   = 32;
    unsigned int writeSize  = 4;
    unsigned int readInd    = BufferOverHead;
    unsigned int readSize   = 1024;
    unsigned int blockCount = (BufferSizeInBytes - BufferOverHead)/readSize;
    //make the crc object
    boost::crc_optimal<32,0x32583499, 0x00000000, 0x00000000, false, false> crcComputer;
    //now cycle through the buffer calculating crc 32 values
    for(int i=0; i<blockCount; ++i)
    {
        crcComputer.process_block(&(this->currentBuffer[readInd]),&(this->currentBuffer[readInd+readSize]));
        reinterpret_cast<unsigned int*>(&(this->currentBuffer[writeInd]))[0] = crcComputer.checksum();
        crcComputer.reset();
        writeInd += writeSize;
        readInd += readSize;
    }
    //now we have written all the CRC checksumsm now we can write the buffer to disk
    this->writeBufferToDisk(BufferSizeInBytes);
    this->eventCount = 0;
}

void FileOutputThread::writeBufferToDisk(int bufferSize)
{
    this->outFile->writeBuf(this->currentBuffer, bufferSize);
    this->fileData->increaseSize(bufferSize);
    //now that we have sent that buffer out
    //get another to use in the actual file handling
    ++bufferNumber;
    this->getNextBuffer();
}

void FileOutputThread::getNextBuffer()
{
    this->bufferQueue.pop(this->currentBuffer);
    this->buffInd = 0;
}

void FileOutputThread::writeBufferHeader()
{
    //first we write the buffer separator
    reinterpret_cast<unsigned int*>(&(this->currentBuffer[this->buffInd]))[0] = 0xF0F0F0F0;
    this->buffInd += 4;
    //then we write the buffer ID
    reinterpret_cast<unsigned int*>(&(this->currentBuffer[this->buffInd]))[0] = 0x00000002;
    this->buffInd += 4;
    //then we write a stand in for the number of events
    reinterpret_cast<unsigned int*>(&(this->currentBuffer[this->buffInd]))[0] = 0;
    this->buffInd += 4;
    //then we write which buffer number this is in the file
    reinterpret_cast<unsigned int*>(&(this->currentBuffer[this->buffInd]))[0] = this->bufferNumber;
    this->buffInd += 4;
    //then we write the time this buffer is started
    this->currFileTime = boost::posix_time::microsec_clock::universal_time();
    boost::posix_time::time_duration epochTime = (this->currFileTime - epoch);
    reinterpret_cast<long long*>(&(this->currentBuffer[this->buffInd]))[0] = epochTime.total_microseconds();
    this->buffInd += 8;
    //now we write a stand in for the time the buffer is finished
    reinterpret_cast<long long*>(&(this->currentBuffer[this->buffInd]))[0] = 0;
    this->buffInd += 8;
    //then we write zero to the remainder of the 8kb, since we are aligned to an 8byte boundary go in 8 byte chunks
    std::fill_n(reinterpret_cast<unsigned long long*>(&(this->currentBuffer[this->buffInd])), ((BufferOverHead - 32)/8), 0ULL);
    this->buffInd = BufferOverHead;
}

}
