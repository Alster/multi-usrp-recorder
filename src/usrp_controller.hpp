#include <string>

#include <uhd/utils/thread.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>

#include "../lib/readerwriterqueue/atomicops.h"
#include "../lib/readerwriterqueue/readerwriterqueue.h"

#include "colorize.hpp"
#include "sink/base.hpp"
#include "sink/memcopy.hpp"
#include "sink/file.hpp"
#include "sink/file_ab_mode.hpp"
#include "sink/file_rotator.hpp"
#include "sink/null.hpp"
#include "buffer_wrapper.hpp"
#include "config.hpp"
#include "interrupt.hpp"
#include "logging.hpp"
#include "utils.hpp"

#ifndef INC_USRP_CONTROLLER
#define INC_USRP_CONTROLLER

boost::posix_time::ptime startup_time = boost::posix_time::microsec_clock::local_time();

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
        for (int i = 0; i < QUEUE_SIZE; i++) queue_to_read.enqueue(new BufferWrapper<DATA_TYPE>(BUFFER_SIZE));
//        for (int i = 0; i < QUEUE_SIZE; i++) queue_to_read.enqueue(new BufferWrapper<DATA_TYPE>(this->rate));

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

    BufferWrapper<DATA_TYPE> *active_read_buffer;
    BufferWrapper<DATA_TYPE> *active_write_buffer;
    moodycamel::ReaderWriterQueue<BufferWrapper<DATA_TYPE> *> queue_to_read;
    moodycamel::ReaderWriterQueue<BufferWrapper<DATA_TYPE> *> queue_to_write;

    uhd::rx_metadata_t md;
    bool overflow_message = true;

    std::string log_prepend;

    SinkBase * sink;
};

#endif