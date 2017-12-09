#ifndef INC_CONFIG
#define INC_CONFIG

#define LLRECORDER_VERSION "1.0"

typedef float DATA_TYPE;
uint QUEUE_SIZE = 4;
uint WRITE_SLEEP_TIME = 200;
uint BUFFER_SIZE = 1e6;
uint RECV_FRAME_SIZE = 6000;//4000
//default 16
uint NUM_RECV_FRAMES = 128;//500
bool VERBOSE = false;

#endif