#include "dealtor.h"

namespace GLCC{
    ObjectDetector::ObjectDetector(const char * model_path, 
                                   const char * device_name, 
                                   const int device_id) {
        int ret = mmdeploy_detector_create_by_path(model_path, device_name, device_id, &detector);
        if (ret != MM_SUCCESS) {
            LOG_F(ERROR, "Create detector failed! Code: %d", (int)ret);
            state = -1;
        } 
    }

    ObjectDetector::ObjectDetector(std::string & model_path, 
                                   std::string & device_name, 
                                   const int device_id) {
        int ret = mmdeploy_detector_create_by_path(model_path.c_str(), device_name.c_str(), device_id, &detector);
        if (ret != MM_SUCCESS) {
            LOG_F(ERROR, "Create detector failed! Code: %d", (int)ret);
            state = -1;
        } 
    }

    ObjectDetector::~ObjectDetector() {
    }

    int ObjectDetector::dect(const char * image_path, 
                              const float score_thre,
                              const bool is_save,
                              const bool is_show,
                              const bool is_text,
                              const char * save_path,
                              const int rect_thickness,
                              const int text_thickness,
                              const float text_scale,
                              const int * rect_color,
                              const int * text_color,
                              const char ** class_name,
                              const bool verbose) {
        std::vector<std::string> file_set;
        int ret = GLCC::check_file(image_path, &file_set, {"png", "tif", "jpg", "jpeg"}, verbose);
        if (ret == -1) {
            LOG_F(ERROR, "%s didn't exist", image_path);
            return -1;
        }
        for (auto file_path: file_set) {
            cv::Mat img = cv::imread(file_path);
            if (!img.data) {
                LOG_F(ERROR, "Read Image: %s failed!", file_path.c_str());
                return -1;
            }
            else {
               ret = dect(img, 
                         score_thre,
                         is_save,
                         is_show,
                         is_text,
                         save_path,
                         rect_thickness,
                         text_thickness,
                         text_scale,
                         rect_color,
                         text_color,
                         class_name,
                         verbose);
                if (ret == -1) {
                    LOG_F(ERROR, "Dect %s failed!", file_path.c_str());
                    return -1;
                }
            }
        }
        return ret;
    }

    int ObjectDetector::dect(std::vector<cv::Mat> & imgs, 
                              const float score_thre,
                              const bool is_save,
                              const bool is_show,
                              const bool is_text,
                              const char * save_path,
                              const int rect_thickness,
                              const int text_thickness,
                              const float text_scale,
                              const int * rect_color,
                              const int * text_color,
                              const char ** class_name,
                              const bool verbose) {
        int ret;
        char text[512] = {0};
        int num_imgs = (int)imgs.size();
        std::vector<mm_mat_t> mm_mat_v;
        mm_mat_v.resize(num_imgs);
        for (auto img: imgs) {
            // mm_mat_v.emplace_back(img.data, img.rows, img.cols, 3, MM_BGR, MM_INT8);
            mm_mat_v.push_back({img.data, img.rows, img.cols, 3, MM_BGR, MM_INT8});
        }
        mm_detect_t * bboxes;
        int * res_count;
        ret = mmdeploy_detector_apply(detector, (mm_mat_t *)mm_mat_v.data(), num_imgs, &bboxes, &res_count);

        if (ret != MM_SUCCESS) {
            LOG_F(ERROR, "Failed to apply detector, code: %d", (int)ret);
            return -1;
        }

        if (verbose) {
            std::stringstream bbox_info;
            for (int i = 0; i < num_imgs; i++) {
                bbox_info << "\n" << "image: " << i << " | " << "bbox_num: " << *(res_count + i);
            }
            LOG_F(INFO, bbox_info.str().c_str());
        }
        int cur_obj_id = 0;
        for (int img_id = 0; img_id < num_imgs; img_id++){
            for (int obj_id = 0; obj_id< *(res_count + img_id); obj_id++) {
                cur_obj_id++;
                const auto &box = bboxes[cur_obj_id].bbox;
                const auto &mask = bboxes[cur_obj_id].mask;
                const auto &label_id = bboxes[cur_obj_id].label_id;
                const auto &score = bboxes[cur_obj_id].score;

                if (verbose) {
                    LOG_F(INFO, "img: %d, obj: %d, left=%.2f, top=%.2f, right=%.2f, bottom=%.2f, label=%d, score=%.4f\n",
                            img_id, obj_id, box.left, box.top, box.right, box.bottom, label_id, score);
                }
                
                if ((box.right - box.left) < 1 || (box.bottom - box.top) < 1) {
                    continue;
                }
                if (score < score_thre) {
                    continue;
                }

                if (mask != nullptr) {
                    if (verbose) {
                        LOG_F(INFO, "img: %d, mask %d, height=%d, width=%d\n", img_id, obj_id, mask->height, mask->width);
                    }
                    cv::Mat imgMask(mask->height, mask->width, CV_8UC1, &mask->data[0]);
                    auto x0 = std::max(std::floor(box.left) - 1, 0.f);
                    auto y0 = std::max(std::floor(box.top) - 1, 0.f);
                    cv::Rect roi((int)x0, (int)y0, mask->width, mask->height);
                    // split the RGB channels, overlay mask to a specific color channel
                    cv::Mat ch[3];
                    split(imgs[img_id], ch);
                    int col = 0;  // int col = i % 3;
                    cv::bitwise_or(imgMask, ch[col](roi), ch[col](roi));
                    merge(ch, 3, imgs[img_id]);
                }
                cv::Scalar rct_color = rect_color != nullptr ? cv::Scalar(rect_color[0], rect_color[1], rect_color[2]) : cv::Scalar(0, 255, 0);
                if (is_text) {
                    int baseline = 0;
                    memset(text, 0, sizeof(text));
                    if (class_name != nullptr) {
                        snprintf(text, sizeof(text), "%s|%.2f ", class_name[label_id], score);
                    } else {
                        snprintf(text, sizeof(text), "%d|%.2f ", label_id, score);
                    }
                    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, text_scale, text_thickness, &baseline);
                    cv::Scalar txt_color = text_color != nullptr ? cv::Scalar(text_color[0], text_color[1], text_color[2]) : cv::Scalar(255, 255, 255);
                    cv::rectangle(imgs[img_id], cv::Rect(cv::Point(box.left, box.top), cv::Size(label_size.width, label_size.height + baseline)), rct_color, -1);
                    cv::putText(imgs[img_id], text, cv::Point(box.left + rect_thickness, box.top + label_size.height + rect_thickness), cv::FONT_HERSHEY_SIMPLEX, text_scale, txt_color, text_thickness);
                }

                cv::rectangle(imgs[img_id], cv::Point{(int)box.left, (int)box.top},
                            cv::Point{(int)box.right, (int)box.bottom}, rct_color, rect_thickness);
            }
            if (is_save) {
                if (save_path == nullptr) {
                    LOG_F(ERROR, "Image save path is null");
                    mmdeploy_detector_release_result(bboxes, res_count, num_imgs);
                    return -1;
                }
                cv::imwrite(save_path, imgs[img_id]);
                if (verbose) {
                    LOG_F(INFO, "Image is saved to %s", save_path);
                }
            }
            if (is_show) {
                memset(text, 0, sizeof(text));
                sprintf(text, "image_path: %s", save_path);
                cv::imshow(text, imgs[img_id]);
                cv::waitKey(0);
            }
        }
        mmdeploy_detector_release_result(bboxes, res_count, num_imgs);
        return 0;
    }

    int ObjectDetector::dect(cv::Mat & img, 
                              const float score_thre,
                              const bool is_save,
                              const bool is_show,
                              const bool is_text,
                              const char * save_path,
                              const int rect_thickness,
                              const int text_thickness,
                              const float text_scale,
                              const int * rect_color,
                              const int * text_color,
                              const char ** class_name,
                              const bool verbose) { 
        int ret;
        char text[512] = {0};
        mm_mat_t mat{img.data, img.rows, img.cols, 3, MM_BGR, MM_INT8};
        mm_detect_t * bboxes;
        int * res_count;
        ret = mmdeploy_detector_apply(detector, &mat, 1, &bboxes, &res_count);
        if (ret != MM_SUCCESS) {
            LOG_F(ERROR, "Failed to apply detector, code: %d", (int)ret);
            return -1;
        }
        if (verbose ){
            LOG_F(INFO, "bbox_count=%d\n", *res_count);
        }
        for (int obj_id = 0; obj_id< *res_count; obj_id++) {
            const auto &box = bboxes[obj_id].bbox;
            const auto &mask = bboxes[obj_id].mask;
            const auto &label_id = bboxes[obj_id].label_id;
            const auto &score = bboxes[obj_id].score;

            if (verbose) {
                LOG_F(INFO, "obj: %d, left=%.2f, top=%.2f, right=%.2f, bottom=%.2f, label=%d, score=%.4f\n",
                        obj_id, box.left, box.top, box.right, box.bottom, label_id, score);
            }
            
            if ((box.right - box.left) < 1 || (box.bottom - box.top) < 1) {
                continue;
            }
            if (score < score_thre) {
                continue;
            }

            if (mask != nullptr) {
                if (verbose) {
                    LOG_F(INFO, "mask %d, height=%d, width=%d\n", obj_id, mask->height, mask->width);
                }
                cv::Mat imgMask(mask->height, mask->width, CV_8UC1, &mask->data[0]);
                auto x0 = std::max(std::floor(box.left) - 1, 0.f);
                auto y0 = std::max(std::floor(box.top) - 1, 0.f);
                cv::Rect roi((int)x0, (int)y0, mask->width, mask->height);
                // split the RGB channels, overlay mask to a specific color channel
                cv::Mat ch[3];
                split(img, ch);
                int col = 0;  // int col = i % 3;
                cv::bitwise_or(imgMask, ch[col](roi), ch[col](roi));
                merge(ch, 3, img);
            }
            cv::Scalar rct_color = rect_color != nullptr ? cv::Scalar(rect_color[0], rect_color[1], rect_color[2]) : cv::Scalar(0, 255, 0);
            if (is_text) {
                int baseline = text_thickness;
                memset(text, 0, sizeof(text));
                if (class_name != nullptr) {
                    snprintf(text, sizeof(text), "%s|%.2f", class_name[label_id], score);
                } else {
                    snprintf(text, sizeof(text), "%d|%.2f", label_id, score);
                }
                cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, text_scale, text_thickness, &baseline);
                cv::Scalar txt_color = text_color != nullptr ? cv::Scalar(text_color[0], text_color[1], text_color[2]) : cv::Scalar(255, 255, 255);
                cv::rectangle(img, cv::Rect(cv::Point(box.left, box.top), cv::Size(label_size.width, label_size.height + baseline)), rct_color, -1);
                cv::putText(img, text, cv::Point(box.left + rect_thickness, box.top + label_size.height + rect_thickness), cv::FONT_HERSHEY_SIMPLEX, text_scale, txt_color, text_thickness);
            }

            cv::rectangle(img, cv::Point{(int)box.left, (int)box.top},
                        cv::Point{(int)box.right, (int)box.bottom}, rct_color, rect_thickness);

        }
        if (is_save) {
            if (save_path == nullptr) {
                LOG_F(ERROR, "Image save path is null");
                mmdeploy_detector_release_result(bboxes, res_count, 1);
                return -1;
            }
            cv::imwrite(save_path, img);
            if (verbose) {
                LOG_F(INFO, "Image is saved to %s", save_path);
            }
        }
        if (is_show) {
            memset(text, 0, sizeof(text));
            sprintf(text, "image_path: %s", save_path);
            cv::imshow(text, img);
            cv::waitKey(0);
        }
        mmdeploy_detector_release_result(bboxes, res_count, 1);
        return 0;
    }

    int ObjectDetector::run(void * args) {
        int ret, state;
        struct detector_run_context * context = (struct detector_run_context *) args; 
        const std::string video_path = context->video_path;
        const std::string upload_path = context->upload_path;
        // char text[1024] = {0};
        // info
        LOG_F(INFO, "Runner: video_path: %s | upload_path: %s", \
            video_path.c_str(), upload_path.c_str());
        // video
        cv::Mat frame;
        cv::VideoCapture capture;
        ret = capture.open(video_path);
        if (!ret) {
            LOG_F(ERROR, "Open %s failed!", video_path.c_str());
            capture.release();
            cv::destroyAllWindows();
            return -1;
        }
        const int width = capture.get(cv::CAP_PROP_FRAME_WIDTH);
        const int height = capture.get(cv::CAP_PROP_FRAME_HEIGHT);
        const int fps = capture.get(cv::CAP_PROP_FPS);
        // command
        char command[256] = {0};
        snprintf(command, sizeof(command), \
            "ffmpeg -y -an -f rawvideo -vcodec rawvideo -pix_fmt bgr24 -s %dx%d -r %d -i - -c:v libx264 -pix_fmt yuv420p -preset ultrafast -f flv %s", \
                width, height, fps, upload_path.c_str());
        FILE* fp = popen(command, "w");
        if (fp == nullptr) {
            LOG_F(ERROR, "Couldn't open process pipe with command: %s", command);
            state = -1;
            goto final;
        }

        LOG_F(INFO, "\nRead the video from %s: \nwidth: %d | height: %d | fps: %d.\nPush the video to: %s", 
            video_path.c_str(), width, height, fps, upload_path.c_str());
        LOG_F(INFO, "Push command: %s", command);
        for(;;) {
            ret = capture.read(frame);
            if (ret == 0) {
                break;
            }
            if (frame.empty()) {
                continue;
            }

            if (this->state < 1) {
                break;
            };

            ret = dect(frame, 0.3);
            if (ret == -1) {
                LOG_F(ERROR, "Dect image failed!");
                state = -1;
                break;
            }

            ret = fwrite(frame.data, sizeof(char), frame.total() * frame.elemSize(), fp);
            if (ret <= 0) {
                LOG_F(INFO, "Write pipe failed!");
                state = -1;
                break;
            }
            if (true) {
                cv::imshow("frame of video", frame);
                if (cv::waitKey(10) == ESC) break ;
            }
        }
    final:
        capture.release();
        cv::destroyAllWindows();
        pclose(fp);
        return state;
    }

    ObjectDetector* ObjectDetector::init_func(void * args) {
        Json::Value params = *(Json::Value *)args;
        std::string model_path = params["model"].asString();
        std::string device_name = params["device"].asString();
        const int device_id = params["device_id"].asInt();
        return new ObjectDetector(model_path, device_name, device_id);
    }

}