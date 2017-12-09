#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include "./base.hpp"

#ifndef INC_SINK_FILE_AB_MODE
#define INC_SINK_FILE_AB_MODE

std::vector<char> AB_MODE = std::vector<char>();

class SinkFileABMode: public SinkBase {
public:
    SinkFileABMode(std::string fn) {
        file_name = std::vector<std::string>(AB_MODE.size());
        outfile = std::vector<std::ofstream>(AB_MODE.size());
        for (int i = 0; i < AB_MODE.size(); i++){
            std::stringstream ss;
            ss << fn << "_" << AB_MODE[i];
            file_name[i] = ss.str();

            std::cout << "Opening " << file_name[i].c_str() << ".." << std::endl;
            outfile[i].open(file_name[i].c_str(), std::ofstream::binary);
            std::cout << file_name[i] << " is OPENED" << std::endl;
        }
        std::cout << "All opened" << std::endl;
    }

    void write(const char *__s, std::streamsize __n) {
        outfile[current_state].write(__s, __n);
        outfile[current_state].flush();

        current_state++;
        if (current_state == AB_MODE.size()) current_state = 0;
    }

    bool is_open() {
        return true;
    }

    void close() {
        for (int i = 0; i < AB_MODE.size(); i++) outfile[current_state].close();
    }

    std::string get_full_file_name() {
        return "noop";
    }

private:
    uint current_state = 0;
    std::vector<std::string> file_name;
    std::vector<std::ofstream> outfile;
};

#endif