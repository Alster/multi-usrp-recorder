#include <fstream>
#include "./base.hpp"

#ifndef INC_SINK_FILE
#define INC_SINK_FILE

class SinkFile: public SinkBase {
public:
    SinkFile(std::string file_name) : file_name(file_name) {
        outfile.open(file_name.c_str(), std::ofstream::binary);
        std::cout << file_name << " is OPENED" << std::endl;
    }

    void write(const char *__s, std::streamsize __n) {
        outfile.write(__s, __n);
        outfile.flush();
    }

    bool is_open() {
        return outfile.is_open();
    }

    void close() {
        outfile.close();
    }

    std::string get_full_file_name() {
        return full_file_name;
    }

private:
    std::string file_name;
    std::string full_file_name;
    std::ofstream outfile;
};

#endif