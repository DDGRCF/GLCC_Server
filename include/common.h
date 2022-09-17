#ifndef _COMMON_H
#define _COMMON_H

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <glob.h>
#include <string.h>
#include <vector>
#include <array>
#include <string>
#include <regex>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <loguru.hpp>
#include <jsoncpp/json/json.h>
#include <fstream>
#include <set>
#include <atomic>
#include <mutex>
#include <numeric>
#include <iomanip>
#include <dirent.h>


namespace GLCC {
    extern const float color_list[][3];
    enum Key {ESC=27};
    enum CancelMode {NORMAL_CANCEL=0, FORCE_CANCEL=1, WAKE_CANCEL=2};
    enum RegisterMode {Create_Register=0, Judge_Register=1};
    namespace constants {
        extern const std::string mysql_create_db_command;
        extern const long num_millisecond_per_second;
        extern const long num_microsecond_per_second;

        extern long long max_detector_live_time;
        extern long long interval_to_watch_detector;
        extern long long interval_to_watch_file;

        extern long max_video_save_day;
        extern std::string file_time_format;
        extern std::string livego_check_stat_template;
        extern std::string livego_push_url_template;
        extern std::string livego_stop_reply_pull_url_template;
        extern std::string livego_kick_url;
        extern std::string ffmpeg_push_command;
        extern std::string ffmpeg_file_push_command;
        extern std::string cover_save_suffix;
        extern std::string video_path_template;

        extern const std::vector<std::string> video_suffixes;
        extern const std::vector<std::string> video_prefixes;

        extern std::string mysql_root_url;
        extern std::string mysql_glccserver_url;
        extern std::string ssl_crt_path;
        extern std::string ssl_key_path;

    }

    typedef struct ssl_context {
        std::string ssl_crt_path;
        std::string ssl_key_path;
    } ssl_context_t;

    typedef struct server_dir {
        std::string work_dir;
        std::string user_dir;
    } server_dir_t;

    typedef struct url_context {
        std::string ip;
        u_int16_t port;
    } url_context_t;

    typedef struct video_context {
        std::string video_path;
        std::string video_name;
        bool use_template_path=false;
    } video_context_t;

    typedef struct livego_context {
        std::string room_name;
        std::string livego_push_url;
    } livego_context_t;

    typedef struct detector_init_context {
        std::string model_path=nullptr; // camera 0
        std::string device_name="cpu"; // cuda
        int device_id=0; // 0
    } detector_init_context_t;

    typedef struct detector_run_context {
        std::string video_path; // video to be sampled
        std::string upload_path; // rtmp to be load
        Json::Value vis_params;
    } detector_run_context_t;

    typedef struct glcc_server_context {
        int state;
        int error;
        // Base
        url_context_t server_context;
        url_context_t client_context;
        server_dir_t server_dir;
        video_context_t video_context;
        livego_context_t livego_context;
        // Detector
        Json::Value detector_init_context;
        detector_run_context_t  detector_run_context;
        // Extra info
        Json::Value extra_info;
    } glcc_server_context_t;

    template <class ProductType_t>
    class ProductFactory
    {
        public:
            // delete all copy 
            ProductFactory(const ProductFactory &) = delete;
            ProductFactory(const ProductFactory &&) = delete;
            const ProductFactory& operator=(const ProductFactory &) = delete;
            const ProductFactory& operator=(const ProductFactory &&) = delete;
            static std::mutex lock;

            static ProductFactory<ProductType_t> &Instance() {
                static ProductFactory<ProductType_t> instance;
                return instance;
            }

            static ProductFactory<ProductType_t> *Instance_ptr() {
                return &Instance();
            }

            void RegisterProduct(const std::string & name, ProductType_t *registrar)
            {
                std::lock_guard<std::mutex> lock_guard(lock);
                m_ProductRegistry[name] = registrar;
            }

            void RegisterProduct(const std::string & name, std::function<ProductType_t* (void *)> init_func, void * init_args)
            {
                std::lock_guard<std::mutex> lock_guard(lock);
                m_ProductRegistry[name] = init_func(init_args);
            }

            int EraseProductDel(const std::string & name) {
                std::lock_guard<std::mutex> lock_guard(lock);
                if (m_ProductRegistry.find(name) != m_ProductRegistry.end()) {
                    ProductType_t * elem = m_ProductRegistry[name];
                    int n = m_ProductRegistry.erase(name);
                    delete elem;
                    return n;
                }
                return m_ProductRegistry.erase(name);
            }
            int EraseProduct(const std::string & name) {
                std::lock_guard<std::mutex> lock_guard(lock);
                return m_ProductRegistry.erase(name);
            }

            ProductType_t *GetProduct(const std::string & name)
            {
                std::lock_guard<std::mutex> lock_guard(lock);
                if (m_ProductRegistry.find(name) != m_ProductRegistry.end())
                {
                    return m_ProductRegistry[name];
                }
                return nullptr;
            }

            ProductType_t *GetProduct(const char * name)
            {
                std::string _name = name;
                return GetProduct(_name);
            }

    private:
        ProductFactory() {}
        ~ProductFactory() {}

        std::unordered_map<std::string, ProductType_t *> m_ProductRegistry;
    };

    template<class ProductType_t>
    std::mutex ProductFactory<ProductType_t>::lock;


    std::string get_now_time(const std::string & time_format) noexcept ;

    std::string join(std::vector<std::string> & strings, std::string delim, 
        std::function<std::string(std::string &, std::string &)> = nullptr);
    
    int check_dir(const std::string & check_path, const bool is_mkdir=false) noexcept;

    int parse_path(const std::string & path, std::unordered_map<std::string, std::string> & result_map) noexcept;

    int read_file_list(const std::string & base_path, std::vector<std::string> files) noexcept;

    int check_file(const std::string & check_path, 
                   std::vector<std::string> * file_set = nullptr, 
                   const std::vector<std::string> suffix={}) noexcept;
    int get_cwd(std::string & file_path) noexcept;


}

#endif