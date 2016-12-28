/***************************************************************************//**
********************************************************************************
**
** @file UIThread.h
** @author James Till Matta
** @date 27 Apr, 2016
** @brief
**
** @copyright Copyright (C) 2016 James Till Matta
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
**
** @details Holds the implementation of the class that is run by main to handle
** the user interface
**
********************************************************************************
*******************************************************************************/

#include"UIThread.h"
// includes for C system headers
// includes for C++ system headers
#include<string>
#include<sstream>
#include<iomanip>
// includes from other libraries
#include<boost/thread.hpp>
#include<boost/algorithm/string.hpp>
#include <boost/spirit/include/qi.hpp>
#include<boost/date_time/posix_time/posix_time.hpp>
// includes from ORCHID
#include"UICommandTable.h"
#include"Utility/OrchidLogger.h"

namespace Threads
{

static const int errorColor = 1;
static const int goodColor = 2;
static const int gridStartLine = 5;
static const int trigStartCol = 1;
static const int tempStartCol = 76;
static const int volStartCol = 35;
static const float expAvgSmthFactor = 0.5;

//this is the array of settings for internal pulser rates. There are 72 different
//combinations, each is at least 6.8% smaller in rate than the next one, all the
//rates here are 1MHz or less, the format is: {#channels at 1kHz, #channels at 10kHz,
//#channels at 100kHz, and #channels at 1MHz}
static const unsigned int rateCountArray[72][4] =
{{ 1,  0, 0, 0}, { 2,  0, 0, 0}, { 3,  0, 0, 0}, { 4,  0,  0, 0},
 { 5,  0, 0, 0}, { 6,  0, 0, 0}, { 7,  0, 0, 0}, { 8,  0,  0, 0},
 { 9,  0, 0, 0}, {10,  0, 0, 0}, {11,  0, 0, 0}, {12,  0,  0, 0},
 {13,  0, 0, 0}, {14,  0, 0, 0}, {15,  0, 0, 0}, { 7,  1,  0, 0},
 { 9,  1, 0, 0}, {11,  1, 0, 0}, {13,  1, 0, 0}, {15,  1,  0, 0},
 { 7,  2, 0, 0}, { 9,  2, 0, 0}, {11,  2, 0, 0}, {14,  2,  0, 0},
 { 7,  3, 0, 0}, {10,  3, 0, 0}, {13,  3, 0, 0}, { 6,  4,  0, 0},
 {10,  4, 0, 0}, { 4,  5, 0, 0}, { 8,  5, 0, 0}, { 2,  6,  0, 0},
 { 7,  6, 0, 0}, { 2,  7, 0, 0}, { 7,  7, 0, 0}, { 3,  8,  0, 0},
 { 0,  9, 0, 0}, { 7,  9, 0, 0}, { 4, 10, 0, 0}, { 2, 11,  0, 0},
 { 0, 12, 0, 0}, { 9,  2, 1, 0}, { 8,  3, 1, 0}, { 8,  4,  1, 0},
 { 9,  5, 1, 0}, { 0,  7, 1, 0}, { 2,  8, 1, 0}, { 5,  9,  1, 0},
 { 9,  0, 2, 0}, { 4,  2, 2, 0}, { 0, 14, 1, 0}, { 7,  5,  2, 0},
 { 5,  7, 2, 0}, { 4,  9, 2, 0}, { 4,  1, 3, 0}, { 6,  3,  3, 0},
 { 0,  6, 3, 0}, { 5,  8, 3, 0}, { 2, 11, 3, 0}, { 0,  4,  4, 0},
 { 0,  7, 4, 0}, { 2, 10, 4, 0}, { 7,  3, 5, 0}, { 4,  7,  5, 0},
 { 3,  1, 6, 0}, { 5,  5, 6, 0}, { 0, 10, 6, 0}, { 0,  5,  7, 0},
 { 1,  0, 8, 0}, { 0,  6, 8, 0}, { 0,  2, 9, 0}, { 0,  0, 10, 0}};

UIThread::UIThread(InterThread::SlowData* slDat, InterThread::AcquisitionData* rtDat,
                   InterThread::FileData* fiDat, Utility::MpodMapper* mpodMap,
                   InterThread::AcquisitionThreadControl* acqCtrl,
                   InterThread::SlowControlsThreadController* sctCtrl,
                   InterThread::FileOutputThreadController* fileCtrl,
                   InterThread::ProcessingThreadControl* procCtrl,
                   Utility::ToProcessingQueuePair* procDataQueue,
                   Utility::ToFileMultiQueue* fileDataQueue,
                   SlowControls::MpodController* mpCtrl,
                   Digitizer::Vx1730Digitizer* digitizer, int refreshFrequency,
                   int pollingRate, int numAcqThr, int numPrThr, bool runPowerUp,
                   bool runPowerDown):
    slowData(slDat), acqData(rtDat), fileData(fiDat), mpodMapper(mpodMap), procQueuePair(procDataQueue),
    fileMultiQueue(fileDataQueue), numAcqThreads(numAcqThr), numProcThreads(numPrThr),
    acqControl(acqCtrl), sctControl(sctCtrl), fileControl(fileCtrl), procControl(procCtrl), mpodController(mpCtrl),
    persistCount(-1), command(""),  persistentMessage(""), runLoop(true),
    refreshRate(refreshFrequency), mode(UIMode::Init), textWindow(nullptr),
    messageWindow(nullptr), lg(OrchidLog::get()), powerUp(runPowerUp), powerDown(runPowerDown),
    smthFileSize(0.0), smthDigiSize(nullptr), updateLoops(0), smthTrigRate(nullptr), digi(digitizer)
{
    //calculate the refresh period in seconds then multiply by one billion to get
    //nanoseconds, which is what boost thread takes
    long long int refPeriod = (1000000000/refreshFrequency);
    this->rateMultiplier = static_cast<float>(refreshFrequency);
    this->refreshPeriod = boost::chrono::nanoseconds(refPeriod);
    
    long long int pollPeriod = static_cast<long long int>(2*static_cast<float>(1000000000/pollingRate));
    this->slowControlsPollingWaitPeriod = boost::chrono::nanoseconds(pollPeriod);
    
    smthDigiSize = new float[numAcqThr];
    
    smthTrigRate = new float[acqData->numChannels];
}

void UIThread::operator() ()
{
    //do initial set up of ncurses
    this->initScreen();
    //prep for initial drawing
    
    //holds characters that were input during the loop
    int inChar;
    //start the event loop
    /*while(runLoop)
    {
        //here we draw the screen
        drawScreen();
        //here we check if a key was pressed
        if((inChar = getch()) != ERR) //if we get ERR then no key was pressed in the period
        {
            this->handleKeyPress(inChar);
        }
        //here we sleep a bit
        boost::this_thread::sleep_for(this->refreshPeriod);
    }*/
    this->turnOn();
    this->runRateTests();
    this->runGracefulShutdown();
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Preparing for shutdown";
    this->waitForAllTerminations();
    delwin(this->textWindow);
    delwin(this->messageWindow);
    endwin();
}

void UIThread::initScreen()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Initializing the UI";
    initscr();
    keypad(stdscr, true);
    nodelay(stdscr, true);
    cbreak();
    noecho();
    clear();
    
    //get the terminal size, if it is not big enough force the user to resize
    //the terminal until we get at least the minimum size
    this->numRows = LINES; this->numCols = COLS;
    while((this->numRows < 14) || (this->numCols < 72))
    {
        wclear(this->textWindow);
        wclear(this->messageWindow);
        clear();
        wmove(this->textWindow, 0, 0);
        printw("Too Small! Make Window Bigger");
        this->waitForResize();
        this->numRows = LINES; this->numCols = COLS;
    }
    clear();
    
    //make the sub windows
    this->textWindow = newwin(this->numRows - 4, this->numCols, 0, 0);
    this->messageWindow = newwin(4, this->numCols, this->numRows - 4, 0);
    scrollok(this->textWindow, true);
    //find out if we can use color to draw things
    if(has_colors())
    {  
        start_color();
        use_default_colors();
    }
    //init some colors for usage
    init_color(COLOR_BLACK, 0, 0, 0);
    init_color(COLOR_RED, 1000, 0, 0);
    init_color(COLOR_GREEN, 0, 1000, 0);
    init_color(COLOR_BLUE, 0, 0, 1000);
    init_color(COLOR_WHITE, 1000, 1000, 1000);
    init_pair(errorColor, COLOR_RED, COLOR_BLACK);
    init_pair(goodColor, COLOR_GREEN, COLOR_BLACK);
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Done Initializing UI";
}

void UIThread::drawScreen()
{
    switch(mode)
    {
    case UIMode::Init:
        this->drawInitScreen();
        break;
    case UIMode::Idle:
        this->drawIdleScreen();
        break;
    case UIMode::Running:
        this->drawRunningScreen();
        break;
    case UIMode::Testing:
        this->drawTestingScreen();
        break;
    }
    refresh();
    wrefresh(this->textWindow);
    wrefresh(this->messageWindow);
}

void UIThread::drawInitScreen()
{
    //this->sizeDiff = 0;
    mvwprintw(this->textWindow, 0, 0,  "Status: Not Ready");
    mvwprintw(this->textWindow, 1, 0,  "Commands Available");
    mvwprintw(this->textWindow, 2, 4,  "turnon");
    mvwprintw(this->textWindow, 2, 16, "- Activates MPOD and Preps Acquisition");
    mvwprintw(this->textWindow, 3, 4,  "quit/exit");
    mvwprintw(this->textWindow, 3, 16, "- Exit ORCHID");
    
    this->drawPersistentMessage();
    this->drawCommandInProgress();
}

void UIThread::drawIdleScreen()
{
    //this->sizeDiff = 0;
    mvwprintw(this->textWindow, 0, 0, "Status: Idle");
    mvwprintw(this->textWindow, 1, 0,  "Commands Available");
    mvwprintw(this->textWindow, 2, 4,  "start");
    mvwprintw(this->textWindow, 2, 16, "- Start taking data");
    mvwprintw(this->textWindow, 3, 4,  "dotests");
    mvwprintw(this->textWindow, 3, 16, "- RunAcq Tests");
    mvwprintw(this->textWindow, 4, 4,  "changerun");
    mvwprintw(this->textWindow, 4, 16, "- Change Run Title and Number");
    mvwprintw(this->textWindow, 5, 4,  "runnumber");
    mvwprintw(this->textWindow, 5, 16, "- Change Run Number");
    mvwprintw(this->textWindow, 6, 4,  "next");
    mvwprintw(this->textWindow, 6, 16, "- Increment Run Number");
    mvwprintw(this->textWindow, 7, 4,  "turnoff");
    mvwprintw(this->textWindow, 7, 16, "- Shutdown MPOD and Demobilize Acquisition");
    mvwprintw(this->textWindow, 8, 4,  "quit/exit");
    mvwprintw(this->textWindow, 8, 16, "- Exit ORCHID");
    
    this->drawPersistentMessage();
    this->drawCommandInProgress();
}

void UIThread::drawFileInfo()
{
    wmove(this->textWindow, 0, 0);
    wclrtoeol(this->textWindow);
    static boost::posix_time::ptime fileStartTime;
    static boost::posix_time::time_duration runTime;
    //check if we need to get new values
    if(fileData->fileNameChangeSinceLastGet())
    {
        fileData->getFileName(this->fileName);
        smthFileSize = 0.0;
        fileStartTime = boost::posix_time::microsec_clock::universal_time();
        wmove(this->textWindow, 0, 0);
        wclrtoeol(this->textWindow);
    }
    if(fileData->runTitleChangeSinceLastGet())
    {
        fileData->getRunTitle(this->runTitle);
        smthFileSize = 0.0;
        fileStartTime = boost::posix_time::microsec_clock::universal_time();
        wmove(this->textWindow, 0, 0);
        wclrtoeol(this->textWindow);
    }
    if(fileData->runNumberChangeSinceLastGet())
    {
        this->runNumber = fileData->getRunNumber();
        smthFileSize = 0.0;
        fileStartTime = boost::posix_time::microsec_clock::universal_time();
        wmove(this->textWindow, 0, 0);
        wclrtoeol(this->textWindow);
    }
    if(fileData->sequenceNumberChangeSinceLastGet())
    {
        this->sequenceNumber = fileData->getSequenceNumber();
        smthFileSize = 0.0;
        fileStartTime = boost::posix_time::microsec_clock::universal_time();
        wmove(this->textWindow, 0, 0);
        wclrtoeol(this->textWindow);
    }
    runTime = (this->currTime - fileStartTime);
    float fileMilliSeconds = static_cast<float>(runTime.total_microseconds())/1000.0;
    
    //Generate the file info string
    std::ostringstream builder;
    builder << "Run Title: " << this->runTitle << " | Run #: " << this->runNumber;
    builder << " | File #: " << this->sequenceNumber << " | Rate: ";
    //get the file size and calculate the current file write rate
    long long tempFileSize = this->fileData->getSize();
    smthFileSize = (expAvgSmthFactor*static_cast<float>(tempFileSize) + (1-expAvgSmthFactor)*smthFileSize);
    float rate = (smthFileSize*1000)/fileMilliSeconds;
    //calculate if the file write rate is in thousands or millions etc
    if(rate > 999999.95)
    {
        builder << std::fixed << std::setw(6) << std::setfill(' ') << std::setprecision(1) << (rate/1048576.0) << "M";
    }
    else if(rate > 999.95)
    {
        builder << std::fixed << std::setw(6) << std::setfill(' ') << std::setprecision(1) << (rate/1024.0) << "k";
    }
    else
    {
        builder << std::fixed << std::setw(6) << std::setfill(' ') << std::setprecision(0) << rate;
    }
    builder << "B/s | Size: ";
    if(smthFileSize > 999999999.95)
    {
        builder << std::fixed << std::setw(6) << std::setfill(' ') << static_cast<int>(smthFileSize/1048576.0) << "M";
    }
    else if(smthFileSize > 999999.95)
    {
        builder << std::fixed << std::setw(6) << std::setfill(' ') << std::setprecision(0) << (smthFileSize/1048576.0) << "M";
    }
    else if(smthFileSize > 999.95)
    {
        builder << std::fixed << std::setw(6) << std::setfill(' ') << std::setprecision(0) << (smthFileSize/1024.0) << "k";
    }
    else
    {
        builder << std::fixed << std::setw(6) << std::setfill(' ') << std::setprecision(0) << smthFileSize;
    }
    builder  << "B | File: " << this->fileName;
    mvwprintw(this->textWindow, 0, 0, builder.str().c_str());
}

void UIThread::drawGlobalSlowControlsInformation()
{
    std::ostringstream builder;
    //since we do not want to constantly regenerate and resize this variable
    //plus we want to avoid having too many class members (there are a crapton already)
    static std::string mpodCrateStatus = "Status Not Updating";
    this->slowData->genCrateInfoString(mpodCrateStatus);
    builder << "MPOD Crate Status: " << mpodCrateStatus;
    static std::string temperatureReaderStatus = "Not Updating";
    //this->slowData->genTempReaderInfoString(temperatureReaderStatus);
    builder << " | Temperature Reader Status: " << temperatureReaderStatus;
    mvwprintw(this->textWindow, 3, 0, builder.str().c_str());
}

void UIThread::drawRuntimeInformation()
{
    std::ostringstream builder;
    static long long rTime = 0;
    wmove(this->textWindow, 1, 0);
    wclrtoeol(this->textWindow);
    rTime = (milliSeconds/1000);
    builder << "Runtime: " << rTime <<" s";
    mvwprintw(this->textWindow, 1, 0, builder.str().c_str());
}

void UIThread::drawAcquisitionGlobalInformation()
{
    std::ostringstream builder;
    wmove(this->textWindow, 2, 0);
    wclrtoeol(this->textWindow);
    for(int i=0; i < numAcqThreads; ++i)
    {
        unsigned long long tempDataSize = (acqData->dataSizes[i]);
        smthDigiSize[i] = (expAvgSmthFactor*tempDataSize + (1-expAvgSmthFactor)*smthDigiSize[i]);
        float rate = (smthDigiSize[i]*1000)/milliSeconds;
        if(rate > 999999.95)
        {
            builder << "Mod " << i << ": " << std::fixed << std::setw(5) << std::setfill(' ') << std::setprecision(1) << (rate/1048576.0) << "M";
        }
        else if(rate > 999.95)
        {
            builder << "Mod " << i << ": " << std::fixed << std::setw(5) << std::setfill(' ') << std::setprecision(1) << (rate/1024.0) << "k";
        }
        else
        {
            builder << "Mod " << i << ": " << std::fixed << std::setw(5) << std::setfill(' ') << std::setprecision(1) << rate;
        }
        builder << "B/s";
        mvwprintw(this->textWindow, 2, 0, builder.str().c_str());
    }
}

void UIThread::drawTriggersGrid()
{
    //TODO: Add highligting of params outside range
    int currentRow = gridStartLine;
    //now draw the topmost separators
    mvwprintw(this->textWindow, currentRow, trigStartCol, "-------------------------------");
    ++currentRow;
    mvwprintw(this->textWindow, currentRow, trigStartCol, "| Chan | Rate(Hz) |   Count   |");
    ++currentRow;
    //more separators
    mvwprintw(this->textWindow, currentRow, trigStartCol, "===============================");
    ++currentRow;
    //now loop through the digitizer channels
    int currDataLine = currentRow;
    int stopLine = currentRow + this->acqData->numChannels;
    int chanInd = 0;
    while(currDataLine < stopLine)
    {
        //build the trigger data string
        std::ostringstream builder;
        //first add the channel in the usual format
        builder << "| " << std::setw(4) << std::setfill(' ') << chanInd << " | " << std::setfill(' ');
        //add the voltage
        unsigned long long tempTrigs = (acqData->triggers[chanInd].load());
        smthTrigRate[chanInd] = (expAvgSmthFactor*tempTrigs + (1-expAvgSmthFactor)*smthTrigRate[chanInd]);
        float trigRate = (smthTrigRate[chanInd]*1000)/milliSeconds;
        if(trigRate >= 9999.95)//choose 999.95 to prevent rounding weirdness
        {
            builder << std::fixed << std::setw(6) << std::setprecision(1) << (trigRate/1000.0) << " k | ";
        }
        else
        {
            builder << std::fixed << std::setw(8) << std::setprecision(0) << trigRate <<" | ";
        }
        
        if(smthTrigRate[chanInd] > 999999999999.95)
        {
            builder << std::fixed << std::setw(7) << std::setprecision(3) << (smthTrigRate[chanInd]/1000000000000.0) << " T |";
        }
        else if(smthTrigRate[chanInd] > 999999999.95)
        {
            builder << std::fixed << std::setw(7) << std::setprecision(1) << (smthTrigRate[chanInd]/1000000000.0) << " G |";
        }
        else if(smthTrigRate[chanInd] > 999999.95)
        {
            builder << std::fixed << std::setw(7) << std::setprecision(1) << (smthTrigRate[chanInd]/1000000.0) << " M |";
        }
        else if(smthTrigRate[chanInd] > 999.95)
        {
            builder << std::fixed << std::setw(7) << std::setprecision(1) << (smthTrigRate[chanInd]/1000.0) << " k |";
        }
        else
        {
            builder << std::fixed << std::setw(7) << std::setprecision(1) << smthTrigRate[chanInd] << "   |";
        }
        mvwprintw(this->textWindow, currDataLine, trigStartCol, builder.str().c_str());
        ++chanInd;
        ++currDataLine;
    }
    currentRow = currDataLine;
    mvwprintw(this->textWindow, currentRow, trigStartCol, "===============================");
    
}

void UIThread::drawSlowControlsGrid()
{
    //TODO: Add highligting of params outside range
    int currentRow = gridStartLine;
    //now draw the topmost separators
    mvwprintw(this->textWindow, currentRow, tempStartCol, "--------------");
    mvwprintw(this->textWindow, currentRow, volStartCol, "---------------------------------------");
    ++currentRow;
    //now draw the column headers
    mvwprintw(this->textWindow, currentRow, tempStartCol, "|Chan|  Temp |");
    mvwprintw(this->textWindow, currentRow, volStartCol, "| Chan | TermVol | Current | Status");
    ++currentRow;
    //more separators
    mvwprintw(this->textWindow, currentRow, tempStartCol, "==============");
    mvwprintw(this->textWindow, currentRow, volStartCol, "=======================================");
    ++currentRow;
    //now loop through the temp sensors
    int currDataLine = currentRow;
    int stopLine = currentRow + this->slowData->numTemperatureChannels;
    int chanInd = 0;
    while(currDataLine < stopLine)
    {
        mvwprintw(this->textWindow, currDataLine, tempStartCol, "|    |       |");
        //TODO: write out temp information
        ++currDataLine;
    }
    mvwprintw(this->textWindow, currDataLine, tempStartCol, "==============");
    //now loop through the voltage sensors
    currDataLine = currentRow;
    stopLine = currentRow + this->slowData->numVoltageChannels;
    chanInd = 0;
    while(currDataLine < stopLine)
    {
        //build the voltage data string
        std::ostringstream builder;
        //first add the channel in the usual format
        builder << "| u" << (this->mpodMapper->moduleNum[chanInd]);
        builder << std::setw(2) << std::setfill('0') << (this->mpodMapper->channelNum[chanInd]) << " | " << std::setfill(' ');
        //add the voltage
        float termVol =  this->slowData->terminalVoltage[chanInd];
        if(termVol >= 999.95)//choose 999.95 to prevent rounding weirdness
        {
            builder << std::fixed << std::setw(5) << std::setprecision(3) << (termVol/1000.0) <<"kV | ";
        }
        else
        {
            builder << std::fixed << std::setw(5) << std::setprecision(1) << termVol <<" V | ";
        }
        //add the current
        builder << std::fixed << std::setw(5) << std::setprecision(1) << (this->slowData->current[chanInd]) <<"uA | ";
        //add the status
        std::string statusString;
        this->slowData->genChannelInfoString(chanInd, statusString);
        builder << statusString;
        mvwprintw(this->textWindow, currDataLine, volStartCol, builder.str().c_str());
        ++chanInd;
        ++currDataLine;
    }
    currentRow = currDataLine;
    mvwprintw(this->textWindow, currentRow, volStartCol, "=======================================");
    
}

void UIThread::drawRunningScreen()
{
    //draw the file information line
    this->drawFileInfo();
    currTime = boost::posix_time::microsec_clock::universal_time();
    runTime = (this->currTime - startTime);
    milliSeconds = runTime.total_microseconds()/1000;
    //draw the run time line
    this->drawRuntimeInformation();
    //Draw the acquisition info line
    this->drawAcquisitionGlobalInformation();
    //draw the slow controls global info line
    this->drawGlobalSlowControlsInformation();
    //draw the slow controls info grid();
    this->drawTriggersGrid();
    //draw the slow controls info grid();
    this->drawSlowControlsGrid();
    //draw the message stuff
    this->drawPersistentMessage();
    this->drawCommandInProgress();
    ++updateLoops;
}

void UIThread::drawCommandInProgress()
{
    mvwprintw(this->messageWindow, 3, 0, "?> ");
    mvwprintw(this->messageWindow, 3, 3, command.c_str());
}

void UIThread::drawPersistentMessage()
{
    //first figure out if we should display the persistent message
    if(persistCount > 0)
    {
        //attron(COLOR_PAIR(persistColor));
        wattron(this->messageWindow, A_BOLD);
        wattron(this->messageWindow, COLOR_PAIR(persistColor));
        mvwprintw(this->messageWindow, 1, 0, this->persistentMessage.c_str());
        wattroff(this->messageWindow, A_BOLD);
        wattroff(this->messageWindow, COLOR_PAIR(persistColor));
        --persistCount;
    }
    else if(persistCount == 0)
    {
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        --persistCount;
    }
}

void UIThread::handleCommand()
{
    //normalize the command to lowercase
    boost::to_lower(command);
    // log that we have a command
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Got Command: " << command;
    //look up the command inside dispatch map
    std::map<std::string, UICommands>::const_iterator cmdFind = UI_COMMAND_DISPATCH.find(command);
    //clear the command and screen
    command.clear();
    clear();
    // get the command enumeration or set it to invalid
    UICommands cmd;
    if(cmdFind != UI_COMMAND_DISPATCH.end())
    {
        cmd = cmdFind->second;
        bool okFlag = false;
        switch(this->mode)
        {
        case UIMode::Init:
            okFlag = INIT_MODE_VALID_CMD.find(cmd)->second;
            break;
        case UIMode::Idle:
            okFlag = IDLE_MODE_VALID_CMD.find(cmd)->second;
            break;
        case UIMode::Running:
        case UIMode::Testing:
            okFlag = RUN_MODE_VALID_CMD.find(cmd)->second;
            break;
        }
        if(!okFlag)
        {
            BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Command is unavailable in current mode";
            cmd = UICommands::Unavailable;
        }
    }
    else
    {
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Command is invalid";
        cmd = UICommands::Invalid;
    }
    
    // switch on the command enum to figure out what to do
    switch(cmd)
    {
    case UICommands::Invalid:
        this->persistentMessage = "Error:  Invalid Command";
        this->persistColor = errorColor;
        this->persistCount = refreshRate*5;
        break;
    case UICommands::Quit:
        this->persistCount = -1; //clear any error, clearly they fixed it
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        this->runGracefulShutdown();
        break;
    case UICommands::TurnOn:
        this->persistCount = -1; //clear any error, clearly they fixed it
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        this->turnOn();
        break;
    case UICommands::TurnOff:
        this->persistCount = -1; //clear any error, clearly they fixed it
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        this->turnOff();
        break;
    case UICommands::Start:
        this->persistCount = -1; //clear any error, clearly they fixed it
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        switch(this->fileData->getInitState())
        {
        case 'i':
            this->startDataTaking();
            break;
        case 'u':
            BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Cannot Start Run, no Run Title or Run Number";
            this->persistentMessage = "Error:  Must set run title and number";
            this->persistColor = errorColor;
            this->persistCount = refreshRate*5;
            break;
        case 'n':
            BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Cannot Start Run, no Run Number";
            this->persistentMessage = "Error:  Must set at least number";
            this->persistColor = errorColor;
            this->persistCount = refreshRate*5;
            break;
        }
        break;
    case UICommands::RunTests:
        this->persistCount = -1; //clear any error, clearly they fixed it
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        this->runRateTests();
    case UICommands::Next:
        this->persistCount = -1; //clear any error, clearly they fixed it
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        switch(this->fileData->getInitState())
        {
        case 'i':
        case 'n':
            ++(this->tempRunNumber);
            while(!this->fileControl->setNewRunParameters(this->tempRunTitle, this->tempRunNumber))
            {//failed at setting the new parameters, try again in a moment
                boost::this_thread::sleep_for(this->refreshPeriod);
            }
            break;
        case 'u':
            this->persistentMessage = "Error:  Set run title and number before incrementing run number";
            this->persistColor = errorColor;
            this->persistCount = refreshRate*5;
            break;
        }
        break;
    case UICommands::ChangeNum:
        switch(this->fileData->getInitState())
        {
        case 'i':
        case 'n':
            this->setRunNumber();
            while(!this->fileControl->setNewRunParameters(this->tempRunTitle, this->tempRunNumber))
            {//failed at setting the new parameters, try again in a moment
                boost::this_thread::sleep_for(this->refreshPeriod);
            }
            break;
        case 'u':
            this->persistentMessage = "Error:  Must set run title before trying to modify only the number";
            this->persistColor = errorColor;
            this->persistCount = refreshRate*5;
            break;
        }
        break;
    case UICommands::Stop:
        this->persistCount = -1; //clear any error, clearly they fixed it
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        this->stopDataTaking();
        break;
    case UICommands::ChangeRun:
        this->persistCount = -1; //clear any error, clearly they fixed it
        wmove(this->messageWindow, 1, 0);
        persistentMessage.clear();
        wclrtoeol(this->messageWindow);
        this->setRunParams();
        break;
    case UICommands::Unavailable:
        this->persistentMessage = "Error:  Command unvailable in this mode";
        this->persistColor = errorColor;
        this->persistCount = refreshRate*5;
        break;
    default:
        BOOST_LOG_SEV(this->lg, Critical) << "UI Thread: Critical Error: Got to command switch default case. This should not be possible";
        this->persistentMessage = "Critical Error: Got to command switch default case. This should be IMPOSSIBLE!";
        this->persistColor = errorColor;
        this->persistCount = refreshRate*60;
        break;
    }
}

void UIThread::runRateTests()
{
    //set up a lambda to change runs and stuff every time we need to
    auto setRunsLambda = 
    [this](const std::string& runTil, const int runNum)
    {
        while(!this->fileControl->setNewRunParameters(runTil, runNum))
        {
            boost::this_thread::sleep_for(this->refreshPeriod);
        }
    };
    //this is the run name for everything to come
    this->tempRunTitle = "NewDigitizerTests";
    
    //iterate through the set of digitizer settings
    for(int i=71; i<72; ++i)
    {
        unsigned totalRate = rateCountArray[i][0]     + rateCountArray[i][1] * 10   +
                             rateCountArray[i][2]*100 + rateCountArray[i][3] * 1000;
        totalRate*=1000;
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting test: " << i << "With Rate: " << totalRate;
        //set the run number
        this->tempRunNumber = i;
        //configure the pulser settings for this run
        pulseSettings.setOverallRate(rateCountArray[i][0],
                                     rateCountArray[i][1],
                                     rateCountArray[i][2],
                                     rateCountArray[i][3]);
        //setup the digitizer pulser
        this->digi->setupPulsing(pulseSettings);
        //setup the run
        setRunsLambda(this->tempRunTitle, this->tempRunNumber);
        //run the test
        this->startTestDataTaking();
        //draw the testing screen
        while(this->currTime < this->stopTime)
        {
            this->drawTestingScreen();
            boost::this_thread::sleep_for(this->refreshPeriod);
        }
        //now shut this run down
        this->stopDataTaking();
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Ending test: " << i << "With Rate: " << totalRate;
        //now pause a few seconds
        boost::this_thread::sleep_for(boost::chrono::seconds(2));
    }
}

void UIThread::startTestDataTaking()
{
    updateLoops = 1;
    smthFileSize = 0.0;
    for(int i=0; i<numAcqThreads; ++i)
    {
        smthDigiSize[i] = 0.0;
    }
    for(int i=0; i<acqData->numChannels; ++i)
    {
        smthTrigRate[i] = 0.0;
    }
    this->acqData->clearData();
    this->acqData->clearTrigs();
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting File Writing";
    this->fileControl->setToWriting();
    //wait to be certain the file thread is up and running before we start anything else
    boost::this_thread::sleep_for(this->refreshPeriod);
    //remove slow controls stuff, we aren't using it, just leave it polling
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting Event Processing";
    this->procControl->setToRunning();
    //wait to be certain the processing threads are up and running before we start anything else
    boost::this_thread::sleep_for(this->refreshPeriod);
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Online Processing Thread Set To Start";
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting Acquisition Thread";
    this->acqControl->setToAcquiring();
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For Acquisition Starts");
    wrefresh(this->textWindow);
    while(this->acqControl->getThreadsWaiting() > 0)
    {//until we see the acquisition threads are done waiting on their wait condition, sleep and spin
        boost::this_thread::sleep_for(this->refreshPeriod);
    }
    //TODO: Add code for sending acquisition start signal from USB device for the S-In front panel LEMO
    this->startTime = boost::posix_time::microsec_clock::universal_time();
    this->stopTime = (this->startTime + boost::posix_time::minutes(10));
    mode = UIMode::Testing;
    //this->startLine = 0;
    wclear(this->textWindow);
}

void UIThread::drawTestingScreen()
{
    //draw the file information line
    this->drawFileInfo();
    currTime = boost::posix_time::microsec_clock::universal_time();
    runTime = (this->currTime - startTime);
    milliSeconds = runTime.total_microseconds()/1000;
    //draw the run time line
    this->drawRuntimeInformation();
    //Draw the acquisition info line
    this->drawAcquisitionGlobalInformation();
    //draw the slow controls global info line
    this->drawPulserInfo();
    //draw the slow controls info grid();
    this->drawTriggersGrid();
    //draw the message stuff
    this->drawPersistentMessage();
    this->drawCommandInProgress();
    ++updateLoops;
}

void UIThread::drawPulserInfo()
{
    std::ostringstream builder;
    builder << "Internal Pulser: ";
    for(int i=0; i<16; ++i)
    {
        if(!pulseSettings.active[i])
        {
            builder<<"ch "<<i<<": off | ";
        }
        else
        {
            if(pulseSettings.rates[i]==0)
            {
                builder<<"ch "<<i<<": 1kHz | ";
            }
            else if(pulseSettings.rates[i]==1)
            {
                builder<<"ch "<<i<<": 10kHz | ";
            }
            else if(pulseSettings.rates[i]==2)
            {
                builder<<"ch "<<i<<": 100kHz | ";
            }
            else if(pulseSettings.rates[i]==3)
            {
                builder<<"ch "<<i<<": 1MHz | ";
            }
            else
            {
                builder<<"ch "<<i<<": WTF | ";
            }
        }
    }
    mvwprintw(this->textWindow, 3, 0, builder.str().c_str());
}


void UIThread::handleScreenResize()
{
    //get the terminal size, if it is not big enough force the user to resize
    //the terminal until we get at least the minimum size
    this->numRows = LINES; this->numCols = COLS;
    while((this->numRows < 14) || (this->numCols < 80))
    {
        wclear(this->textWindow);
        wclear(this->messageWindow);
        clear();
        wmove(this->textWindow, 0, 0);
        printw("Too Small! Make Window Bigger");
        this->waitForResize();
        this->numRows = LINES; this->numCols = COLS;
    }
    clear();
    delwin(this->textWindow);
    delwin(this->messageWindow);
    //make the sub windows
    this->textWindow = newwin(this->numRows - 4, this->numCols, 0, 0);
    this->messageWindow = newwin(4, this->numCols, this->numRows - 4, 0);
    scrollok(this->textWindow, true);
}

void UIThread::waitForResize()
{
    bool flag = true;
    int inChar;
    while(flag)
    {
        boost::this_thread::sleep_for(this->refreshPeriod);
        //here we check if a key was pressed
        if((inChar = getch()) == KEY_RESIZE) flag=false;
    }
    boost::this_thread::sleep_for(this->refreshPeriod);
}

void UIThread::handleKeyPress(int inChar)
{
    int lastCharInd = (command.size() - 1);
    switch(inChar)
    {
    case KEY_ENTER:
    case '\r':
    case '\n':
    case '\f':
        this->handleCommand();
        break;
    case KEY_RESIZE:
        this->handleScreenResize();
        break;
    case KEY_BACKSPACE:
    case KEY_DC:
        if(lastCharInd >= 0)
        {
            mvwprintw(this->messageWindow, 3, lastCharInd+3, " ");
            this->command.erase(lastCharInd, 1);   
        }
        break;
    case 'a'://I know it is probably a bit dumb, but this was my first thought
    case 'b'://on how to make certain that only non silly characters make it
    case 'c'://to the command with this all the meta characters etc are filtered
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '_':
    case '-':
    case '(':
    case ')':
    case '*':
    case '+':
    case '=':
    case ',':
    case '.':
    case '[':
    case ']':
    case '{':
    case '}':
    case ' ':
        this->command.append(1, inChar);
        break;
    default://anything not listed explicitly, ignore
        break;
    }
}

void UIThread::handleSetRunNumKeyPress(int inChar)
{
    int lastCharInd = (command.size() - 1);
    switch(inChar)
    {
    case KEY_ENTER:
    case '\r':
    case '\n':
    case '\f':
    {
        int temp;
        std::ostringstream builder;
        builder << "Run number set to: " << command ;
        this->persistentMessage = builder.str();
        this->persistColor = goodColor;
        this->persistCount = refreshRate*2;
        boost::spirit::qi::parse(command.begin(), command.end(), boost::spirit::qi::int_, temp);
        this->tempRunNumber = temp;
        command.clear();
        this->runSubLoop = false;
    }
        break;
    case KEY_RESIZE:
        this->handleScreenResize();
        break;
    case KEY_BACKSPACE:
    case KEY_DC:
        if(lastCharInd >= 0)
        {
            mvwprintw(this->messageWindow, 3, lastCharInd+3, " ");
            command.erase(lastCharInd, 1);   
        }
        break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        command.append(1, inChar);
        break;
 
    default://anything not listed explicitly, ignore
        break;
    }
}

void UIThread::handleSetRunTitleKeyPress(int inChar)
{
    int lastCharInd = (command.size() - 1);
    switch(inChar)
    {
    case KEY_ENTER:
    case '\r':
    case '\n':
    case '\f':
    {
        std::ostringstream builder;
        builder << "Run title set to: " << command ;
        this->persistentMessage = builder.str();
        this->persistColor = goodColor;
        this->persistCount = refreshRate*2;
        this->tempRunTitle = this->command;
        command.clear();
        this->runSubLoop = false;
    }
        break;
    case KEY_RESIZE:
        this->handleScreenResize();
        break;
    case KEY_BACKSPACE:
    case KEY_DC:
        if(lastCharInd >= 0)
        {
            mvwprintw(this->messageWindow, 3, lastCharInd+3, " ");
            command.erase(lastCharInd, 1);   
        }
        break;
    case 'a'://I know it is probably a bit dumb, but this was my first thought
    case 'b'://on how to make certain that only non silly characters make it
    case 'c'://to the command with this all the meta characters etc are filtered
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '_':
    case '-':
    case '(':
    case ')':
    case '*':
    case '+':
    case '=':
    case ',':
    case '.':
    case '[':
    case ']':
        command.append(1, inChar);
        break;
    case ' ':
        command.append(1, '_');
        break;
    default://anything not listed explicitly, ignore
        break;
    }
}

void UIThread::waitForAllTerminations()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Terminating Threads";
    this->waitForSlowControlsThreadTermination();
    this->waitForAcquisitionThreadsTermination();
    this->waitForProcessingThreadsTermination();
    //this->waitForOnlineProcThreadTermination();
    this->waitForFileThreadTermination();
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Done Terminating Threads";
}

void UIThread::waitForProcessingThreadsTermination()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Terminating Processing Threads";
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For Termination of Processing Threads");
    wrefresh(this->textWindow);
    wclear(this->messageWindow);
    wrefresh(this->messageWindow);
    this->procControl->setToTerminate();
    //make certain the slow controls thread is awake
    this->fileMultiQueue->setForceStayAwake();
    this->fileMultiQueue->wakeAllProducer<Utility::ProcessingQueueIndex>();
    //loop until we see the terminate signals from the slow controls thread
    while(this->numProcThreads > this->procControl->getThreadsTerminated())
    {//until we see the acquisition threads waiting on their wait condition, sleep and spin
        boost::this_thread::sleep_for(this->refreshPeriod);
        this->fileMultiQueue->wakeAllProducer<Utility::ProcessingQueueIndex>();
    }
    this->fileMultiQueue->clearForceStayAwake();
}

void UIThread::waitForAcquisitionThreadsTermination()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Terminating Acquisition Threads";
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For Termination of Acquisition Threads");
    wrefresh(this->textWindow);
    wclear(this->messageWindow);
    wrefresh(this->messageWindow);
    this->acqControl->setToTerminate();
    //make certain the slow controls thread is awake
    this->procQueuePair->forceWakeAll();
    //loop until we see the terminate signals from the slow controls thread
    while(!(this->acqControl->getThreadsTerminated() == this->numAcqThreads))
    {
        boost::this_thread::sleep_for(this->refreshPeriod);
        this->procQueuePair->forceWakeAll();
    }
    this->procQueuePair->clearForce();
}

void UIThread::waitForSlowControlsThreadTermination()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Terminating Slow Controls Thread";
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For Termination of Slow Controls Thread");
    wrefresh(this->textWindow);
    wclear(this->messageWindow);
    wrefresh(this->messageWindow);
    this->sctControl->setToTerminate();
    //make certain the slow controls thread is awake
    this->fileMultiQueue->setForceStayAwake();
    this->fileMultiQueue->wakeAllProducer<Utility::SlowControlsQueueIndex>();
    //loop until we see the terminate signal from the slow controls thread
    while(!(this->sctControl->isDone()))
    {
        boost::this_thread::sleep_for(this->refreshPeriod);
    }
    this->fileMultiQueue->clearForceStayAwake();
}

void UIThread::waitForFileThreadTermination()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Terminating File Output Thread";
    wclear(this->textWindow);
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For Termination of File Output Thread");
    wrefresh(this->textWindow);
    wclear(this->messageWindow);
    wrefresh(this->messageWindow);
    this->fileControl->setToTerminate();
    //make certain the file thread is awake
    this->fileMultiQueue->setForceStayAwake();
    this->fileMultiQueue->wakeAllConsumer();
    //loop until we see the terminate signal from the slow controls thread
    while(!(this->fileControl->isDone()))
    {
        boost::this_thread::sleep_for(this->refreshPeriod);
    }
    this->fileMultiQueue->clearForceStayAwake();
}

void UIThread::runGracefulShutdown()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Running Graceful Shutdown";
    if(mode == UIMode::Idle)
    {
        turnOff();
    }
    else if(mode == UIMode::Running)
    {
        stopDataTaking();
        turnOff();
    }
    this->runLoop = false;
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Graceful Shutdown Complete";
}

void UIThread::turnOn()
{
    if(powerUp)
    {
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Turning on the MPOD Crate";
        //if we do not have a wait we hit the crate too hard and fast and tell it to do things before it is fully booted
        wclear(this->textWindow);
        mvwprintw(this->textWindow, 0, 0, "Pause for MPOD crate bootup.");
        wrefresh(this->textWindow);
        if(!this->mpodController->turnCrateOn())
        {
            BOOST_LOG_SEV(this->lg, Critical) << "UI Thread: Critical Error: MPOD either did not turn on or did not initialize";
            this->persistentMessage = "Critical Error: MPOD either did not turn on or did not initialize";
            this->persistColor = errorColor;
            this->persistCount = refreshRate*10;
            this->mpodController->turnCrateOff();//Undo anything that may have happened
            return; //then return without changing mode
        }
        wclear(this->textWindow);
        mvwprintw(this->textWindow, 0, 0, "Pause for MPOD channel starts.");
        wrefresh(this->textWindow);
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Turning on the HV channels";
        if(!this->mpodController->activateAllChannels())
        {
            BOOST_LOG_SEV(this->lg, Critical) << "UI Thread: Critical Error:  Error in turning on HV channels";
            this->persistentMessage = "Critical Error:  Error in turning on HV channels";
            this->persistColor = errorColor;
            this->persistCount = refreshRate*10;
            this->mpodController->deactivateAllChannels();
            this->mpodController->turnCrateOff();//Undo anything that may have happened
            return; //then return without changing mode
        }
    }
    else
    {
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Skipping MPOD powerup" << std::flush;
    }
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Setting Slow Controls thread to polling" << std::flush;
    this->sctControl->setToPolling();
    if(powerUp)
    {
        wclear(this->textWindow);
        mvwprintw(this->textWindow, 0, 0, "Waiting for voltage ramp up");
        wrefresh(this->textWindow);
        //sleep for 1.5 * poll period to give the slow controls thread a chance to
        //read when the ramp up is running
        boost::this_thread::sleep_for(this->slowControlsPollingWaitPeriod);
        bool stillRamping = true;
        while(stillRamping)
        {
            wclear(this->textWindow);
            mvwprintw(this->textWindow, 0, 0, "Waiting for voltage ramp up");
            wrefresh(this->textWindow);
            //here we sleep a bit
            boost::this_thread::sleep_for(this->refreshPeriod);
            //here we check if there are any channels still ramping their voltages
            int rampCount = std::count(this->slowData->outputRampUp,
                                       &(this->slowData->outputRampUp[this->slowData->numVoltageChannels]), true);
            if(rampCount == 0)
            {
                stillRamping = false;
            }
        }
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: HV done ramping" << std::flush;
    }
    else
    {
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Skipping HV rampup" << std::flush;
    }
    mode = UIMode::Idle;
    //this->startLine = 0;
    wclear(this->textWindow);
}

void UIThread::turnOff()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Putting Slow Controls thread into polling mode";
    this->sctControl->setToPolling();
    if(powerDown)
    {
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting channel ramp down";
        wclear(this->textWindow);
        mvwprintw(this->textWindow, 0, 0, "Waiting for voltage ramp down");
        wrefresh(this->textWindow);
        this->mpodController->deactivateAllChannels();
        //sleep for 1.5 * poll period to give the slow controls thread a chance to
        //read when the ramp down is running
        boost::this_thread::sleep_for(this->slowControlsPollingWaitPeriod);
        bool stillRamping = true;
        while(stillRamping)
        {
            wclear(this->textWindow);
            mvwprintw(this->textWindow, 0, 0, "Waiting for voltage ramp down");
            wrefresh(this->textWindow);
            //here we sleep a bit
            boost::this_thread::sleep_for(this->refreshPeriod);
            //here we check if there are any channels still ramping their voltages
            int rampCount = std::count(this->slowData->outputRampDown,
                                       &(this->slowData->outputRampDown[this->slowData->numVoltageChannels]), true);
            
            if(rampCount == 0)
            {
                stillRamping = false;
            }
        }
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Done Ramping, stopping slow controls thread";
    }
    else
    {
        BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Skipping voltage rampdown";
    }
    this->sctControl->setToStop();
    if(powerDown)
    {
        this->mpodController->turnCrateOff();
    }
    mode = UIMode::Init;
    wclear(this->textWindow);
}

void UIThread::startDataTaking()
{
    updateLoops = 1;
    smthFileSize = 0.0;
    for(int i=0; i<numAcqThreads; ++i)
    {
        smthDigiSize[i] = 0.0;
    }
    for(int i=0; i<acqData->numChannels; ++i)
    {
        smthTrigRate[i] = 0.0;
    }
    this->acqData->clearData();
    this->acqData->clearTrigs();
    this->startTime = boost::posix_time::microsec_clock::universal_time();
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting File Writing";
    this->fileControl->setToWriting();
    //wait to be certain the file thread is up and running before we start anything else
    boost::this_thread::sleep_for(this->refreshPeriod);
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting Slow Controls Event Generation";
    this->sctControl->setToWriting();
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Pausing to Ensure First Event Is Slow Controls";
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Pause to ensure first event is slow controls");
    wrefresh(this->textWindow);
    boost::this_thread::sleep_for(slowControlsPollingWaitPeriod);
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting Event Processing";
    this->procControl->setToRunning();
    //wait to be certain the processing threads are up and running before we start anything else
    boost::this_thread::sleep_for(this->refreshPeriod);
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Online Processing Thread Set To Start";
    //TODO put the online processing thread into stop mode
    //TODO wait for online processing to stop
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Starting Acquisition Thread";
    this->acqControl->setToAcquiring();
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For Acquisition Starts");
    wrefresh(this->textWindow);
    while(this->acqControl->getThreadsWaiting() > 0)
    {//until we see the acquisition threads are done waiting on their wait condition, sleep and spin
        boost::this_thread::sleep_for(this->refreshPeriod);
    }
    //TODO: Add code for sending acquisition start signal from USB device for the S-In front panel LEMO
    mode = UIMode::Running;
    //this->startLine = 0;
    wclear(this->textWindow);
}

void UIThread::stopDataTaking()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Acquistion Threads Set To Stop";
    this->acqControl->setToStopped();
    this->procQueuePair->setProducerForceWake();
    this->procQueuePair->wakeAllProducer();
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For Acquisition Stop");
    wrefresh(this->textWindow);
    while(this->numAcqThreads > this->acqControl->getThreadsWaiting())
    {//until we see the acquisition threads waiting on their wait condition, sleep and spin
        boost::this_thread::sleep_for(this->refreshPeriod);
        this->procQueuePair->wakeAllProducer();
    }
    this->procQueuePair->clearForce();
    
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Event Processing Threads Set To Stop";
    this->procControl->setToStopped();
    this->fileMultiQueue->setForceStayAwake();
    this->fileMultiQueue->wakeAllProducer<Utility::ProcessingQueueIndex>();
    this->procQueuePair->setConsumerForceWake();
    this->procQueuePair->wakeAllConsumer();
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For Processing Stop");
    wrefresh(this->textWindow);
    while(this->numProcThreads > this->procControl->getThreadsWaiting())
    {//until we see the acquisition threads waiting on their wait condition, sleep and spin
        boost::this_thread::sleep_for(this->refreshPeriod);
        this->fileMultiQueue->wakeAllProducer<Utility::ProcessingQueueIndex>();
        this->procQueuePair->setConsumerForceWake();
        this->procQueuePair->wakeAllConsumer();
    }
    this->procQueuePair->clearForce();
    this->fileMultiQueue->clearForceStayAwake();
    
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Online Processing Thread Set To Stop";
    //TODO put the online processing thread into stop mode
    //TODO wait for online processing to stop
    
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Slow Controls Set To Polling Only";
    this->sctControl->setToPolling();
    //make certain the slow controls thread is not waiting on the multiqueue
    this->fileMultiQueue->setForceStayAwake();
    this->fileMultiQueue->wakeAllProducer<Utility::SlowControlsQueueIndex>();
    this->fileMultiQueue->clearForceStayAwake();
    //don't bother waiting since we are still polling
    
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: File Thread Set To Stop";
    this->fileControl->setToWaiting();
    //make certain the file thread is not waiting in the multi queue
    this->fileMultiQueue->setForceStayAwake();
    this->fileMultiQueue->wakeAllConsumer();
    wclear(this->textWindow);
    mvwprintw(this->textWindow, 0, 0, "Waiting For File Thread Stopping");
    wrefresh(this->textWindow);
    while(!this->fileControl->isWaiting())
    {//until we see the file thread waiting on its wait condition, sleep and spin
        boost::this_thread::sleep_for(this->refreshPeriod);
    }
    this->fileMultiQueue->clearForceStayAwake();
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Setting Mode to Idle";
    mode = UIMode::Idle;
    wclear(this->textWindow);
}

void UIThread::setRunNumber()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Getting Run Number";
    //run number to that and the sequence number to zero
    int inChar;
    this->runSubLoop = true;
    while(runSubLoop)
    {
        //here we draw the screen
        wclear(this->textWindow);
        mvwprintw(this->textWindow, 0, 0, "Please enter a run number");
        wrefresh(this->textWindow);
        this->drawPersistentMessage();
        this->drawCommandInProgress();
        wrefresh(this->messageWindow);
        //here we check if a key was pressed
        if((inChar = getch()) != ERR) //if we get ERR then no key was pressed in the period
        {
            this->handleSetRunNumKeyPress(inChar);
        }
        //now sleep for a bit
        boost::this_thread::sleep_for(this->refreshPeriod);
    }
    wclear(this->messageWindow);
}

void UIThread::setRunName()
{
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Getting Run Name";
    //run number to that and the sequence number to zero
    int inChar;
    this->runSubLoop = true;
    while(runSubLoop)
    {
        //here we draw the screen
        wclear(this->textWindow);
        mvwprintw(this->textWindow, 0, 0, "Please enter a run title");
        wrefresh(this->textWindow);
        this->drawPersistentMessage();
        this->drawCommandInProgress();
        wrefresh(this->messageWindow);
        //here we check if a key was pressed
        if((inChar = getch()) != ERR) //if we get ERR then no key was pressed in the period
        {
            this->handleSetRunTitleKeyPress(inChar);
        }
        //now pause for a bit
        boost::this_thread::sleep_for(this->refreshPeriod);
    }
    wclear(this->messageWindow);
}


void UIThread::setRunParams()
{
    this->setRunName();
    this->setRunNumber();
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Setting All Run Info";
    //TODO: call run changing code
    while(!this->fileControl->setNewRunParameters(this->tempRunTitle, this->tempRunNumber))
    {//failed at setting the new parameters, try again in a moment
        boost::this_thread::sleep_for(this->refreshPeriod);
    }
    BOOST_LOG_SEV(this->lg, Information) << "UI Thread: Run Info Set";
}

}
