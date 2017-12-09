#include <sstream>
#include "./colorize.hpp"

#ifndef INC_LOGGING
#define INC_LOGGING

std::string ffatal(std::string m){
    std::stringstream ss;
    ss << bg_red << yellow << " " << m << " " << def << bg_def;
    return ss.str();
}

std::string ftrivial(std::string m){
    std::stringstream ss;
    ss << red << m << def;
    return ss.str();
}

#endif