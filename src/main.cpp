#include <string>
#include <loguru.hpp>
#include <workflow/WFHttpServer.h>
#include "server.h"
#include "common.h"

int main(int argc, char ** argv)
{
    loguru::init(argc, argv);
    const char * log_path = "./logs";
    int ret = GLCC::check_dir(log_path, true);
    CHECK_F(ret != -1, "create Log Path Fail!");
    char glcc_server_latest_log[512] = {0};
    CHECK_F(GLCC::get_time_file("server_latest", "./logs/glcc_", ".log",  glcc_server_latest_log) != -1, 
        "get time lateest log fail");
    loguru::add_file("./logs/glcc_server_all.log", loguru::Append, loguru::Verbosity_MAX);
    loguru::add_file(glcc_server_latest_log, loguru::Truncate, loguru::Verbosity_MAX);
    std::string config_path = "/home/r/Scripts/C++/New_GLCC_Server/configs/config.json";
    GLCC::GLCCServer server{config_path};
    server.run();
    return 0;
}