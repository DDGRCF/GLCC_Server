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
        WFMySQLTask * task = WFTaskFactory::create_mysql_task(
            constants::mysql_url_root, 0, [&](WFMySQLTask * task) {
                create_db_callbck(task, &state);
        });
        task->get_req()->set_query(constants::mysql_create_db_command);
        task->start();
        mysql_wait_group.wait(); 
        if (state == WFT_STATE_SUCCESS) {
            WFHttpServer server([&](WFHttpTask * task) {
                main_callback(task, &glcc_server_context);
            });

            // TODO: figure it out
            int ret = server.start(glcc_server_context.server_context.port, 
                constants::ssl_crt_path.c_str(), 
                constants::ssl_key_path.c_str());
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
        get_connection_infos(task, &connection_infos);
        LOG_F(INFO, connection_infos.str().c_str());

        REGEX_FUNC(login_callback, "POST", "/login.*", task, context);
        REGEX_FUNC(hello_world_callback, "GET", "/hello_world", task);
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

    void GLCCServer::login_callback(WFHttpTask * task, void * context) {
        int login_state;
        user_login_callback(task, &login_state);
        if (login_state == WFT_STATE_SUCCESS) {
            REGEX_FUNC(dect_video_callback, "POST", "/login/dect_video", task, context);
            REGEX_FUNC(disdect_video_callback, "POST", "/login/disdect_video", task, context);
        } else {
            LOG_F(ERROR, "Login in Server failed! Code: %d", login_state);
        }
    }

    void GLCCServer::user_login_callback(WFHttpTask * task, void * context) {
        int * state_ptr = (int *)context;
        *state_ptr = task->get_state();
        int error = task->get_error();
        if (*state_ptr == WFT_STATE_TOREPLY) {
            protocol::HttpRequest * req = task->get_req();
            protocol::HttpResponse * resp = task->get_resp();
            const bool only_login = REGEX_VALUE("/login", task);

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
                *state_ptr = WFT_STATE_UNDEFINED;
                return;
            }
            if (!root.isMember("user_name") || !root.isMember("user_password")) {
                LOG_F(ERROR, "Find request body key: %s, %s failed!", "use_name", "video_password");
                set_common_resp(resp, "400", "Bad Request");
                *state_ptr = WFT_STATE_UNDEFINED;
                return;
            }
            std::string user_name = root["user_name"].asString();
            std::string user_password = root["user_password"].asString();
            Json::Value resp_root;
            if (user_name == "rcf" && user_password == "9") {
                *state_ptr = WFT_STATE_SUCCESS;
                if (only_login) {
                    set_common_resp(resp, "200", "OK");
                    resp_root["status"] = "200";
                    resp_root["msg"] = "Success";
                    resp->append_output_body(resp_root.toStyledString());
                }
            } else {
                *state_ptr = WFT_STATE_UNDEFINED;
                set_common_resp(resp, "400", "Bad Request");
                resp_root["status"] = "400";
                resp_root["msg"] = "Error user_name or user_password";
                resp->append_output_body(resp_root.toStyledString());
            }
        } else {
            *state_ptr = WFT_STATE_SSL_ERROR;
            LOG_F(ERROR, "Login failed! Code: %d", error);
        }
    }


    void GLCCServer::user_register_callback(WFHttpTask * task) {
        int state = task->get_state();
        int error = task->get_error();
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
    }

    void GLCCServer::dect_video_callback(WFHttpTask * task, void * context) {
        protocol::HttpRequest * req = task->get_req();
        protocol::HttpResponse * resp = task->get_resp();
        struct glcc_server_context * _context  = (struct glcc_server_context *) context;

        const void * body;
        size_t body_len;
        req->get_parsed_body(&body, &body_len);
        const std::string body_text = (const char *)body;

        Json::Value root;
        Json::Reader reader;
        reader.parse(body_text, root);
        if (!root.isMember("use_template_path") || !root.isMember("video_name")) {
            LOG_F(ERROR, "Find request body key: %s, %s failed!", "use_template_path", "video_name");
            set_common_resp(resp, "400", "Bad Request");
            return;
        }
        std::string user_name = root["user_name"].asString();
        std::string user_password = root["user_password"].asString();
        bool use_template_path = root["use_template_path"].asBool();
        std::string video_name = root["video_name"].asString();
        char video_path[128] = {0};
        if (use_template_path) {
            snprintf(video_path, sizeof(video_path), 
                constants::video_path_template.c_str(), video_name.c_str());
        } else {
            std::strcpy(video_path, video_name.c_str());
        }
        char room_name[128] = {0};
        snprintf(room_name, sizeof(room_name), "%s-%s-%s", 
            user_name.c_str(), user_password.c_str(), video_name.c_str());
        char livego_manger_url[128] = {0};
        snprintf(livego_manger_url, sizeof(livego_manger_url), 
            constants::livego_manger_url_template.c_str(), room_name);
        char livego_delete_url[128] = {0};
        snprintf(livego_delete_url, sizeof(livego_delete_url), 
            constants::livego_delete_url_template.c_str(), room_name);
        LOG_F(INFO, "Source Video: %s | LiveGo Manger: %s | LiveGo Delete: %s", 
            video_path, livego_manger_url, livego_delete_url);

        struct glcc_server_context * req_context = new struct glcc_server_context;
        req_context->livego_context.room_name = room_name;
        req_context->livego_context.livego_manger_url = livego_manger_url;
        req_context->livego_context.livego_delete_url = livego_delete_url;
        req_context->video_context.video_path = video_path;
        req_context->detector_init_context = _context->detector_init_context;
        req_context->detector_run_context = _context->detector_run_context;

        // req_context->livego_context = _context->livego_context.
        SeriesWork * series = series_of(task); 
        series->set_context(req_context);
        WFHttpTask * http_task = WFTaskFactory::create_http_task(livego_manger_url, 
            0, 0, livego_manger_callback);
        http_task->user_data = task;
        protocol::HttpRequest * http_req = http_task->get_req();
        set_common_req(http_req);
        *series << http_task;
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
        if (!root.isMember("room_name") || !root.isMember("room_key")) {
            LOG_F(ERROR, "Find request body key: %s, %s failed!", "room_name", "room_key");
            set_common_resp(resp, "400", "Bad Request");
            return;
        }

        if (root["room_name"].isArray() && root["room_key"].isArray() \
                && root["room_name"].size() == root["room_key"].size()) {
            std::string room_key;
            std::stringstream room_names;
            room_names << "Stop room names: ";
            for (auto i = 0; i < (int)root["room_name"].size(); i++) {
                room_names << root["room_name"][i].asString() << " ";
                room_key = root["room_key"][i].asString();
                Detector * detector = ProductFactory<Detector>::Instance().GetProduct(room_key);
                detector->state = -1;
                if (detector == nullptr) {
                    LOG_F(INFO, "Find room: %s failed!", root["room_name"][i].asCString());
                    set_common_resp(resp, "400", "Bad Request");
                    return;
                }
            }
            LOG_F(INFO, room_names.str().c_str());
        } else if (!root["room_name"].isArray() && !root["room_name"].isArray()) {
            std::string room_name = root["room_name"].asString();
            std::string room_key = root["room_key"].asString();
            Detector * detector = ProductFactory<Detector>::Instance().GetProduct(room_key);
            detector->state = -1;
            if (detector == nullptr) {
                LOG_F(INFO, "Find room: %s failed!", room_name.c_str());
                set_common_resp(resp, "400", "Bad Request");
                return;
            }
            LOG_F(INFO, "Stop room: %s", room_name.c_str());
        }else {
            LOG_F(ERROR, "Error request format: %s", root.toStyledString().c_str());
            set_common_resp(resp, "400", "Bad Request");
            return;
        }
        set_common_resp(resp, "200", "OK");
    }

    void GLCCServer::livego_manger_callback(WFHttpTask * task) {
        int state = task->get_state();
        int error = task->get_error();
        SeriesWork * series = series_of(task); 
        struct glcc_server_context * context = (struct glcc_server_context *)series->get_context();
        WFHttpTask * upstream_task = (WFHttpTask *)task->user_data;
        protocol::HttpResponse * upstream_resq = (protocol::HttpResponse *)upstream_task->get_resp();
        if (state == WFT_STATE_SUCCESS) {
            // protocol::HttpRequest * req = task->get_req();
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
                goto del;
            }
            state = root["status"].asInt();
            std::string key = root["data"].asString();
            char livego_upload_url[128] = {0};
            snprintf(livego_upload_url, sizeof(livego_upload_url), 
                constants::livego_upload_url_template.c_str(), key.c_str());
            context->detector_run_context.upload_path = livego_upload_url;
            context->detector_run_context.video_path = context->video_context.video_path;
            context->livego_context.room_key = key;
            LOG_F(INFO, "LiveGO UpLoad: %s", livego_upload_url);
            WFGoTask * go_task = WFTaskFactory::create_go_task(key.c_str(), run_detector, key, context);

            go_task->set_callback([](WFGoTask * task) {
                struct glcc_server_context * context = (struct glcc_server_context *)task->user_data;
                std::string & key = context->livego_context.room_key;
                std::string & name = context->livego_context.room_name;
                std::string & livego_delete_url = context->livego_context.livego_delete_url;
                char * _name = new char [128];
                std::strcpy(_name, name.c_str());
                ProductFactory<Detector>::Instance().EraseProductDel(key);
                WFHttpTask * http_task = WFTaskFactory::create_http_task(
                    livego_delete_url, 0, 0, [](WFHttpTask * task){
                        int state = task->get_state(); int error = task->get_error();
                        const char * room_name = (const char *)task->user_data;
                        if (state == WFT_STATE_SUCCESS) {
                            LOG_F(INFO, "Delete the room: %s", room_name);
                        } else {
                            LOG_F(ERROR, "Delete the room: %s failed! Code: %d", room_name, error);
                        }
                        delete room_name;
                    });
                http_task->user_data = _name;
                *(series_of(task)) << http_task;
                delete context;
                LOG_F(INFO, "Release the context of %s", name.c_str());
            });
            go_task->user_data = context;
            go_task->start();
            set_common_resp(upstream_resq, "200", "OK");
            Json::Value reply;
            reply["room_name"] = context->livego_context.room_name;
            reply["room_key"] = key;
            upstream_resq->append_output_body(reply.toStyledString());
            return;
        } else {
            LOG_F(ERROR, "Send to LiveGo-Server failed! Code: %d", error);
        }
    del:
        set_common_resp(upstream_resq, "400", "Bad Request");
        delete context;
    }


    void GLCCServer::video_put_lattice_callback(WFHttpTask * task, void * context) {

    }

    void GLCCServer::video_disput_lattice_callback(WFHttpTask * task, void * context) {

    }

    void GLCCServer::run_detector(std::string key, struct glcc_server_context * context) {
        int state;
        Json::Value detector_init_context = context->detector_init_context;
        struct detector_run_context detector_run_context = context->detector_run_context;
        Detector * detector = register_detector(key, detector_init_context, &state);
        if (state == WFT_STATE_SYS_ERROR) {
            LOG_F(ERROR, "Register Detector failed! Code: %d", state);
        }
        std::string mode = detector_init_context["mode"].asString();
        LOG_F(INFO, "Mode: %s", mode.c_str());
        detector_run_context.vis_params = detector_init_context[(const char *)mode.c_str()]["vis_config"];
        state = detector->run(&detector_run_context);
        if (state == -1) {
            LOG_F(ERROR, "Detector run failed!");
            context->state = WFT_STATE_SYS_ERROR;
        }
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
    
    void GLCCServer::get_connection_infos(WFHttpTask * task, void * context) {
        auto seq = task->get_task_seq();
        char addstr[INET6_ADDRSTRLEN];
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        unsigned short port = 0;
        task->get_peer_addr((struct sockaddr *)&addr, &addrlen);
        switch (addr.ss_family)
        {
            case AF_INET: {
                struct sockaddr_in * sin = (struct sockaddr_in *)&addr;
                inet_ntop(AF_INET, &sin->sin_addr, addstr, sizeof(addstr));
                port = ntohs(sin->sin_port);
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 * sin6 = (struct sockaddr_in6 *)&addr;
                inet_ntop(AF_INET6, &sin6->sin6_addr, addstr, sizeof(addr));
                port = ntohs(sin6->sin6_port);
                break;
            }
            default: {
                strcpy(addstr, "Unknown connection!");
                break;
            }
        }
        *((std::iostream *)context) << "Peer address: " 
                                    << addstr << ":"
                                    << port << ", "
                                    << "seq: " <<  seq;
    }

    Detector * GLCCServer::register_detector(std::string & key, const Json::Value & detector_init_context, int * state) {
        const std::string mode = detector_init_context["mode"].asString();
        Detector * detector;
        if (mode == "ObjectDetector") {
            Json::Value context = detector_init_context[(const char *)mode.c_str()];
            if ((detector = ProductFactory<Detector>::Instance().GetProduct(key)) == nullptr) {
                ProductFactory<Detector>::Instance().RegisterProduct(key, ObjectDetector::init_func, (void *)&context);
                detector = ProductFactory<Detector>::Instance().GetProduct(key);
            }
            if (detector->state == -1) {
                ProductFactory<Detector>::Instance().EraseProductDel(key);
                detector = nullptr;
                *state = WFT_STATE_SYS_ERROR;
            } else {
                *state = WFT_STATE_SUCCESS;
            }

        }
        return detector;
    }

    int GLCCServer::cancel_detector(std::string & key) {
        return ProductFactory<Detector>::Instance().EraseProductDel(key);
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
                    std::vector<protocol::MySQLCell> row_result;
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