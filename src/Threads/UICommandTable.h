/***************************************************************************//**
********************************************************************************
**
** @file UICommandTable.h
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
** @details Holds the command enumeration list and the map to strings to enums
**
********************************************************************************
*******************************************************************************/
#ifndef ORCHID_SRC_THREADS_UICOMMANDTABLE_H
#define ORCHID_SRC_THREADS_UICOMMANDTABLE_H

// includes for C system headers
// includes for C++ system headers
#include<map>
#include<string>
// includes from other libraries
#include<boost/assign/list_of.hpp>
using boost::assign::map_list_of;
// includes from ORCHID
namespace Threads
{

//The various enumerated commands
enum class UICommands : char {TurnOn,       TurnOff,    Start,      Stop,
                              ChangeRun,    ChangeNum,  Next,       Quit,
                              Invalid,      RunTests,   Unavailable};

//A lookup between string and the command
static const std::map<std::string, UICommands> UI_COMMAND_DISPATCH =
    map_list_of("exit", UICommands::Quit)
        ("quit",      UICommands::Quit)
        ("nope",      UICommands::Quit)
        ("turnon",    UICommands::TurnOn)
        ("dotests",   UICommands::RunTests)
        ("turnoff",   UICommands::TurnOff)
        ("start",     UICommands::Start)
        ("stop",      UICommands::Stop)
        ("changerun", UICommands::ChangeRun)
        ("runnumber", UICommands::ChangeNum)
        ("next",      UICommands::Next);

//A lookup for valid commands while in init mode
static const std::map<UICommands, bool> INIT_MODE_VALID_CMD =
    map_list_of(UICommands::Quit, true)
        (UICommands::TurnOn,    true)
        (UICommands::RunTests,  false)
        (UICommands::TurnOff,   false)
        (UICommands::Start,     false)
        (UICommands::Stop,      false)
        (UICommands::ChangeRun, false)
        (UICommands::ChangeNum, false)
        (UICommands::Next,      false);

//A lookup for valid commands while in idle mode
static const std::map<UICommands, bool> IDLE_MODE_VALID_CMD =
    map_list_of(UICommands::Quit, true)
        (UICommands::TurnOn,    false)
        (UICommands::RunTests,  true)
        (UICommands::TurnOff,   true)
        (UICommands::Start,     true)
        (UICommands::Stop,      false)
        (UICommands::ChangeRun, true)
        (UICommands::ChangeNum, true)
        (UICommands::Next,      true);

//A lookup for valid commands while in run mode
static const std::map<UICommands, bool> RUN_MODE_VALID_CMD =
    map_list_of(UICommands::Quit, true)
        (UICommands::TurnOn,    false)
        (UICommands::TurnOff,   false)
        (UICommands::Start,     false)
        (UICommands::RunTests,  false)
        (UICommands::Stop,      true)
        (UICommands::ChangeRun, false)
        (UICommands::ChangeNum, false)
        (UICommands::Next, false);
}

#endif //ORCHID_SRC_THREADS_UICOMMANDTABLE_H
