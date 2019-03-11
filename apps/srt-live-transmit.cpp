/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

// NOTE: This application uses C++11.

// This program uses quite a simple architecture, which is mainly related to
// the way how it's invoked: stransmit <source> <target> (plus options).
//
// The media for <source> and <target> are filled by abstract classes
// named Source and Target respectively. Most important virtuals to
// be filled by the derived classes are Source::Read and Target::Write.
//
// For SRT please take a look at the SrtCommon class first. This contains
// everything that is needed for creating an SRT medium, that is, making
// a connection as listener, as caller, and as rendezvous. The listener
// and caller modes are built upon the same philosophy as those for
// BSD/POSIX socket API (bind/listen/accept or connect).
//
// The instance class is selected per details in the URI (usually scheme)
// and then this URI is used to configure the medium object. Medium-specific
// options are specified in the URI: SCHEME://HOST:PORT?opt1=val1&opt2=val2 etc.
//
// Options for connection are set by ConfigurePre and ConfigurePost.
// This is a philosophy that exists also in BSD/POSIX sockets, just not
// officially mentioned:
// - The "PRE" options must be set prior to connecting and can't be altered
//   on a connected socket, however if set on a listening socket, they are
//   derived by accept-ed socket. 
// - The "POST" options can be altered any time on a connected socket.
//   They MAY have also some meaning when set prior to connecting; such
//   option is SRTO_RCVSYN, which makes connect/accept call asynchronous.
//   Because of that this option is treated special way in this app.
//
// See 'srt_options' global variable (common/socketoptions.hpp) for a list of
// all options.

// MSVS likes to complain about lots of standard C functions being unsafe.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#define REQUIRE_CXX11 1

#include <cctype>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <list>

#include "apputil.hpp"  // CreateAddrInet
#include "argtable3.h"
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "transmitbase.hpp"
#include "verbose.hpp"

// NOTE: This is without "haisrt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <srt/srt.h>
#include <srt.h>
#include <udt.h> // This TEMPORARILY contains extra C++-only SRT API.
#include <logging.h>

using namespace std;

map<string,string> g_options;

string Option(string deflt="") { return deflt; }

template <class... Args>
string Option(string deflt, string key, Args... further_keys)
{
    map<string, string>::iterator i = g_options.find(key);
    if ( i == g_options.end() )
        return Option(deflt, further_keys...);
    return i->second;
}

struct ForcedExit: public std::runtime_error
{
    ForcedExit(const std::string& arg):
        std::runtime_error(arg)
    {
    }
};

struct AlarmExit: public std::runtime_error
{
    AlarmExit(const std::string& arg):
        std::runtime_error(arg)
    {
    }
};

volatile bool int_state = false;
volatile bool timer_state = false;
void OnINT_ForceExit(int)
{
    Verb() << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;
}

void OnAlarm_Interrupt(int)
{
    Verb() << "\n---------- INTERRUPT ON TIMEOUT!\n";

    int_state = false; // JIC
    timer_state = true;

    if ((false))
    {
        throw AlarmExit("Watchdog bites hangup");
    }
}

extern "C" void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message);



struct Config
{
    int timeout = 0;
    int chunk_size = SRT_LIVE_DEF_PLSIZE;
    bool quiet  = false;
    srt_logging::LogLevel::type loglevel = srt_logging::LogLevel::error;
    set<srt_logging::LogFA> logfas;
    string logfile;
    int bw_report = 0;
    int stats_report = 0;
    string stats_out;
    PrintFormat stats_pf;
    bool auto_reconnect = true;
    bool full_stats = false;

    string source;
    string target;
};



///
/// @return    0 on success, 1 on failure, 2 on help output
int parse_args(Config &cfg, int argc, char** argv)
{
    /*
        cerr << "Usage: " << argv[0] << " [options] <input-uri> <output-uri>\n";
        cerr << "\t-t:<timeout=0> - exit timer in seconds\n";
        cerr << "\t-c:<chunk=1316> - max size of data read in one step\n";
        X cerr << "\t-b:<bandwidth> - set SRT bandwidth\n";
        cerr << "\t-r:<report-frequency=0> - bandwidth report frequency\n";
        cerr << "\t-s:<stats-report-freq=0> - frequency of status report\n";
        cerr << "\t-pf:<format> - printformat (json or default)\n";
        cerr << "\t-statsreport:<filename> - stats report file name (cout for output to cout, or a filename)\n";
        cerr << "\t-f - full counters in stats-report (prints total statistics)\n";
        cerr << "\t-q - quiet mode (default no)\n";
        cerr << "\t-v - verbose mode (default no)\n";
        
        cerr << "\t-a - auto-reconnect mode, default yes, -a:no to disable\n";
    */

    /* Define the allowable command line options, collecting them in argtable[] */
    arg_int *tout        = arg_int0("t", "to,timeout", "<sec>", "exit timer in seconds");
    arg_int *chunk       = arg_int0("c", "chunk", "<bytes>", "max size of data read in one step (default 1316)");
    arg_int *bwreport    = arg_int0("r", "report,bandwidth-report,bitrate-report", "<every N packets>", "bandwidth report frequency");
    arg_int *statsrep    = arg_int0("s", "stats,stats-report-frequency", "<every N packets>", "status report frequency");
    arg_str *statsout    = arg_str0(NULL, "statsout", "<output>", "stats report output (cout, cerr or a filename)");
    arg_str *statspf     = arg_str0(NULL, "statspf", "<default|json|csv>", "stats report print format");
    arg_lit *statsfull   = arg_lit0("f", "fullstats", "full counters in stats-report (prints total statistics)");
    arg_lit *version     = arg_lit0(NULL, "version", "print version information and exit");
    arg_str *loglevel    = arg_str0(NULL, "ll,loglevel", "<error|note|debug>", "log level");
    arg_str *logfa       = arg_str0(NULL, "logfa", "<general,bstats,control,data,tsbpd,rexmit,all>", "log functional area");
    //arg_lit *loginternal = arg_lit0(NULL, "loginternal", "Use default SRT logging output");
    arg_str *logfile     = arg_str0(NULL, "logfile", "<filename>", "output SRT log to file");
    arg_lit *help        = arg_lit0("h", "help", "print this help and exit");
    arg_lit *verb        = arg_lit0("v", "verbose", "verbose output (default off)");
    arg_lit *quiet       = arg_lit0("q", "quiet", "quiet mode (default no)");
    arg_str *autorecon   = arg_str0("a", "auto,autoreconnect", "<on|off>", "auto-reconnect mode, default yes, -a:no to disable");
    arg_str *source      = arg_strn(NULL, NULL, "<source>", 1, 1, "input");
    arg_str *target      = arg_strn(NULL, NULL, "<target>", 1, 1, "output");
    struct arg_end *end   = arg_end(20);

    void* argtable[] = { tout, chunk, bwreport, statsrep, statsout, statspf, version, loglevel, logfa, logfile,
                         help, verb, quiet, autorecon, source, target, end };


    struct ArgtableCleanup
    {
        void** argtable = nullptr;

        ~ArgtableCleanup()
        {
            arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        }
    } cleanupobj = { argtable };


    const char* progname = "srt-live-transmit";
    int nerrors;

    /* verify the argtable[] entries were allocated sucessfully */
    if (arg_nullcheck(argtable) != 0)
    {
        /* NULL entries were detected, some allocations must have failed */
        printf("%s: insufficient memory\n", progname);
        //arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return EXIT_FAILURE;
    }

    /* Parse the command line as defined by argtable[] */
    nerrors = arg_parse(argc, argv, argtable);

    /* special case: '--help' takes precedence over error reporting */
    if (help->count > 0)
    {
        printf("Usage: %s", progname);
        arg_print_syntaxv(stdout, argtable, "\n");
        printf("SRT sample application to transmit live streaming.\n\n");
        arg_print_glossary(stdout, argtable, "  %-10s %s\n");
        //arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 2;
    }

    if (version->count > 0)
    {
        printf("SRT Library version: %s\n", SRT_VERSION);
        return 2;
    }

    /* If the parser returned any errors then display them and exit */
    if (nerrors > 0)
    {
        /* Display the error details contained in the arg_end struct.*/
        arg_print_errors(stdout, end, progname);
        printf("Try '%s --help' for more information.\n", progname);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return EXIT_FAILURE;
    }

    // Now map the arguments to configuration
    if (tout->count > 0)
        cfg.timeout = tout->ival[0];

    if (chunk->count > 0)
        cfg.chunk_size = chunk->ival[0];

    if (loglevel->count > 0)
        cfg.loglevel = SrtParseLogLevel(loglevel->sval[0]);

    if (logfa->count > 0)
        cfg.logfas = SrtParseLogFA(logfa->sval[0]);

    if (logfile->count > 0)
        cfg.logfile = logfile->sval[0];

    if (quiet->count > 0)
        cfg.quiet = true;

    if (bwreport->count > 0)
        cfg.bw_report = bwreport->ival[0];

    if (statsrep->count > 0)
        cfg.stats_report = statsrep->ival[0];

    if (statsout->count > 0)
        cfg.stats_out = statsout->sval[0];

    if (statspf->count > 0)
    {
        const string pf = statspf->sval[0];
        if (pf == "json")
        {
            cfg.stats_pf = PRINT_FORMAT_JSON;
        }
        if (pf == "csv")
        {
            cfg.stats_pf = PRINT_FORMAT_CSV;
        }
        else if (pf != "default")
        {
            cfg.stats_pf = PRINT_FORMAT_2COLS;
            cerr << "ERROR: Unsupported print format: " << pf << endl;
            arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
            return 1;
        }
    }

    if (verb->count > 0)
        Verbose::on = !cfg.quiet;

    if (autorecon->count > 0)
        cfg.auto_reconnect = string(autorecon->sval[0]) != "no";

    if (statsfull->count > 0)
        cfg.full_stats = true;

    cfg.source = source->sval[0];
    cfg.target = target->sval[0];
    cout << source->sval[0] << endl;
    cout << target->sval[0] << endl;

    return 0;
}



int main( int argc, char** argv )
{
    // This is mainly required on Windows to initialize the network system,
    // for a case when the instance would use UDP. SRT does it on its own, independently.
    if ( !SysInitializeNetwork() )
        throw std::runtime_error("Can't initialize network!");

    // Symmetrically, this does a cleanup; put into a local destructor to ensure that
    // it's called regardless of how this function returns.
    struct NetworkCleanup
    {
        ~NetworkCleanup()
        {
            SysCleanupNetwork();
        }
    } cleanupobj;


    Config cfg;
    const int parse_ret =  parse_args(cfg, argc, argv);
    if (parse_ret != 0)
        return parse_ret == 1 ? EXIT_FAILURE : 0;

    //
    // Set global config variables
    //
    if (cfg.chunk_size != SRT_LIVE_DEF_PLSIZE)
        transmit_chunk_size = cfg.chunk_size;
    printformat = cfg.stats_pf;
    transmit_bw_report    = cfg.bw_report;
    transmit_stats_report = cfg.stats_report;
    transmit_total_stats  = cfg.full_stats;

    //
    // Set SRT log levels and functional areas
    //
    srt_setloglevel(cfg.loglevel);
    for (set<srt_logging::LogFA>::iterator i = cfg.logfas.begin(); i != cfg.logfas.end(); ++i)
        srt_addlogfa(*i);

    //
    // SRT log handler
    //
    std::ofstream logfile_stream; // leave unused if not set
    char NAME[] = "SRTLIB";
    if (cfg.logfile.empty())
    {
        srt_setlogflags( 0
                | SRT_LOGF_DISABLE_TIME
                | SRT_LOGF_DISABLE_SEVERITY
                | SRT_LOGF_DISABLE_THREADNAME
                | SRT_LOGF_DISABLE_EOL
                );
        srt_setloghandler(NAME, TestLogHandler);
    }
    else
    {
        logfile_stream.open(cfg.logfile.c_str());
        if (!logfile_stream)
        {
            cerr << "ERROR: Can't open '" << cfg.logfile.c_str() << "' for writing - fallback to cerr\n";
        }
        else
        {
            UDT::setlogstream(logfile_stream);
        }
    }


    //
    // SRT stats output
    //
    std::ofstream logfile_stats; // leave unused if not set
    if (cfg.stats_out != "" && cfg.stats_out != "cout")
    {
        logfile_stats.open(cfg.stats_out.c_str());
        if (!logfile_stats)
        {
            cerr << "ERROR: Can't open '" << cfg.stats_out << "' for writing stats. Fallback to cout.\n";
            logfile_stats.close();
        }
    }

    ostream &out_stats = logfile_stats.is_open() ? logfile_stats : cout;


#ifdef _WIN32

    if (cfg.timeout > 0)
    {
        cerr << "ERROR: The -timeout option (-t) is not implemented on Windows\n";
        return EXIT_FAILURE;
    }

#else
    if (cfg.timeout > 0)
    {
        signal(SIGALRM, OnAlarm_Interrupt);
        if (!quiet)
            cerr << "TIMEOUT: will interrupt after " << cfg.timeout << "s\n";
        alarm(cfg.timeout);
    }
#endif
    signal(SIGINT,  OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);


    if (!cfg.quiet)
    {
        cerr << "Media path: '"
            << cfg.source
            << "' --> '"
            << cfg.target
            << "'\n";
    }

    unique_ptr<Source> src;
    bool srcConnected = false;
    unique_ptr<Target> tar;
    bool tarConnected = false;

    int pollid = srt_epoll_create();
    if ( pollid < 0 )
    {
        cerr << "Can't initialize epoll";
        return 1;
    }

    size_t receivedBytes = 0;
    size_t wroteBytes = 0;
    size_t lostBytes = 0;
    size_t lastReportedtLostBytes = 0;
    std::time_t writeErrorLogTimer(std::time(nullptr));

    try {
        // Now loop until broken
        while (!int_state && !timer_state)
        {
            if (!src.get())
            {
                src = Source::Create(cfg.source);
                if (!src.get())
                {
                    cerr << "Unsupported source type" << endl;
                    return 1;
                }
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                switch(src->uri.type())
                {
                case UriParser::SRT:
                    if (srt_epoll_add_usock(pollid,
                            src->GetSRTSocket(), &events))
                    {
                        cerr << "Failed to add SRT source to poll, "
                            << src->GetSRTSocket() << endl;
                        return 1;
                    }
                    break;
                case UriParser::UDP:
                    if (srt_epoll_add_ssock(pollid,
                            src->GetSysSocket(), &events))
                    {
                        cerr << "Failed to add UDP source to poll, "
                            << src->GetSysSocket() << endl;
                        return 1;
                    }
                    break;
                case UriParser::FILE:
                    if (srt_epoll_add_ssock(pollid,
                            src->GetSysSocket(), &events))
                    {
                        cerr << "Failed to add FILE source to poll, "
                            << src->GetSysSocket() << endl;
                        return 1;
                    }
                    break;
                default:
                    break;
                }

                receivedBytes = 0;
            }

            if (!tar.get())
            {
                tar = Target::Create(cfg.target);
                if (!tar.get())
                {
                    cerr << "Unsupported target type" << endl;
                    return 1;
                }

                // IN because we care for state transitions only
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                switch(tar->uri.type())
                {
                case UriParser::SRT:
                    if (srt_epoll_add_usock(pollid,
                            tar->GetSRTSocket(), &events))
                    {
                        cerr << "Failed to add SRT destination to poll, "
                            << tar->GetSRTSocket() << endl;
                        return 1;
                    }
                    break;
                default:
                    break;
                }

                wroteBytes = 0;
                lostBytes = 0;
                lastReportedtLostBytes = 0;
            }

            int srtrfdslen = 2;
            SRTSOCKET srtrfds[2];
            int sysrfdslen = 2;
            SYSSOCKET sysrfds[2];
            if (srt_epoll_wait(pollid,
                &srtrfds[0], &srtrfdslen, 0, 0,
                100,
                &sysrfds[0], &sysrfdslen, 0, 0) >= 0)
            {
                if ((false))
                {
                    cerr << "Event:"
                        << " srtrfdslen " << srtrfdslen
                        << " sysrfdslen " << sysrfdslen
                        << endl;
                }

                bool doabort = false;
                for (int i = 0; i < srtrfdslen; i++)
                {
                    bool issource = false;
                    SRTSOCKET s = srtrfds[i];
                    if (src->GetSRTSocket() == s)
                    {
                        issource = true;
                    }
                    else if (tar->GetSRTSocket() != s)
                    {
                        cerr << "Unexpected socket poll: " << s;
                        doabort = true;
                        break;
                    }

                    const char * dirstring = (issource)? "source" : "target";

                    SRT_SOCKSTATUS status = srt_getsockstate(s);
                    if ((false) && status != SRTS_CONNECTED)
                    {
                        cerr << dirstring << " status " << status << endl;
                    }
                    switch (status)
                    {
                        case SRTS_LISTENING:
                        {
                            const bool res = (issource) ?
                                src->AcceptNewClient() : tar->AcceptNewClient();
                            if (!res)
                            {
                                cerr << "Failed to accept SRT connection"
                                    << endl;
                                doabort = true;
                                break;
                            }

                            srt_epoll_remove_usock(pollid, s);

                            SRTSOCKET ns = (issource) ?
                                src->GetSRTSocket() : tar->GetSRTSocket();
                            int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                            if (srt_epoll_add_usock(pollid, ns, &events))
                            {
                                cerr << "Failed to add SRT client to poll, "
                                    << ns << endl;
                                doabort = true;
                            }
                            else
                            {
                                if (!cfg.quiet)
                                {
                                    cerr << "Accepted SRT "
                                        << dirstring
                                        <<  " connection"
                                        << endl;
                                }
                                if (issource)
                                    srcConnected = true;
                                else
                                    tarConnected = true;
                            }
                        }
                        break;
                        case SRTS_BROKEN:
                        case SRTS_NONEXIST:
                        case SRTS_CLOSED:
                        {
                            if (issource)
                            {
                                if (srcConnected)
                                {
                                    if (!cfg.quiet)
                                    {
                                        cerr << "SRT source disconnected"
                                            << endl;
                                    }
                                    srcConnected = false;
                                }
                            }
                            else if (tarConnected)
                            {
                                if (!cfg.quiet)
                                    cerr << "SRT target disconnected" << endl;
                                tarConnected = false;
                            }

                            if(!cfg.auto_reconnect)
                            {
                                doabort = true;
                            }
                            else
                            {
                                // force re-connection
                                srt_epoll_remove_usock(pollid, s);
                                if (issource)
                                    src.reset();
                                else
                                    tar.reset();
                            }
                        }
                        break;
                        case SRTS_CONNECTED:
                        {
                            if (issource)
                            {
                                if (!srcConnected)
                                {
                                    if (!cfg.quiet)
                                        cerr << "SRT source connected" << endl;
                                    srcConnected = true;
                                }
                            }
                            else if (!tarConnected)
                            {
                                if (!cfg.quiet)
                                    cerr << "SRT target connected" << endl;
                                tarConnected = true;
                            }
                        }

                        default:
                        {
                            // No-Op
                        }
                        break;
                    }
                }

                if (doabort)
                {
                    break;
                }

                // read a few chunks at a time in attempt to deplete
                // read buffers as much as possible on each read event
                // note that this implies live streams and does not
                // work for cached/file sources
                std::list<std::shared_ptr<bytevector>> dataqueue;
                if (src.get() && (srtrfdslen || sysrfdslen))
                {
                    while (dataqueue.size() < 10)
                    {
                        std::shared_ptr<bytevector> pdata(
                            new bytevector(cfg.chunk_size));
                        if (!src->Read(cfg.chunk_size, *pdata, out_stats) || (*pdata).empty())
                        {
                            break;
                        }
                        dataqueue.push_back(pdata);
                        receivedBytes += (*pdata).size();
                    }
                }

                // if no target, let received data fall to the floor
                while (!dataqueue.empty())
                {
                    std::shared_ptr<bytevector> pdata = dataqueue.front();
                    if (!tar.get() || !tar->IsOpen()) {
                        lostBytes += (*pdata).size();
                    } else if (!tar->Write(pdata->data(), pdata->size(), out_stats)) {
                        lostBytes += (*pdata).size();
                    } else
                        wroteBytes += (*pdata).size();

                    dataqueue.pop_front();
                }

                if (!cfg.quiet && (lastReportedtLostBytes != lostBytes))
                {
                    std::time_t now(std::time(nullptr));
                    if (std::difftime(now, writeErrorLogTimer) >= 5.0)
                    {
                        cerr << lostBytes << " bytes lost, "
                            << wroteBytes << " bytes sent, "
                            << receivedBytes << " bytes received"
                            << endl;
                        writeErrorLogTimer = now;
                        lastReportedtLostBytes = lostBytes;
                    }
                }
            }
        }
    }
    catch (std::exception& x)
    {
        cerr << "ERROR: " << x.what() << endl;
        return 255;
    }

    return 0;
}

// Class utilities


void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message)
{
    char prefix[100] = "";
    if ( opaque )
        strncpy(prefix, (char*)opaque, 99);
    time_t now;
    time(&now);
    char buf[1024];
    struct tm local = SysLocalTime(now);
    size_t pos = strftime(buf, 1024, "[%c ", &local);

#ifdef _MSC_VER
    // That's something weird that happens on Microsoft Visual Studio 2013
    // Trying to keep portability, while every version of MSVS is a different plaform.
    // On MSVS 2015 there's already a standard-compliant snprintf, whereas _snprintf
    // is available on backward compatibility and it doesn't work exactly the same way.
#define snprintf _snprintf
#endif
    snprintf(buf+pos, 1024-pos, "%s:%d(%s)]{%d} %s", file, line, area, level, message);

    cerr << buf << endl;
}

