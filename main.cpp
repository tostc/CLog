#include <iostream>
#include <iomanip>

#define CLOG_IMPLEMENTATION
#include "Log.hpp"

using namespace std;

int main(int argc, char const *argv[])
{
    llog.LogToFile("Test.log");
    llog.EnableDebugLog(true);
    llog.ShowLogLevel(5);
    llog << llevel(5) << ldebug << "Test " << lbin << (unsigned long)0xF << string(" Test ") << true << lhex << 0xff << lendl;
    llog << lerror << "Test" << lendl;
    llog << linfo << "Test" << lendl;
    llog << lwarning << "Test" << lendl;
    llog << ltag("Custom Tag") << "Test" << lendl;

    return 0;
}
