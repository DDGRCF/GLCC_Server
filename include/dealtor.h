#ifndef _DEALTOR_H
#define _DEALTOR_H
#include <vector>
#include <opencv2/opencv.hpp>
#include <string>
#include "loguru.hpp"
#include "detector.h"
#include "common.h"
#include "BYTETracker.h"


namespace GLCC{

    class Detector {
        public:
            std::atomic_int32_t state{0};
            std::atomic_bool is_put_lattice{true};
            std::mutex resource_lock;
            std::unordered_map<std::string, std::vector<cv::Point>> contour_list;
            std::string resource_dir;
            virtual int run(
                void * args, 
                std::function<void(void *)> cancel_func = nullptr,
                std::function<void(void *)> deal_func = nullptr) = 0;
            virtual int put_lattice(cv::Mat & image) {return 0;}
            virtual ~Detector() {}
        protected:
    };

    class ObjectDetector: protected Detector {
        public:
            ObjectDetector(const char * model_path, 
                            const char * device_name,
                            const int device_id);
            ObjectDetector(const std::string & model_path,
                           const std::string & device_name,
                           const int device_id);
            ~ObjectDetector();
            int run(void * args, 
                std::function<void(void *)> cancel_func = nullptr,
                std::function<void(void *)> deal_func = nullptr) override;
            int put_lattice(cv::Mat & image) override;
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
            static Detector * init_func(void * init_args);
        protected:
            mm_handle_t detector;
    };

    class TrackerDetector: protected ObjectDetector {
        public:
            TrackerDetector(const char * model_path,
                            const char * device_name,
                            const int device_id);
            TrackerDetector(const std::string & model_path,
                            const std::string & device_name,
                            const int device_id);
            ~TrackerDetector();

            int dect(cv::Mat & img, std::vector<Object> & objects, float score_thre);

            int run(void * args, 
                std::function<void(void *)> cancel_func = nullptr,
                std::function<void(void *)> deal_func = nullptr) override;
            static Detector * init_func(void * init_args);
    };
}

#endif
