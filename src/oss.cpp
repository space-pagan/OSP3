 /* Author: Zoya Samsonov
  * Date: October 6, 2020
  */

#include <iostream>              //cout
#include <string>                //string
#include <cstring>               //strcmp()
#include <unistd.h>              //alarm()
#include <csignal>               //signal()
#include <vector>                //vector
#include "cli_handler.h"         //getcliarg()
#include "child_handler.h"       //forkexec(), updatechildcount()
                                 //    waitforanychild(), killallchildren()
#include "error_handler.h"       //setupprefix(), perrquit(), 
                                 //    custerrhelpprompt()
#include "shm_handler.h"         //shmfromfile(), semcreate(), semunlockall()
                                 //    ipc_cleanup()
#include "help_handler.h"        //printhelp()
#include "file_handler.h"        //add_outfile_append(), writeline()
#include "sys_clk.h"             //struct clk

// variables used in interrupt handling
volatile bool earlyquit = false;
volatile bool handlinginterrupt = false;
volatile int quittype = 0;

// set flags so program can quit gracefully
void signalhandler(int signum) {
    if (signum == SIGALRM || signum == SIGINT) {
        earlyquit = true;
        quittype = signum;
    }
}

// ignore nested interrupts, only need to quit once :)
void earlyquithandler() {
    if (!handlinginterrupt) {
        handlinginterrupt = true;
        killallchildren(); //child_handler.h
        // print message to indicate reason for termination
        if (quittype == SIGINT) {
            std::cerr << "SIGINT received! Terminating...\n";
        } else if (quittype == SIGALRM) {
            std::cerr << "Timeout limit exceeded! Terminating...\n";
        }
    }
}

// tests and modifies parsed cli arguments and flags. If all correct, 
// returns to main(), otherwise exit().
void testopts(int argc, char** argv, int optind, int& conc, \
              const char* logfile, int max_time, bool* flags) {
    // print help message and quit
    if (flags[0]) {
        printhelp(argv[0]);
        exit(0);
    }

    if (!strcmp(logfile, "")) custerrhelpprompt(
            "-l FILE is a required argument!");
    if (argc > optind+1) custerrhelpprompt(
            "Unknown argument '" + std::string(argv[optind]) + "'");
    // inappropriate values for -c, -t
    if (conc < 1) custerrhelpprompt(
            "option -s must be an integer greater than 0");
    if (conc > 20) {
        std::cout << "This system does not allow more than 20 concurrent";
        std::cout << " processes. Option -c has been set to 20.\n";
        conc = 20;
    }
    if (max_time < 1) custerrhelpprompt(
            "option -t must be an integer greater than 0");
}

void main_loop(int conc, const char* logfile) {
    int max_count = 0;    //count of children created
    int conc_count = 0;   //count of currently running children
    int currID = 0;       //value of next unused ftok id
    int logid = add_outfile_append(logfile); //log stream id

    // create shared clock
    clk* shclk = (clk*)shmcreate(sizeof(clk), currID);
    // create shm object for children to put their PID before terminating
    int* shmPID = (int*)shmcreate(sizeof(int), currID);
    msgcreate(currID);    //instantiate message queue
    int msqid = currID-1; //save the id for future messaging

    *shmPID = 0;          //ensure this is zeroed before creating children
    msgsend(msqid);       //critical section unlocked

    while (max_count < 100 && shclk->tofloat() < 2) {
        // check if interrupt happened
        if (earlyquit) {
            earlyquithandler();
            break; // stop spawning children
        }
        if (conc_count < conc) {
            int pid = forkexec("oss_child", conc_count);
            writeline(logid, "Master: Creating new child pid " +\
                    std::to_string(pid) + " at my time " + shclk->tostring());
            max_count++;
        }
        // check if any children have exited
        if (*shmPID != 0) {
            writeline(logid, "Master: Child pid " + std::to_string(*shmPID) +\
                    " is terminating at system clock time "+shclk->tostring());
            waitforchildpid(*shmPID, conc_count);
            *shmPID = 0;
            msgsend(msqid);
        }
        shclk->inc(2200);
    }
    // reached termination conditions. Kill all children and quit.
    while (conc_count > 0) {
        killallchildren();
        updatechildcount(conc_count); //waitforanychild was hanging
    }

    std::cout << "Terminated after running " << max_count << " threads...\n";

    // release all shared memory created
    ipc_cleanup();
}

int main(int argc, char **argv) {
    // register SIGINT (^C) and SIGALRM with signal handler
    signal(SIGINT, signalhandler);
    signal(SIGALRM, signalhandler);
    // set perror to display the correct program name
    setupprefix(argv[0]);

    // parse runtime arguments
    std::vector<std::string> opts{"5", "", "20"};
    bool flags[1] = {false};   // only -h flag for this program
    int optind = getcliarg(argc, argv, "clt", "h", opts, flags);

    // local variables
    // parsed variables and flags are stored in opts and flags arrays
    int conc = std::stoi(opts[0]);
    const char* logfile = opts[1].c_str(); // needs to be -l FILE
    int max_time = std::stoi(opts[2]);
    // this line will terminate the program if any options are mis-set
    testopts(argc, argv, optind, conc, logfile,  max_time, flags);
    // set up kill timer
    //alarm(max_time);
    main_loop(conc, logfile);

    return 0;
}
