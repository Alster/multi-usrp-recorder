#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include "./base.hpp"

#ifndef INC_SINK_FILE_ROTATOR
#define INC_SINK_FILE_ROTATOR

class SinkFileRotator: public SinkBase {
public:
    SinkFileRotator(std::string file_name) : file_name(file_name), counter(0) {
        std::cout << file_name << " is OPENED" << std::endl;
    }

    void write(const char *__s, std::streamsize __n) {
        std::stringstream ss;
        ss << file_name << "." << counter;
        std::cout << ss.str() << std::endl;
        std::ofstream outfile;
        outfile.open(ss.str(), std::ofstream::binary);
        outfile.write(__s, __n);
        outfile.flush();
        outfile.close();
        counter++;

        uint offset = 4;
        if ((counter - offset) >= 0){
            std::stringstream ss_rem;
            ss_rem << file_name << "." << (counter - offset);
            std::remove(ss_rem.str().c_str());
        }
    }

    bool is_open() {
        return true;
    }

    void close() {

    }

    std::string get_full_file_name() {
        return full_file_name;
    }

private:
    std::string file_name;
    uint counter;
    std::string full_file_name;
};

#endif