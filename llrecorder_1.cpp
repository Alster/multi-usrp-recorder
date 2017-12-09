#define HAVE_PTHREAD_SETNAME

#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <string>
#include <csignal>
#include <future>
#include <sys/stat.h>
#include <fcntl.h>
#include <boost/variant/detail/substitute.hpp>
#include <atomic>
#include "./lib/readerwriterqueue/atomicops.h"
#include "./lib/readerwriterqueue/readerwriterqueue.h"
#include <boost/algorithm/string/split.hpp>
#include <gnuradio/types.h>

//region Settings
#define USE_NULL_SINK false
#define CAPTURE_ONE_SECOND false
//endregion

namespace po = boost::program_options;
namespace fs = boost::filesystem;
using namespace moodycamel;

boost::posix_time::ptime startup_time = boost::posix_time::microsec_clock::local_time();

//region Colorize
namespace Color {
    enum Code {
        FG_RED = 31,
        FG_GREEN = 32,
        FG_BLUE = 34,
        FG_DEFAULT = 39,
        FG_YELLOW = 93,
        BG_RED = 41,
        BG_GREEN = 42,
        BG_BLUE = 44,
        BG_DEFAULT = 49,
        S_BOLD = 1,
        S_DEFAULT = 0
    };

    class Modifier {
        Code code;
    public:
        Modifier(Code pCode) : code(pCode) {}

        friend std::ostream &
        operator<<(std::ostream &os, const Modifier &mod) {
            return os << "\033[" << mod.code << "m";
        }
    };
}

Color::Modifier red(Color::FG_RED);
Color::Modifier green(Color::FG_GREEN);
Color::Modifier blue(Color::FG_BLUE);
Color::Modifier def(Color::S_DEFAULT);
Color::Modifier bold(Color::S_BOLD);
Color::Modifier yellow(Color::FG_YELLOW);
Color::Modifier bg_red(Color::BG_RED);
Color::Modifier bg_def(Color::BG_DEFAULT);

//endregion

typedef float DATA_TYPE;
uint QUEUE_SIZE = 4;
uint WRITE_SLEEP_TIME = 200;
uint BUFFER_SIZE = 1e6;
uint RECV_FRAME_SIZE = 6000;//4000
//default 16
uint NUM_RECV_FRAMES = 128;//500
bool VERBOSE = false;
std::vector<char> AB_MODE = std::vector<char>();

static bool stop_signal_called = false;

void sig_int_handler(int) {
    std::cout << blue << "SIG INT HANDLED" << def << std::endl;
    stop_signal_called = true;
    std::exit(EXIT_SUCCESS);
}

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

//region Sinks
class SinkBase {
public:
    virtual void write(const char *, std::streamsize) = 0;

    virtual std::string get_full_file_name() = 0;

    virtual bool is_open() = 0;

    virtual void close() = 0;
};

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

class SinkNull: public SinkBase {
public:
    SinkNull(std::string file_name) {
    }

    void write(const char *__s, std::streamsize __n) {
    }

    bool is_open() {
        return true;
    }

    void close() {
    }

    std::string get_full_file_name() {
        return "NULL SINK";
    }
};
//endregion

class BufferWrapper {
public:
    BufferWrapper(size_t buf_size) : buffer(buf_size) {}

    //TODO: Сделать просто массивом, а то оно походу не просто выделяет память, а создает кучу комплексных чисел
    std::vector<std::complex<DATA_TYPE>> buffer;
    size_t samples_num;
};

unsigned long getThreadId(){
    std::string threadId = boost::lexical_cast<std::string>(boost::this_thread::get_id());
    unsigned long threadNumber = 0;
    sscanf(threadId.c_str(), "%lx", &threadNumber);
    return threadNumber;
}

//void uhd::set_thread_name(
//        boost::thread *thrd,
//        const std::string &name
//) {
//#ifdef HAVE_PTHREAD_SETNAME
//    pthread_setname_np(thrd->native_handle(), name.substr(0,16).c_str());
//#endif /* HAVE_PTHREAD_SETNAME */
//#ifdef HAVE_THREAD_SETNAME_DUMMY
//    UHD_LOG_DEBUG("UHD", "Setting thread name is not implemented; wanted to set to " << name);
//#endif /* HAVE_THREAD_SETNAME_DUMMY */
//}


class USRPController {
public:
    uhd::usrp::multi_usrp::sptr device;

    USRPController(std::string serial, double rate, double freq, double gain, std::string file_name) :
            serial(serial), rate(rate), freq(freq), gain(gain), queue_to_read(QUEUE_SIZE),
            queue_to_write(QUEUE_SIZE), writes_count(0), total_samples_written(0) {

        BUFFER_SIZE = rate;

        std::cout << "create sink" << std::endl;

#if USE_NULL_SINK == true
        sink = new SinkNull(file_name);
#else
        if (AB_MODE.size() > 0) sink = new SinkFileABMode(file_name);
        else sink = new SinkFile(file_name);
#endif
        std::cout << "create sink DONE" << std::endl;
        //region Log prefix
        std::ostringstream string_stream;
        string_stream << def << bold << "[DEV " << blue << serial << "] " << def;
        log_prepend = string_stream.str();

        std::cout << "DEBUG: accessfile() called by process " << ::getpid() << " (parent: " << ::getppid() << ")" << std::endl;
        std::cout << "Thread id is " << getThreadId() << std::endl;

        std::stringstream ss_create_log;
        ss_create_log
                << log_prepend
                << "Creating..."
                << std::endl
        ;
        std::cout << ss_create_log.str();

        std::stringstream ss_init_log;
        ss_init_log
                << log_prepend
                << "Initialized"
                << std::endl
                ;
        //endregion

        //region Setup device
        /*
         * defaults
         * max_num_samps 2725
         * 16384 > 4092
         */
//        this->device = uhd::usrp::multi_usrp::make("serial=" + serial);
//        std::string devArgs = str(
//                boost::format("serial=%s,recv_frame_size=%s,num_recv_frames=%s")
//                % serial
//                % 4000//4 | 2   > 4000
//                % 500//2 | 4   > 500
//        );
        //5 devs 20 mhz
        std::string devArgs = str(
                boost::format("serial=%s,recv_frame_size=%s,num_recv_frames=%s")
//                boost::format("serial=%s")
                % serial
                % RECV_FRAME_SIZE//min 4000 max 8000
//                //min - ValueError: bad vrt header or packet fragment
//                //max - overflow
                % NUM_RECV_FRAMES
        );

        ss_init_log << "Dev args " << devArgs << std::endl;
        std::cout << "Make" << std::endl;
        this->device = uhd::usrp::multi_usrp::make(devArgs);
        std::cout << "set time source" << std::endl;
        this->device->set_time_source("external");
//        this->device->set_rx_subdev_spec(std::string("A:A"));

        std::cout << "tune" << std::endl;
        uhd::tune_request_t tune_request(this->freq);
        this->device->set_rx_freq(tune_request);
        this->device->set_rx_gain(this->gain);
        this->device->set_rx_rate(this->rate);
        this->device->set_rx_bandwidth(this->rate);
        this->device->set_rx_antenna("RX2");

        std::cout << "start lo locking" << std::endl;
        while (not this->device->get_rx_sensor("lo_locked").to_bool())
            boost::this_thread::sleep(boost::posix_time::milliseconds(1));
        std::cout << "lo locked" << std::endl;

        std::string cpu_format = "fc32";
        //TODO: Попробовать sc32
        std::string wire_format = "sc16";

        uhd::stream_args_t stream_args(cpu_format, wire_format);
        this->rx_stream = this->device->get_rx_stream(stream_args);

        ss_init_log << "max_num_samps " << this->rx_stream->get_max_num_samps() << std::endl;

        std::cout << "initialize buffers" << std::endl;
        for (int i = 0; i < QUEUE_SIZE; i++) queue_to_read.enqueue(new BufferWrapper(BUFFER_SIZE));
//        for (int i = 0; i < QUEUE_SIZE; i++) queue_to_read.enqueue(new BufferWrapper(this->rate));

        prev_write_time = boost::posix_time::microsec_clock::local_time();

        std::cout << ss_init_log.str();
    }

    void schedule_stream(){
        int cmd_time_seconds = command_execution_time_suka.get_full_secs();
        int device_time_seconds = this->device->get_time_now().get_full_secs();
        std::stringstream ss;
        if (device_time_seconds >= cmd_time_seconds){
            ss
                    << ffatal("Late command")
                    << std::endl
                    ;
            stop_signal_called = true;
        }
        ss << str(
                boost::format("Schedule command for %3i, now %i")
                % cmd_time_seconds
                % device_time_seconds
            )
            ;
        log_debug(ss.str());
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_MORE);
        stream_cmd.num_samps = size_t(BUFFER_SIZE);
        stream_cmd.stream_now = false;
        stream_cmd.time_spec = uhd::time_spec_t(command_execution_time_suka);
        this->rx_stream->issue_stream_cmd(stream_cmd);
        command_execution_time_suka = command_execution_time_suka + uhd::time_spec_t(1.0);
    }

    void start(uhd::time_spec_t cmd_time) {
        command_execution_time_suka = cmd_time;
        // for (int i = 0; i < 64; i++) schedule_stream();
    }

    void stream_runner(){
        for (int i = 0; i < 8; i++) schedule_stream();
        while (not stop_signal_called) {
            schedule_stream();
            boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
        }
    }

    void read_thread() {
//        uhd::set_thread_name(boost::this_thread::, "Reading");
        uhd::set_thread_priority_safe(1.0, true);
        while (not stop_signal_called) {
            if (queue_to_read.size_approx() == 0){
                log_important(ffatal("All buffers are full"));
                stop_signal_called = true;
                return;
            }
            boost::posix_time::ptime before = boost::posix_time::microsec_clock::local_time();

            if (not queue_to_read.try_dequeue(active_read_buffer)) {
                continue;
            }

            boost::posix_time::ptime after = boost::posix_time::microsec_clock::local_time();

            log_debug(str(
                boost::format("Dequeue %i5i ms, buffs to write %2i, buffs to recv %2i")
                % (after.time_of_day().total_milliseconds() - before.time_of_day().total_milliseconds())
                % queue_to_write.size_approx() 
                % queue_to_read.size_approx() 
                ));

            active_read_buffer->samples_num = this->rx_stream->recv(
                    &active_read_buffer->buffer.front(),
                    BUFFER_SIZE, md, 3000.0,
                    false
            );
            log_debug(md.to_pp_string(false));
            if (md.fragment_offset == 0) log_debug(ftrivial("Frag is 0"));

            if (active_read_buffer->samples_num != 0 && active_read_buffer->samples_num != BUFFER_SIZE){
                log_debug(str(boost::format("wtf: received %i5 samples") % active_read_buffer->samples_num));
            }
            //region Catch errors
            if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
                if (md.out_of_sequence){
                    log_important(ftrivial("!!! out_of_sequence"));
                }
                if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_OVERFLOW){
                    std::stringstream err_ss;
                    err_ss
                            << ffatal(md.strerror()) << std::endl
//                            << "  Buffers: " << queue_to_read.size_approx() << " > " << queue_to_write.size_approx() << std::endl
                            << "\t" << queue_to_read.size_approx() << " free buffers" << std::endl
                            << "\tWritten " << writes_count << " times" << std::endl
                            << "\t" << active_read_buffer->samples_num << " samples left" << std::endl
                            << "\tDevice time: " << (this->device->get_time_now().get_full_secs()) << " seconds from start" << std::endl
                            ;
                    log_important(err_ss.str());
                    // stop_signal_called = true;
                    // return;
                }
                else {
                    log_important(ftrivial(md.strerror()));
                }
                queue_to_read.enqueue(active_read_buffer);
                continue;
            }
            else {
                if (active_read_buffer->samples_num != 0 && active_read_buffer->samples_num != BUFFER_SIZE){
                    log_debug(str(boost::format("Received %5i samples, but no error!") % active_read_buffer->samples_num));
                }
            }
            if (active_read_buffer->samples_num != BUFFER_SIZE) {
                active_read_buffer->samples_num = BUFFER_SIZE;
                log_debug("Write fake data");
                for (int i = active_read_buffer->samples_num; i < BUFFER_SIZE; i++) {
                    active_read_buffer->buffer[i] = 0;
                }
            }
            //endregion
            queue_to_write.enqueue(active_read_buffer);
            // schedule_stream();
        }
    }

    void write_thread() {
//        uhd::set_thread_priority_safe(1.0, true);
//        uhd::set_thread_name(boost::this_thread, "Writing");
        while (not stop_signal_called) {
            if (queue_to_write.size_approx() == 0) {
                boost::this_thread::sleep(boost::posix_time::milliseconds(WRITE_SLEEP_TIME));
                continue;
            }
            active_write_buffer = *queue_to_write.peek();
            if (not queue_to_write.pop()) {
                log_important(ffatal("Cannot pop write queue"));
                stop_signal_called = true;
                return;
            }
            boost::posix_time::ptime write_start = boost::posix_time::microsec_clock::local_time();
            int q_size = int(queue_to_write.size_approx()) + 1;
            write(active_write_buffer->buffer, active_write_buffer->samples_num);
            queue_to_read.enqueue(active_write_buffer);

            boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();

            writes_count++;

            if (VERBOSE){
                boost::posix_time::ptime current_time_bitch = boost::posix_time::microsec_clock::local_time();
                log_verbose(str(boost::format("Wait %5i ms, write %5i ms, buffers: free %2i, busy %2i. Freq %5i, samples written %11i, seconds %9i")
                        % (writes_count > 1 ? (write_start.time_of_day().total_milliseconds() - prev_write_time.time_of_day().total_milliseconds()) : -1)
                        % (now.time_of_day().total_milliseconds() - write_start.time_of_day().total_milliseconds())
                        % queue_to_read.size_approx()
                        % queue_to_write.size_approx()
                        % this->freq
                        % total_samples_written
                        % (current_time_bitch.time_of_day().total_seconds() - startup_time.time_of_day().total_seconds())
                ));
            }

            prev_write_time = now;
        }
    }

    void log_important(std::string s){
        std::stringstream ss;
        ss << log_prepend << s << std::endl;
        std::cerr << ss.str();
        // fprintf(stderr, "%s %s", log_prepend, s);
    }

    void log_verbose(std::string s){
        if (!VERBOSE) return;
        std::stringstream ss;
        ss << log_prepend << s << std::endl;
        std::cerr << ss.str();
        // if (VERBOSE) fprintf(stderr, "%s %s", log_prepend, s);
    }

    void log_debug(std::string s){
        // std::stringstream ss;
        // ss << log_prepend << s << std::endl;
        // std::cerr << ss.str();
        // fprintf(stderr, "%s %s", log_prepend, s);
    }

    void write(const std::vector<std::complex<DATA_TYPE>>& buff, size_t samples_num) {
//        std::cerr << log_prepend << samples_num << std::endl;
        if (samples_num != BUFFER_SIZE) log_important("Strange fucked shit");
        total_samples_written += samples_num;
        if (sink->is_open()) {
            sink->write((const char *) &buff.front(), samples_num * sizeof(std::complex<DATA_TYPE>));
        }
    }

    uhd::time_spec_t command_execution_time_suka;

    uint total_samples_written;

    boost::posix_time::ptime prev_write_time;
    uhd::rx_streamer::sptr rx_stream;

    uint writes_count;

private:
    std::string serial;
    double rate;
    double freq;
    double gain;

    BufferWrapper *active_read_buffer;
    BufferWrapper *active_write_buffer;
    ReaderWriterQueue<BufferWrapper *> queue_to_read;
    ReaderWriterQueue<BufferWrapper *> queue_to_write;

    uhd::rx_metadata_t md;
    bool overflow_message = true;

    std::string log_prepend;

    SinkBase * sink;
};

std::vector<std::string> device_serials = {};

std::vector<USRPController *> devices;

void create_device(
    std::string serial,
    double rate,
    double freq,
    double gain,
    std::string file_name
) {
    std::cout << "createdStart creating..." << std::endl;
    USRPController *device = new USRPController(serial, rate, freq, gain, file_name);
    devices.push_back(device);
    std::cout << "created!" << std::endl;
}

struct BandOpt {
    float sample_rate;
    float freq;
    fs::path targetFile;
};

int UHD_SAFE_MAIN(int argc, char *argv[]) {
    ////////////////////////////////////////////////////////////////////
    // Hi
    ////////////////////////////////////////////////////////////////////
    std::ostringstream string_stream;
    string_stream << bold << blue << "[LL Recorder" << def << "] ";
    std::string log_prepend = string_stream.str();

    uhd::set_thread_priority_safe();

//    std::cout << "gr complex " << sizeof(gr_complex) << std::endl;
//    std::cout << "std complex " << sizeof(std::complex<float>) << std::endl;
//    std::cout << "float " << sizeof(float) << std::endl;
    uhd::device_addrs_t device_addrs = uhd::device::find(std::string());
    for (auto it = device_addrs.begin(); it != device_addrs.end(); ++it) {
        std::string serial = (*it)["serial"];
        std::cout << log_prepend << "FOUND device " << serial << std::endl;
        device_serials.push_back(serial);
    }

    //region Play with gsm_header
//// uint8_t version;	/* version, set to GSMTAP_VERSION */
//    std::cout << sizeof(uint8_t) << std::endl;
//// uint8_t hdr_len;	/* length in number of 32bit words */
//    std::cout << sizeof(uint8_t) << std::endl;
//// uint8_t type;		/* see GSMTAP_TYPE_* */
//    std::cout << sizeof(uint8_t) << std::endl;
//// uint8_t timeslot;	/* timeslot (0..7 on Um) */
//    std::cout << sizeof(uint8_t) << std::endl;
////
//// uint16_t arfcn;		/* ARFCN (frequency) */
//    std::cout << sizeof(uint16_t) << std::endl;
//// int8_t signal_dbm;	/* signal level in dBm */
//    std::cout << sizeof(int8_t) << std::endl;
//// int8_t snr_db;		/* signal/noise ratio in dB */
//    std::cout << sizeof(int8_t) << std::endl;
////
//// uint32_t frame_number;	/* GSM Frame Number (FN) */
//    std::cout << sizeof(uint32_t) << std::endl;
////
//// uint8_t sub_type;	/* Type of burst/channel, see above */
//    std::cout << sizeof(int8_t) << std::endl;
//// uint8_t antenna_nr;	/* Antenna Number */
//    std::cout << sizeof(int8_t) << std::endl;
//// uint8_t sub_slot;	/* sub-slot within timeslot */
//    std::cout << sizeof(int8_t) << std::endl;
//// uint8_t res;		/* reserved for future use (RFU) */
//    std::cout << sizeof(int8_t) << std::endl;
//
    //endregion
//    return 0;

    std::signal(SIGINT, &sig_int_handler);
    std::signal(SIGHUP, &sig_int_handler);

    //region Parse options
    po::options_description desc("LLRecorder");

    std::string _dir;
    std::string _ab_mode;
    desc.add_options()
            ("opt", po::value<std::vector<std::string> >()->multitoken(), "description")
            ("dir", po::value<std::string>(&_dir)->default_value("./"), "name of the folder to write binary samples to")
            ("queue_size", po::value<uint>(&QUEUE_SIZE)->default_value(QUEUE_SIZE), "Queue size")
//            ("buffer_size", po::value<uint>(&BUFFER_SIZE)->default_value(BUFFER_SIZE), "Buffer size")
            ("recv_frame_size", po::value<uint>(&RECV_FRAME_SIZE)->default_value(RECV_FRAME_SIZE), "Recv frame size")
            ("num_recv_frames", po::value<uint>(&NUM_RECV_FRAMES)->default_value(NUM_RECV_FRAMES), "Num recv frames")
            ("verbose", po::value<bool>(&VERBOSE)->default_value(VERBOSE), "Print additional shit")
            ("ab_mode", po::value<std::string>(&_ab_mode)->default_value(""), "AB mode. Example: --ab_mode A:B:C")
            ("write_sleep", po::value<uint>(&WRITE_SLEEP_TIME)->default_value(WRITE_SLEEP_TIME), "Write sleep (milliseconds)");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);


    if (_ab_mode.size() != 0) {
        std::vector<std::string> _ab_mode_strs;
        boost::split(_ab_mode_strs, _ab_mode, boost::is_any_of(":"));
        for (auto s: _ab_mode_strs) {
            if (s.size() != 1) {
                throw std::runtime_error(
                        "AB Mode error: '" + s + "' is invalid option. Only one symbol allowed, azaza.");
            }
            AB_MODE.push_back(s[0]);
        }
    }

    fs::path targetFolder = fs::path(_dir);
//    if (targetFolder.exs) {
//        throw std::runtime_error("Invalid opt dir: " + _dir);
//    }

    std::cout << log_prepend << "Trager folder is " << targetFolder << std::endl;

    std::vector<std::string> opts;
    if (vm["opt"].empty() || (opts = vm["opt"].as<std::vector<std::string> >()).size() == 0) {
        throw std::runtime_error("No opt provided");
    }

    if (opts.size() > device_serials.size()){
        throw std::runtime_error("Devices is not enough");
    }
    //endregion

    //regionDevice creation
    boost::thread_group create_threads_group;
    double gain(32);

    bool get_serial_from_opt = true;
    auto splitted_opts = std::vector<std::vector<std::string>>();
    for (std::string dev_opt : opts) {
        std::vector<std::string> strs;
        boost::split(strs, dev_opt, boost::is_any_of(":"));
        if (strs.size() != 4) get_serial_from_opt = false;
        if (!get_serial_from_opt && strs.size() != 3) {
            std::stringstream ss_err;
            ss_err << boost::format("Invalid option '%s', parts count: %s")
                      % dev_opt
                      % strs.size();
            throw std::runtime_error(ss_err.str());
        }
        splitted_opts.push_back(strs);
    }

    int device_serial_iterator = 0;
    for (auto strs : splitted_opts) {
        BandOpt bandOpt = BandOpt{stof(strs[0]), stof(strs[1]), fs::path(strs[2])};
        std::cout << log_prepend <<
                  boost::format("BandOpt freq %f, rate %f, file name %s")
                  % bandOpt.freq
                  % bandOpt.sample_rate
                  % bandOpt.targetFile
                  << std::endl;
        auto cr_th = create_threads_group.create_thread(boost::bind(
                &create_device,
                get_serial_from_opt ? strs[3] : device_serials[device_serial_iterator],
                bandOpt.sample_rate,
                bandOpt.freq,
                gain,
                (targetFolder / bandOpt.targetFile).string()
        ));
        uhd::set_thread_name(cr_th, "Creating");
        device_serial_iterator++;
    }

    std::cout << "wait for device creating..." << std::endl;

    create_threads_group.join_all();
//    create_threads_group.interrupt_all();
    std::cout << create_threads_group.size() << std::endl;

    std::cout << "all devices created" << std::endl;

    USRPController *common_dev = devices[0];
    uhd::time_spec_t clock_time = common_dev->device->get_time_now() + uhd::time_spec_t(1.0);
    for (USRPController *device : devices) {
        device->device->set_time_next_pps(uhd::time_spec_t(0.0));
    }

    boost::this_thread::sleep(boost::posix_time::seconds(2));

    uhd::time_spec_t start_timeout = common_dev->device->get_time_now() + uhd::time_spec_t(2.0);

    for (USRPController *device : devices) {
        device->start(start_timeout);
    }

    boost::thread_group work_threads_group;

    for (USRPController *device : devices) {
        auto th_r = work_threads_group.create_thread(boost::bind(&USRPController::read_thread, device));
        uhd::set_thread_name(th_r, "Reader");
        auto th_w = work_threads_group.create_thread(boost::bind(&USRPController::write_thread, device));
        uhd::set_thread_name(th_w, "Writer");
        auto th_sch = work_threads_group.create_thread(boost::bind(&USRPController::stream_runner, device));
        uhd::set_thread_name(th_sch, "Stream scheduler");
    }

    work_threads_group.join_all();
    //endregion

    return EXIT_SUCCESS;
}