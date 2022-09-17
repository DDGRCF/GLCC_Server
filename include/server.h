#ifndef _SERVER_H
#define _SERVER_H

#include "common.h"
#include "dealtor.h"
#include <workflow/WFFacilities.h>
#include <workflow/WFHttpServer.h>
#include <workflow/WFAlgoTaskFactory.h>
#include <workflow/HttpUtil.h>
#include <workflow/HttpMessage.h>
#include <workflow/MySQLResult.h>
#include <workflow/Workflow.h>

namespace GLCC {
    class GLCCServer
    {
        public:

            int server_state = 0;
            int run();
            GLCCServer(const std::string config_path);
            static WFFacilities::WaitGroup server_wait_group;
            static WFFacilities::WaitGroup mysql_wait_group;
            static void sig_handler(int signo);
            static bool parse_request(WFHttpTask * task, 
                                        std::string method, 
                                        std::string rex);
            static void get_connection_infos(WFHttpTask * task, std::iostream & context);
            static void get_connection_infos(WFHttpTask * task, Json::Value & context);
            static void get_server_infos(WFHttpServer * server, std::iostream & context);
            static void get_server_infos(WFHttpServer * server, Json::Value & context);
            static int parse_mysql_response(WFMySQLTask * task, std::unordered_map<std::string, std::vector<protocol::MySQLCell>> & results);
            static int parse_mysql_response(WFMySQLTask * task);

            static void run_detector(const std::string & room_name, std::shared_ptr<glcc_server_context_t> context);
            static Detector * register_detector(const std::string & room_name, std::shared_ptr<glcc_server_context_t> context, int mode=Create_Register);
            static int cancel_detector(const std::string & room_name, int mode=WAKE_CANCEL);
            static int parse_url(const std::string & url, std::unordered_map<std::string, std::string> & results_map);
            static int parse_url(const char * url, std::unordered_map<std::string, std::string> & results_map);
            static int parse_room_url(const std::string & url, std::unordered_map<std::string, std::string> & results_map);
            static int parse_room_url(const char * url, std::unordered_map<std::string, std::string> & results_map);
        protected:
            glcc_server_context_t glcc_server_context;
            ssl_context_t glcc_server_ssl_context;

            static void login_activity(WFHttpTask * task, void * context);
            // base
            static void main_callback(WFHttpTask * task, void * context);
            static void login_callback(WFHttpTask * task, void * context);
            static void user_register_callback(WFHttpTask * task, void * context);
            static void hello_world_callback(WFHttpTask * task);
            // mysql
            static void create_db_callbck(WFMySQLTask * task, void * context);
            // detector
            static void dect_video_callback(WFHttpTask * task, void * context);
            static void dect_video_file_callback(WFHttpTask * task, void * context);
            static void kick_dect_video_file_callback(WFHttpTask * task, void * context);
            static void disdect_video_callback(WFHttpTask * task, void * context);
            static void video_put_lattice_callback(WFHttpTask * task, void * context);
            static void video_disput_lattice_callback(WFHttpTask * task, void * context);
            static void fetch_video_file_callback(WFHttpTask * task, void * context);
            static void delete_video_file_callback(WFHttpTask * task, void * context);
            static void transmiss_video_file_callback(WFHttpTask * task, void * context);
            static void register_map(const std::string & mode, const std::string & room_name, void * context);
            // timer
            static void detector_timer_callback(WFTimerTask * timer);
            static void file_timer_callback(WFTimerTask * timer);
            // video
            static void register_video_callback(WFHttpTask * task, void * context);
            static void delete_video_callback(WFHttpTask * task, void * context);
            // common
            static protocol::HttpResponse * set_common_resp(protocol::HttpResponse * resp, 
                                                            std::string code="200", std::string phrase="OK",
                                                            std::string verion="HTTP/1.1", std::string content_type="text/html");
            static protocol::HttpRequest * set_common_req(protocol::HttpRequest * req, 
                                                                std::string accept="*/*", std::string status="close", std::string method=HttpMethodGet);
    };

}


#endif