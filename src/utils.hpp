#include <string>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>

#ifndef INC_UTILS
#define INC_UTILS

unsigned long getThreadId(){
    std::string threadId = boost::lexical_cast<std::string>(boost::this_thread::get_id());
    unsigned long threadNumber = 0;
    sscanf(threadId.c_str(), "%lx", &threadNumber);
    return threadNumber;
}

#endif