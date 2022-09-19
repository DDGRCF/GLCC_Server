#include <string>
#include <loguru.hpp>
#include <workflow/WFHttpServer.h>
#include "server.h"
#include "common.h"

int main(int argc, char ** argv)
{
    std::string config_path = argv[1];
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
    const std::string log_dir = log_root["log_dir"].asString();
    const std::string log_file_time_format = log_root["log_file_time_format"].asString();
    const std::string log_add_file_verbosity_s = log_root["log_add_file_verbosity"].asString();
    const std::string log_all_file_verbosity_s = log_root["log_all_file_verbosity"].asString();

    const std::string all_log_file = log_dir + "/" + "all.log"; 
    const std::string local_log_file = log_dir + "/" + GLCC::get_now_time(log_file_time_format) + ".log";

    ret = loguru::create_directories(log_dir.c_str());
    if (!ret) {
        perror("Create the log dir failed");
        exit(1);
    }
    loguru::add_file(all_log_file.c_str(), loguru::Append, loguru::get_verbosity_from_name(log_all_file_verbosity_s.c_str()));
    loguru::add_file(local_log_file.c_str(), loguru::Truncate, loguru::get_verbosity_from_name(log_add_file_verbosity_s.c_str()));;

    Json::Value livego_root = config_root["LiveGo"];
    const int camera_push_port = livego_root["camera_push_port"].asInt();
    const int dect_push_port = livego_root["dect_push_port"].asInt();
    const int state_check_port = livego_root["state_check_port"].asInt();
    GLCC::constants::video_path_template = "rtsp://" + GLCC::constants::localhost \
        + ":" + std::to_string(camera_push_port) + "/live/%s";
    GLCC::constants::livego_push_url_template = "rtmp://" + GLCC::constants::localhost \
        + ":" + std::to_string(dect_push_port) + "/live/%s";
    GLCC::constants::livego_check_stat_template = "http://" + GLCC::constants::localhost \
        + ":" + std::to_string(state_check_port) + "/api/stat/group?stream_name=%s";
    GLCC::constants::livego_kick_url = "http://" + GLCC::constants::localhost \
        + ":" + std::to_string(state_check_port) + "/api/ctrl/kick_session";

    Json::Value db_root = config_root["DB"];
    const std::string user_name = db_root["user_name"].asString();
    const std::string user_password = db_root["user_password"].asString();
    const std::string db_server_ip = db_root["db_server_ip"].asString();
    const int db_server_port = db_root["db_server_port"].asInt();
    GLCC::constants::mysql_root_url = "mysql://" + user_name + ":" \
        + user_password + "@" + db_server_ip + ":" + std::to_string(db_server_port);

    Json::Value timer_root = config_root["Timer"];
    GLCC::constants::interval_to_watch_detector_second = timer_root["interval_to_watch_detector_second"].asInt() \
        * GLCC::constants::num_second_per_minute;
    GLCC::constants::interval_to_watch_file_second = timer_root["interval_to_watch_file_second"].asInt() \
        * GLCC::constants::num_second_per_minute;
    GLCC::constants::max_detector_live_day = timer_root["max_detector_live_day"].asInt();
    GLCC::constants::max_video_file_save_day = timer_root["max_video_file_save_day"].asInt();

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
