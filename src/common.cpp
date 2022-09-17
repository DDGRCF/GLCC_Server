#include "common.h"


namespace GLCC {
    namespace constants {
        // time
        const long num_millisecond_per_second = 1000;
        const long num_microsecond_per_second = num_millisecond_per_second * 1000;
        // 
        long long max_detector_live_time = 5 * 60 * num_microsecond_per_second;
        long long interval_to_watch_detector = 60 * 60 * num_microsecond_per_second;
        long long interval_to_watch_file = 5 * 60 * num_microsecond_per_second;
        long max_video_save_day = 2; 

        // livego 
        std::string file_time_format = "%Y-%m-%d_%H:%M:%S";
        std::string video_path_template = "rtsp://127.0.0.1:5544/live/%s";
        std::string livego_push_url_template = "rtmp://127.0.0.1:1935/live/%s";
        std::string livego_check_stat_template = "http://127.0.0.1:8083/api/stat/group?stream_name=%s";
        std::string livego_stop_reply_pull_url_template = "http://127.0.0.1:8083/api/ctrl/stop_relay_pull?stream_name=%s";
        std::string livego_kick_url = "http://127.0.0.1:8083/api/ctrl/kick_session";

        std::string ffmpeg_file_push_command = "ffmpeg -stream_loop -1 -i %s -vcodec libx264 -f flv %s";
        std::string ffmpeg_push_command = "ffmpeg -y -an -f rawvideo -vcodec rawvideo -pix_fmt bgr24 -s %dx%d -r %d -i - -c:v libx264 -pix_fmt yuv420p -preset ultrafast -f flv %s";
        std::string cover_save_suffix = "jpg";

        std::string mysql_root_url = "mysql://root:9696@127.0.0.1:3306";
        std::string mysql_glccserver_url = mysql_root_url + "/glccserver";

        const std::vector<std::string> video_suffixes = {"mp4", "flv", "wmv", "mpeg", "avi"};
        const std::vector<std::string> video_prefixes = {"http", "https", "rtmp", "rtsp"};

        std::string ssl_crt_path = "/home/r/Scripts/C++/New_GLCC_Server/.ssl/test/server.crt";
        std::string ssl_key_path = "/home/r/Scripts/C++/New_GLCC_Server/.ssl/test/server_rsa_private.pem.unsecure";
        const std::string mysql_create_db_command = R"(
            SET global log_bin_trust_function_creators = 1;
            CREATE DATABASE IF NOT EXISTS glccserver;
            CREATE TABLE IF NOT EXISTS glccserver.User(username VARCHAR(20) NOT NULL UNIQUE, password VARCHAR(20) NOT NULL, nickname VARCHAR(20) NOT NULL, 
                PRIMARY KEY (username));
            CREATE TABLE IF NOT EXISTS glccserver.Video(video_name VARCHAR(20) NOT NULL, username VARCHAR(20) NOT NULL, video_url VARCHAR(256) NOT NULL, 
                PRIMARY KEY (video_name, username), 
                FOREIGN KEY (username) REFERENCES glccserver.User(username));
            CREATE TABLE IF NOT EXISTS glccserver.Room(room_name VARCHAR(50) NOT NULL, username VARCHAR(20) NOT NULL, 
                video_name VARCHAR(20) NOT NULL, start_time TIMESTAMP NOT NULL, end_time TIMESTAMP NOT NULL, 
                PRIMARY KEY (username, video_name, room_name), 
                FOREIGN KEY (username) REFERENCES glccserver.User(username),
                FOREIGN KEY (video_name) REFERENCES glccserver.Video(video_name));

            CREATE TABLE IF NOT EXISTS glccserver.Room(room_name VARCHAR(50) NOT NULL UNIQUE, username VARCHAR(20) NOT NULL, 
                video_name VARCHAR(20) NOT NULL, start_time TIMESTAMP NOT NULL, end_time TIMESTAMP NOT NULL, 
                PRIMARY KEY (room_name), 
                FOREIGN KEY (username) REFERENCES glccserver.User(username),
                FOREIGN KEY (video_name) REFERENCES glccserver.Video(video_name));

            CREATE TABLE IF NOT EXISTS glccserver.Contour(contour_name VARCHAR(20) NOT NULL, username VARCHAR(20) NOT NULL, video_name VARCHAR(20) NOT NULL, contour_path JSON NOT NULL,
                PRIMARY KEY (username, video_name, contour_name), FOREIGN KEY (username) REFERENCES glccserver.User(username),
                FOREIGN KEY (video_name) REFERENCES glccserver.Video(video_name));

            CREATE TABLE IF NOT EXISTS glccserver.File(file_path VARCHAR(256) NOT NULL, video_name VARCHAR(20) NOT NULL, 
                username VARCHAR(20) NOT NULL, start_time TIMESTAMP NOT NULL, end_time TIMESTAMP NOT NULL, 
                PRIMARY KEY (username, video_name, file_path), 
                FOREIGN KEY (video_name) references glccserver.Video(video_name), 
                FOREIGN KEY (username) REFERENCES glccserver.User(username));

            DROP PROCEDURE IF EXISTS glccserver.proc_time_compare;
            CREATE PROCEDURE glccserver.proc_time_compare(
                IN start_time TIMESTAMP,
                IN end_time TIMESTAMP,
                OUT result TINYINT
            )BEGIN
                DECLARE ctime INT;
                set ctime = TIMESTAMPDIFF(SECOND, start_time, end_time);
                set result = IF(ctime >= 0, 1, -1);
            END;

            DROP FUNCTION IF EXISTS glccserver.func_time_compare;
            CREATE 
                FUNCTION glccserver.func_time_compare(start_time TIMESTAMP, end_time TIMESTAMP)
                RETURNS INT
            BEGIN
                DECLARE ctime INT;
                DECLARE res INT;
                set ctime = TIMESTAMPDIFF(SECOND, start_time, end_time);
                set res = IF(ctime > 0, 1, -1);
                return res;
            END;

            DROP TRIGGER if exists glccserver.before_file_insert;
            create trigger glccserver.before_file_insert
            before insert on glccserver.File FOR EACH ROW
            begin
                DECLARE num INT DEFAULT 0; 
                DECLARE msg VARCHAR(100);
                SELECT COUNT(*) INTO num FROM glccserver.Video WHERE video_name=NEW.video_name AND username=NEW.username;
                IF num <= 0 THEN
                    set msg=concat("Video: Find the ", NEW.video_name, " failed!");
                	signal sqlstate '45000' set message_text=msg;
                END IF;
            END;


            DROP TRIGGER IF EXISTS glccserver.before_room_insert;
            CREATE TRIGGER glccserver.before_room_insert
            BEFORE INSERT ON glccserver.Room FOR EACH ROW
            BEGIN
                DECLARE num INT DEFAULT 0;
                DECLARE compare INT DEFAULT 0;
                DECLARE msg VARCHAR(100);
                SELECT COUNT(*) INTO num FROM glccserver.Video WHERE video_name=NEW.video_name AND username=NEW.username;
                IF num <= 0 THEN 
                    set msg=concat("Room: Find ", "username: ", NEW.username,  ", video name: ", NEW.video_name, " failed!");
                    signal sqlstate "45000" set message_text=msg;
                END IF;
                CALL glccserver.proc_time_compare(NEW.start_time, NEW.end_time, compare);
                IF compare < 0 THEN
                    SET msg=concat("Room: Insert ", "start_time: ", NEW.start_time, " end_time: ", NEW.end_time, " failed!");
                    signal sqlstate "45000" set message_text=msg;
                END IF;
            END;

            DROP TRIGGER IF EXISTS glccserver.before_video_insert;
            CREATE TRIGGER glccserver.before_video_insert
            BEFORE INSERT ON glccserver.Video FOR EACH ROW
            BEGIN
                DECLARE num INT DEFAULT 0;
                DECLARE msg VARCHAR(100);
                SELECT COUNT(*) INTO num FROM glccserver.User WHERE username=NEW.username;
                IF num <= 0 THEN
                    set msg=concat("Video: Find the ", NEW.username, " failed!");
                	signal sqlstate '45000' set message_text=msg;
                END IF;
            END;

            DROP TRIGGER IF EXISTS glccserver.before_contour_insert;
            CREATE TRIGGER glccserver.before_contour_insert
            BEFORE INSERT ON glccserver.Contour FOR EACH ROW
            BEGIN
                DECLARE num INT DEFAULT 0;
                DECLARE msg VARCHAR(100);
                SELECT COUNT(*) INTO num FROM glccserver.Video WHERE video_name=NEW.video_name;
                IF num <= 0 THEN
                    set msg=concat("Video: Find the ", NEW.video_name, " failed!");
                	signal sqlstate '45000' set message_text=msg;
                END IF;
            END;

            DROP TRIGGER IF EXISTS glccserver.before_user_delete;
            CREATE TRIGGER glccserver.before_user_delete
            BEFORE DELETE ON glccserver.User FOR EACH ROW
            BEGIN
                DELETE FROM glccserver.Video WHERE username=OLD.username;
            END;

            DROP TRIGGER IF EXISTS glccserver.before_video_delete;
            CREATE TRIGGER glccserver.before_video_delete
            BEFORE DELETE ON glccserver.Video FOR EACH ROW
            BEGIN
                DELETE FROM glccserver.Room WHERE video_name=OLD.video_name and username=OLD.username;
                DELETE FROM glccserver.Contour WHERE video_name=OLD.video_name and username=OLD.username;
                DELETE from glccserver.File where video_name=OLD.video_name and username=OLD.username;
            END;

        )";
    }


    std::string get_now_time(const std::string & time_format) noexcept {
        std::stringstream ss;
        auto time_now = std::chrono::system_clock::now();
        time_t now = std::chrono::system_clock::to_time_t(time_now);
        ss << std::put_time(localtime(&now), time_format.c_str());
        return ss.str();
    }


    int get_cwd(std::string & file_path) noexcept {
        char * buffer;
        if ((buffer = getcwd(NULL, 0)) == NULL) {
            return -1;
        } else {
            file_path = buffer;
            free(buffer);
            return 0;
        }
    }

    int parse_path(const std::string & path, std::unordered_map<std::string, std::string> & result_map) noexcept {
        static std::vector<std::string> fields{"path", "dirname", "basename", "stem", "suffix"};
        static std::regex pattern{"^(.*)/((.*)\\.(.*))"};
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                result_map["path"] = path;
                result_map["dirname"] = path;
                return 0;
            } else {
                std::smatch results;
                if (std::regex_match(path, results, pattern)) {
                    for (int i = 0; i < (int)fields.size(); i++) {
                        auto & key = fields[i];
                        result_map[key] = results[i];
                    }
                    return 0;
                } else {
                    return -1;
                }
            }
        } else {
            return -1;
        }
    }

    int read_file_list(const std::string & base_path, std::vector<std::string> files) noexcept {
        DIR * dir;
        struct dirent * ptr;

        if ((dir = opendir(base_path.c_str())) == nullptr) {
            return -1;
        }

        while ((ptr = readdir(dir)) != nullptr) {
            if (strcmp(ptr->d_name, ".") == 0 || strcmp(ptr->d_name, "..") == 0) {
                continue;
            } else {
                std::stringstream ss;
                ss << base_path << "/" << ptr->d_name;
                files.emplace_back(ss.str());
            }
        }
        return 0;
    }


    std::string join(std::vector<std::string> &strings, std::string delim, 
            std::function<std::string(std::string &, std::string &)> func) {
        func = func==nullptr ? [&delim](std::string &x, std::string &y) {
            return x.empty() ? y : x + delim + y;} : func;
        return std::accumulate(
            strings.begin(), strings.end(), std::string(), func
        );
    }


    int check_dir(const std::string & check_path, const bool is_mkdir) noexcept {
        int ret;
        struct stat st;
        if (stat(check_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                return 0;
            } else {
                return -1;
            }
        } else {
            if (is_mkdir) {
                ret = mkdir(check_path.c_str(), 00700);
                return ret;
            } else {
                return -1;
            }
        }
    }


    int check_file(const std::string & check_path, 
                   std::vector<std::string> * file_set, 
                   const std::vector<std::string> suffix) noexcept {
        struct stat st;
        char buf[BUFSIZ] = {0};
        if (stat(check_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                size_t total_file = 0;
                if (suffix.size()) {
                    for (auto sfx: suffix) {
                        std::snprintf(buf, sizeof(buf), "%s%s%s", check_path.c_str(), "/*.", sfx.c_str());
                        glob_t gl;
                        /*    `errno' value from the failing call; if it returns non-zero
                            `glob' returns GLOB_ABEND; if it returns zero, the error is ignored. */
                        int ret = glob(buf, GLOB_ERR, nullptr, &gl);
                        if (ret != 0) {
                            return -2;
                        }
                        for (size_t i = 0; i < gl.gl_pathc; i++) {
                            total_file ++;
                            const char * subpath = gl.gl_pathv[i];
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
                return 0;
            }
        } else {
            return -1;
        }
        return 0;
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