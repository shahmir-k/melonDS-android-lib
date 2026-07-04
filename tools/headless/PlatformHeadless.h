/*
    liteDS-v2 headless harness - Platform backend helpers.
*/

#pragma once

#include <string>

namespace HeadlessHost
{
// Sets the directory used to resolve "local" files (firmware/save/wifi settings).
// Created if it does not exist. Default is "./headless-data".
void SetDataDir(const std::string& dir);
const std::string& GetDataDir();
}
