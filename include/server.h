#ifndef _SERVER_H
#define _SERVER_H

#include "common.h"
#include "dealtor.h"
#include <workflow/WFFacilities.h>
#include <workflow/WFHttpServer.h>
#include <workflow/WFAlgoTaskFactory.h>
#include <workflow/HttpUtil.h>
#include <workflow/HttpMessage.h>

namespace GLCC {
    class GLCCServer
    {
        public:
            GLCCServer(const std::string config_path);
            int run();
            static WFFacilities::WaitGroup wait_group;
            static void sig_handler(int signo);
            static bool parse_request(WFHttpTask * task, 
                                        std::string method, 
                                        std::string rex);
            static void get_connection_infos(WFHttpTask * task, void * context);
            static Detector * register_detector(std::string & key, const Json::Value & detector_init_context, int * state);
            static int cancel_detector(std::string & key);
        protected:
            struct glcc_server_context glcc_server_context;

            // base
            static void main_callback(WFHttpTask * task, void * context);
            static void login_callback(WFHttpTask * task, void * context);
            static void user_login_callback(WFHttpTask * task, void * context);
            static void user_register_callback(WFHttpTask * task);
            static void hello_world_callback(WFHttpTask * task);
            // detector
            static void dect_video_callback(WFHttpTask * task, void * context);
            static void disdect_video_callback(WFHttpTask * task, void * context);
            static void video_put_lattice_callback(WFHttpTask * task, void * context);
            static void video_disput_lattice_callback(WFHttpTask * Task, void * context);
            static void run_detector(std::string key, struct glcc_server_context * context);
            // livego
            static void livego_manger_callback(WFHttpTask * task);
            // common
            static protocol::HttpResponse * set_common_resp(protocol::HttpResponse * resp, 
                                                            std::string code="200", std::string phrase="OK",
                                                            std::string verion="HTTP/1.1", std::string content_type="text/html");
            static protocol::HttpRequest * set_common_req(protocol::HttpRequest * req, 
                                                            std::string accept="*/*", std::string status="close");
    };

}


#endif