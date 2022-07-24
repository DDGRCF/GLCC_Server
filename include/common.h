#ifndef _COMMON_H
#define _COMMON_H

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
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
#include <loguru.hpp>
#include <jsoncpp/json/json.h>
#include <fstream>


namespace GLCC {
    extern const float color_list[][3];
    enum Key {ESC=27};

    struct server_context {
        std::string server_ip="0.0.0.0";
        u_int16_t server_port=9999;
    };

    struct client_context {
        std::string client_ip;
        u_int16_t client_port;
    };

    struct video_context {
        std::string video_path;
        std::string video_path_template;
        bool use_template_path=false;
    };

    struct livego_context {
        std::string room_name;
        std::string room_key;
        std::string livego_manger_url;
        std::string livego_upload_url;
        std::string livego_delete_url;
        std::string livego_manger_url_template;
        std::string livego_upload_url_template;
        std::string livego_delete_url_template;
    };

    struct detector_init_context {
        std::string model_path=nullptr; // camera 0
        std::string device_name="cpu"; // cuda
        int device_id=0; // 0
    };

    struct detector_run_context {
        std::string video_path; // video to be sampled
        std::string upload_path; // rtmp to be load
        Json::Value vis_params;
    };

    struct glcc_server_context {
        int state;
        // Base
        struct server_context server_context;
        struct client_context client_context;
        struct video_context  video_context;
        struct livego_context  livego_context;
        // Detector
        // struct detector_init_context  detector_init_context;
        Json::Value detector_init_context;
        struct detector_run_context  detector_run_context;
    };


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

            void RegisterProduct(std::string & name, ProductType_t *registrar)
            {
                std::lock_guard<std::mutex> lock_guard(lock);
                m_ProductRegistry[name] = registrar;
            }

            void RegisterProduct(std::string & name, std::function<ProductType_t* (void *)> init_func, void * init_args)
            {
                std::lock_guard<std::mutex> lock_guard(lock);
                m_ProductRegistry[name] = init_func(init_args);
            }

            int EraseProductDel(std::string name) {
                std::lock_guard<std::mutex> lock_guard(lock);
                if (m_ProductRegistry.find(name) != m_ProductRegistry.end()) {
                    ProductType_t * elem = m_ProductRegistry[name];
                    int n = m_ProductRegistry.erase(name);
                    delete elem;
                    return n;
                }
                return m_ProductRegistry.erase(name);
            }
            int EraseProduct(std::string name) {
                std::lock_guard<std::mutex> lock_guard(lock);
                return m_ProductRegistry.erase(name);
            }

            ProductType_t *GetProduct(std::string & name)
            {
                std::lock_guard<std::mutex> lock_guard(lock);
                if (m_ProductRegistry.find(name) != m_ProductRegistry.end())
                {
                    return m_ProductRegistry[name];
                }
                return nullptr;
            }

    private:
        ProductFactory() {}
        ~ProductFactory() {}

        std::unordered_map<std::string, ProductType_t *> m_ProductRegistry;
    };

    template<class ProductType_t>
    std::mutex ProductFactory<ProductType_t>::lock;

    int get_time_file(const char* file_stem, 
                    const char * file_prefix, 
                    const char* file_suffix, 
                    char * file_path);
    
    int check_dir(const char * check_path, const bool if_exists_mkdir=false);

    int check_file(std::string check_path, 
                   std::vector<std::string> * file_set, 
                   const std::vector<std::string> suffix={}, 
                   const bool verbose=false);
}

#endif