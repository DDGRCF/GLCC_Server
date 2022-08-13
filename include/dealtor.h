#ifndef _DEALTOR_H
#define _DEALTOR_H
#include <vector>
#include <opencv2/opencv.hpp>
#include <string>
#include "loguru.hpp"
#include "detector.h"
#include "common.h"


namespace GLCC{

    class Detector {
        public:
            std::atomic_int32_t state{0};
            std::atomic_bool is_put_lattice{false};
            std::mutex resource_lock;
            std::unordered_map<std::string, std::vector<cv::Point>> contour_list;
            virtual int run(void * args, std::function<void()> cancel_func=nullptr) = 0;
            virtual int put_lattice(cv::Mat & image) {return 0;}
            virtual ~Detector() {}
        protected:
    };

    class ObjectDetector: public Detector {
        public:
            ObjectDetector(const char * model_path, 
                            const char * device_name,
                            const int device_id);
            ObjectDetector(std::string & model_path,
                           std::string & device_name,
                           const int device_id);
            ~ObjectDetector();
            // int init() override;
            int run(void * args, std::function<void()> cancel_func = nullptr) override;
            int put_lattice(cv::Mat & image) override;
            int dect(const char * image_path, 
                     const float score_thre,
                     const bool is_save=false, 
                     const bool is_show=false,
                     const bool is_text=false,
                     const char * save_path="./dect.jpg",
                     const int rect_thickness=2,
                     const int text_thickness=2,
                     const float text_scale=0.6,
                     const int * rect_color=nullptr,
                     const int * text_color=nullptr,
                     const char ** class_name=nullptr,
                     const bool verbose = false);
            int dect(cv::Mat & imgs, 
                     const float score_thre,
                     const bool is_save=false, 
                     const bool is_show=false,
                     const bool is_text=false,
                     const char * save_path="./dect.jpg",
                     const int rect_thickness=2,
                     const int text_thickness=2,
                     const float text_scale=0.6,
                     const int * rect_color=nullptr,
                     const int * text_color=nullptr,
                     const char ** class_name=nullptr,
                     const bool verbose = false);
            int dect(std::vector<cv::Mat> & imgs, 
                     const float score_thre,
                     const bool is_save=false, 
                     const bool is_show=false,
                     const bool is_text=false,
                     const char * save_path="./dect.jpg",
                     const int rect_thickness=2,
                     const int text_thickness=2,
                     const float text_scale=0.6,
                     const int * rect_color=nullptr,
                     const int * text_color=nullptr,
                     const char ** class_name=nullptr,
                     const bool verbose = false);
            static ObjectDetector* init_func(void * init_args);
        protected:
            mm_handle_t detector;
    };
}

#endif
