#include "common.h"


namespace GLCC {
    namespace constants {
        // u_int16_t livego_manger_url_prot = "";
        // u_int16_t livego_upload_url_port = "";
        // u_int16_t livego_delete_url_port = "";
        std::string livego_manger_url_template = "http://127.0.0.1:8090/control/get?room=%s";
        std::string livego_upload_url_template = "rtmp://127.0.0.1:1935/live/%s";
        std::string livego_delete_url_template = "http://127.0.0.1:8090/control/delete?room=%s";
        std::string video_path_template = "rtsp://127.0.0.1:8554/%s";
        std::string mysql_url_root = "mysql://root:9696@127.0.0.1:3306";
        std::string mysql_url_template = "mysql://root:9696@127.0.0.1:3306/%s";
        std::string ssl_crt_path = "/home/r/Scripts/C++/New_GLCC_Server/.ssl/test/server.crt";
        std::string ssl_key_path = "/home/r/Scripts/C++/New_GLCC_Server/.ssl/test/server_rsa_private.pem.unsecure";
        std::string mysql_create_db_command = R"(
            CREATE DATABASE IF NOT EXISTS glccserver;
            CREATE table IF NOT EXISTS glccserver.User(username INTEGER NOT NULL UNIQUE, password VARCHAR(20) NOT NULL, usernickname VARCHAR(20), PRIMARY KEY (username));
            CREATE table IF NOT EXISTS glccserver.VideoDevice(room_key VARCHAR(50) NOT NULL UNIQUE, username INTEGER NOT NULL, room_name VARCHAR(50), 
                video_from_url VARCHAR(100), video_to_url VARCHAR(100), PRIMARY KEY (room_key), FOREIGN KEY (username) REFERENCES glccserver.User(username));

            DROP TRIGGER if EXISTS glccserver.after_user_delete;
            CREATE TRIGGER glccserver.after_user_delete
            BEFORE DELETE ON glccserver.User FOR EACH ROW
            BEGIN
                DELETE from glccserver.VideoDevice WHERE username=OLD.username;
            END;

            DROP TRIGGER IF EXISTS glccserver.before_videodevice_insert;
            CREATE TRIGGER glccserver.before_videodevice_insert
            BEFORE INSERT ON glccserver.VideoDevice FOR EACH ROW
            BEGIN
                declare num int default 0;
                declare msg varchar(100);
                select count(*) into num from glccserver.User where username=NEW.username;
                if num <= 0 then
                    set msg=concat("Find the ", NEW.username, "failed!");
                	signal sqlstate '45000' set message_text=msg;
                end if;
            END;
        )";
    }

    int get_time_file(const char* file_stem, 
                    const char * file_prefix, 
                    const char* file_suffix, 
                    char * file_path){
        time_t timep;    
        struct tm *p;
        char name[BUFSIZ] = {0};
        time(&timep);
        p = localtime(&timep);
        return snprintf(file_path, sizeof(name), 
            "%s%s-%d-%d-%d-%d-%02d%s", 
            file_prefix?file_prefix:"",
            file_stem?file_stem:"",
            1900 + p->tm_year, 
            1 + p->tm_mon, 
            p->tm_mday, p->tm_hour, 
            p->tm_min, 
            file_suffix?file_suffix:"");
    }

    int check_dir(const char * check_path, const bool if_exists_mkdir) {
        int ret;
        struct stat st;
        if (stat(check_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                return 0;
            } else {
                return -1;
            }
        } else {
            if (if_exists_mkdir) {
                ret = mkdir(check_path, 00700);
                return ret;
            } else {
                return -1;
            }
        }
    }

    int check_file(std::string check_path, 
                   std::vector<std::string> * file_set, 
                   const std::vector<std::string> suffix, 
                   const bool verbose) {
        struct stat st;
        char buf[BUFSIZ] = {0};
        if (stat(check_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (verbose) {
                    LOG_F(INFO, "Find dir: %s\n", check_path.c_str());
                }
                size_t total_file = 0;
                if (suffix.size()) {
                    for (auto sfx: suffix) {
                        snprintf(buf, sizeof(buf), "%s%s%s", check_path.c_str(), "/*.", sfx.c_str());
                        glob_t gl;
                        // TODO: confirm error num
                        glob(buf, GLOB_ERR, nullptr, &gl);
                        for (size_t i = 0; i < gl.gl_pathc; i++) {
                            total_file ++;
                            const char * subpath = gl.gl_pathv[i];
                            if (verbose) {
                                LOG_F(INFO, "Find subpath of dir: %s\n", subpath);
                            }
                            if (file_set != nullptr) {
                                file_set->emplace_back(subpath);
                            }
                        }
                        globfree(&gl);
                    }
                } else {
                    if (file_set != nullptr) {
                        file_set->emplace_back(check_path);
                    }
                }
                return 1 + total_file;
            } else if (S_ISREG(st.st_mode)) {
                if (file_set != nullptr) {
                    file_set->emplace_back(check_path);
                }
                if (verbose) {
                    LOG_F(INFO, "Find file path: %s\n", check_path.c_str());
                }
                return 0;
            }
        } else {
            return -1;
        }
        return -1;
    }

    const float color_list[][3] = {
        {0.000, 0.447, 0.741},
        {0.850, 0.325, 0.098},
        {0.929, 0.694, 0.125},
        {0.494, 0.184, 0.556},
        {0.466, 0.674, 0.188},
        {0.301, 0.745, 0.933},
        {0.635, 0.078, 0.184},
        {0.300, 0.300, 0.300},
        {0.600, 0.600, 0.600},
        {1.000, 0.000, 0.000},
        {1.000, 0.500, 0.000},
        {0.749, 0.749, 0.000},
        {0.000, 1.000, 0.000},
        {0.000, 0.000, 1.000},
        {0.667, 0.000, 1.000},
        {0.333, 0.333, 0.000},
        {0.333, 0.667, 0.000},
        {0.333, 1.000, 0.000},
        {0.667, 0.333, 0.000},
        {0.667, 0.667, 0.000},
        {0.667, 1.000, 0.000},
        {1.000, 0.333, 0.000},
        {1.000, 0.667, 0.000},
        {1.000, 1.000, 0.000},
        {0.000, 0.333, 0.500},
        {0.000, 0.667, 0.500},
        {0.000, 1.000, 0.500},
        {0.333, 0.000, 0.500},
        {0.333, 0.333, 0.500},
        {0.333, 0.667, 0.500},
        {0.333, 1.000, 0.500},
        {0.667, 0.000, 0.500},
        {0.667, 0.333, 0.500},
        {0.667, 0.667, 0.500},
        {0.667, 1.000, 0.500},
        {1.000, 0.000, 0.500},
        {1.000, 0.333, 0.500},
        {1.000, 0.667, 0.500},
        {1.000, 1.000, 0.500},
        {0.000, 0.333, 1.000},
        {0.000, 0.667, 1.000},
        {0.000, 1.000, 1.000},
        {0.333, 0.000, 1.000},
        {0.333, 0.333, 1.000},
        {0.333, 0.667, 1.000},
        {0.333, 1.000, 1.000},
        {0.667, 0.000, 1.000},
        {0.667, 0.333, 1.000},
        {0.667, 0.667, 1.000},
        {0.667, 1.000, 1.000},
        {1.000, 0.000, 1.000},
        {1.000, 0.333, 1.000},
        {1.000, 0.667, 1.000},
        {0.333, 0.000, 0.000},
        {0.500, 0.000, 0.000},
        {0.667, 0.000, 0.000},
        {0.833, 0.000, 0.000},
        {1.000, 0.000, 0.000},
        {0.000, 0.167, 0.000},
        {0.000, 0.333, 0.000},
        {0.000, 0.500, 0.000},
        {0.000, 0.667, 0.000},
        {0.000, 0.833, 0.000},
        {0.000, 1.000, 0.000},
        {0.000, 0.000, 0.167},
        {0.000, 0.000, 0.333},
        {0.000, 0.000, 0.500},
        {0.000, 0.000, 0.667},
        {0.000, 0.000, 0.833},
        {0.000, 0.000, 1.000},
        {0.000, 0.000, 0.000},
        {0.143, 0.143, 0.143},
        {0.286, 0.286, 0.286},
        {0.429, 0.429, 0.429},
        {0.571, 0.571, 0.571},
        {0.714, 0.714, 0.714},
        {0.857, 0.857, 0.857},
        {0.000, 0.447, 0.741},
        {0.314, 0.717, 0.741},
        {0.50,  0.5,   0}
    };

}