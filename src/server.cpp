#include "server.h"

#define REGEX_FUNC(func, method, rex, task, ...) if \ 
    (GLCC::GLCCServer::parse_request(task, method, rex)) { \
    func(task, ## __VA_ARGS__) ; \
}

#define REGEX_VALUE(rex, task) std::regex_match(task->get_req()->get_request_uri(), std::regex(rex));

namespace GLCC {

    WFFacilities::WaitGroup GLCCServer::server_wait_group(1);
    WFFacilities::WaitGroup GLCCServer::mysql_wait_group(1);

    GLCCServer::GLCCServer(const std::string config_path) {
        std::ifstream ifs;
        Json::Value root; 
        Json::Reader reader;
        ifs.open(config_path);
        int ret = reader.parse(ifs, root);
        if (!ret) {
            LOG_F(ERROR, "Parse %s failed!", config_path.c_str());
            return;
        }
        glcc_server_context.server_context.ip = root["Server"]["server_ip"].asString();
        glcc_server_context.server_context.port = root["Server"]["server_port"].asInt64();
        glcc_server_context.detector_init_context = root["Detector"];
    }

    int GLCCServer::run() {
        signal(SIGINT, sig_handler);
        signal(SIGTERM, sig_handler);
        int state = WFT_STATE_TOREPLY;
        // MySQL DB
        WFMySQLTask * task = WFTaskFactory::create_mysql_task(
            constants::mysql_root_url, 0, [&](WFMySQLTask * task) {
                create_db_callbck(task, &state);
        });
        task->get_req()->set_query(constants::mysql_create_db_command);
        task->start();
        mysql_wait_group.wait(); 
        // Detector Watcher
        WFTimerTask * timer = WFTaskFactory::create_timer_task(
            60 * 10 * 1000, detector_timer_callback   
        );

        if (state == WFT_STATE_SUCCESS) {
            WFHttpServer server([&](WFHttpTask * task) {
                main_callback(task, &glcc_server_context);
            });

            // TODO: figure it out
            int ret = server.start(glcc_server_context.server_context.port, 
                constants::ssl_crt_path.c_str(), 
                constants::ssl_key_path.c_str());
            std::stringstream server_infos;
            get_server_infos(&server, server_infos);
            LOG_F(INFO, "Server: %s", server_infos.str().c_str());
            if (ret == 0) {
                server_wait_group.wait();
                server.stop();
            } else {
                LOG_F(ERROR, "Start server failed!");
                return -1;
            }
            return 0;
        } else {
            LOG_F(ERROR, "Init mysql databases failed!");
            return -1;
        }
    }

    void GLCCServer::create_db_callbck(WFMySQLTask * task, void * context) {
        int * ex_state_ptr = (int *)context;
        int state = task->get_state();
        int error = task->get_error();
        if (state == WFT_STATE_SUCCESS) {
            parse_mysql_response(task);
            *ex_state_ptr = WFT_STATE_SUCCESS;
        } else {
            LOG_F(ERROR, "Init DB failed! Code: %d", error);
            *ex_state_ptr = state;
        }
        mysql_wait_group.done();
    }

    void GLCCServer::main_callback(WFHttpTask * task, void * context) {
        std::stringstream connection_infos;
        get_connection_infos(task, connection_infos);
        LOG_F(INFO, connection_infos.str().c_str());
        REGEX_FUNC(hello_world_callback, "GET", "/hello_world", task);
        REGEX_FUNC(login_callback, "POST", "/login.*", task, context);
        REGEX_FUNC(user_register_callback, "POST", "/register", task);
    }

    void GLCCServer::hello_world_callback(WFHttpTask * task) {
        int state = task->get_state();
        int error = task->get_error();
        if (state == WFT_STATE_TOREPLY) {
            protocol::HttpResponse * resp = task->get_resp();
            auto seq = task->get_task_seq();
            set_common_resp(resp, "200", "OK");
            std::string msg = "Hello World!";
            LOG_F(INFO, msg.c_str());
            resp->append_output_body(msg);
            if (seq == 9) resp->add_header_pair("Connection", "close");
        } else {
            LOG_F(ERROR, "Hello World task failed! Code: %d", error);
        }
    }

    void GLCCServer::login_activity(WFHttpTask * task, void * context) {
            REGEX_FUNC(dect_video_callback, "POST", "/login/dect_video", task, context);
            REGEX_FUNC(disdect_video_callback, "POST", "/login/disdect_video", task, context);
            REGEX_FUNC(register_video_callback, "POST", "/login/register_video", task, context);
            REGEX_FUNC(delete_video_callback, "POST", "/login/delete_video", task, context);
    }

    void GLCCServer::login_callback(WFHttpTask * task, void * context) {
        int state = task->get_state();
        int error = task->get_error();
        if (state == WFT_STATE_TOREPLY) {
            protocol::HttpRequest * req = task->get_req();
            protocol::HttpResponse * resp = task->get_resp();

            const void * body;
            size_t body_len;
            req->get_parsed_body(&body, &body_len);
            const std::string body_text = (const char *)body;

            Json::Value root;
            Json::Reader reader;
            int ret = reader.parse(body_text, root);
            if (!ret) {
                LOG_F(ERROR, "Parse %s failed!", body_text.c_str());
                set_common_resp(resp, "400", "Bad Request");
                return;
            }
            if (!root.isMember("user_name") || !root.isMember("user_password")) {
                LOG_F(ERROR, "Find request body key: %s, %s failed!", "user_name", "user_password");
                set_common_resp(resp, "400", "Bad Request");
                return;
            }
            std::string user_name = root["user_name"].asString();
            std::string user_password = root["user_password"].asString();
            Json::Value resp_root;
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0, [user_name, user_password, context](WFMySQLTask * task){
                int state = task->get_state();
                int error = task->get_error();
                WFHttpTask * http_task = (WFHttpTask *)task->user_data;
                protocol::HttpResponse * http_resp = http_task->get_resp();
                Json::Value root;
                if (state == WFT_STATE_SUCCESS) {
                    std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                    parse_mysql_response(task, results);
                    const bool only_login = REGEX_VALUE("/login", http_task);
                    LOG_F(INFO, "Only Login: %s", only_login ? "yes" : "no");
                    if (results.size() > 0) {
                        if (user_name == results["username"][0].as_string() \
                            && user_password == results["password"][0].as_string()) {
                            if (only_login) {
                                set_common_resp(http_resp, "200", "OK");
                                root["status"] = "200";
                                root["msg"] = "Success";
                                http_resp->append_output_body(root.toStyledString());
                            } else {
                                login_activity(http_task, context);
                            }
                        } else {
                            set_common_resp(http_resp, "400", "Basd Request");
                            root["status"] = "400";
                            root["msg"] = "Error user_name or user_password";
                            http_resp->append_output_body(root.toStyledString());
                        }
                    } else {
                        set_common_resp(http_resp, "400", "Basd Request");
                        root["status"] = "400";
                        root["msg"] = "Error user_name or user_password";
                        http_resp->append_output_body(root.toStyledString());
                    }
                } else {
                    set_common_resp(http_resp, "400", "Basd Request");
                    root["status"] = "400";
                    root["msg"] = "Error user_name or user_password";
                    http_resp->append_output_body(root.toStyledString());
                    LOG_F(ERROR, "Connect to MySQL failed! Code: %d", error);
                }
            });
            char mysql_query[128];
            snprintf(mysql_query, sizeof(mysql_query), 
                "SELECT * FROM User WHERE username=\"%s\" AND password=\"%s\"",
                 user_name.c_str(), user_password.c_str());
            mysql_task->user_data = task;
            mysql_task->get_req()->set_query(mysql_query);
            *series_of(task) << mysql_task;
        } else {
            LOG_F(ERROR, "Login failed! Code: %d", error);
        }
    }


    void GLCCServer::user_register_callback(WFHttpTask * task) {
        int state = task->get_state();
        int error = task->get_error();
        if (state == WFT_STATE_TOREPLY) {
            protocol::HttpRequest * req = task->get_req();
            protocol::HttpResponse * resp = task->get_resp();
            const void * body;
            size_t body_len;
            req->get_parsed_body(&body, &body_len);
            const std::string body_text = (const char *)body;
            Json::Value root;
            Json::Reader reader;
            int ret = reader.parse(body_text, root);
            if (!ret) {
                LOG_F(ERROR, "Parse %s failed!", body_text.c_str());
                set_common_resp(resp, "400", "Bad Request");
                return;
            }
            if (!root.isMember("user_name") || !root.isMember("user_password") || !root.isMember("user_nickname")) {
                LOG_F(ERROR, "Find request body key: %s, %s failed!", "use_name", "video_password");
                set_common_resp(resp, "400", "Bad Request");
                return;
            }
            std::string user_name = root["user_name"].asString();
            std::string user_password = root["user_password"].asString();
            std::string user_nickname = root["user_nickname"].asString();

            // creat sql task
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0, 
                [user_name, user_password, user_nickname](WFMySQLTask * task){
                    int state = task->get_state();
                    int error = task->get_error();
                    WFHttpTask * http_task = (WFHttpTask * )task->user_data;
                    protocol::HttpResponse * http_resp = http_task->get_resp();
                    if (state == WFT_STATE_SUCCESS) {
                        int parse_state = parse_mysql_response(task);
                        if (parse_state != WFT_STATE_SUCCESS) {
                            LOG_F(ERROR, "Register failed!");
                            set_common_resp(http_resp, "400", "Bad Request");
                        } else {
                            set_common_resp(http_resp, "200", "OK");
                        }
                    } else {
                        LOG_F(INFO, "Register failed! Code: %d", error);
                        set_common_resp(http_resp, "400", "Bad Request");
                    }
                }
            );
            mysql_task->user_data = task;
            char mysql_query[128];
            snprintf(mysql_query, sizeof(mysql_query), 
                "INSERT INTO glccserver.User(username, password, nickname) VALUES (\"%s\", \"%s\", \"%s\");",
                 user_name.c_str(), user_password.c_str(), user_nickname.c_str());
            mysql_task->get_req()->set_query(mysql_query);
            *series_of(task) << mysql_task;
        } else {
            LOG_F(ERROR, "Register failed! Code: %d", error);
        }

    }

    void GLCCServer::dect_video_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
        glcc_server_context_t * glcc_context  = (glcc_server_context_t *) context;

        const void * body;
        size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *) body;

        // check and parse the body
        Json::Value root;
        Json::Reader reader;
        reader.parse(body_text, root);
        if (!root.isMember("video_url") && !root.isMember("video_name")) {
            LOG_F(ERROR, "Find request body key: %s, %s failed!", "video_url", "video_name");
            set_common_resp(resp, "400", "Bad Request");
            return;
        }
        std::string user_name = root["user_name"].asString();
        std::string user_password = root["user_password"].asString();
        std::string video_url = root["video_url"].asString();
        std::string video_name = root["video_name"].asString();

        char room_name[128] = {0};
        snprintf(room_name, sizeof(room_name), "%s-%s-%s", 
            user_name.c_str(), user_password.c_str(), video_name.c_str());
        char livego_manger_url[128] = {0};
        snprintf(livego_manger_url, sizeof(livego_manger_url), 
            constants::livego_manger_url_template.c_str(), room_name);
        char livego_delete_url[128] = {0};
        snprintf(livego_delete_url, sizeof(livego_delete_url), 
            constants::livego_delete_url_template.c_str(), room_name);
        char livego_stop_pull_url[256] = {0};
        snprintf(livego_stop_pull_url, sizeof(livego_stop_pull_url), 
            constants::livego_pull_switch_tempalte.c_str(), "stop", room_name, room_name);
        LOG_F(INFO, "Source Video: %s | LiveGo Manger: %s | LiveGo Delete: %s", 
            video_url.c_str(), livego_manger_url, livego_delete_url);

        // create detector run context
        std::shared_ptr<glcc_server_context_t> dect_context = \
            std::make_shared<glcc_server_context_t>();

        dect_context->livego_context.room_name = room_name;
        dect_context->livego_context.livego_manger_url = livego_manger_url;
        dect_context->livego_context.livego_delete_url = livego_delete_url;
        dect_context->livego_context.livego_pull_switch_url = livego_stop_pull_url;
        dect_context->video_context.video_path = video_url;
        dect_context->detector_init_context = glcc_context->detector_init_context;
        dect_context->detector_run_context = glcc_context->detector_run_context;

        // create graph task
        SeriesWork * series = series_of(task); 
        WFGraphTask * graph_task = WFTaskFactory::create_graph_task(
            [](WFGraphTask * task){
                LOG_F(INFO, "Video Dect graph task complete!");
            });

        // check video_url 
        char go_task_name[128];
        snprintf(go_task_name, sizeof(go_task_name), "check: %s", video_url.c_str());
        WFGoTask * go_task = WFTaskFactory::create_go_task(go_task_name, 
            [](std::shared_ptr<glcc_server_context_t> context){
                std::string video_url = context->video_context.video_path;
                cv::VideoCapture capture;
                int ret = capture.open(video_url);
                if (ret) {
                    context->state = WFT_STATE_SUCCESS;
                } else {
                    context->state = WFT_STATE_TASK_ERROR;
                }
                capture.release();
            }, dect_context);

        go_task->set_callback([dect_context](WFGoTask * task){
            int state = task->get_state(); int error = task->get_error();
            if (state == WFT_STATE_SUCCESS) {
                LOG_F(INFO, "%s: %s", 
                    dect_context->video_context.video_path.c_str(), 
                    dect_context->state == WFT_STATE_SUCCESS ? "validate" : "invalidate");
            } else {
                LOG_F(ERROR, "Check the %s failed! Code: %d", 
                    dect_context->video_context.video_path.c_str(), error);
                dect_context->state = WFT_STATE_TASK_ERROR;
            }
        });

        // create manager request
        WFHttpTask * http_task = WFTaskFactory::create_http_task(livego_manger_url, 0, 0, 
            [dect_context](WFHttpTask * http_task) {
                livego_manger_callback(http_task, dect_context);
            });

        protocol::HttpRequest * http_req = http_task->get_req();
        set_common_req(http_req);
        http_task->user_data = task;

        // connect node1 -> node2
        WFGraphNode & node1 = graph_task->create_graph_node(go_task);
        WFGraphNode & node2 = graph_task->create_graph_node(http_task);
        node1-->node2;
        
        // series task
        *series << graph_task;
    }

    void GLCCServer::livego_manger_callback(WFHttpTask * task, std::shared_ptr<glcc_server_context_t> context) {
        int state = task->get_state();
        int error = task->get_error();
        WFHttpTask * up_task = (WFHttpTask *)task->user_data;
        protocol::HttpResponse * up_resq = (protocol::HttpResponse *)up_task->get_resp();
        if (state == WFT_STATE_SUCCESS && context->state == WFT_STATE_SUCCESS) {
            protocol::HttpResponse * resp = task->get_resp();

            const void * body;
            size_t body_len;

            resp->get_parsed_body(&body, &body_len);
            const std::string body_text = (const char *)body;

            Json::Value root;
            Json::Reader reader;
            state = reader.parse(body_text, root);
            if (!state) {
                LOG_F(ERROR, "Parse %s failed!", body_text.c_str());
                set_common_resp(up_resq, "400", "Bad Request");
            }
            state = root["status"].asInt();
            std::string room_key = root["data"].asString();
            if (state == 200) {
                char livego_upload_url[128] = {0};
                snprintf(livego_upload_url, sizeof(livego_upload_url), 
                    constants::livego_upload_url_template.c_str(), room_key.c_str());
                context->detector_run_context.upload_path = livego_upload_url;
                context->detector_run_context.video_path = context->video_context.video_path;
                context->livego_context.room_key = room_key;
                std::string room_name = context->livego_context.room_name;
                LOG_F(INFO, "LiveGO UpLoad: %s", livego_upload_url);

                WFGoTask * go_task = WFTaskFactory::create_go_task(room_name, run_detector, room_name, context);
                go_task->set_callback([context](WFGoTask * task) {
                    std::string & room_key = context->livego_context.room_key;
                    std::string & room_name = context->livego_context.room_name;
                    std::string & livego_delete_url = context->livego_context.livego_delete_url;
                    std::string & livego_pull_stop_url = context->livego_context.livego_pull_switch_url;
                    if (context->state != WFT_STATE_TOREPLY) {
                        SeriesWork * series = series_of(task);
                        WFGraphTask * graph_task = WFTaskFactory::create_graph_task(
                            [](WFGraphTask * task) {
                                LOG_F(INFO, "Release Dect graph task complate!");
                            });
                        
                        WFHttpTask * stop_http_task = WFTaskFactory::create_http_task(
                            livego_pull_stop_url, 0, 0, 
                            [room_name](WFHttpTask * task) {
                                int state = task->get_state(); int error = task->get_error();
                                if (state == WFT_STATE_SUCCESS) {
                                    LOG_F(INFO, "Stop the pull of room: %s", room_name.c_str());
                                } else {
                                    LOG_F(ERROR, "Stop the pull of room: %s failed! Code: %d", room_name.c_str(), error);
                                }
                                int ret = ProductFactory<Detector>::Instance().EraseProductDel(room_name);
                                if (ret != 1) {
                                    LOG_F(INFO, "Erase Product: %s failed! Code: %d", room_name.c_str(), ret);
                                } else {
                                    LOG_F(INFO, "Erase Prodcut: %s:%d success!", room_name.c_str(), ret);
                                }
                            }
                        );

                        WFHttpTask * del_http_task = WFTaskFactory::create_http_task(
                            livego_delete_url, 0, 0, 
                            [room_name](WFHttpTask * task) {
                                int state = task->get_state(); int error = task->get_error();
                                if (state == WFT_STATE_SUCCESS) {
                                    LOG_F(INFO, "Delete the room: %s", room_name.c_str());
                                } else {
                                    LOG_F(ERROR, "Delete the room: %s failed! Code: %d", room_name.c_str(), error);
                                }
                            });
                        
                        WFGraphNode & node1 = graph_task->create_graph_node(stop_http_task);
                        WFGraphNode & node2 = graph_task->create_graph_node(del_http_task);
                        node1-->node2;
                        *series << graph_task;
                    }
                });
                go_task->start();
                set_common_resp(up_resq, "200", "OK");
                Json::Value reply;
                reply["room_name"] = context->livego_context.room_name;
                up_resq->append_output_body(reply.toStyledString());
            } else {
                LOG_F(ERROR, "Create Manager Room failed!");
                set_common_resp(up_resq, "400", "Bad Request");
            }
        } else {
            LOG_F(ERROR, "Send to LiveGo-Server failed! Code: %d", error);
            set_common_resp(up_resq, "400", "Bad Request");
        }
    }

    void GLCCServer::disdect_video_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();

        const void * body;
        size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *)body;

        Json::Value root;
        Json::Reader reader;
        reader.parse(body_text, root);
        if (!root.isMember("room_url")) {
            LOG_F(ERROR, "Find request body key: %s failed!", "room_url");
            set_common_resp(resp, "400", "Bad Request");
            return;
        }

        std::string room_url;
        std::string room_name;
        std::string user_name = root["user_name"].asString();
        std::vector<std::string> failed_stop_room_url{};
        if (root["room_url"].isArray()) {
            for (auto i = 0; (int)root["room_url"].size(); i++) {
                std::unordered_map<std::string, std::string> results;
                room_url = root["room_url"][i].asString();
                int ret = parse_room_url(room_url, results);
                if (ret == -1) {
                    LOG_F(ERROR, "Parse room_url: %s failed!", room_url.c_str());
                    failed_stop_room_url.emplace_back(room_url);
                } else {
                    room_name = results["room_name"];
                    Detector * detector = ProductFactory<Detector>::Instance().GetProduct(room_name);
                    if (detector == nullptr) {
                        LOG_F(ERROR, "Find the room: %s failed!", room_name.c_str());
                        failed_stop_room_url.emplace_back(room_url);
                    } else {
                        detector->state = -1;
                        LOG_F(INFO, "Stop the detector: %s", room_name.c_str());
                    }
                }
            }
        } else {
            std::unordered_map<std::string, std::string> results;
            room_url = root["room_url"].asString();
            int ret = parse_room_url(room_url, results);
            if (ret == -1) {
                LOG_F(ERROR, "Parse room_url: %s failed!", room_url.c_str());
                failed_stop_room_url.emplace_back(room_url);
            } else {
                room_name = results["room_name"];
                Detector * detector = ProductFactory<Detector>::Instance().GetProduct(room_name);
                if (detector == nullptr) {
                    LOG_F(ERROR, "Find the room: %s failed!", room_name.c_str());
                    failed_stop_room_url.emplace_back(room_url);
                } else {
                    detector->state = -1;
                    LOG_F(INFO, "Stop the detector: %s", room_name.c_str());
                }
            }
        }
        Json::Value reply;
        for (auto f : failed_stop_room_url) {
            reply["failed_stop_room_url"].append(f);
        }
        set_common_resp(resp, "200", "OK");
        resp->append_output_body(reply.toStyledString());
    }

    void GLCCServer::register_video_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
        const void * body;
        size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *) body; 

        Json::Value root;
        Json::Reader reader;
        reader.parse(body_text, root);
        if (!root.isMember("use_template_url") || !root.isMember("video_url") || !root.isMember("video_name")) {
            LOG_F(ERROR, "Find request body key: %s, %s, %s failed!", "use_template_url", "video_name", "video_url");
            set_common_resp(resp, "400", "Bad Request");
            return;
        }

        bool use_template_url = root["use_template_url"].asBool();
        std::string video_name = root["video_name"].asString();
        std::string video_url = root["video_url"].asString();
        std::string user_name = root["user_name"].asString();
        LOG_F(INFO, "Body: %s", root.toStyledString().c_str());
        char video_path[128] = {0};
        if (use_template_url) {
            snprintf(video_path, sizeof(video_path), 
                constants::video_path_template.c_str(), video_url.c_str());
        } else {
            std::strcpy(video_path, video_url.c_str());
        }
        video_url = video_path;
        WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
            constants::mysql_glccserver_url, 0, [video_url, video_name, user_name](WFMySQLTask * task){
                int state = task->get_state();
                int error = task->get_error();
                WFHttpTask * http_task = (WFHttpTask *) task->user_data;
                protocol::HttpResponse * http_resp = http_task->get_resp();
                if (state == WFT_STATE_SUCCESS) {
                    int parse_state = parse_mysql_response(task);
                    if (parse_state == WFT_STATE_SUCCESS) {
                        Json::Value reply;
                        reply["video_name"] = video_name;
                        reply["video_url"] = video_url;
                        set_common_resp(http_resp, "200", "OK");
                        http_resp->append_output_body(reply.toStyledString());
                        LOG_F(INFO, reply.toStyledString().c_str());
                    } else {
                        LOG_F(ERROR, "Register Video failed!");
                        set_common_resp(http_resp, "400", "Bad Request");
                    }
                } else {
                    LOG_F(INFO, "Register failed! Code: %d", error);
                    set_common_resp(http_resp, "400", "Bad Request");
                }
            }
        );
        mysql_task->user_data = task;
        char mysql_query[512];
        snprintf(mysql_query, sizeof(mysql_query),
            "INSERT into glccserver.Video(video_name, username, video_url) values (\"%s\", \"%s\", \"%s\")",
            video_name.c_str(), user_name.c_str(), video_url.c_str());
        mysql_task->get_req()->set_query(mysql_query);
        *series_of(task) << mysql_task;
    }


    void GLCCServer::delete_video_callback(WFHttpTask * task, void * context) {

    }

    void GLCCServer::video_put_lattice_callback(WFHttpTask * task, void * context) {

    }

    void GLCCServer::video_disput_lattice_callback(WFHttpTask * task, void * context) {

    }


    int GLCCServer::parse_url(std::string & url, std::unordered_map<std::string, std::string> & result_map) {
	    static std::vector<std::string> fields{"url", "scheme", "slash", "host", "port", "path", "query", "hash"};
	    static std::regex pattern{"^(?:([A-Za-z]+):)?(\\/{0,3})([0-9.\\-A-Za-z]+)(?::(\\d+))?(?:\\/([^?#]*))?(?:\\?([^#]*))?(?:#(.*))?$"};
        std::smatch results;
        if (std::regex_match(url, results, pattern)) {
            for (int i = 0; i < (int)fields.size(); i++) {
                std::string & key = fields[i];
                result_map[key] = results[i];
            }
            return 0;
        } else {
            return -1;
        }
    }

    int GLCCServer::parse_url(const char * url, std::unordered_map<std::string, std::string> & result_map) {
        std::string _url = url;
        return parse_url(_url, result_map);
    }

    int GLCCServer::parse_room_url(std::string & url, std::unordered_map<std::string, std::string> & result_map) {
	    static std::vector<std::string> fields{"path", "room_name", "username", "password", "video_name"};
        std::regex pattern{".*live*/((.*)-(.*)-(.*)).flv"};
        std::smatch results;
        if (std::regex_match(url, results, pattern)) {
            for (int i = 0; i < (int)fields.size(); i++) {
                std::string & key = fields[i];
                result_map[key] = results[i];
            }
            return 0;
        } else {
            return -1;
        }
    }

    int GLCCServer::parse_room_url(const char * url, std::unordered_map<std::string, std::string> & result_map) {
        std::string _url = url;
        return parse_room_url(_url, result_map);
    }

    void GLCCServer::sig_handler(int signo) {
        LOG_F(INFO, "Get signal code: %d", signo);
        GLCCServer::server_wait_group.done();
        GLCCServer::mysql_wait_group.done();
    }

    bool GLCCServer::parse_request(WFHttpTask * task, 
                                  std::string _med, 
                                  std::string _uri) {
        protocol::HttpRequest * req = task->get_req();
        std::string uri = req->get_request_uri();
        std::string med = req->get_method();
        std::stringstream med_regex_str;
        med_regex_str << "^" << _med << "$";
        std::regex med_regex(med_regex_str.str(), std::regex_constants::icase);
        std::regex uri_regex(_uri);
        bool ret1 = std::regex_match(med, med_regex);
        bool ret2 = std::regex_match(uri, uri_regex);
        return ret1 && ret2;
    }
    
    void GLCCServer::get_connection_infos(WFHttpTask * task, std::iostream & context) {
        auto seq = task->get_task_seq();
        char addrstr[INET6_ADDRSTRLEN];
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        unsigned short port = 0;
        task->get_peer_addr((struct sockaddr *)&addr, &addrlen);
        switch (addr.ss_family)
        {
            case AF_INET: {
                struct sockaddr_in * sin = (struct sockaddr_in *)&addr;
                inet_ntop(AF_INET, &sin->sin_addr, addrstr, sizeof(addrstr));
                port = ntohs(sin->sin_port);
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 * sin6 = (struct sockaddr_in6 *)&addr;
                inet_ntop(AF_INET6, &sin6->sin6_addr, addrstr, sizeof(addr));
                port = ntohs(sin6->sin6_port);
                break;
            }
            default: {
                strcpy(addrstr, "Unknown connection!");
                break;
            }
        }
        context << "Peer address: " 
                << addrstr << ":"
                << port << ", "
                << "seq: " <<  seq;
    }

    void GLCCServer::get_connection_infos(WFHttpTask * task, Json::Value & context) {
        auto seq = task->get_task_seq();
        char addrstr[INET6_ADDRSTRLEN];
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        unsigned short port = 0;
        task->get_peer_addr((struct sockaddr *)&addr, &addrlen);
        switch (addr.ss_family)
        {
            case AF_INET: {
                struct sockaddr_in * sin = (struct sockaddr_in *)&addr;
                inet_ntop(AF_INET, &sin->sin_addr, addrstr, sizeof(addrstr));
                port = ntohs(sin->sin_port);
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 * sin6 = (struct sockaddr_in6 *)&addr;
                inet_ntop(AF_INET6, &sin6->sin6_addr, addrstr, sizeof(addr));
                port = ntohs(sin6->sin6_port);
                break;
            }
            default: {
                strcpy(addrstr, "Unknown connection!");
                break;
            }
        }
        context["ip"] = std::string(addrstr);
        context["port"] = port;
        context["seq"] = (size_t)seq;
    }

    void GLCCServer::get_server_infos(WFHttpServer * server, std::iostream & context) {
        char addrstr[INET6_ADDRSTRLEN];
        unsigned short port = 0;
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        server->get_listen_addr((struct sockaddr *)&addr, &addrlen);
        switch (addr.ss_family)
        {
            case AF_INET: {
                struct sockaddr_in * sin = (struct sockaddr_in *)&addr;
                inet_ntop(AF_INET, &sin->sin_addr, addrstr, sizeof(addrstr));
                port = ntohs(sin->sin_port);
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 * sin6 = (struct sockaddr_in6 *)&addr;
                inet_ntop(AF_INET6, &sin6->sin6_addr, addrstr, sizeof(addr));
                port = ntohs(sin6->sin6_port);
                break;
            }
        }
        context << "address: " 
                << addrstr << ":"
                << port;

    }

    void GLCCServer::get_server_infos(WFHttpServer * server, Json::Value & context) {
        char addrstr[INET6_ADDRSTRLEN];
        unsigned short port = 0;
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        server->get_listen_addr((struct sockaddr *)&addr, &addrlen);
        switch (addr.ss_family)
        {
            case AF_INET: {
                struct sockaddr_in * sin = (struct sockaddr_in *)&addr;
                inet_ntop(AF_INET, &sin->sin_addr, addrstr, sizeof(addrstr));
                port = ntohs(sin->sin_port);
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 * sin6 = (struct sockaddr_in6 *)&addr;
                inet_ntop(AF_INET6, &sin6->sin6_addr, addrstr, sizeof(addr));
                port = ntohs(sin6->sin6_port);
                break;
            }
        }
        context["ip"] = std::string(addrstr);
        context["port"] = port;
    }

    void GLCCServer::run_detector(std::string & room_name, std::shared_ptr<glcc_server_context_t> context) {
        int state;
        Json::Value detector_init_context = context->detector_init_context;
        struct detector_run_context detector_run_context = context->detector_run_context;
        Detector * detector = register_detector(room_name, detector_init_context, &state);
        if (state == WFT_STATE_TASK_ERROR) {
            LOG_F(ERROR, "Register Detector failed!");
        } else if (state == WFT_STATE_SUCCESS) {
            std::string mode = detector_init_context["mode"].asString();
            LOG_F(INFO, "Mode: %s", mode.c_str());
            detector_run_context.vis_params = detector_init_context[(const char *)mode.c_str()]["vis_config"];
            state = detector->run(&detector_run_context);
            if (state == -1) {
                LOG_F(INFO, "Detector: %s run stop!", room_name.c_str());
                context->state = WFT_STATE_TASK_ERROR;
            }
        } else if (state == WFT_STATE_TOREPLY){
            LOG_F(INFO, "detector-%s exists!", room_name.c_str());
        }
    }

    void GLCCServer::detector_timer_callback(WFTimerTask * timer) {
        SeriesWork * series = series_of(timer);
        WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
            constants::mysql_glccserver_url, 0, 
            [](WFMySQLTask * task){
                int state = task->get_state();
                int error = task->get_error();
                // TODO:
                if (state == WFT_STATE_SUCCESS) {
                    std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                    parse_mysql_response(task, results);
                    if (results.size() > 0) {
                        LOG_F(INFO, "Nothing to watch");
                    } else {
                        std::vector<protocol::MySQLCell> & room_name_cells = results["room_name"];
                        std::vector<protocol::MySQLCell> & compare_results_cell = results["compare_results"];
                        std::string room_name;
                        int compare_results;
                        std::stringstream mysql_delete_query;
                        std::stringstream keep_room_name_infos;
                        std::stringstream remove_room_name_infos;
                        keep_room_name_infos << "Keeping rooms: ";
                        remove_room_name_infos << "Removing rooms: ";
                        for (int i = 0; i < room_name_cells.size(); i++) {
                            room_name = room_name_cells[i].as_string();
                            compare_results = compare_results_cell[i].as_int();
                            if (compare_results < 0) {
                                mysql_delete_query << "delete from glccserver.Room where room_name=" << room_name << ";"; 
                                // Detector * detector = ProductFactory<>
                                remove_room_name_infos << room_name << " ";
                            } else {
                                keep_room_name_infos << room_name << " ";
                            }
                        }
                    }
                } else {
                    LOG_F(ERROR, "Timmer watch all detectors failed!");
                }
            }
        );
        char mysql_query[128] = "select room_name, func_time_compare(now(), end_time) as compare_results from glccserver.Room";
        mysql_task->get_req()->set_query(mysql_query);
        *series << mysql_task;
    }

    Detector * GLCCServer::register_detector(std::string & room_name, const Json::Value & detector_init_context, int * state) {
        const std::string mode = detector_init_context["mode"].asString();
        Detector * detector;
        if (mode == "ObjectDetector") {
            Json::Value context = detector_init_context[(const char *)mode.c_str()];
            if ((detector = ProductFactory<Detector>::Instance().GetProduct(room_name)) == nullptr) {
                ProductFactory<Detector>::Instance().RegisterProduct(room_name, ObjectDetector::init_func, (void *)&context);
                detector = ProductFactory<Detector>::Instance().GetProduct(room_name);
            } else {
                *state = WFT_STATE_TOREPLY;
            }
            if (detector->state == -1) {
                ProductFactory<Detector>::Instance().EraseProductDel(room_name);
                detector = nullptr;
                *state = WFT_STATE_TASK_ERROR;
            } else {
                if (*state != WFT_STATE_TOREPLY) {
                    *state = WFT_STATE_SUCCESS;
                }
            }
        }
        return detector;
    }

    int GLCCServer::cancel_detector(std::string & room_name, int mode) {
        int state;
        Detector * detector = ProductFactory<Detector>::Instance().GetProduct(room_name);
        if (detector == nullptr) {
            state = WFT_STATE_NOREPLY;
        } else {
            state = WFT_STATE_SUCCESS;
            if (detector->state > 0) {
                if ((--detector->state == 0 && mode == 0) || mode == 1) {
                    detector->state = -1;
                    char livego_delete_url[128] = {0};
                    snprintf(livego_delete_url, sizeof(livego_delete_url), 
                        constants::livego_delete_url_template.c_str(), room_name.c_str());
                    char livego_stop_pull_url[256] = {0};
                    snprintf(livego_stop_pull_url, sizeof(livego_stop_pull_url), 
                        constants::livego_pull_switch_tempalte.c_str(), "stop", room_name.c_str(), room_name.c_str());
                    char livego_stop_push_url[256] = {0};
                    snprintf(livego_stop_push_url, sizeof(livego_stop_push_url), "stop", room_name.c_str(), room_name.c_str());
                    LOG_F(INFO, "delete url: %s | stop pull url: %s | stop push url: %s", 
                        livego_delete_url, livego_stop_pull_url, livego_stop_push_url);

                    WFGraphTask * graph_task = WFTaskFactory::create_graph_task(
                        [](WFGraphTask * task) {
                            LOG_F(INFO, "Release Dect graph task complete!");
                        }
                    );
                    WFHttpTask * stop_push_http_task = WFTaskFactory::create_http_task(
                        livego_stop_push_url, 0, 0,
                        [room_name](WFHttpTask * task) {
                            int state = task->get_state(); int error = task->get_error();
                            if (state == WFT_STATE_SUCCESS) {
                                LOG_F(INFO, "Stop the pull of room: %s", room_name.c_str());
                            } else {
                                LOG_F(ERROR, "Stop the pull of room: %s failed! Code: %d", room_name.c_str(), error);
                            }
                            int ret = ProductFactory<Detector>::Instance().EraseProductDel(room_name);
                            if (ret != 1) {
                                LOG_F(INFO, "Erase Product: %s failed! Code: %d", room_name.c_str(), ret);
                            } else {
                                LOG_F(INFO, "Erase Prodcut: %s:%d success!", room_name.c_str(), ret);
                            }
                        }
                    );


                    WFHttpTask * stop_pull_http_task = WFTaskFactory::create_http_task(
                        livego_stop_pull_url, 0, 0,
                        [room_name](WFHttpTask * task) {
                            int state = task->get_state(); int error = task->get_error();
                            if (state == WFT_STATE_SUCCESS) {
                                LOG_F(INFO, "Stop the push of room: %s", room_name.c_str());
                            } else {
                                LOG_F(ERROR, "Stop the push of room: %s failed! Code: %d", room_name.c_str(), error);
                            }
                        }
                    );

                    WFHttpTask * delete_room_task = WFTaskFactory::create_http_task(
                        livego_delete_url, 0, 0,
                        [room_name](WFHttpTask * task) {
                            int state = task->get_state(); int error = task->get_error();
                            if (state == WFT_STATE_SUCCESS) {
                                LOG_F(INFO, "Delete the room: %s", room_name.c_str());
                            } else {
                                LOG_F(ERROR, "Delete the room: %s failed! Code: %d", room_name.c_str(), error);
                            }
                        }
                    );

                    WFMySQLTask * delete_sql_task = WFTaskFactory::create_mysql_task(
                        constants::mysql_glccserver_url, 0, 
                        [room_name](WFMySQLTask * task) {
                            int state = task->get_state(); int error = task->get_error();
                            if (state == WFT_STATE_SUCCESS) {
                                LOG_F(INFO, "Delete room: %s success!", room_name.c_str());
                            } else {
                                LOG_F(INFO, "Delete room: %s failed! Code: %d", room_name.c_str(), error);
                            }
                        }
                    );

                    char mysql_query[512];
                    snprintf(mysql_query, sizeof(mysql_query), 
                        "delete from glccserver.Room where room_name=%s", room_name.c_str());
                    delete_sql_task->get_req()->set_query(mysql_query);
                    WFGraphNode & node1 = graph_task->create_graph_node(stop_push_http_task);
                    WFGraphNode & node2 = graph_task->create_graph_node(stop_pull_http_task);
                    WFGraphNode & node3 = graph_task->create_graph_node(delete_room_task);
                    WFGraphNode & node4 = graph_task->create_graph_node(delete_sql_task);
                    node1-->node2-->node3-->node4;
                    graph_task->start();
                } else if (--detector->state > 0 && mode == 0) {
                    WFMySQLTask * sub_sql_task = WFTaskFactory::create_mysql_task(
                        constants::mysql_glccserver_url, 0, 
                        [room_name](WFMySQLTask * task) {
                            int state = task->get_state(); int error = task->get_error();
                            if (state == WFT_STATE_SUCCESS) {
                                std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                                parse_mysql_response(task, results);
                                if (results.size() > 0){
                                    int num_connection = results["num_connection"][0].as_int();
                                    LOG_F(INFO, "Sub room: %s, connection: %d", room_name.c_str(), num_connection);
                                } else {
                                    LOG_F(INFO, "Find the num connection of %s failed!", room_name.c_str());
                                }
                            } else {
                                LOG_F(INFO, "Sub room: %s failed! Code: %d", room_name.c_str(), error);
                            }
                        }
                    );

                    char mysql_query1[512];
                    char mysql_query2[256];
                    snprintf(mysql_query1, sizeof(mysql_query1), 
                        "update glccserver.Room set num_connection=num_connection - 1 where room_name=%s;", room_name.c_str());
                    snprintf(mysql_query2, sizeof(mysql_query2), 
                        "select num_connection from glccserver.Room where room_name=%s;", room_name.c_str());
                    std::strcat(mysql_query1, mysql_query2);
                    sub_sql_task->get_req()->set_query(mysql_query1);
                    sub_sql_task->start();
                }
            } else if (detector->state == 0) {
                detector->state = -1;
                ProductFactory<Detector>::Instance().EraseProductDel(room_name);
                LOG_F(INFO, "Delete the unrun detector: %s", room_name.c_str());
            } else if (detector->state == -1) {
                ProductFactory<Detector>::Instance().EraseProduct(room_name);
            }
        }
        return state;
    }

    protocol::HttpResponse * GLCCServer::set_common_resp(protocol::HttpResponse * resp, 
                                      std::string code, std::string phrase,
                                      std::string version, std::string content_type) {
        resp->set_http_version(version);
        resp->set_status_code(code);
        resp->set_reason_phrase(phrase);
        resp->add_header_pair("Content-Type", content_type);
        resp->add_header_pair("Server", "GLCC Server Implemented by WorkFlow");
        return resp;
    }
    protocol::HttpRequest * GLCCServer::set_common_req(protocol::HttpRequest * req, 
                                      std::string accept, 
                                      std::string status) {
    
        req->add_header_pair("Accept", accept);
        req->add_header_pair("User-Agent", "GLCC Server Implemented by WorkFlow");
        req->add_header_pair("Connection", status);
        return req;
    }

    int GLCCServer::parse_mysql_response(WFMySQLTask * task) {
        protocol::MySQLResponse *resp = task->get_resp();
        protocol::MySQLResultCursor cursor(resp);
        const protocol::MySQLField * const * fields;
        std::vector<protocol::MySQLCell> arr;
        do {
            if (cursor.get_cursor_status() != MYSQL_STATUS_GET_RESULT &&
                cursor.get_cursor_status() != MYSQL_STATUS_OK)
            {
                break;
            }

            LOG_F(INFO, "---------------- RESULT SET ----------------\n");

            if (cursor.get_cursor_status() == MYSQL_STATUS_GET_RESULT)
            {
                LOG_F(INFO, "cursor_status=%d field_count=%u rows_count=%u\n",
                        cursor.get_cursor_status(), cursor.get_field_count(),
                        cursor.get_rows_count());

                //nocopy api
                fields = cursor.fetch_fields();
                for (int i = 0; i < cursor.get_field_count(); i++)
                {
                    if (i == 0)
                    {
                        LOG_F(INFO, "db=%s table=%s\n",
                            fields[i]->get_db().c_str(), fields[i]->get_table().c_str());
                        LOG_F(INFO, "  ---------- COLUMNS ----------\n");
                    }
                    LOG_F(INFO, "  name[%s] type[%s]\n",
                            fields[i]->get_name().c_str(),
                            datatype2str(fields[i]->get_data_type()));
                }
                LOG_F(INFO, "  _________ COLUMNS END _________\n\n");

                while (cursor.fetch_row(arr))
                {
                    LOG_F(INFO, "  ------------ ROW ------------\n");
                    for (size_t i = 0; i < arr.size(); i++)
                    {
                        LOG_F(INFO, "  [%s][%s]", fields[i]->get_name().c_str(),
                                datatype2str(arr[i].get_data_type()));
                        if (arr[i].is_string())
                        {
                            std::string res = arr[i].as_string();
                            if (res.length() == 0)
                                LOG_F(INFO, "[\"\"]\n");
                            else 
                                LOG_F(INFO, "[%s]\n", res.c_str());
                        } else if (arr[i].is_int()) {
                            LOG_F(INFO, "[%d]\n", arr[i].as_int());
                        } else if (arr[i].is_ulonglong()) {
                            LOG_F(INFO, "[%llu]\n", arr[i].as_ulonglong());
                        } else if (arr[i].is_float()) {
                            const void *ptr;
                            size_t len;
                            int data_type;
                            arr[i].get_cell_nocopy(&ptr, &len, &data_type);
                            size_t pos;
                            for (pos = 0; pos < len; pos++)
                                if (*((const char *)ptr + pos) == '.')
                                    break;
                            if (pos != len)
                                pos = len - pos - 1;
                            else
                                pos = 0;
                            LOG_F(INFO, "[%.*f]\n", (int)pos, arr[i].as_float());
                        } else if (arr[i].is_double()) {
                            const void *ptr;
                            size_t len;
                            int data_type;
                            arr[i].get_cell_nocopy(&ptr, &len, &data_type);
                            size_t pos;
                            for (pos = 0; pos < len; pos++)
                                if (*((const char *)ptr + pos) == '.')
                                    break;
                            if (pos != len)
                                pos = len - pos - 1;
                            else
                                pos= 0;
                            LOG_F(INFO, "[%.*lf]\n", (int)pos, arr[i].as_double());
                        } else if (arr[i].is_date()) {
                            LOG_F(INFO, "[%s]\n", arr[i].as_string().c_str());
                        } else if (arr[i].is_time()) {
                            LOG_F(INFO, "[%s]\n", arr[i].as_string().c_str());
                        } else if (arr[i].is_datetime()) {
                            LOG_F(INFO, "[%s]\n", arr[i].as_string().c_str());
                        } else if (arr[i].is_null()) {
                            LOG_F(INFO, "[NULL]\n");
                        } else {
                            std::string res = arr[i].as_binary_string();
                            if (res.length() == 0)
                                LOG_F(INFO, "[\"\"]\n");
                            else 
                                LOG_F(INFO, "[%s]\n", res.c_str());
                        }
                    }
                    LOG_F(INFO, "  __________ ROW END __________\n");
                }
            }
            else if (cursor.get_cursor_status() == MYSQL_STATUS_OK)
            {
                LOG_F(INFO, "  OK. %llu ", cursor.get_affected_rows());
                if (cursor.get_affected_rows() == 1)
                    LOG_F(INFO, "row ");
                else
                    LOG_F(INFO, "rows ");
                LOG_F(INFO, "affected. %d warnings. insert_id=%llu. %s\n",
                        cursor.get_warnings(), cursor.get_insert_id(),
                        cursor.get_info().c_str());
            }

            LOG_F(INFO, "________________ RESULT SET END ________________\n\n");
        } while (cursor.next_result_set());


        if (resp->get_packet_type() == MYSQL_PACKET_ERROR)
        {
            LOG_F(ERROR, "Error_code=%d %s\n",
                    task->get_resp()->get_error_code(),
                    task->get_resp()->get_error_msg().c_str());
            return WFT_STATE_TASK_ERROR;
        }
        else if (resp->get_packet_type() == MYSQL_PACKET_OK) // just check origin APIs
        {
            LOG_F(INFO, "OK. %llu ", task->get_resp()->get_affected_rows());
            if (task->get_resp()->get_affected_rows() == 1)
                LOG_F(INFO, "row ");
            else
                LOG_F(INFO, "rows ");
            LOG_F(INFO, "affected. %d warnings. insert_id=%llu. %s\n",
                    task->get_resp()->get_warnings(),
                    task->get_resp()->get_last_insert_id(),
                    task->get_resp()->get_info().c_str());
        }

        return WFT_STATE_SUCCESS;
    }

    int GLCCServer::parse_mysql_response(WFMySQLTask * task, std::unordered_map<std::string, std::vector<protocol::MySQLCell>> & results) {
        protocol::MySQLResponse *resp = task->get_resp();
        protocol::MySQLResultCursor cursor(resp);
        const protocol::MySQLField * const * fields;
        std::vector<protocol::MySQLCell> arr;
        do {
            if (cursor.get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
                break;
            }

            LOG_F(INFO, "---------------- RESULT SET ----------------\n");

            LOG_F(INFO, "cursor_status=%d field_count=%u rows_count=%u\n",
                    cursor.get_cursor_status(), cursor.get_field_count(),
                    cursor.get_rows_count());

            //nocopy api
            fields = cursor.fetch_fields();
            for (int i = 0; i < cursor.get_field_count(); i++) {
                if (i == 0)
                {
                    LOG_F(INFO, "db=%s table=%s\n",
                        fields[i]->get_db().c_str(), fields[i]->get_table().c_str());
                    LOG_F(INFO, "  ---------- COLUMNS ----------\n");
                }
                LOG_F(INFO, "  name[%s] type[%s]\n",
                        fields[i]->get_name().c_str(),
                        datatype2str(fields[i]->get_data_type()));
            }
            LOG_F(INFO, "  _________ COLUMNS END _________\n\n");

            while (cursor.fetch_row(arr)) {
                LOG_F(INFO, "  ------------ ROW ------------\n");
                for (size_t i = 0; i < arr.size(); i++)
                {
                    std::string column_name = fields[i]->get_name();
                    LOG_F(INFO, "  [%s][%s]",  column_name.c_str(),
                            datatype2str(arr[i].get_data_type()));
                    if (arr[i].is_string())
                    {
                        std::string res = arr[i].as_string();
                        if (res.length() == 0)
                            LOG_F(INFO, "[\"\"]\n");
                        else 
                            LOG_F(INFO, "[%s]\n", res.c_str());
                    } else if (arr[i].is_int()) {
                        LOG_F(INFO, "[%d]\n", arr[i].as_int());
                    } else if (arr[i].is_ulonglong()) {
                        LOG_F(INFO, "[%llu]\n", arr[i].as_ulonglong());
                    } else if (arr[i].is_float()) {
                        const void *ptr;
                        size_t len;
                        int data_type;
                        arr[i].get_cell_nocopy(&ptr, &len, &data_type);
                        size_t pos;
                        for (pos = 0; pos < len; pos++)
                            if (*((const char *)ptr + pos) == '.')
                                break;
                        if (pos != len)
                            pos = len - pos - 1;
                        else
                            pos = 0;
                        LOG_F(INFO, "[%.*f]\n", (int)pos, arr[i].as_float());
                    } else if (arr[i].is_double()) {
                        const void *ptr;
                        size_t len;
                        int data_type;
                        arr[i].get_cell_nocopy(&ptr, &len, &data_type);
                        size_t pos;
                        for (pos = 0; pos < len; pos++)
                            if (*((const char *)ptr + pos) == '.')
                                break;
                        if (pos != len)
                            pos = len - pos - 1;
                        else
                            pos= 0;
                        LOG_F(INFO, "[%.*lf]\n", (int)pos, arr[i].as_double());
                    } else if (arr[i].is_date()) {
                        LOG_F(INFO, "[%s]\n", arr[i].as_string().c_str());
                    } else if (arr[i].is_time()) {
                        LOG_F(INFO, "[%s]\n", arr[i].as_string().c_str());
                    } else if (arr[i].is_datetime()) {
                        LOG_F(INFO, "[%s]\n", arr[i].as_string().c_str());
                    } else if (arr[i].is_null()) {
                        LOG_F(INFO, "[NULL]\n");
                    } else {
                        std::string res = arr[i].as_binary_string();
                        if (res.length() == 0)
                            LOG_F(INFO, "[\"\"]\n");
                        else 
                            LOG_F(INFO, "[%s]\n", res.c_str());
                    }
                    results[column_name].emplace_back(std::move(arr[i]));
                }
                LOG_F(INFO, "  __________ ROW END __________\n");
            }
            LOG_F(INFO, "________________ RESULT SET END ________________\n\n");
        } while (cursor.next_result_set());


        if (resp->get_packet_type() == MYSQL_PACKET_ERROR)
        {
            LOG_F(INFO, "ERROR. error_code=%d %s\n",
                    task->get_resp()->get_error_code(),
                    task->get_resp()->get_error_msg().c_str());
            return WFT_STATE_TASK_ERROR;
        }
        else if (resp->get_packet_type() == MYSQL_PACKET_OK) // just check origin APIs
        {
            LOG_F(INFO, "OK. %llu ", task->get_resp()->get_affected_rows());
            if (task->get_resp()->get_affected_rows() == 1)
                LOG_F(INFO, "row ");
            else
                LOG_F(INFO, "rows ");
            LOG_F(INFO, "affected. %d warnings. insert_id=%llu. %s\n",
                    task->get_resp()->get_warnings(),
                    task->get_resp()->get_last_insert_id(),
                    task->get_resp()->get_info().c_str());
        }

        return WFT_STATE_SUCCESS;

    }
}