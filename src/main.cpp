#include <string>
#include <loguru.hpp>
#include <workflow/WFHttpServer.h>
#include "server.h"
#include "common.h"

int main(int argc, char ** argv)
{
    std::string config_path = "/home/r/Scripts/C++/GLCC_Server/configs/config.json";
    loguru::init(argc, argv);
    Json::Value config_root; Json::Reader reader;

    std::ifstream ifs;
    ifs.open(config_path);
    auto ret = reader.parse(ifs, config_root);
    if (!ret) {
        perror("Parse the log failed");
        exit(1);
    }
    Json::Value log_root = config_root["Log"]; 
    std::string log_dir = log_root["log_dir"].asString();
    std::string log_file_time_format = log_root["log_file_time_format"].asString();
    std::string log_add_file_verbosity_s = log_root["log_add_file_verbosity"].asString();
    std::string log_all_file_verbosity_s = log_root["log_all_file_verbosity"].asString();

    std::string all_log_file = log_dir + "/" + "all.log"; 
    std::string local_log_file = log_dir + "/" + GLCC::get_now_time(log_file_time_format) + ".log";

    ret = loguru::create_directories(log_dir.c_str());
    if (!ret) {
        perror("Create the log dir failed");
        exit(1);
    }
    loguru::add_file(all_log_file.c_str(), loguru::Append, loguru::get_verbosity_from_name(log_all_file_verbosity_s.c_str()));
    loguru::add_file(local_log_file.c_str(), loguru::Truncate, loguru::get_verbosity_from_name(log_add_file_verbosity_s.c_str()));;
    GLCC::GLCCServer server{config_path};
    if (server.server_state == -1) {
        LOG_F(INFO, "Init GLCCServer fail!");
        return -1;
    }
    ret = server.run();
    if (server.server_state == -1) {
        LOG_F(INFO, "Run GLCCServer fail!");
        return -1;
    }
    return 0;
}
