#ifndef CLI_HANDLER_H
#define CLI_HANDLER_H

int getcliarg(int argc, char** argv, const char* options, \
        const char* flags, std::vector<std::string> &optout,\
        bool* flagout);

#endif
