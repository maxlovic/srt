#include <stdio.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <signal.h>
#include <mutex>

#include "apputil.hpp"
#include "utilities.h"
#include "logsupport.hpp"
#include "srt_messaging.h"


using namespace std;


volatile bool int_state = false;


void OnINT_ForceExit(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;
    const int undelivered = srt_msgn_wait_undelievered(3000);
    if (undelivered)
    {
        cerr << "ERROR: Still have undelivered bytes " << undelivered << "\n";
        if (undelivered == -1)
            cerr << srt_msgn_getlasterror_str() << "\n";
    }
    srt_msgn_destroy();
}



void receive_message(const char *uri, size_t msg_size, bool reply, bool printmsg)
{
    ::cout << "Listen to " << uri << "\n";

    const size_t &message_size = msg_size;
    if (0 != srt_msgn_listen(uri, message_size))
    {
        cerr << "ERROR: Listen failed.\n";

        srt_msgn_destroy();
        return;
    }

    vector<char> message_rcvd(message_size);

    while (!int_state)
    {
        int connection_id = 0;
        const int recv_res = srt_msgn_recv(message_rcvd.data(), message_rcvd.size(), &connection_id);

        if (recv_res == 0)
        {
            continue;
        }
        else if (recv_res < 0)
        {
            cerr << "ERROR: Receiving message. Result: " << recv_res;
            cerr << " on conn ID " << connection_id << "\n";
            cerr << srt_msgn_getlasterror_str() << endl;

            srt_msgn_destroy();
            return;
        }

        if (printmsg)
        {
            ::cout << "RECEIVED MESSAGE length " << recv_res << " on conn ID " << connection_id;
            if (recv_res < 50)
            {
                ::cout << ":\n";
                ::cout << string(message_rcvd.data(), recv_res).c_str();
            }
            else if (message_rcvd[0] >= '0' && message_rcvd[0] <= 'z')
            {
                ::cout << " (first character):";
                ::cout << message_rcvd[0];
            }
            ::cout << endl;
        }

        if (reply)
        {
            const string out_message("Message received");
            const int send_res = srt_msgn_send_on_conn(out_message.data(), out_message.size(), connection_id);
            if (send_res <= 0)
            {
                cerr << "ERROR: Sending reply message. Result: " << send_res;
                cerr << " on conn ID " << connection_id << "\n";
                cerr << srt_msgn_getlasterror_str() << endl;

                srt_msgn_destroy();
                return;
            }
            if (printmsg)
                ::cout << "Reply sent on conn ID " << connection_id << "\n";
        }
    }

    if (int_state)
    {
        cerr << "\n (interrupted on request)\n";
    }

    srt_msgn_destroy();
}



void send_message(const char *uri, const char* message, size_t length,
    size_t msg_size, bool reply, const bool printmsg, int target_bitrate_bps,
    int num_repeats)
{
    ::cout << "Connect to " << uri << "\n";
    const size_t message_size = msg_size;
    if (-1 == srt_msgn_connect(uri, message_size))
    {
        cerr << "ERROR: Connect failed.\n";
        srt_msgn_destroy();
        return;
    }

    const int num_messages = num_repeats;

    auto rcvth = reply ? std::thread([&message_size, &num_messages, &printmsg]
    {
        vector<char> message_rcv(message_size);

        for (int i = 0; i < num_messages + 1; ++i)
        {
            if (int_state)
                break;

            if (printmsg)
                ::cout << "WAITING FOR MESSAGE no." << i << "\n";
            const int rcv_res = srt_msgn_recv(message_rcv.data(), message_rcv.size(), nullptr);
            if (rcv_res <= 0)
            {
                cerr << "ERROR: Receiving message. Result: " << rcv_res << "\n";
                cerr << srt_msgn_getlasterror_str() << endl;
                srt_msgn_destroy();
                return;
            }

            if (printmsg)
            {
                ::cout << "RECEIVED MESSAGE no." << i << ":\n";
                ::cout << string(message_rcv.data(), rcv_res).c_str() << endl;
            }
        }
    }) : std::thread();

    if (length > 0)
    {
        int sent_res = srt_msgn_send(message, length);
        if (sent_res != (int)length)
        {
            cerr << "ERROR: Sending message " << length << ". Result: " << sent_res << "\n";
            cerr << srt_msgn_getlasterror_str() << endl;
            srt_msgn_destroy();
            return;
        }

        if (printmsg)
        {
            ::cout << "SENT MESSAGE:\n";
            ::cout << message << endl;
        }
    }

    const long msgs_per_s = static_cast<long long>(target_bitrate_bps / 8) / message_size;
    const long msg_interval_us = 1000000 / msgs_per_s;

    vector<char> message_to_send(message_size);
    char c = 0;
    for (size_t i = 0; i < message_to_send.size(); ++i)
    {
        message_to_send[i] = c++;
    }

    auto time_prev = chrono::steady_clock::now();
    long time_dev_us = 0;

    for (int i = 0; i < num_messages; ++i)
    {
        if (int_state)
            break;

        if (target_bitrate_bps)
        {
            const long duration_us = time_dev_us > msg_interval_us ? 0 : (msg_interval_us - time_dev_us);
            const auto next_time = time_prev + chrono::microseconds(duration_us);
            chrono::time_point<chrono::steady_clock> time_now;
            for (;;)
            {
                time_now = chrono::steady_clock::now();
                if (time_now >= next_time)
                    break;
                if (int_state)
                    break;
            }

            time_dev_us += (long) chrono::duration_cast<chrono::microseconds>(time_now - time_prev).count() - msg_interval_us;
            time_prev = time_now;
        }
        if (int_state)
            break;

        message_to_send[0] = '0' + i % 74;
        const int res = srt_msgn_send(message_to_send.data(), message_to_send.size());
        if (res != (int)message_size)
        {
            cerr << "ERROR: Sending " << message_size << ", sent " << res << "\n";
            cerr << srt_msgn_getlasterror_str() << endl;
            break;
        }
        if (printmsg)
            ::cout << "SENT MESSAGE #" << i << "\n";
    }

    if (rcvth.joinable())
        rcvth.join();

    const int undelivered = srt_msgn_wait_undelievered(5000);
    if (undelivered)
    {
        cerr << "ERROR: Still have undelivered bytes " << undelivered << "\n";
    }

    int_state = true;

    srt_msgn_destroy();
}


void print_help()
{
    cout << "The CLI syntax for the two peers test is\n"
         << "    Send:    srt-test-messaging \"srt://ip:port\" \"message\"\n"
         << "    Receive: srt-test-messaging \"srt://ip:port\"\n";
}


int main(int argc, char** argv)
{
    // Check options
    vector<OptionScheme> optargs = {
        { {"reply"},          OptionScheme::ARG_ONE },
        { {"printmsg"},       OptionScheme::ARG_ONE },
        { {"ll", "loglevel"}, OptionScheme::ARG_ONE },
        { {"statsfile"},      OptionScheme::ARG_ONE },
        { {"statsfreq"},      OptionScheme::ARG_ONE },
        { {"repeat"},         OptionScheme::ARG_ONE },
        { {"bitrate"},        OptionScheme::ARG_ONE },
        { {"msgsize"},        OptionScheme::ARG_ONE },
    };
    map<string, vector<string>> params = ProcessOptions(argv, argc, optargs);

    if (params.count("-help") || params.count("-h"))
    {
        print_help();
        return 1;
    }

    if (params[""].empty())
    {
        print_help();
        return 1;
    }

    if (params[""].size() > 2)
    {
        cerr << "Extra parameter after the first one: " << Printable(params[""]) << endl;
        print_help();
        return 1;
    }

    signal(SIGINT,  OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);


    const string loglevel = Option<OutString>(params, "error", "ll", "loglevel");
    srt_logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    srt_msgn_set_loglevel(lev, params.count("v") != 0);

    const bool reply    = stoi(Option<OutString>(params, "0", "reply")) != 0;
    const bool printmsg = stoi(Option<OutString>(params, "1", "printmsg")) != 0;
    const int bitrate   = stoi(Option<OutString>(params, "0", "bitrate"));
    const int msg_size  = stoi(Option<OutString>(params, "8388608", "msgsize"));    // 8 MB

    if (params[""].size() == 1)
    {
        receive_message(params[""][0].c_str(), msg_size, reply, printmsg);
        return 0;
    }


    if (params[""][1].size() > msg_size)
    {
        cerr << "ERROR. Message size exceeds maximum size of " << msg_size << endl;
        return 1;
    }

    const int repeat = stoi(Option<OutString>(params, "60", "repeat"));

    send_message(params[""][0].c_str(), params[""][1].c_str(), params[""][1].size(), msg_size,
        reply, printmsg, bitrate, repeat);

    return 0;
}

