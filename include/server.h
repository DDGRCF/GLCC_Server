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
            GLCCServer(const std::string config_path);
            int run();
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

            static Detector * register_detector(std::string & room_name, const Json::Value & detector_init_context, int * state);
            static void run_detector(std::string & room_name, std::shared_ptr<glcc_server_context_t> context);
            static int cancel_detector(std::string & room_name, int mode=0);
            static int parse_url(std::string & url, std::unordered_map<std::string, std::string> & results_map);
            static int parse_url(const char * url, std::unordered_map<std::string, std::string> & results_map);
            static int parse_room_url(std::string & url, std::unordered_map<std::string, std::string> & results_map);
            static int parse_room_url(const char * url, std::unordered_map<std::string, std::string> & results_map);
        protected:
            glcc_server_context_t glcc_server_context;

            static void login_activity(WFHttpTask * task, void * context);
            // base
            static void main_callback(WFHttpTask * task, void * context);
            static void login_callback(WFHttpTask * task, void * context);
            static void user_register_callback(WFHttpTask * task);
            static void hello_world_callback(WFHttpTask * task);
            // mysql
            static void create_db_callbck(WFMySQLTask * task, void * context);
            // detector
            static void dect_video_callback(WFHttpTask * task, void * context);
            static void disdect_video_callback(WFHttpTask * task, void * context);
            static void video_put_lattice_callback(WFHttpTask * task, void * context);
            static void video_disput_lattice_callback(WFHttpTask * Task, void * context);
            static void detector_timer_callback(WFTimerTask * timer);
            // video
            static void register_video_callback(WFHttpTask * task, void * context);
            static void delete_video_callback(WFHttpTask * task, void * context);
            // livego
            static void livego_manger_callback(WFHttpTask * task, std::shared_ptr<glcc_server_context_t> context);
            // common
            static protocol::HttpResponse * set_common_resp(protocol::HttpResponse * resp, 
                                                            std::string code="200", std::string phrase="OK",
                                                            std::string verion="HTTP/1.1", std::string content_type="text/html");
            static protocol::HttpRequest * set_common_req(protocol::HttpRequest * req, 
                                                            std::string accept="*/*", std::string status="close");
    };

}


#endif