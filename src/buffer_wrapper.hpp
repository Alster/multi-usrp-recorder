#include <vector>
#include <string>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>

#ifndef INC_BUFFER_WRAPPER
#define INC_BUFFER_WRAPPER

template <class TBase>
class BufferWrapper {
public:
    BufferWrapper(size_t buf_size) : buffer(buf_size) {}

    //TODO: Сделать просто массивом, а то оно походу не просто выделяет память, а создает кучу комплексных чисел
    std::vector<std::complex<TBase>> buffer;
    size_t samples_num;
};

unsigned long getThreadId(){
    std::string threadId = boost::lexical_cast<std::string>(boost::this_thread::get_id());
    unsigned long threadNumber = 0;
    sscanf(threadId.c_str(), "%lx", &threadNumber);
    return threadNumber;
}

#endif