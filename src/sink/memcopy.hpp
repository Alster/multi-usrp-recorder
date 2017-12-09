#include <iostream>
#include <stdio.h>
#include <vector>
#include "./base.hpp"

#ifndef INC_SINK_MEMCOPY
#define INC_SINK_MEMCOPY

class SinkMemCopy: public SinkBase {
public:
    SinkMemCopy(std::string file_name) : bank(0) {
    }

    void write(const char *__s, std::streamsize __n) {
        make_copy(__s, __n);
        make_copy(__s, __n);
        make_copy(__s, __n);
        make_copy(__s, __n);
//        boost::this_thread::sleep(boost::posix_time::milliseconds(500));
    }

    bool is_open() {
        return true;
    }

    void close() {
    }

    std::string get_full_file_name() {
        return "noop";
    }

private:
    std::vector<char *> bank;

    void make_copy(const char *__s, std::streamsize __n){
        char * b;
        b = new char[__n + 1];
        memcpy(b, __s, __n);
        delete b;
    }
};

#endif