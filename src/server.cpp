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
            server_state = -1;
            LOG_F(ERROR, "[SERVER] Parse %s fail!", config_path.c_str());
            return;
        }
        glcc_server_context.server_context.ip = root["Server"]["server_ip"].asString();
        glcc_server_context.server_context.port = root["Server"]["server_port"].asInt64();
        glcc_server_context.detector_init_context = root["Detector"];
        glcc_server_context.server_dir.work_dir = root["Server"]["work_dir"].asString();
        glcc_server_ssl_context.ssl_crt_path = root["Server"]["ssl_crt_path"].asString();
        glcc_server_ssl_context.ssl_key_path = root["Server"]["ssl_key_path"].asString();
        ret = GLCC::check_dir(glcc_server_context.server_dir.work_dir.c_str(), true);
        if (ret == -1) {
            server_state = -1;
            LOG_F(ERROR, "[SERVER] Dir %s don't exist", glcc_server_context.server_dir.work_dir.c_str());
            return;
        }
        chdir(glcc_server_context.server_dir.work_dir.c_str());
        std::string cwd;
        ret = get_cwd(cwd);
        if (ret == -1) {
            server_state = -1;
            LOG_F(ERROR, "[SERVER] Get current work dir: %s fail", cwd.c_str());
        }
        LOG_F(INFO, "[SERVER] Current dir: %s", cwd.c_str());
    }

    int GLCCServer::run() {
        if (server_state == -1) {
            LOG_F(ERROR, "GLCCServer init fail!");
            return -1;
        }
        signal(SIGINT, sig_handler);
        signal(SIGTERM, sig_handler);
        int state = WFT_STATE_TOREPLY;
        // MySQL DB
        WFMySQLTask * db_task = WFTaskFactory::create_mysql_task(
            constants::mysql_root_url, 0, [&](WFMySQLTask * task) {
                create_db_callbck(task, &state);
        });
        db_task->get_req()->set_query(constants::mysql_create_db_command);
        db_task->start();
        mysql_wait_group.wait(); 

        if (state == WFT_STATE_SUCCESS) {
            WFTimerTask * file_timer_task = WFTaskFactory::create_timer_task(
                constants::interval_to_watch_file_second, file_timer_callback
            );
            file_timer_task->start();
            WFTimerTask * detector_timer_task = WFTaskFactory::create_timer_task(
                constants::interval_to_watch_detector_second, detector_timer_callback
            );
            detector_timer_task->start();

            WFHttpServer server([&](WFHttpTask * task) {
                main_callback(task, &glcc_server_context);
            });

            // TODO: figure it out
            int ret = server.start(glcc_server_context.server_context.port, 
                glcc_server_ssl_context.ssl_crt_path.c_str(),
                glcc_server_ssl_context.ssl_key_path.c_str());

            std::stringstream server_infos;
            get_server_infos(&server, server_infos);
            LOG_F(INFO, "[SERVER] %s", server_infos.str().c_str());
            if (ret == 0) {
                server_wait_group.wait();
                server.stop();
            } else {
                LOG_F(ERROR, "[SERVER] Start server fail!");
                return -1;
            }
            return 0;
        } else {
            LOG_F(ERROR, "[SERVER] Init database fail!");
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
            LOG_F(ERROR, "[SERVER] Init database fail! Code: %d", error);
            *ex_state_ptr = state;
        }
        mysql_wait_group.done();
    }

    void GLCCServer::main_callback(WFHttpTask * task, void * context) {
        std::stringstream connection_infos;
        get_connection_infos(task, connection_infos);
        LOG_F(INFO, "[SERVER] %s", connection_infos.str().c_str());
        REGEX_FUNC(hello_world_callback, "GET", "/hello_world", task);
        REGEX_FUNC(login_callback, "POST", "/login.*", task, context);
        REGEX_FUNC(user_register_callback, "POST", "/register", task,  context);
    }

    void GLCCServer::hello_world_callback(WFHttpTask * task) {
        int state = task->get_state();
        int error = task->get_error();
        if (state == WFT_STATE_TOREPLY) {
            protocol::HttpResponse * resp = task->get_resp();
            auto seq = task->get_task_seq();
            set_common_resp(resp, "200", "OK");
            std::string msg = "Hello World!";
            msg = "[SERVER] " + msg;
            LOG_F(INFO, msg.c_str());
            resp->append_output_body(msg);
            if (seq == 9) resp->add_header_pair("Connection", "close");
        } else {
            LOG_F(ERROR, "[SERVER] Hello World task fail! Code: %d", error);
        }
    }

    void GLCCServer::login_activity(WFHttpTask * task, void * context) {
        REGEX_FUNC(dect_video_callback, "POST", "/login/dect_video", task, context);
        REGEX_FUNC(disdect_video_callback, "POST", "/login/disdect_video", task, context);
        REGEX_FUNC(register_video_callback, "POST", "/login/register_video", task, context);
        REGEX_FUNC(delete_video_callback, "POST", "/login/delete_video", task, context);
        REGEX_FUNC(video_put_lattice_callback, "POST", "/login/put_lattice", task, context);
        REGEX_FUNC(video_disput_lattice_callback, "POST", "/login/disput_lattice", task, context);
        REGEX_FUNC(delete_video_file_callback, "POST", "/login/delete_video_file", task, context);
        REGEX_FUNC(fetch_video_file_callback, "POST", "/login/fetch_video_file", task, context);
        REGEX_FUNC(dect_video_file_callback, "POST", "/login/dect_video_file", task, context);
        REGEX_FUNC(kick_dect_video_file_callback, "POST", "/login/kick_dect_video_file", task, context);
        REGEX_FUNC(transmiss_video_file_callback, "POST", "/login/transmiss_video_file", task, context);
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
                set_common_resp(resp, "400", "Bad Request");
                LOG_F(ERROR, "[SERVER][LOGIN] Parse %s fail!", body_text.c_str());
                return;
            }
            if (!root.isMember("user_name") || !root.isMember("user_password")) {
                set_common_resp(resp, "400", "Bad Request");
                LOG_F(ERROR, "[SERVER][LOGIN] Find request body key: %s, %s fail!", "user_name", "user_password");
                return;
            }
            std::string user_name = root["user_name"].asString();
            std::string user_password = root["user_password"].asString();
            Json::Value resp_root;
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0, 
                [user_name, user_password, context](WFMySQLTask * task){
                    int state = task->get_state(); int error = task->get_error();
                    WFHttpTask * http_task = (WFHttpTask *)task->user_data;
                    protocol::HttpResponse * http_resp = http_task->get_resp();
                    Json::Value root;
                    if (state == WFT_STATE_SUCCESS) {
                        std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                        parse_mysql_response(task, results);
                        const bool only_login = REGEX_VALUE("/login", http_task);
                        auto & work_dir = ((glcc_server_context_t *)context)->server_dir.work_dir;
                        LOG_F(INFO, "[SERVER][LOGIN][%s] Only Login: %s", user_name.c_str(), only_login ? "yes" : "no");
                        if (results.size() > 0) {
                            if (user_name == results["username"][0].as_string() \
                                && user_password == results["password"][0].as_string()) {
                                if (only_login) {
                                    WFMySQLTask * dump_info_task = WFTaskFactory::create_mysql_task(
                                        constants::mysql_glccserver_url, 0,
                                        [user_name, user_password, work_dir](WFMySQLTask * task) {
                                            int state = task->get_state(); int error = task->get_error();
                                            Json::Value reply;
                                            reply["user_name"] = user_name;
                                            reply["user_password"] = user_password;
                                            protocol::HttpResponse * up_resp = (protocol::HttpResponse *) task->user_data;
                                            if (state == WFT_STATE_SUCCESS) {
                                                std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                                                parse_mysql_response(task, results);
                                                if (results.size() > 0) {
                                                    reply["msg"] = "login success";
                                                    if (results.find("video_name") != results.end()) {
                                                        std::vector<protocol::MySQLCell> & video_names =  results["video_name"];
                                                        std::vector<protocol::MySQLCell> & video_urls = results["video_url"];
                                                        for (int i = 0; i < (int)video_names.size(); i++) {
                                                            reply["video_name"].append(video_names[i].as_string());
                                                            reply["video_url"].append(video_urls[i].as_string());
                                                        }
                                                    }
                                                    if (results.find("contour_name") != results.end()) {
                                                        std::vector<protocol::MySQLCell> & contour_name =  results["contour_name"];
                                                        std::vector<protocol::MySQLCell> & contour_path = results["contour_path"];
                                                        std::vector<protocol::MySQLCell> & contour_video_name =  results["contour_video_name"];
                                                        Json::Reader reader; Json::Value value;
                                                        for (int i = 0; i < (int)contour_name.size(); i++) {
                                                            value.clear();
                                                            reply["contour_name"].append(contour_name[i].as_string());
                                                            std::string contour_path_str = contour_path[i].as_string();
                                                            reader.parse(contour_path_str, value);
                                                            reply["contour_path"].append(value);
                                                            reply["contour_video_name"].append(contour_video_name[i].as_string());
                                                        }
                                                    }
                                                    set_common_resp(up_resp, "200", "OK");
                                                    up_resp->append_output_body(reply.toStyledString());
                                                    std::string user_dir = work_dir + "/" + user_name;
                                                    std::string user_custom_dir = user_dir + "/" + "custom";
                                                    check_dir(user_dir.c_str(), true);
                                                    check_dir(user_custom_dir.c_str(), true);
                                                    LOG_F(INFO, "[SERVER][LOGIN][%s] Login success!", user_name.c_str());
                                                } else {
                                                    set_common_resp(up_resp, "200", "OK");
                                                    reply["msg"] = "login success!";
                                                    up_resp->append_output_body(reply.toStyledString());
                                                    LOG_F(INFO, "[SERVER][LOGIN][%s] Login success", user_name.c_str());
                                                }
                                            } else {
                                                set_common_resp(up_resp, "400", "Bad Request");
                                                reply["msg"] = "Error in Server Sql connect";
                                                up_resp->append_output_body(reply.toStyledString());
                                                LOG_F(ERROR, "[SERVER][LOGIN][%s] Connect server sql fail! Code: %d", user_name.c_str(), error);
                                            }
                                        }
                                    );
                                    dump_info_task->user_data = http_resp;
                                    char mysql_query[512];
                                    std::snprintf(mysql_query, sizeof(mysql_query), 
                                        "SELECT video_name, video_url FROM glccserver.Video WHERE username=\"%s\";"
                                        "SELECT glccserver.Contour.video_name as contour_video_name, glccserver.Contour.contour_name,"
                                        "glccserver.Contour.contour_path from glccserver.Contour, glccserver.Video "
                                        "where glccserver.Contour.video_name=glccserver.Video.video_name and glccserver.Video.username=\"%s\" "
                                        "and glccserver.Video.username=glccserver.Contour.username;", 
                                        user_name.c_str(), user_name.c_str());
                                    dump_info_task->get_req()->set_query(mysql_query);
                                    *series_of(task)<<dump_info_task;
                                } else {
                                    login_activity(http_task, context);
                                }
                            } else {
                                set_common_resp(http_resp, "400", "Bad Request");
                                root["msg"] = "Error user_name or user_password";
                                http_resp->append_output_body(root.toStyledString());
                                LOG_F(ERROR, "[SERVER][LOGIN][%s] Error user_name or user_password", user_name.c_str());
                            }
                        } else {
                            set_common_resp(http_resp, "400", "Bad Request");
                            root["msg"] = "Error user_name or user_password";
                            http_resp->append_output_body(root.toStyledString());
                            LOG_F(ERROR, "[SERVER][LOGIN][%s] Error user_name or user_password", user_name.c_str());
                        }
                    } else {
                        set_common_resp(http_resp, "400", "Bad Request");
                        root["msg"] = "Error user_name or user_password";
                        http_resp->append_output_body(root.toStyledString());
                        LOG_F(ERROR, "[SERVER][LOGIN][%s] Connect to mysql fail! Code: %d", user_name.c_str(), error);
                    }
                }
            );
            char mysql_query[256];
            std::snprintf(mysql_query, sizeof(mysql_query), 
                "SELECT * FROM User WHERE username=\"%s\" AND password=\"%s\";",
                 user_name.c_str(), user_password.c_str());
            mysql_task->user_data = task;
            mysql_task->get_req()->set_query(mysql_query);
            *series_of(task) << mysql_task;
        } else {
            LOG_F(ERROR, "[SERVER][LOGIN] Login fail! Code: %d", error);
        }
    }


    void GLCCServer::user_register_callback(WFHttpTask * task, void * context) {
        int state = task->get_state(); int error = task->get_error();
        if (state == WFT_STATE_TOREPLY) {
            protocol::HttpRequest * req = task->get_req();
            protocol::HttpResponse * resp = task->get_resp();
            auto & work_dir = ((glcc_server_context_t *) context)->server_dir.work_dir;
            const void * body; size_t body_len;
            req->get_parsed_body(&body, &body_len);
            const std::string body_text = (const char *)body;
            Json::Value root; Json::Reader reader;
            int ret = reader.parse(body_text, root);
            if (!ret) {
                LOG_F(ERROR, "[SERVER][REGISTER] Parse %s fail!", body_text.c_str());
                set_common_resp(resp, "400", "Bad Request");
                return;
            }
            if (!root.isMember("user_name") || !root.isMember("user_password") || !root.isMember("user_nickname")) {
                LOG_F(ERROR, "[SERVER][REGISTER] Find request body key: %s, %s fail!", "use_name", "video_password");
                set_common_resp(resp, "400", "Bad Request");
                return;
            }
            std::string user_name = root["user_name"].asString();
            std::string user_password = root["user_password"].asString();
            std::string user_nickname = root["user_nickname"].asString();

            // creat sql task
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0, 
                [user_name, user_password, user_nickname, work_dir](WFMySQLTask * task){
                    int state = task->get_state();
                    int error = task->get_error();
                    WFHttpTask * http_task = (WFHttpTask * )task->user_data;
                    protocol::HttpResponse * http_resp = http_task->get_resp();
                    if (state == WFT_STATE_SUCCESS) {
                        int parse_state = parse_mysql_response(task);
                        if (parse_state != WFT_STATE_SUCCESS) {
                            set_common_resp(http_resp, "400", "Bad Request");
                            LOG_F(ERROR, "[SERVER][REGISTER][%s] Register fail!", user_name.c_str());
                        } else {
                            std::string user_dir = work_dir + "/" + user_name;
                            std::string custom_dir = user_dir + "/" + "custom";
                            check_dir(user_dir, true); check_dir(custom_dir, true);
                            set_common_resp(http_resp, "200", "OK");
                            LOG_F(INFO, "[SERVER][REGISTER][%s] Register success!", user_name.c_str());
                        }
                    } else {
                        set_common_resp(http_resp, "400", "Bad Request");
                        LOG_F(INFO, "[SERVER][REGISTER][%s] Register fail! Code: %d", user_name.c_str(), error);
                    }
                }
            );
            mysql_task->user_data = task;
            char mysql_query[256];
            std::snprintf(mysql_query, sizeof(mysql_query), 
                "INSERT INTO glccserver.User(username, password, nickname) VALUES (\"%s\", \"%s\", \"%s\");",
                 user_name.c_str(), user_password.c_str(), user_nickname.c_str());
            mysql_task->get_req()->set_query(mysql_query);
            *series_of(task) << mysql_task;
        } else {
            LOG_F(ERROR, "[SERVER][REGISTER] Register fail! Code: %d", error);
        }
    }

    void GLCCServer::dect_video_file_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();

        const void * body; size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *) body;
        Json::Value root; Json::Reader reader;
        int ret = reader.parse(body_text, root);

        if (!ret) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][DECT_VIDEO_FILE] Parse %s fail!", body_text.c_str());
            return;
        }

        std::string user_name = root["user_name"].asString();
        std::string user_password = root["user_password"].asString();

        if (!root.isMember("video_url") && !root.isMember("video_name")) {
            LOG_F(ERROR, "[SERVER][DECT_VIDEO_FILE][%s] Find request body key: %s, %s fail!",
                user_name.c_str(), "video_url", "video_name");
            set_common_resp(resp, "400", "Bad Request");
            return;
        }

        std::string video_url = root["video_url"].asString();
        std::string video_name = root["video_name"].asString();

        glcc_server_context_t * glcc_context = (glcc_server_context_t *) context;

        auto & work_dir = glcc_context->server_dir.work_dir;
        std::string user_dir = work_dir + "/" + user_name;
        std::string video_dir = user_dir + "/" + video_name;
        std::string video_path = video_dir + "/" + video_url;

        if (check_file(video_path, nullptr, {}) < 0) {
            LOG_F(ERROR, "[SERVER][DECT_VIDEO_FILE][%s][%s] Path %s don't exist!",
                user_name.c_str(), video_name.c_str(), video_url.c_str());
            set_common_resp(resp, "400", "Bad Request");
            return;
        }

        std::unordered_map<std::string, std::string> path_parse_result;
        ret = parse_path(video_path, path_parse_result);
        if (ret == -1) {
            LOG_F(ERROR, "[SERVER][DECT_VIDEO_FILE][%s][%s] Parse %s fail!",
                user_name.c_str(), video_name.c_str(), video_path.c_str());
            set_common_resp(resp, "400", "Bad Request");
            return;
        }
        std::string stem = path_parse_result["stem"];
        std::string room_name = video_name + "-" + stem;
        char room_url[256];
        std::snprintf(room_url, sizeof(room_url), 
            constants::livego_push_url_template.c_str(), 
            room_name.c_str());
        char push_file_command[512];
        std::snprintf(push_file_command, sizeof(push_file_command), 
            constants::ffmpeg_file_push_command.c_str(), 
            video_path.c_str(), room_url);
        std::string command = push_file_command;
        WFGoTask * go_task = WFTaskFactory::create_go_task(
            "video_file_run", [](std::string command){
                LOG_F(INFO, "[SERVER][DECT_VIDEO_FILE][GO] Push Command: %s", 
                    command.c_str());
                system(command.c_str());
            }, command
        );
        go_task->set_callback([user_name, video_name, room_name](WFGoTask * task){
            int state = task->get_state(); int error = task->get_error();
            if (state == WFT_STATE_SUCCESS) {
                LOG_F(INFO, "[SERVER][DECT_VIDEO_FILE][%s][%s][%s] run push video file complete!",
                    user_name.c_str(), video_name.c_str(), room_name.c_str());
            } else {
                LOG_F(ERROR, "[SERVER][DECT_VIDEO_FILE][%s][%s][%s] run push video file fail! Code: %d",
                    user_name.c_str(), video_name.c_str(), room_name.c_str(), error);
            }
        });
        *series_of(task) << go_task;
        // go_task->start();
        LOG_F(INFO, "[SERVER][DECT_VIDEO_FILE][%s][%s] Push Command: %s", 
            user_name.c_str(), video_name.c_str(), push_file_command);
        root["room_name"] = room_name;
        set_common_resp(resp, "200", "OK");
        resp->append_output_body(root.toStyledString());
    }

    void GLCCServer::kick_dect_video_file_callback(WFHttpTask * task, void * context) {
        SeriesWork * series = series_of(task);
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();

        const void * body;
        size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *) body;

        Json::Value root;
        Json::Reader reader;
        reader.parse(body_text, root);

        std::string user_name = root["user_name"].asString();

        if (!root.isMember("room_name")) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][KICK_DECT_VIDEO_FILE][%s] Find request body key: %s fail!", 
                user_name.c_str(), "room_name");
            return;
        }

        std::string room_name = root["room_name"].asString();

        char livego_check_stat_url[256] = {0};
        std::snprintf(livego_check_stat_url, sizeof(livego_check_stat_url),
            constants::livego_check_stat_template.c_str(), room_name.c_str());

        WFHttpTask * check_state_http_task = WFTaskFactory::create_http_task(
            livego_check_stat_url, 0, 0,
            [user_name, room_name] (WFHttpTask * task) {
                int state = task->get_state(); int error = task->get_error();
                protocol::HttpResponse * up_resp = (protocol::HttpResponse *) series_of(task)->get_context();
                if (state == WFT_STATE_SUCCESS) {
                    protocol::HttpResponse * resp = task->get_resp();
                    SeriesWork * series = series_of(task);
                    const void * body; size_t body_len;
                    resp->get_parsed_body(&body, &body_len);
                    Json::Value root; Json::Reader reader;
                    std::string body_text = (const char *) body;
                    reader.parse(body_text, root);
                    Json::Value subs = root["data"]["subs"];
                    Json::Value pub = root["data"]["pub"];
                    LOG_F(INFO, "[SERVER][KICK_DECT_VIDEO_FILE][%s][%s] \n pub: %s \n subs: %s",
                        user_name.c_str(), room_name.c_str(), pub.toStyledString().c_str(), subs.toStyledString().c_str());
                    std::string session_id;
                    for (int i = 0; i < (int) subs.size(); i++) {
                        session_id = subs[i]["session_id"].asString();
                        Json::Value kick_session_body;
                        kick_session_body["stream_name"] = room_name;
                        kick_session_body["session_id"] = session_id;
                        WFHttpTask * kick_sub_task = WFTaskFactory::create_http_task(
                            constants::livego_kick_url, 0, 0,
                            [user_name, room_name, session_id](WFHttpTask * task) {
                                int state = task->get_state(); int error = task->get_error();
                                if (state == WFT_STATE_SUCCESS) {
                                    protocol::HttpResponse * resp = task->get_resp();
                                    const void * body; size_t body_len;
                                    resp->get_parsed_body(&body, &body_len);
                                    Json::Value root; Json::Reader reader;
                                    reader.parse((const char * )body, root);
                                    LOG_F(INFO, 
                                        "[SERVER][KICK_DECT_VIDEO_FILE][KICK_SUB][%s][%s][%s] Response: %s\n",
                                        user_name.c_str(), room_name.c_str(), session_id.c_str(), root.toStyledString().c_str());
                                } else {
                                    LOG_F(ERROR, 
                                        "[SERVER][KICK_DECT_VIDEO_FILE][KICK_SUB][%s][%s][%s] Request fail! Code: %d",
                                        user_name.c_str(), room_name.c_str(), session_id.c_str(), error);
                                }
                            }
                        );
                        kick_sub_task->get_req()->append_output_body(kick_session_body.toStyledString());
                        set_common_req(kick_sub_task->get_req(), "*/*", "close", HttpMethodPost);
                        *series << kick_sub_task;
                    }

                    Json::Value kick_session_body;
                    session_id = pub["session_id"].asString();
                    kick_session_body["stream_name"] = room_name;
                    kick_session_body["session_id"] = session_id;
                    WFHttpTask * kick_pub_task = WFTaskFactory::create_http_task(
                        constants::livego_kick_url, 0, 0,
                        [user_name, room_name, session_id](WFHttpTask * task) {
                            int state = task->get_state(); int error = task->get_error();
                            if (state == WFT_STATE_SUCCESS) {
                                if (state == WFT_STATE_SUCCESS) {
                                    protocol::HttpResponse * resp = task->get_resp();
                                    const void * body; size_t body_len;
                                    resp->get_parsed_body(&body, &body_len);
                                    Json::Value root; Json::Reader reader;
                                    reader.parse((const char * )body, root);
                                    LOG_F(INFO, 
                                        "[SERVER][KICK_DECT_VIDEO_FILE][KICK_PUB][%s][%s][%s] Response: %s\n",
                                        user_name.c_str(), room_name.c_str(), session_id.c_str(), root.toStyledString().c_str());
                                } else {
                                    LOG_F(ERROR, 
                                        "[SERVER][KICK_DECT_VIDEO_FILE][KICK_PUB][%s][%s][%s] Request fail! Code: %d",
                                        user_name.c_str(), room_name.c_str(), session_id.c_str(), error);
                                }
                            }
                        }
                    );
                    kick_pub_task->get_req()->append_output_body(kick_session_body.toStyledString());
                    set_common_req(kick_pub_task->get_req(), "*/*", "close", HttpMethodPost);
                    *series << kick_pub_task;
                    set_common_resp(up_resp, "200", "OK");
                } else {
                    set_common_resp(up_resp, "400", "Bad Request");
                    LOG_F(ERROR, "[SERVER][KICK_DECT_VIDEO_FILE][%s][%s] Check connect fail! Code: %d",
                        user_name.c_str(), room_name.c_str(), error);
                }
            }
        );
        series->set_context(resp);
        *series << check_state_http_task;
    }

    void GLCCServer::transmiss_video_file_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
        SeriesWork * series = series_of(task);
        glcc_server_context_t * glcc_context = (glcc_server_context_t *) context;
        auto & work_dir = glcc_context->server_dir.work_dir;
        const void * body; size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *) body;
        Json::Value root; Json::Reader reader;
        int ret = reader.parse(body_text, root);
        std::string user_name = root["user_name"].asString();

        if (!ret) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][TRANSMISS_VIDEO_FILE][%s] Parse %s fail!", 
                user_name.c_str(), body_text.c_str());
            return;
        }

        if (!root.isMember("video_url") && !root.isMember("video_name")) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][DELETE_VIDEO_FILE][%s] Find request body key: %s, %s fail!", 
                user_name.c_str(), "video_name", "video_url");
            return;
        }
        

        std::string video_name = root["video_name"].asString();
        std::string video_url = root["video_url"].asString();
        std::string user_dir = work_dir + "/" + user_name;
        std::string video_dir = user_dir + "/" + video_name;
        std::string video_path = video_dir + "/" + video_url;

        int fd = open(video_path.c_str(), O_RDONLY);
        if (fd >= 0) {
            size_t size = lseek(fd, 0, SEEK_END);
            void * buf = malloc(size);
            WFFileIOTask * pread_task;
            pread_task = WFTaskFactory::create_pread_task(
                fd, buf, size, 0, 
                [user_name, video_name, video_path](WFFileIOTask * task) {
                    int state = task->get_state(); int error = task->get_error();
                    protocol::HttpResponse * up_resp = (protocol::HttpResponse *) task->user_data;
                    FileIOArgs * args = task->get_args(); auto ret = task->get_retval();
                    close(args->fd);
                    if (state == WFT_STATE_SUCCESS) {
                        set_common_resp(up_resp, "200", "OK", "HTTP/1.1", "image/jpeg");
                        up_resp->append_output_body_nocopy(args->buf, ret);
                        LOG_F(INFO, "[SERVER][TRANSMISS_VIDEO_FILE][%s][%s] Transmiss %s success!",
                            user_name.c_str(), video_name.c_str(), video_path.c_str());
                    } else {
                        set_common_resp(up_resp, "404", "Not Found");
                        LOG_F(ERROR, "[SERVER][TRANSMISS_VIDEO_FILE][%s][%s] Write %s fail! Code: %d", 
                            user_name.c_str(), video_name.c_str(), video_path.c_str(), error);
                    }
                }
            );
            pread_task->user_data = resp;
            series->set_context(buf);
            task->set_callback(
                [user_name, video_name, video_path](WFHttpTask * task) {
                    SeriesWork * series = series_of(task);
                    void * buf = series->get_context();
                    free(buf);
                    LOG_F(INFO, "[SERVER][TRANSMISS_VIDEO_FILE][%s][%s] Release %s buf success!",
                        user_name.c_str(), video_name.c_str(), video_path.c_str());
                }
            );
            *series << pread_task;
        } else {
            set_common_resp(resp, "404", "Not Found");
            LOG_F(ERROR, "[SERVER][TRANSMISS_VIDEO_FILE][%s][%s] Write %s fail!", 
                user_name.c_str(), video_name.c_str(), video_path.c_str());
        }
    }

    void GLCCServer::delete_video_file_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
        glcc_server_context_t * glcc_context = (glcc_server_context_t *) context;
        auto & work_dir = glcc_context->server_dir.work_dir;
        const void * body; size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *) body;
        Json::Value root; Json::Reader reader;
        int ret = reader.parse(body_text, root);

        std::string user_name = root["user_name"].asString();

        if (!ret) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][DELETE_VIDEO_FILE][%s] Parse %s fail!", 
                user_name.c_str(), body_text.c_str());
            return;
        }
        
        if (!root.isMember("video_url") && !root.isMember("video_name")) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][DELETE_VIDEO_FILE][%s] Find request body key: %s fail!", 
                user_name.c_str(), "video_url");
            return;
        }


        std::string video_name = root["video_name"].asString();

        std::string user_dir = work_dir + "/" + user_name;
        std::string video_dir = user_dir + "/" + video_name;

        std::string video_path;
        std::vector<std::string> delete_success_file;
        if (root["video_url"].isArray()) {
            for (auto i = 0; i < (int)root["video_url"].size(); i++) {
                std::string video_url = root["video_url"][i].asString();
                video_path = video_dir + "/" + video_url;
                ret = remove(video_path.c_str());
                if (ret == 0) {
                    delete_success_file.emplace_back(video_path);
                    LOG_F(INFO, "[SERVER][DELETE_VIDEO_FILE][%s][%s] delete %s success!", 
                        user_name.c_str(), video_name.c_str(), video_url.c_str());
                } else {
                    LOG_F(WARNING, "[SERVER][DELETE_VIDOE_FILE][%s][%s] delete %s fail!", 
                        user_name.c_str(), video_name.c_str(), video_url.c_str());
                }
            }
        } else {
            std::string video_url = root["video_url"].asString();
            video_path = video_dir + "/" + video_url;
            ret = remove(video_path.c_str());
            if (ret == 0) {
                delete_success_file.emplace_back(video_path);
                LOG_F(INFO, "[SERVER][DELETE_VIDEO_FILE][%s][%s] delete %s success!",
                    user_name.c_str(), video_name.c_str(), video_url.c_str());
            } else {
                LOG_F(WARNING, "[SERVER][DELETE_VIDEO_FILE][%s][%s] delete %s fail!", 
                    user_name.c_str(), video_name.c_str(), video_url.c_str());
            }
        }

        WFMySQLTask * mysql_task;

        mysql_task = WFTaskFactory::create_mysql_task(
            constants::mysql_glccserver_url, 0, 
            [user_name, video_name](WFMySQLTask * task){
                int state = task->get_state(); int error = task->get_error();
                if (state == WFT_STATE_SUCCESS) {
                    LOG_F(INFO, "[SERVER][DELETE_VIDEO_FILE][%s][%s] Mysql Delete task success!", 
                        user_name.c_str(), video_name.c_str());
                } else {
                    LOG_F(INFO, "[SERVER][DELETE_VIDEO_FILE][%s][%s] Mysql Delete task failed! Code: %d", 
                        user_name.c_str(), video_name.c_str(), error);
                }
            }
        );

        // letter accumulate function
        auto accumulate_func = [](std::string & x, std::string & y) {
            return x.empty() ? ("\"" + y + "\"") : (x + "," + "\"" + y + "\"");
        };

        auto file_str_concat = join(delete_success_file, "", accumulate_func); 
        std::stringstream mysql_delete_query;
        mysql_delete_query << "delete from glccserver.FILE where "
                           << "username=" << "\"" << user_name << "\""  << " and "
                           << "video_name=" << "\"" << video_name << "\""  << " and " 
                           << "file_path in (" << file_str_concat << ");";

        mysql_task->get_req()->set_query(mysql_delete_query.str());
        *series_of(task) << mysql_task;
        set_common_resp(resp, "200", "OK");
    }

    void GLCCServer::fetch_video_file_callback(WFHttpTask * task, void * context) {
        // static std::string cover_suffix = "jpg";
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
        glcc_server_context_t * glcc_context = (glcc_server_context_t *) context;
        auto & work_dir = glcc_context->server_dir.work_dir;
        const void * body; size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *)body;
        Json::Value root; Json::Reader reader;
        int ret = reader.parse(body_text, root);

        std::string user_name = root["user_name"].asString();

        if (!ret) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][FETCH_VIDEO_FILE][%s] Parse %s fail!", 
                user_name.c_str(), body_text.c_str());
            return;
        }

        if (!root.isMember("video_name")) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][FETCH_VIDEO_FILE][%s] Find request body key: %s fail!", 
                user_name.c_str(), "video_name");
            return;
        }

        std::string user_dir = work_dir + "/" + user_name;

        std::string video_dir;
        SeriesWork * series = series_of(task);
        
        if (root["video_name"].isArray()) {
            std::string video_name;
            std::vector<std::string> video_names;
            for (auto i = 0; i < (int)root["video_name"].size(); i++) {
                video_names.emplace_back(root["video_name"][i].asString());
            }
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0, 
                [user_name] (WFMySQLTask * task) {
                    int state = task->get_state(); int error = task->get_error();
                    SeriesWork * series = series_of(task);
                    WFHttpTask * up_task = (WFHttpTask *) series->get_context(); 
                    protocol::HttpResponse * up_resp = (protocol::HttpResponse *) up_task->get_resp();
                    Json::Value reply;
                    if (state == WFT_STATE_SUCCESS) {
                        std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                        int parse_state = parse_mysql_response(task, results);
                        if (parse_state == WFT_STATE_SUCCESS) {
                            if (results.size() > 0) {
                                if (results.find("video_name") != results.end()) {
                                    auto & video_names = results["video_name"];
                                    auto & file_paths = results["file_path"];
                                    auto & start_times = results["start_time"];
                                    auto & end_times = results["end_time"];
                                    std::string video_name;
                                    std::string file_path;
                                    std::string start_time;
                                    std::string end_time;
                                    for (auto i = 0; i < (int)video_names.size(); i++) {
                                        video_name = video_names[i].as_string();
                                        file_path = file_paths[i].as_string();
                                        start_time = start_times[i].as_string();
                                        end_time = end_times[i].as_string();

                                        std::unordered_map<std::string, std::string> path_parse_results;
                                        int ret = parse_path(file_path, path_parse_results);
                                        if (ret == -1) {
                                            continue;
                                            LOG_F(WARNING, "[SERVER][FETCH_VIDEO_FILE][%s][%s] Parse %s fail!", 
                                                user_name.c_str(), video_name.c_str(), file_path.c_str());
                                        }
                                        auto & dirname = path_parse_results["dirname"];
                                        auto & stem = path_parse_results["stem"];
                                        auto & basename = path_parse_results["basename"];
                                        std::string cover_path = dirname + "/" + stem + "." + constants::cover_save_suffix;
                                        if (check_file(cover_path) < 0) {
                                            LOG_F(WARNING, "[SERVER][FETCH_VIDEO_FILE][%s][%s] %s don't exist! Will create one", 
                                                user_name.c_str(), video_name.c_str(), cover_path.c_str());
                                            std::string command = "ffmpeg -y -i " + file_path + " -ss 1 -frames:v 1 " + cover_path;
                                            system(command.c_str());
                                        }
                                        Json::Value item;
                                        item["video_url"] = basename;
                                        item["start_time"] = start_time;
                                        item["end_time"] = end_time;
                                        reply[video_name].append(item);
                                    }
                                    set_common_resp(up_resp, "200", "OK");
                                    LOG_F(INFO, "[SERVER][FETCH_VIDEO_FILE][%s] Fetch video files success! Body: %s", 
                                        user_name.c_str(), reply.toStyledString().c_str());
                                } else {
                                    set_common_resp(up_resp, "404", "Not Found");
                                    LOG_F(WARNING, "[SERVER][FETCH_VIDEO_FILE][%s] Fetch video files fail!", 
                                        user_name.c_str());
                                }
                            } else {
                                set_common_resp(up_resp, "404", "Not Found");
                                LOG_F(WARNING, "[SERVER][FETCH_VIDEO_FILE][%s] Fetch zero recorder!",
                                    user_name.c_str());
                            }
                            up_resp->append_output_body(reply.toStyledString());
                        } else {
                            set_common_resp(up_resp, "400", "Bad Request");
                            LOG_F(ERROR, "[SERVER][FETCH_VIDEO_FILE][%s] Parse mysql results task fail! Code: %d",
                                user_name.c_str(), error);
                        }
                    } else {
                        set_common_resp(up_resp, "400", "Bad Request");
                        LOG_F(ERROR, "[SERVER][FETCH_VIDEO_FILE][%s] Parse mysql results task fail!",
                            user_name.c_str());
                    }
                }
            );
            auto accumulate_func = [](std::string & x, std::string & y) {
                return x.empty() ? ("\"" + y + "\"") : (x + "," + "\"" + y + "\"");
            };

            auto video_name_concat = join(video_names, "", accumulate_func);
            std::stringstream mysql_check_query;
            mysql_check_query << "SELECT video_name, file_path, start_time, end_time FROM glccserver.File WHERE "
                              << "username=" << "\"" << user_name << "\"" << " AND "
                              << "video_name IN (" << video_name_concat << ");";
            mysql_task->get_req()->set_query(mysql_check_query.str());
            series->set_context(task);
            *series << mysql_task;
        } else {
            std::string video_name = root["video_name"].asString();
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0, 
                [user_name, video_name] (WFMySQLTask * task) {
                    int state = task->get_state(); int error = task->get_error();
                    SeriesWork * series = series_of(task);
                    WFHttpTask * up_task = (WFHttpTask *) series->get_context(); 
                    protocol::HttpResponse * up_resp = (protocol::HttpResponse *) up_task->get_resp();
                    Json::Value reply;
                    if (state == WFT_STATE_SUCCESS) {
                        std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                        int parse_state = parse_mysql_response(task, results);
                        if (parse_state == WFT_STATE_SUCCESS) {
                            if (results.size() > 0) {
                                if (results.find("file_path") != results.end()) {
                                    auto & file_paths = results["file_path"];
                                    auto & start_times = results["start_time"];
                                    auto & end_times = results["end_time"];
                                    std::string file_path;
                                    std::string start_time;
                                    std::string end_time;
                                    for (auto i = 0; i < (int)file_paths.size(); i++) {
                                        file_path = file_paths[i].as_string();
                                        start_time = start_times[i].as_string();
                                        end_time = end_times[i].as_string();
                                        std::unordered_map<std::string, std::string> path_parse_results;
                                        int ret = parse_path(file_path, path_parse_results);
                                        if (ret == -1) {
                                            continue;
                                            LOG_F(ERROR, "[SERVER][FETCH_VIDEO_FILE][%s] Parse %s fail!", 
                                                user_name.c_str(), file_path.c_str());
                                        }
                                        auto & dirname = path_parse_results["dirname"];
                                        auto & stem = path_parse_results["stem"];
                                        auto & basename = path_parse_results["basename"];

                                        std::string cover_path = dirname + "/" + stem + constants::cover_save_suffix;
                                        if (check_file(cover_path) < 0) {
                                            LOG_F(WARNING, "[SERVER][FETCH_VIDEO_FILE][%s] %s don't exist!", 
                                                user_name.c_str(), cover_path.c_str());
                                            std::string command = "ffmpeg -y -i " + file_path + " -ss 1 -frames:v 1 " + cover_path;
                                            system(command.c_str());
                                        }
                                        Json::Value item;
                                        item["video_url"] = basename;
                                        item["start_time"] = start_time;
                                        item["end_time"] = end_time;
                                        reply[video_name].append(item);
                                    }
                                } else {
                                    LOG_F(WARNING, "[SERVER][FETCH_VIDEO_FILE][%s] Fetch video files fail!", 
                                        user_name.c_str());
                                }
                            } else {
                                LOG_F(WARNING, "[SERVER][FETCh_VIDEO_FILE][%s] Fetch zero recorder!",
                                    user_name.c_str());
                            }
                            up_resp->append_output_body(reply.toStyledString());
                        } else {
                            set_common_resp(up_resp, "400", "Bad Request");
                            LOG_F(ERROR, "[SERVER][FETCH_VIDEO_FILE][%s] Parse mysql results task fail!",
                                user_name.c_str());
                        }
                    } else {
                        set_common_resp(up_resp, "400", "Bad Request");
                        LOG_F(ERROR, "[SERVER][FETCH_VIDEO_FILE][%s] Parse mysql results task fail! Code: %d",
                            user_name.c_str(), error);
                    }
                }
            );
            std::stringstream mysql_check_query;
            mysql_check_query << "select file_path, start_time, end_time from glccserver.FILE where "
                              << "username=" << "\"" << user_name << "\""
                              << "video_name=" << "\"" << video_name << "\"" 
                              << ";";
            mysql_task->get_req()->set_query(mysql_check_query.str());
            series->set_context(task);
            *series << mysql_task;
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
        std::string user_name = root["user_name"].asString();
        std::string user_password = root["user_password"].asString();

        if (!root.isMember("video_url") && !root.isMember("video_name")) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][DECT][%s] Find request body key: %s, %s fail!", 
                user_name.c_str(), "video_url", "video_name");
            return;
        }

        std::string video_url = root["video_url"].asString();
        std::string video_name = root["video_name"].asString();

        char room_name[256] = {0};
        std::snprintf(room_name, sizeof(room_name), "%s_%s_%s", 
            user_name.c_str(), user_password.c_str(), video_name.c_str());
        char livego_push_url[256] = {0};
        std::snprintf(livego_push_url, sizeof(livego_push_url), 
            constants::livego_push_url_template.c_str(), room_name);

        // create detector run context
        std::shared_ptr<glcc_server_context_t> dect_context = \
            std::make_shared<glcc_server_context_t>();
        
        dect_context->server_dir = glcc_context->server_dir;
        dect_context->server_dir.user_dir \
            = glcc_context->server_dir.work_dir + "/" + user_name;
        dect_context->livego_context.room_name = room_name;
        dect_context->livego_context.livego_push_url = livego_push_url;
        dect_context->video_context.video_path = video_url;
        dect_context->video_context.video_name = video_name;
        dect_context->detector_init_context = glcc_context->detector_init_context;
        dect_context->detector_run_context = glcc_context->detector_run_context;
        dect_context->detector_run_context.upload_path = livego_push_url;
        dect_context->detector_run_context.video_path = video_url;

        dect_context->extra_info["user_name"] = user_name;
        dect_context->extra_info["user_password"] = user_password;

        if (register_detector(room_name,
                dect_context, Judge_Register) == nullptr) {
            // cv::VideoCapture capture;
            // int ret = capture.open(video_url);
            // capture.release();
            WFGoTask * go_run_task = WFTaskFactory::create_go_task(
                "detector_run", run_detector, dect_context->livego_context.room_name, dect_context);
            go_run_task->set_callback([dect_context, user_name, video_name](WFGoTask * task) {
                std::string & room_name = dect_context->livego_context.room_name;
                LOG_F(INFO, "[SERVER][DECT][%s][%s][%s] Detect task finish!", 
                    user_name.c_str(), video_name.c_str(), room_name.c_str());
            });
            go_run_task->start();
            set_common_resp(resp, "200", "OK");
            Json::Value reply;
            reply["room_name"] = room_name;
            // TODO: append body nocopy
            resp->append_output_body(reply.toStyledString());
        } else {
            set_common_resp(resp, "200", "OK");
            Json::Value reply;
            reply["room_name"] = room_name;
            resp->append_output_body(reply.toStyledString());
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
        std::string user_name = root["user_name"].asString();
        if (!root.isMember("room_url")) {
            LOG_F(ERROR, "[SERVER][DISDECT][%s] Find request body key: %s fail!", user_name.c_str(), "room_url");
            set_common_resp(resp, "400", "Bad Request");
            return;
        }

        std::string room_url;
        std::string room_name;
        std::vector<std::string> failed_stop_room_url{};
        if (root["room_url"].isArray()) {
            for (auto i = 0; (int)root["room_url"].size(); i++) {
                std::unordered_map<std::string, std::string> results;
                room_url = root["room_url"][i].asString();
                int ret = parse_room_url(room_url, results);
                if (ret == -1) {
                    LOG_F(ERROR, "[SERVER][DISDECT][%s] Parse URL: %s fail!", 
                        user_name.c_str(), room_url.c_str());
                    failed_stop_room_url.emplace_back(room_url);
                } else {
                    room_name = results["room_name"];
                    cancel_detector(room_name, WAKE_CANCEL);
                    LOG_F(INFO, "[SERVER][DISDECT][%s][%s] Cancel room success!", user_name.c_str(), room_name.c_str());
                }
            }
        } else {
            std::unordered_map<std::string, std::string> results;
            room_url = root["room_url"].asString();
            int ret = parse_room_url(room_url, results);
            if (ret == -1) {
                failed_stop_room_url.emplace_back(room_url);
                LOG_F(ERROR, "[SERVER][DISDECT][%s] Parse URL: %s fail!", 
                    user_name.c_str(), room_url.c_str());
            } else {
                room_name = results["room_name"];
                cancel_detector(room_name, WAKE_CANCEL);
                LOG_F(INFO, "[SERVER][DISDECT][%s][%s] Cancel room success!", user_name.c_str(), room_name.c_str());
            }
        }

        Json::Value reply;
        for (auto f : failed_stop_room_url) {
            reply["fail_stop_room_url"].append(f);
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
        std::string user_name = root["user_name"].asString();
        if (!root.isMember("use_template_url") || !root.isMember("video_url") || !root.isMember("video_name")) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][REGISTER_VIDEO][%s] Find request body key: %s, %s, %s fail!", 
                user_name.c_str(), "use_template_url", "video_name", "video_url");
            return;
        }

        bool use_template_url = root["use_template_url"].asBool();
        std::string video_name = root["video_name"].asString();
        std::string video_url = root["video_url"].asString();

        auto & work_dir = ((glcc_server_context_t *) context)->server_dir.work_dir;
        
        std::string user_dir = work_dir + "/" + user_name; 
        std::string video_dir = user_dir + "/" + video_name;
        std::string custom_dir = user_dir + "/" + "custom";

        char video_path[512] = {0};
        if (use_template_url) {
            std::snprintf(video_path, sizeof(video_path), 
                constants::video_path_template.c_str(), video_url.c_str());
        } else {
            bool is_online_url = false;
            for (auto & suffix : constants::video_prefixes) {
                if (video_url.find(suffix, 0) == 0) {
                    is_online_url = true;
                    break;
                }
            }
            if (is_online_url) {
                std::strcpy(video_path, video_url.c_str());
            } else {
                std::snprintf(video_path, sizeof(video_path), "%s/%s", 
                    custom_dir.c_str(), video_url.c_str());
            }
        }

        video_url = video_path;
        LOG_F(INFO, "[SERVER][REGISTER_VIDEO][%s][%s] Register %s", 
            user_name.c_str(), video_name.c_str(), video_path);
        WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
            constants::mysql_glccserver_url, 0, 
            [video_url, video_name, user_name, video_dir](WFMySQLTask * task){
                int state = task->get_state(); int error = task->get_error();
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
                        check_dir(video_dir, true);
                        LOG_F(INFO, "[SERVER][REGISTER_VIDEO][%s][%s] %s", 
                            user_name.c_str(), video_name.c_str(), reply.toStyledString().c_str());
                    } else {
                        set_common_resp(http_resp, "400", "Bad Request");
                        LOG_F(ERROR, "[SERVER][REGISTER_VIDEO][%s][%s] Register Video fail!", 
                            user_name.c_str(), video_name.c_str());
                    }
                } else {
                    set_common_resp(http_resp, "400", "Bad Request");
                    LOG_F(INFO, "[SERVER][REGISTER_VIDEO][%s][%s] Register fail! Code: %d", 
                        user_name.c_str(), video_name.c_str(), error);
                }
            }
        );
        mysql_task->user_data = task;

        std::stringstream mysql_query;
        mysql_query << "INSERT INTO glccserver.Video(video_name, username, video_url) values ("
                    << "\"" << video_name << "\"" << ", " 
                    << "\"" << user_name << "\"" << ", "
                    << "\"" << video_url << "\"" << ");";
        mysql_task->get_req()->set_query(mysql_query.str());
        *series_of(task) << mysql_task;
    }


    void GLCCServer::delete_video_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
        const void * body;
        size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *) body; 

        Json::Value root;
        Json::Reader reader;
        reader.parse(body_text, root);
        std::string user_name = root["user_name"].asString();
        if (!root.isMember("video_name")) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][DELETE_VIDEO][%s] Find request body key: %s fail!", user_name.c_str(), "video_name");
            return;
        }

        std::string video_name = root["video_name"].asString();
        LOG_F(INFO, "Body: %s", root.toStyledString().c_str());
        WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
            constants::mysql_glccserver_url, 0, 
            [user_name, video_name](WFMySQLTask * task) {
                int state = task->get_state(); int error = task->get_error();
                WFHttpTask * http_task = (WFHttpTask *) task->user_data;
                protocol::HttpResponse * http_resp = http_task->get_resp();
                if (state == WFT_STATE_SUCCESS) {
                    int parse_state = parse_mysql_response(task);
                    if (parse_state == WFT_STATE_SUCCESS) {
                        set_common_resp(http_resp, "200", "OK");
                        LOG_F(INFO, "[SERVER][DELETE_VIDEO][%s][%s] Delete video success!", user_name.c_str(), video_name.c_str());
                    } else {
                        set_common_resp(http_resp, "400", "Bad Request");
                        LOG_F(INFO, "[SERVER][DELETE_VIDEO][%s][%s] Delete video fail!", user_name.c_str(), video_name.c_str());
                    }
                } else {
                    set_common_resp(http_resp, "400", "Bad Request");
                    LOG_F(INFO, "[SERVER][DELETE_VIDEO][%s][%s] Delete fail! Code: %d", user_name.c_str(), video_name.c_str(), error);
                }
            }
        );
        mysql_task->user_data = task;
        char mysql_query[512];
        std::snprintf(mysql_query, sizeof(mysql_query),
            "DELETE FROM glccserver.Video WHERE video_name=\"%s\" AND username=\"%s\"", video_name.c_str(), user_name.c_str());
        mysql_task->get_req()->set_query(mysql_query);
        *series_of(task) << mysql_task;
    }

    void GLCCServer::video_put_lattice_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
        const void * body; size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *)body;

        Json::Value root; Json::Reader reader;
        int ret = reader.parse(body_text, root);

        std::string user_name = root["user_name"].asString();

        if (!ret) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][PUT_LATTICE][%s] Parse %s fail!", user_name.c_str(), body_text.c_str());
            return;
        }
        if (!root.isMember("video_name") || !root.isMember("contour_name") || !root.isMember("contour_path")) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][PUT_LATTICE][%s] Find request bdoy key: %s, %s, %s fail!", user_name.c_str(), 
                "video_name", "contour_name", "contour_path");
            return;
        }

        if (!root["contour_path"].isArray()) {
            set_common_resp(resp, "400", "Bad Request");
            LOG_F(ERROR, "[SERVER][PUT_LATTICE][%s] Find not array contour path", user_name.c_str());
            return;
        }

        std::string user_password = root["user_password"].asString();
        std::string video_name = root["video_name"].asString();
        std::stringstream task_name;
        task_name << "convexhull: "  << user_name << user_password << video_name;
        std::shared_ptr<Json::Value> reply_ptr = std::make_shared<Json::Value>();
        WFGoTask * go_task = WFTaskFactory::create_go_task(task_name.str(), 
            [reply_ptr](Json::Value root){
                std::string contour_name = root["contour_name"].asString();
                std::string video_name = root["video_name"].asString();
                std::string user_name = root["user_name"].asString();
                std::vector<cv::Point2f> points_list;
                std::vector<cv::Point2f> hull_points_list;
                for (int i = 0; i < (int)(root["contour_path"].size() / 2); i++) {
                    points_list.emplace_back(
                        root["contour_path"][i * 2].asFloat(), root["contour_path"][i * 2 + 1].asFloat());
                }
                cv::convexHull(points_list, hull_points_list);
                for (auto & point : hull_points_list) {
                    (*reply_ptr)["contour_path"].append((int)point.x);
                    (*reply_ptr)["contour_path"].append((int)point.y);
                }
                (*reply_ptr)["user_name"] = user_name;
                (*reply_ptr)["video_name"] = video_name;
                (*reply_ptr)["contour_name"] = contour_name;
            }, root
        );

        go_task->set_callback([reply_ptr, resp, root](WFGoTask * task) {
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0,
                [reply_ptr, resp, root](WFMySQLTask * task){
                    std::string video_name = root["video_name"].asString();
                    std::string contour_name = root["contour_name"].asString();
                    std::string user_name = root["user_name"].asString();
                    int state = task->get_state(); int error = task->get_error();
                    if (state == WFT_STATE_SUCCESS) {
                        int parse_state = parse_mysql_response(task);
                        if (parse_state == WFT_STATE_SUCCESS) {
                            set_common_resp(resp, "200", "OK");
                            resp->append_output_body((*reply_ptr).toStyledString());
                            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                                constants::mysql_glccserver_url, 0, 
                                [user_name, video_name, contour_name, reply_ptr](WFMySQLTask * task){
                                    int state = task->get_state(); int error = task->get_error();
                                    if (state == WFT_STATE_SUCCESS) {
                                        std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                                        parse_mysql_response(task, results);
                                        if (results.size() > 0) {
                                            std::vector<protocol::MySQLCell> & room_name = results["room_name"];
                                            for (int i = 0; i < (int)room_name.size(); i++) {
                                                Detector * detector = ProductFactory<Detector>::Instance().GetProduct(room_name[i].as_string());
                                                if (detector != nullptr) {
                                                    Json::Value contour_path = (*reply_ptr)["contour_path"];
                                                    for (int j = 0; j < (int)contour_path.size() / 2; j++) {
                                                        detector->contour_list[contour_name].emplace_back(contour_path[j * 2].asInt(), 
                                                            contour_path[j * 2 + 1].asInt());
                                                    }
                                                    detector->is_put_lattice = true;
                                                }
                                            }
                                        } else {
                                            LOG_F(ERROR, "[SERVER][PUT_LATTICE][%s][%s] Find room fail!", 
                                                user_name.c_str(), video_name.c_str());
                                        }
                                    } else {
                                        LOG_F(ERROR, "[SERVER][PUT_LATTICE][%s][%s] Find room fail! Code: %d", 
                                            user_name.c_str(), video_name.c_str(), error);
                                    }
                                }
                            );
                            char mysql_query[512];
                            std::snprintf(mysql_query, sizeof(mysql_query), 
                                "SELECT room_name from glccserver.Room where video_name=\"%s\" AND username=\"%s\";", 
                                video_name.c_str(), user_name.c_str());
                            mysql_task->get_req()->set_query(mysql_query);
                            mysql_task->start();
                            LOG_F(INFO, "[SERVER][PUT_LATTICE][%s][%s] Insert %s into Contour success!",
                                user_name.c_str(), video_name.c_str(), contour_name.c_str());
                        } else {
                            set_common_resp(resp, "400", "Bad Request");
                            LOG_F(ERROR, "[SERVER][PUT_LATTICE][%s][%s] Insert %s into Contour fail!", 
                                user_name.c_str(), video_name.c_str(), contour_name.c_str());
                        }
                    } else {
                        set_common_resp(resp, "400", "Bad Request");
                        LOG_F(ERROR, "[SERVER][PUT_LATTICE][%s][%s] Insert %s into Contour fail! Code: %d", 
                            user_name.c_str(), video_name.c_str(), contour_name.c_str(), error);
                    }
                }
            );
            std::stringstream mysql_query;
            mysql_query << "INSERT glccserver.Contour(username, contour_name, video_name, contour_path) VALUES ("
                        << (*reply_ptr)["user_name"] << "," << (*reply_ptr)["contour_name"] << "," << (*reply_ptr)["video_name"] << "," 
                        << "\"" <<(*reply_ptr)["contour_path"].toStyledString() << "\""
                        << ");";
            mysql_task->get_req()->set_query(mysql_query.str());
            *series_of(task) << mysql_task;
        });
        *series_of(task) << go_task;
    }

    void GLCCServer::video_disput_lattice_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();

        const void * body; size_t body_len;
        req->get_parsed_body(&body, &body_len);

        const std::string body_text = (const char *)body;
        Json::Value root; Json::Reader reader;
        int ret = reader.parse(body_text, root);

        std::string user_name = root["user_name"].asString();
        std::string user_password = root["user_password"].asString();
        if (!ret) {
            LOG_F(ERROR, "Parse %s fail!", body_text.c_str());
            set_common_resp(resp, "400", "Bad Request");
            return;
        }
        if (!root.isMember("video_name") || !root.isMember("contour_name")) {
            LOG_F(ERROR, "Find request bdoy key: %s, %s fail!", "video_name", "contour_name");
            set_common_resp(resp, "400", "Bad Request");
            return;
        }
        std::string video_name = root["video_name"].asString();
        std::string contour_name = root["contour_name"].asString();

        WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
            constants::mysql_glccserver_url, 0, 
            [resp, user_name, user_password, video_name, contour_name](WFMySQLTask * task){
                int state = task->get_state(); int error = task->get_error();
                if (state == WFT_STATE_SUCCESS) {
                    int parse_state = parse_mysql_response(task);
                    if (parse_state == WFT_STATE_SUCCESS) {
                        std::string room_name = user_name + "_" + user_password + "_" + video_name.c_str();
                        Detector * detector = ProductFactory<Detector>::Instance().GetProduct(room_name);
                        if (detector != nullptr) {
                            detector->contour_list.erase(contour_name);
                            LOG_F(INFO, "[SERVER][DISPUT_LATTICE][%s][%s] Delete %s from Contour success", 
                                user_name.c_str(), video_name.c_str(), contour_name.c_str());
                        } else {
                            LOG_F(ERROR, "[SERVER][DISPUT_LATTICE][%s][%s] Find %s detector fail!",
                                user_name.c_str(), video_name.c_str(), room_name.c_str());
                        }
                        set_common_resp(resp, "200", "OK");
                    } else {
                        set_common_resp(resp, "400", "Bad Request");
                        LOG_F(ERROR, "[SERVER][DISPUT_LATTICE][%s][%s] Delete %s from Contour fail!",
                            user_name.c_str(), video_name.c_str(), contour_name.c_str());
                    }
                } else {
                    set_common_resp(resp, "400", "Bad Request");
                    LOG_F(ERROR, "[SERVER][DISPUT_LATTICE][%s][%s] Delete %s from Contour fail! Code: %d",
                        user_name.c_str(), video_name.c_str(), contour_name.c_str(), error);
                }
            }
        );

        char mysql_query[256];
        std::snprintf(mysql_query, sizeof(mysql_query), 
            "DELETE FROM glccserver.Contour WHERE contour_name=\"%s\" AND video_name=\"%s\" AND username=\"%s\";",
            contour_name.c_str(), video_name.c_str(), user_name.c_str());
        mysql_task->get_req()->set_query(mysql_query);
        *series_of(task) << mysql_task;
    }


    int GLCCServer::parse_url(const std::string & url, std::unordered_map<std::string, std::string> & result_map) {
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

    int GLCCServer::parse_room_url(const std::string & url, std::unordered_map<std::string, std::string> & result_map) {
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

    void GLCCServer::file_timer_callback(WFTimerTask * timer) {
        SeriesWork * series = series_of(timer);
        WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
            constants::mysql_glccserver_url, 0, 
            [](WFMySQLTask * task){
                int state = task->get_state(); int error = task->get_error();
                if (state == WFT_STATE_SUCCESS) {
                    std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                    parse_mysql_response(task, results);
                    if (results.size() > 0) {
                        std::vector<protocol::MySQLCell> & user_name_cells = results["username"];
                        std::vector<protocol::MySQLCell> & video_name_cells = results["video_name"];
                        std::vector<protocol::MySQLCell> & file_path_cells = results["file_path"];
                        std::vector<protocol::MySQLCell> & compare_results_cell = results["compare_results"];

                        std::string user_name;
                        std::string video_name;
                        std::string file_path;
                        std::string cover_path;
                        int compare_results;

                        std::stringstream keep_file_path_infos;
                        std::stringstream remove_file_path_infos;
                        for (int i = 0; i < (int)file_path_cells.size(); i++) {
                            user_name = user_name_cells[i].as_string();
                            video_name = video_name_cells[i].as_string();
                            file_path = file_path_cells[i].as_string();
                            compare_results = compare_results_cell[i].as_int();
                            if (compare_results < 0) {
                                char mysql_delete_query[512] = {0};
                                std::snprintf(mysql_delete_query, sizeof(mysql_delete_query), 
                                    "DELETE FROM glccserver.File WHERE username=\"%s\" AND video_name=\"%s\" AND file_path=\"%s\";", 
                                    user_name.c_str(), video_name.c_str(), file_path.c_str());
                                WFMySQLTask * mysql_delete_task = WFTaskFactory::create_mysql_task(
                                    constants::mysql_glccserver_url, 0, 
                                    [user_name, video_name, file_path](WFMySQLTask * task){
                                        int state = task->get_state(); int error = task->get_error();
                                        if (state == WFT_STATE_SUCCESS) {
                                            LOG_F(INFO, "[SERVER][FILE_TIMER][%s][%s] DB delete %s success!", 
                                                user_name.c_str(), video_name.c_str(), file_path.c_str());
                                        } else {
                                            LOG_F(ERROR, "[SERVER][FILE_TIMER][%s][%s] DB delete %s fail! Code: %d", 
                                                user_name.c_str(), video_name.c_str(), file_path.c_str(), error);
                                        }
                                    }
                                );
                                mysql_delete_task->get_req()->set_query(mysql_delete_query);
                                *series_of(task) << mysql_delete_task;
                                if (check_file(file_path, nullptr, {}) < 0) {
                                    LOG_F(WARNING, "[SERVER][FILE_TIMER][%s][%s] Can't find %s", 
                                        user_name.c_str(), video_name.c_str(), file_path.c_str());
                                } else {
                                    int ret;
                                    std::unordered_map<std::string, std::string> path_parse_results;
                                    ret = parse_path(file_path, path_parse_results);
                                    if (ret == -1) {
                                        LOG_F(WARNING, "[SERVER][FILE_TIMER][%s][%s] Parse %s fail!", 
                                            user_name.c_str(), video_name.c_str(), file_path.c_str());
                                        return;
                                    }

                                    auto & stem = path_parse_results["stem"];
                                    auto & dirname = path_parse_results["dirname"];

                                    cover_path = dirname + "/" + stem + "." + constants::cover_save_suffix;
                                    ret = remove(file_path.c_str());
                                    if (ret == -1) {
                                        LOG_F(WARNING, "[SERVER][FILE_TIMER][%s][%s] Delete %s fail!", 
                                            user_name.c_str(), video_name.c_str(), file_path.c_str());
                                    } else {
                                        LOG_F(INFO, "[SERVER][FILE_TIMER][%s][%s] Delete %s success!", 
                                            user_name.c_str(), video_name.c_str(), file_path.c_str());
                                    }

                                    ret = remove(cover_path.c_str());
                                    if (ret == -1) {
                                        LOG_F(WARNING, "[SERVER][FILE_TIMER][%s][%s] Delete %s fail!", 
                                            user_name.c_str(), video_name.c_str(), cover_path.c_str());
                                    } else {
                                        LOG_F(INFO, "[SERVER][FILE_TIMER][%s][%s] Delete %s success!", 
                                            user_name.c_str(), video_name.c_str(), cover_path.c_str());
                                    }
                                }
                            } else {
                            }
                        }
                    } else {
                        LOG_F(INFO, "[SERVER][FILE_TIMER] Nothing to do");
                    }
                } else {
                    LOG_F(ERROR, "[SERVER][FILE_TIMER] Timmer watch files fail! Code: %d", error);
                }
            }
        );
        char mysql_query[256] = "select username, video_name, file_path, glccserver.func_time_compare(now(), end_time) as compare_results from glccserver.File";
        mysql_task->get_req()->set_query(mysql_query);
        *series << mysql_task;
        *series << WFTaskFactory::create_timer_task(constants::interval_to_watch_file_second, file_timer_callback);
    }

    void GLCCServer::detector_timer_callback(WFTimerTask * timer) {
        SeriesWork * series = series_of(timer);
        WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
            constants::mysql_glccserver_url, 0, 
            [](WFMySQLTask * task){
                int state = task->get_state(); int error = task->get_error();
                if (state == WFT_STATE_SUCCESS) {
                    std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                    parse_mysql_response(task, results);
                    if (results.size() > 0) {
                        std::vector<protocol::MySQLCell> & room_name_cells = results["room_name"];
                        std::vector<protocol::MySQLCell> & compare_results_cell = results["compare_results"];
                        std::string room_name;
                        int compare_results;
                        std::stringstream mysql_delete_query;
                        std::stringstream keep_room_name_infos;
                        std::stringstream remove_room_name_infos;
                        std::stringstream find_detector_name_infos;
                        keep_room_name_infos << "[SERVER][DETECTOR] Keeping rooms: ";
                        remove_room_name_infos << "[SERVER][DETECTOR] Removing rooms: ";
                        find_detector_name_infos << "[SERVER][DETECTOR] Find detectors: ";
                        for (int i = 0; i < (int)room_name_cells.size(); i++) {
                            room_name = room_name_cells[i].as_string();
                            compare_results = compare_results_cell[i].as_int();
                            if (compare_results < 0) {
                                int ret = cancel_detector(room_name, WAKE_CANCEL);
                                if (ret != WFT_STATE_NOREPLY) {
                                    find_detector_name_infos << room_name << " ";
                                }
                                remove_room_name_infos << room_name << " ";
                            } else {
                                keep_room_name_infos << room_name << " ";
                            }
                        }
                        LOG_F(INFO, keep_room_name_infos.str().c_str());
                        LOG_F(INFO, remove_room_name_infos.str().c_str());
                        LOG_F(INFO, find_detector_name_infos.str().c_str());
                    } else {
                        LOG_F(INFO, "[SERVER][DETECTOR] Nothing to watch");
                    }
                } else {
                    LOG_F(ERROR, "[SERVER][DETECTOR] Timmer watch detectors fail! Code: %d", error);
                }
            }
        );
        char mysql_query[256] = "select room_name, glccserver.func_time_compare(now(), end_time) as compare_results from glccserver.Room";
        mysql_task->get_req()->set_query(mysql_query);
        *series << mysql_task;
        *series << WFTaskFactory::create_timer_task(constants::interval_to_watch_detector_second, detector_timer_callback);
    }

    void GLCCServer::run_detector(const std::string & room_name, std::shared_ptr<glcc_server_context_t> context) {
        Json::Value detector_init_context = context->detector_init_context;
        struct detector_run_context detector_run_context = context->detector_run_context;
        std::string video_name = context->video_context.video_name.c_str();
        std::string user_name = context->extra_info["user_name"].asString();
        auto & user_dir = context->server_dir.user_dir;
        std::string video_dir = user_dir + "/" + video_name;
        Detector * detector = register_detector(room_name, context, Create_Register);
        if (context->state == WFT_STATE_TASK_ERROR) {
            LOG_F(ERROR, "[SERVER][RUN_DETECTOR][%s][%s][%s] Register Detector fail!", 
                user_name.c_str(),video_name.c_str(), room_name.c_str());
        } else if (context->state == WFT_STATE_SUCCESS) {
            std::string mode = detector_init_context["mode"].asString();
            LOG_F(INFO, "[SERVER][RUN_DETECTOR][%s][%s][%s] Mode: %s",
                user_name.c_str(), video_name.c_str(), room_name.c_str(), mode.c_str());
            detector->resource_dir = video_dir;
            detector_run_context.vis_params = detector_init_context[(const char *)mode.c_str()]["extra_config"];
            detector->state = 1;
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0,
                [detector, room_name](WFMySQLTask * task) {
                    int state = task->get_state(); int error = task->get_error();
                    if (state == WFT_STATE_SUCCESS) {
                        std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                         parse_mysql_response(task, results);
                        if (results.size() > 0) {
                            if (results.find("contour_name") != results.end()) {
                                std::vector<protocol::MySQLCell> & contour_name = results["contour_name"];
                                std::vector<protocol::MySQLCell> & contour_path = results["contour_path"];
                                Json::Reader reader; Json::Value value;
                                for (int i = 0; i < (int)contour_name.size(); i++) {
                                    value.clear();
                                    std::string contour_name_str = contour_name[i].as_string();
                                    std::string contour_path_str = contour_path[i].as_string();
                                    reader.parse(contour_path_str, value);
                                    std::vector<cv::Point2i> points_list;
                                    for (int i = 0; i < (int)value.size() / 2; i++) {
                                        points_list.emplace_back(
                                            value[i * 2].asInt(), value[i * 2 + 1].asInt()
                                        );
                                    }
                                    detector->contour_list[contour_name_str] = points_list;
                                }
                            } else {
                                LOG_F(INFO, "[SERVER][DECT][CONTOUR] Find the contour of %s fail!", room_name.c_str());
                            }
                        } else {
                            LOG_F(INFO, "[SERVER][DECT][CONTOUR] Find the contour of %s fail!", room_name.c_str());
                        }
                    } else {
                        LOG_F(INFO, "[SERVER][DECT][CONTOUR] Find the contour of %s fail! Code: %d", room_name.c_str(), error);
                    }
                }
            );

            char mysql_query[512];
            std::snprintf(mysql_query, sizeof(mysql_query),
                "SELECT contour_name, contour_path FROM glccserver.Contour "
                "WHERE video_name=\"%s\" AND username=\"%s\";", video_name.c_str(), user_name.c_str());
            mysql_task->get_req()->set_query(mysql_query);
            mysql_task->start();

            auto deal_func = [video_name, user_name] (void * args) {
                std::string video_file_path = (*(std::stringstream *)args).str();
                char mysql_query[1024];
                std::snprintf(mysql_query, sizeof(mysql_query),
                    "INSERT INTO glccserver.File(file_path, video_name, username, start_time, end_time) VALUES "
                    "(\"%s\", \"%s\", \"%s\", now(), date_add(now(), interval %ld DAY));",
                    video_file_path.c_str(), video_name.c_str(), user_name.c_str(), constants::max_video_file_save_day);
                WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                    constants::mysql_glccserver_url, 0, 
                    [video_file_path, video_name, user_name](WFMySQLTask * task) {
                        int state = task->get_state(); int error = task->get_error();
                        if (state == WFT_STATE_SUCCESS) {
                            int parse_state = parse_mysql_response(task);
                            if (parse_state == WFT_STATE_SUCCESS) {
                                LOG_F(INFO, "[SERVER][DETECTOR][VIDEO_POST_PROCESS][%s][%s] Insert %s success!", 
                                    user_name.c_str(), video_name.c_str(), video_file_path.c_str());
                            } else {
                                LOG_F(ERROR, "[SERVER][DETECTOR][VIDEO_POST_PROCESS][%s][%s] Insert %s fail!",  
                                    user_name.c_str(), video_name.c_str(), video_file_path.c_str());
                            }
                        } else {
                            LOG_F(ERROR, "[SERVER][DETECTOR][VIDEO_POST_PROCESS][%s][%s] Insert %s fail! Code: %d", 
                                user_name.c_str(), video_name.c_str(), video_file_path.c_str(), error);
                        }
                    }
                );
                mysql_task->get_req()->set_query(mysql_query);
                mysql_task->start();
            };

            int ret = detector->run(&detector_run_context, 
                [&room_name](void * args){
                    cancel_detector(room_name, FORCE_CANCEL);
                }, deal_func);
            if (ret == -1) {
                LOG_F(INFO, "[SERVER][RUN_DETECTOR][%s][%s][%s] Run stop!", 
                    user_name.c_str(), video_name.c_str(), room_name.c_str());
                context->state = WFT_STATE_TASK_ERROR;
            }
        } else if (context->state == WFT_STATE_TOREPLY){
            LOG_F(INFO, "[SERVER][RUN_DETECTOR][%s][%s][%s] Detector exists!", 
                user_name.c_str(), video_name.c_str(), room_name.c_str());
        }
    }

    Detector * GLCCServer::register_detector(const std::string & room_name, std::shared_ptr<glcc_server_context_t> context, int mode) {
        Detector * detector;
        Json::Value extra_info = context->extra_info;
        std::string user_name = extra_info["user_name"].asString();
        std::string video_name = context->video_context.video_name;
        Json::Value detector_init_context = context->detector_init_context;
        std::string detector_mode = detector_init_context["mode"].asString();
        Json::Value mode_context = detector_init_context[(const char *)detector_mode.c_str()];
        mode_context["resource_dir"] = mode_context["resource_dir"].asString() + "-" + room_name;
        detector = ProductFactory<Detector>::Instance().GetProduct(room_name);
        if (mode == Create_Register) {
            if (detector == nullptr) {
                register_map(detector_mode, room_name, &mode_context);
                detector = ProductFactory<Detector>::Instance().GetProduct(room_name);
            } else {
                context->state = WFT_STATE_TOREPLY;
            }

            if (detector->state == -1) {
                detector = nullptr;
                cancel_detector(room_name, NORMAL_CANCEL);
                context->state = WFT_STATE_TASK_ERROR;
            } else{
                if (context->state != WFT_STATE_TOREPLY) {
                    context->state = WFT_STATE_SUCCESS;
                }
            }
        } else if (mode == Judge_Register) {
            if (detector == nullptr) {
                return detector;
            } else {
                context->state = WFT_STATE_TOREPLY;
            }
        }

        if (context->state == WFT_STATE_SUCCESS) {
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0, 
                [user_name, video_name, room_name](WFMySQLTask * task) {
                    int state = task->get_state();int error = task->get_error();
                    if (state == WFT_STATE_SUCCESS) {
                        int parse_state = parse_mysql_response(task);
                        if (parse_state == WFT_STATE_SUCCESS) {
                            LOG_F(INFO, "[SERVER][REGISTER_DETECTOR][%s][%s] Create the room: %s success!", 
                                user_name.c_str(), video_name.c_str(), room_name.c_str());
                        } else {
                            LOG_F(INFO, "[SERVER][REGISTER_DETECTOR][%s][%s] Create the room: %s fail!", 
                                user_name.c_str(), video_name.c_str(), room_name.c_str());
                        }
                    } else {
                        LOG_F(ERROR, "[SERVER][REGISTER_DETECTOR][%s][%s] Create the room: %s fail! Code: %d", 
                            user_name.c_str(), video_name.c_str(), room_name.c_str(), error);
                    }
                }
            );
            char mysql_query[512];
            std::snprintf(mysql_query, sizeof(mysql_query), 
                "INSERT INTO glccserver.Room(room_name, username, video_name, start_time, end_time)"
                "VALUES(\"%s\", \"%s\", \"%s\", now(), date_add(now(), interval %ld DAY));", 
                   room_name.c_str(), user_name.c_str(), video_name.c_str(), constants::max_detector_live_day);
            mysql_task->get_req()->set_query(mysql_query);
            mysql_task->start();
        } else if (context->state == WFT_STATE_TOREPLY) {
            WFMySQLTask * mysql_task = WFTaskFactory::create_mysql_task(
                constants::mysql_glccserver_url, 0, 
                [user_name, video_name, room_name](WFMySQLTask * task) {
                    int state = task->get_state(); int error = task->get_error();
                    if (state == WFT_STATE_SUCCESS) {
                        std::unordered_map<std::string, std::vector<protocol::MySQLCell>> results;
                        int parse_state = parse_mysql_response(task, results);
                        if (parse_state == WFT_STATE_SUCCESS){
                            LOG_F(INFO, "[SERVER][REGISTER_DETECTOR][%s][%s][%s] DB update time success!", 
                                user_name.c_str(), video_name.c_str(), room_name.c_str());
                        } else {
                            LOG_F(INFO, "[SERVER][REGISTER_DETECTOR][%s][%s][%s] DB update time fail!",
                                user_name.c_str(), video_name.c_str(), room_name.c_str());
                        }
                    } else {
                        LOG_F(INFO, "[SERVER][REGISTER_DETECTOR][%s][%s][%s] DB update time fail! Code: %d", 
                            user_name.c_str(), video_name.c_str(), room_name.c_str(), error);
                    }
                }
            );

            char mysql_query[512];
            std::snprintf(mysql_query, sizeof(mysql_query), 
                "UPDATE glccserver.Room SET start_time=now(), end_time=date_add(now(), INTERVAL %ld MICROSECOND) "
                "WHERE room_name=\"%s\";", constants::max_detector_live_day, room_name.c_str());
            mysql_task->get_req()->set_query(mysql_query);
            mysql_task->start();
        }
        return detector;
    }

    void GLCCServer::register_map(const std::string & mode, const std::string & room_name, void * context) {
        if (mode == "ObjectDetector") {
            ProductFactory<Detector>::Instance().RegisterProduct(room_name, ObjectDetector::init_func, context);
        } else if (mode == "TrackerDetector") {
            ProductFactory<Detector>::Instance().RegisterProduct(room_name, TrackerDetector::init_func, context);
        }
    };

    int GLCCServer::cancel_detector(const std::string & room_name, int mode) {
        int state;
        Detector * detector = ProductFactory<Detector>::Instance().GetProduct(room_name);
        if (detector == nullptr) {
            state = WFT_STATE_NOREPLY;
        } else {
            state = WFT_STATE_SUCCESS;
            if (mode == FORCE_CANCEL || mode == WAKE_CANCEL) {
                if (mode == FORCE_CANCEL) {
                    detector->state = -1;

                    char livego_check_stat_url[256] = {0};
                    std::snprintf(livego_check_stat_url, sizeof(livego_check_stat_url), 
                        constants::livego_check_stat_template.c_str(), room_name.c_str());
                    WFGraphTask * graph_task = WFTaskFactory::create_graph_task(
                        [room_name](WFGraphTask * task) {
                            int ret = ProductFactory<Detector>::Instance().EraseProductDel(room_name);
                            if (ret != 1) {
                                LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] Erase product fail! Code: %d", room_name.c_str(), ret);
                            } else {
                                LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] Erase product %d", room_name.c_str(), ret);
                            }
                            LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] Release Dect graph task complete!", room_name.c_str());
                        }
                    );

                    WFHttpTask * check_stat_http_task = WFTaskFactory::create_http_task(
                        livego_check_stat_url, 0, 0,
                        [room_name](WFHttpTask * task) {
                            int state = task->get_state(); int error = task->get_error();
                            if (state == WFT_STATE_SUCCESS) {
                                protocol::HttpResponse * resp = task->get_resp();
                                const void * body;
                                size_t body_len;
                                resp->get_parsed_body(&body, &body_len);
                                Json::Value root; Json::Reader reader;
                                reader.parse((const char *)body, root);
                                Json::Value subs = root["data"]["subs"];
                                LOG_F(INFO, subs.toStyledString().c_str());
                                Json::Value kick_body;
                                std::string session_id;
                                WFHttpTask * kick_session_task;
                                for (int i = 0; i < (int)subs.size(); i++) {
                                    session_id = subs[i]["session_id"].asString();
                                    kick_body["stream_name"] = room_name;
                                    kick_body["session_id"] = session_id;
                                    LOG_F(INFO, kick_body.toStyledString().c_str());
                                    kick_session_task = WFTaskFactory::create_http_task(
                                        constants::livego_kick_url, 0, 0,
                                        [room_name, session_id](WFHttpTask * task) {
                                            int state = task->get_state() ;int error = task->get_state();
                                            if (state == WFT_STATE_SUCCESS) {
                                                protocol::HttpResponse * resp = task->get_resp();
                                                const void * body; size_t body_len;
                                                resp->get_parsed_body(&body, &body_len);
                                                Json::Value root; Json::Reader reader;
                                                reader.parse((const char *)body, root);
                                                LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] Kick stream_name: %s session_id: %s success\n%s", 
                                                    room_name.c_str(), room_name.c_str(), session_id.c_str(), root.toStyledString().c_str());
                                            } else {
                                                LOG_F(ERROR, "[SERVER][CANCEL_DETECTOR][%s] Kick stream_name: %s session_id: %s fail! Code: %d", 
                                                    room_name.c_str(), room_name.c_str(), session_id.c_str(), error);
                                            }

                                        }
                                    );
                                    kick_session_task->get_req()->append_output_body(kick_body.toStyledString());
                                    set_common_req(kick_session_task->get_req(), "*/*", "close", HttpMethodPost);
                                    *series_of(task) << kick_session_task;
                                }
                                int error_code = root["error_code"].asInt();
                                if (error_code != 0) {
                                    LOG_F(ERROR, "[SERVER][CANCEL_DETECTOR][%s] Find room fail! Code: %d", room_name.c_str(), error_code);
                                }
                                LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] Stop pull room\n%s", room_name.c_str(), root.toStyledString().c_str());
                            } else {
                                LOG_F(ERROR, "[SERVER][CANCEL_DETECTOR][%s] Stop pull room fail! Code: %d", room_name.c_str(), error);
                            }
                        }
                    );
                    // set_common_resp()
                    set_common_req(check_stat_http_task->get_req());

                    WFMySQLTask * delete_sql_task = WFTaskFactory::create_mysql_task(
                        constants::mysql_glccserver_url, 0, 
                        [room_name](WFMySQLTask * task) {
                            int state = task->get_state(); int error = task->get_error();
                            if (state == WFT_STATE_SUCCESS) {
                                int parse_state = parse_mysql_response(task);
                                if (parse_state == WFT_STATE_SUCCESS) {
                                    LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] DB delete room success!", room_name.c_str());
                                } else {
                                    LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] DB delete room fail!", room_name.c_str());
                                }
                            } else {
                                LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] DB delete room fail! Code: %d", room_name.c_str(), error);
                            }
                        }
                    );


                    char mysql_query[128];
                    std::snprintf(mysql_query, sizeof(mysql_query), 
                        "delete from glccserver.Room where room_name=\"%s\"", room_name.c_str());
                    delete_sql_task->get_req()->set_query(mysql_query);
                    WFGraphNode & node1 = graph_task->create_graph_node(check_stat_http_task);
                    WFGraphNode & node2 = graph_task->create_graph_node(delete_sql_task);
                    node1-->node2;
                    graph_task->start();
                } else if (mode == WAKE_CANCEL && detector->state > 0) {
                    detector->state = -1;
                } else {
                    LOG_F(ERROR, "[SERVER][CANCEL_DETECTOR][%s] Can't find cancel mode: %d", room_name.c_str(), mode);
                }
            } else if (mode == NORMAL_CANCEL && detector->state == 0) {
                detector->state = -1;
                ProductFactory<Detector>::Instance().EraseProductDel(room_name);
                LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] Delete the unrun detector", room_name.c_str());
            } else if (mode == NORMAL_CANCEL && detector->state == -1) {
                ProductFactory<Detector>::Instance().EraseProductDel(room_name);
                LOG_F(INFO, "[SERVER][CANCEL_DETECTOR][%s] Delete the bad detector", room_name.c_str());
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
                                      std::string status,
                                      std::string method) {
    
        req->set_method(method);
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

            LOG_F(1, "---------------- RESULT SET ----------------\n");

            if (cursor.get_cursor_status() == MYSQL_STATUS_GET_RESULT)
            {
                LOG_F(1, "cursor_status=%d field_count=%u rows_count=%u\n",
                        cursor.get_cursor_status(), cursor.get_field_count(),
                        cursor.get_rows_count());

                //nocopy api
                fields = cursor.fetch_fields();
                for (int i = 0; i < cursor.get_field_count(); i++)
                {
                    if (i == 0)
                    {
                        LOG_F(1, "db=%s table=%s\n",
                            fields[i]->get_db().c_str(), fields[i]->get_table().c_str());
                        LOG_F(1, "  ---------- COLUMNS ----------\n");
                    }
                    LOG_F(1, "  name[%s] type[%s]\n",
                            fields[i]->get_name().c_str(),
                            datatype2str(fields[i]->get_data_type()));
                }
                LOG_F(1, "  _________ COLUMNS END _________\n\n");

                while (cursor.fetch_row(arr))
                {
                    LOG_F(1, "  ------------ ROW ------------\n");
                    for (size_t i = 0; i < arr.size(); i++)
                    {
                        LOG_F(1, "  [%s][%s]", fields[i]->get_name().c_str(),
                                datatype2str(arr[i].get_data_type()));
                        if (arr[i].is_string())
                        {
                            std::string res = arr[i].as_string();
                            if (res.length() == 0)
                                LOG_F(1, "[\"\"]\n");
                            else 
                                LOG_F(1, "[%s]\n", res.c_str());
                        } else if (arr[i].is_int()) {
                            LOG_F(1, "[%d]\n", arr[i].as_int());
                        } else if (arr[i].is_ulonglong()) {
                            LOG_F(1, "[%llu]\n", arr[i].as_ulonglong());
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
                            LOG_F(1, "[%.*f]\n", (int)pos, arr[i].as_float());
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
                            LOG_F(1, "[%.*lf]\n", (int)pos, arr[i].as_double());
                        } else if (arr[i].is_date()) {
                            LOG_F(1, "[%s]\n", arr[i].as_string().c_str());
                        } else if (arr[i].is_time()) {
                            LOG_F(1, "[%s]\n", arr[i].as_string().c_str());
                        } else if (arr[i].is_datetime()) {
                            LOG_F(1, "[%s]\n", arr[i].as_string().c_str());
                        } else if (arr[i].is_null()) {
                            LOG_F(1, "[NULL]\n");
                        } else {
                            std::string res = arr[i].as_binary_string();
                            if (res.length() == 0)
                                LOG_F(1, "[\"\"]\n");
                            else 
                                LOG_F(1, "[%s]\n", res.c_str());
                        }
                    }
                    LOG_F(1, "  __________ ROW END __________\n");
                }
            }
            else if (cursor.get_cursor_status() == MYSQL_STATUS_OK)
            {
                LOG_F(1, "  OK. %llu ", cursor.get_affected_rows());
                if (cursor.get_affected_rows() == 1)
                    LOG_F(1, "row ");
                else
                    LOG_F(1, "rows ");
                LOG_F(1, "affected. %d warnings. insert_id=%llu. %s\n",
                        cursor.get_warnings(), cursor.get_insert_id(),
                        cursor.get_info().c_str());
            }

            LOG_F(1, "________________ RESULT SET END ________________\n\n");
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
            LOG_F(1, "OK. %llu ", task->get_resp()->get_affected_rows());
            if (task->get_resp()->get_affected_rows() == 1)
                LOG_F(1, "row ");
            else
                LOG_F(1, "rows ");
            LOG_F(1, "affected. %d warnings. insert_id=%llu. %s\n",
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

            LOG_F(1, "---------------- RESULT SET ----------------\n");

            LOG_F(1, "cursor_status=%d field_count=%u rows_count=%u\n",
                    cursor.get_cursor_status(), cursor.get_field_count(),
                    cursor.get_rows_count());

            //nocopy api
            fields = cursor.fetch_fields();
            for (int i = 0; i < cursor.get_field_count(); i++) {
                if (i == 0)
                {
                    LOG_F(1, "db=%s table=%s\n",
                        fields[i]->get_db().c_str(), fields[i]->get_table().c_str());
                    LOG_F(1, "  ---------- COLUMNS ----------\n");
                }
                LOG_F(1, "  name[%s] type[%s]\n",
                        fields[i]->get_name().c_str(),
                        datatype2str(fields[i]->get_data_type()));
            }
            LOG_F(1, "  _________ COLUMNS END _________\n\n");

            while (cursor.fetch_row(arr)) {
                LOG_F(1, "  ------------ ROW ------------\n");
                for (size_t i = 0; i < arr.size(); i++)
                {
                    std::string column_name = fields[i]->get_name();
                    LOG_F(1, "  [%s][%s]",  column_name.c_str(),
                            datatype2str(arr[i].get_data_type()));
                    if (arr[i].is_string())
                    {
                        std::string res = arr[i].as_string();
                        if (res.length() == 0)
                            LOG_F(1, "[\"\"]\n");
                        else 
                            LOG_F(1, "[%s]\n", res.c_str());
                    } else if (arr[i].is_int()) {
                        LOG_F(1, "[%d]\n", arr[i].as_int());
                    } else if (arr[i].is_ulonglong()) {
                        LOG_F(1, "[%llu]\n", arr[i].as_ulonglong());
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
                        LOG_F(1, "[%.*f]\n", (int)pos, arr[i].as_float());
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
                        LOG_F(1, "[%.*lf]\n", (int)pos, arr[i].as_double());
                    } else if (arr[i].is_date()) {
                        LOG_F(1, "[%s]\n", arr[i].as_string().c_str());
                    } else if (arr[i].is_time()) {
                        LOG_F(1, "[%s]\n", arr[i].as_string().c_str());
                    } else if (arr[i].is_datetime()) {
                        LOG_F(1, "[%s]\n", arr[i].as_string().c_str());
                    } else if (arr[i].is_null()) {
                        LOG_F(1, "[NULL]\n");
                    } else {
                        std::string res = arr[i].as_binary_string();
                        if (res.length() == 0)
                            LOG_F(1, "[\"\"]\n");
                        else 
                            LOG_F(1, "[%s]\n", res.c_str());
                    }
                    results[column_name].emplace_back(std::move(arr[i]));
                }
                LOG_F(1, "  __________ ROW END __________\n");
            }
            LOG_F(1, "________________ RESULT SET END ________________\n\n");
        } while (cursor.next_result_set());


        if (resp->get_packet_type() == MYSQL_PACKET_ERROR)
        {
            LOG_F(1, "ERROR. error_code=%d %s\n",
                    task->get_resp()->get_error_code(),
                    task->get_resp()->get_error_msg().c_str());
            return WFT_STATE_TASK_ERROR;
        }
        else if (resp->get_packet_type() == MYSQL_PACKET_OK) // just check origin APIs
        {
            LOG_F(1, "OK. %llu ", task->get_resp()->get_affected_rows());
            if (task->get_resp()->get_affected_rows() == 1)
                LOG_F(1, "row ");
            else
                LOG_F(1, "rows ");
            LOG_F(1, "affected. %d warnings. insert_id=%llu. %s\n",
                    task->get_resp()->get_warnings(),
                    task->get_resp()->get_last_insert_id(),
                    task->get_resp()->get_info().c_str());
        }

        return WFT_STATE_SUCCESS;
    }
}
