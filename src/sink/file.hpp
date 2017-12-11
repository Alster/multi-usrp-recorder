#include <fstream>
#include <iostream>
#include "./base.hpp"

#ifndef INC_SINK_FILE
#define INC_SINK_FILE

// struct WFTask{
//     const char *buf;
//     std::streamsize size;
// }

// class SinkFile: public SinkBase {
// public:
//     SinkFile(std::string file_name) : file_name(file_name) {
//         pFile = fopen(file_name.c_str(), "wb");
//         std::cout << file_name << " is OPENED" << std::endl;
//     }

//     void write(const char *__s, std::streamsize __n) {
//         fwrite(__s, 1, __n, pFile);
//         // file.flush();
//     }

//     bool is_open() {
//         // return file.is_open();
//         return true;
//     }

//     void close() {
//         fclose(pFile);
//     }

//     std::string get_full_file_name() {
//         return full_file_name;
//     }

// private:
//     std::string file_name;
//     std::string full_file_name;
//     FILE* pFile;
//     std::vector 
// };

class SinkFile: public SinkBase {
public:
    SinkFile(std::string file_name) : file_name(file_name) {
        file.open(file_name.c_str(), std::ios::out | std::ios::binary);
        std::cout << file_name << " is OPENED" << std::endl;
    }

    void write(const char *__s, std::streamsize __n) {
        // std::cerr << __n << std::endl;
        file.write(__s, __n);
        // file.flush();
    }

    bool is_open() {
        return file.is_open();
    }

    void close() {
        file.close();
    }

    std::string get_full_file_name() {
        return full_file_name;
    }

private:
    std::string file_name;
    std::string full_file_name;
    std::ofstream file;
};

#endif