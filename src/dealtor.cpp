#include "dealtor.h"

namespace GLCC{
    ObjectDetector::ObjectDetector(const char * model_path, 
                                   const char * device_name, 
                                   const int device_id) {
        int ret = mmdeploy_detector_create_by_path(model_path, device_name, device_id, &detector);
        if (ret != MM_SUCCESS) {
            LOG_F(ERROR, "Create detector failed! Code: %d", (int)ret);
            state = -1;
            return;
        } 
    }

    ObjectDetector::ObjectDetector(const std::string & model_path, 
                                   const std::string & device_name, 
                                   const int device_id) {
        int ret = mmdeploy_detector_create_by_path(model_path.c_str(), device_name.c_str(), device_id, &detector);
        if (ret != MM_SUCCESS) {
            LOG_F(ERROR, "Create detector failed! Code: %d", (int)ret);
            state = -1;
            return;
        } 
    }

    ObjectDetector::~ObjectDetector() {
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
                    std::snprintf(text, sizeof(text), "%s|%.2f", class_name[label_id], score);
                } else {
                    std::snprintf(text, sizeof(text), "%d|%.2f", label_id, score);
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

    int ObjectDetector::put_lattice(cv::Mat & image) {
        for (auto it = contour_list.begin(); it != contour_list.end(); it++) {
            cv::Scalar color = {255, 255, 0};
            cv::polylines(image, it->second, true, color, 3);
        }
        return 0;
    }

    int ObjectDetector::run(void * args, 
        std::function<void(void *)> cancel_func,
        std::function<void(void *)> deal_func) {
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
            if (cancel_func != nullptr) {
                cancel_func(nullptr);
            }
            capture.release();
            cv::destroyAllWindows();
            return -1;
        }
        const int width = capture.get(cv::CAP_PROP_FRAME_WIDTH);
        const int height = capture.get(cv::CAP_PROP_FRAME_HEIGHT);
        const int fps = capture.get(cv::CAP_PROP_FPS);
        // command
        char command[512] = {0};
        std::snprintf(command, sizeof(command), \
            constants::ffmpeg_push_command.c_str(), \
                width, height, fps, upload_path.c_str());
        FILE* fp = popen(command, "w");

        if (fp == nullptr) {
            LOG_F(ERROR, "Couldn't open process pipe with command: %s", command);
            state = -1;
            if (cancel_func != nullptr) {
                cancel_func(nullptr);
            }
            capture.release();
            cv::destroyAllWindows();
            pclose(fp);
            return -1;
        }

        LOG_F(INFO, "\nRead the video from %s: \nwidth: %d | height: %d | fps: %d.\nPush the video to: %s", 
            video_path.c_str(), width, height, fps, upload_path.c_str());
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

            if (is_put_lattice) {
                for (auto it = contour_list.begin(); it != contour_list.end(); it++) {
                    cv::Scalar color = {255, 255, 0};
                    cv::polylines(frame, it->second, true, color, 3);
                }
            }

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
        if (cancel_func != nullptr) {
            cancel_func(nullptr);
        }
        capture.release();
        cv::destroyAllWindows();
        pclose(fp);
        return state;
    }

    Detector * ObjectDetector::init_func(void * args) {
        Json::Value params = *(Json::Value *)args;
        std::string model_path = params["model"].asString();
        std::string device_name = params["device"].asString();
        const int device_id = params["device_id"].asInt();
        std::string resource_dir = params["resource_dir"].asString();
        return new ObjectDetector(model_path, device_name, device_id);
    }


    TrackerDetector::TrackerDetector(const char * model_path, 
                                     const char * device_name,
                                     const int device_id): ObjectDetector(model_path, device_name, device_id) {
    }

    TrackerDetector::TrackerDetector(const std::string & model_path, 
                                     const std::string & device_name,
                                     const int device_id): ObjectDetector(model_path, device_name, device_id) {
    }

    TrackerDetector::~TrackerDetector() {

    }

    int TrackerDetector::dect(cv::Mat & img, std::vector<Object> & objects, float score_thre) {
        int ret;
        mm_mat_t mat{img.data, img.rows, img.cols, 3, MM_BGR, MM_INT8};
        mm_detect_t * bboxes;
        int * res_count;
        ret = mmdeploy_detector_apply(detector, &mat, 1, & bboxes, &res_count);
        if (ret != MM_SUCCESS) {
            LOG_F(ERROR, "Apply detector failed! Code: %d", (int)ret);
            return -1;
        }
        for (int obj_id = 0; obj_id < *res_count; obj_id++) {
            const auto & score = bboxes[obj_id].score;
            if (score < score_thre) {
                continue;

            }
            const auto & box = bboxes[obj_id].bbox;
            if ((box.right - box.left) < 1 || (box.bottom - box.top) < 1) {
                continue;
            }
            const auto & label_id = bboxes[obj_id].label_id;
            objects.emplace_back(
                (cv::Rect_<float>){
                    box.left, box.top,
                    box.right  - box.left,
                    box.bottom - box.top,
                },
                label_id,
                score
            );
        }
        return 0;
    }

    int TrackerDetector::run(void * args, 
            std::function<void (void *)> cancel_func,
            std::function<void (void *)> deal_func) {
        int ret, state;
        detector_run_context_t * context = (detector_run_context_t *) args; 
        const std::string video_path = context->video_path;
        const std::string upload_path = context->upload_path;
        // video
        cv::Mat frame;
        cv::VideoCapture capture;
        ret = capture.open(video_path);
        if (!ret) {
            LOG_F(ERROR, "[TrackerDetector][Runner] Open %s failed!", video_path.c_str());
            if (cancel_func != nullptr) {
                cancel_func(nullptr);
            }
            capture.release();
            cv::destroyAllWindows();
            return -1;
        }

        const int width = capture.get(cv::CAP_PROP_FRAME_WIDTH);
        const int height = capture.get(cv::CAP_PROP_FRAME_HEIGHT);
        const int fps = capture.get(cv::CAP_PROP_FPS);

        // command NOTE: 
        char command[512] = {0};
        std::snprintf(command, sizeof(command), \
            constants::ffmpeg_push_command.c_str(), \
                width, height, fps, upload_path.c_str());
        FILE* fp = popen(command, "w");

        if (fp == nullptr) {
            LOG_F(ERROR, "[TrackerDetector][Runner] Couldn't open process pipe with command: %s", command);
            state = -1;
            if (cancel_func != nullptr) {
                cancel_func(nullptr);
            }
            capture.release();
            cv::destroyAllWindows();
            pclose(fp);
            return -1;
        }

        LOG_F(INFO, "\n[TrackerDetector][Runner] Read the video from %s: \n"
            "--------------------- width: %d | height: %d | fps: %d.\n"
            "--------------------- Push the video to %s",
            video_path.c_str(), 
            width, height, fps, 
            upload_path.c_str());

        // byteTracker
        BYTETracker tracker(fps, 30);
        int num_frames = 0;
        int into_recoder_time_gap = 5 * constants::num_millisecond_per_second;
        int out_recoder_time_gap = 20 * constants::num_millisecond_per_second;
        std::unordered_map<std::string, bool> is_in_contour = {};
        std::unordered_map<std::string, std::chrono::system_clock::time_point> into_contour_time_point = {};
        std::unordered_map<std::string, std::chrono::system_clock::time_point> out_contour_time_point = {};
        // FILE* fpt = nullptr;

        std::stringstream video_save_path;
        cv::VideoWriter video_writer;
        int video_type = (int)capture.get(CAP_PROP_FOURCC);

        for(;;) {
            ret = capture.read(frame);
            num_frames++;

            if (ret == 0) {
                break;
            }
            if (frame.empty()) {
                continue;
            }

            if (this->state < 1) {
                break;
            };

            std::vector<Object> objects;
            ret = dect(frame, objects, 0.4);
            if (ret == -1) {
                LOG_F(ERROR, "Dect image failed!");
                state = -1;
                break;
            }

            std::vector<STrack> stracks = tracker.update(objects);

            for (auto & strack : stracks) {
                auto & tlwh = strack.tlwh;
                auto xyah = strack.to_xyah();
                bool wh_ratio = tlwh[2] / tlwh[3] > 1.6;
                if (tlwh[2] * tlwh[3] > 20 && !wh_ratio) {
                    Scalar color = tracker.get_color(strack.track_id);
                    cv::putText(frame, cv::format("cat: %d", strack.track_id), cv::Point(tlwh[0], tlwh[1] - 5),
                        0, 0.6, cv::Scalar(0, 0, 255), 2, LINE_AA);
                    cv::rectangle(frame, cv::Rect(tlwh[0], tlwh[1], tlwh[2], tlwh[3]), color, 2);
                    cv::circle(frame, cv::Point(xyah[0], xyah[1]), 10, color, -1);
                }
            }

            if (is_put_lattice) {
                auto time_now = std::chrono::system_clock::now();
                for (auto & item: contour_list) {
                    auto & name = item.first;
                    auto & contour = item.second;
                    int ret = 0;
                    for (auto & strack : stracks) {
                        auto xyah = strack.to_xyah();
                        cv::Point center_point(xyah[0], xyah[1]);
                        ret = cv::pointPolygonTest(contour, center_point, false);
                        if (ret >= 0) {
                            break;
                        }
                    }

                    if (ret >= 0) {
                        if (is_in_contour.size() == 0) {
                            if (into_contour_time_point.find(name) == into_contour_time_point.end()) {
                                into_contour_time_point[name] = time_now;
                            }
                        }
                        out_contour_time_point.erase(name);
                    } else {
                        if (out_contour_time_point.find(name) == out_contour_time_point.end()) {
                            out_contour_time_point[name] = time_now;
                        }
                    }
                }

                std::vector<std::string> into_erase_key = {};
                for (auto & item : into_contour_time_point) {
                    auto & name = item.first;
                    auto & time_point = item.second;
                    if (contour_list.find(name) != contour_list.end()) {
                        auto time_gap = std::chrono::duration_cast<std::chrono::milliseconds>(time_now - time_point);
                        if (time_gap.count() > into_recoder_time_gap) {
                            if (!video_writer.isOpened()) {
                                if (resource_dir != "") {
                                    time_t now = std::chrono::system_clock::to_time_t(time_now);
                                    video_save_path << resource_dir << "/" 
                                       << std::put_time(localtime(&now), constants::file_time_format.c_str())
                                       << ".mp4";
                                    video_writer.open(video_save_path.str(), video_type, fps, frame.size());
                                    if (deal_func != nullptr) {
                                        deal_func(&video_save_path);
                                    }
                                }
                            }
                            is_in_contour[name] = true;
                            into_erase_key.emplace_back(name);
                        }
                    } else {
                        out_contour_time_point.erase(name);
                        is_in_contour.erase(name);
                    }
                }

                for (auto & name : into_erase_key) {
                    into_contour_time_point.erase(name);
                }

                std::vector<std::string> out_erase_key = {};
                for (auto & item : out_contour_time_point) {
                    auto & name = item.first;
                    auto & time_point = item.second;
                    auto time_gap = std::chrono::duration_cast<std::chrono::microseconds>(time_now - time_point);
                    if (time_gap.count() > out_recoder_time_gap) {
                        is_in_contour.erase(name);
                        into_contour_time_point.erase(name);
                        out_erase_key.emplace_back(name);
                    }
                }

                for (auto & name : out_erase_key) {
                    out_contour_time_point.erase(name);
                }

                for (auto & item : contour_list) {
                    cv::Scalar color = {0, 0, 255};
                    auto & name = item.first;
                    auto & contour = item.second;
                    if (is_in_contour.find(name) != is_in_contour.end()) {
                        cv::Mat tmp{frame.rows, frame.cols, CV_8UC3, cv::Scalar(0)};
                        cv::fillPoly(tmp, contour, color, 8);
                        cv::addWeighted(frame, 0.8, tmp, 0.2, 0, frame);
                    } else {
                        cv::polylines(frame, contour, true, color, 3);
                    }
                }

                if (video_writer.isOpened()) {
                    video_writer.write(frame);
                }

                if (is_in_contour.size() == 0) {
                    if (video_writer.isOpened()) {
                        video_writer.release();
                        std::unordered_map<std::string, std::string> path_parse_results = {};
                        int ret = parse_path(video_save_path.str(), path_parse_results);
                        if (ret == -1) {
                            LOG_F(WARNING, "[TrackerDetector][Runner] Save cover path fail!");
                        } else {
                            auto & dirname = path_parse_results["dirname"];
                            auto & stem = path_parse_results["stem"];
                            std::string cover_save_path = dirname + "/" + stem + "." + constants::cover_save_suffix;
                            std::string command = "ffmpeg -y -i " + video_save_path.str() + " -ss 1 -frames:v 1 " + cover_save_path;
                            system(command.c_str());
                        }
                    }
                }
            }

            ret = fwrite(frame.data, sizeof(char), frame.total() * frame.elemSize(), fp);
            if (ret <= 0) {
                LOG_F(ERROR, "Write push pipe failed");
                state = -1;
                break;
            }
            if (true) {
                cv::imshow("frame of video", frame);
                if (cv::waitKey(10) == ESC) break ;
            }
        }

        if (cancel_func != nullptr) {
            cancel_func(nullptr);
        }
        capture.release();
        cv::destroyAllWindows();

        if (video_writer.isOpened()) {
            video_writer.release();
            std::unordered_map<std::string, std::string> path_parse_results = {};
            int ret = parse_path(video_save_path.str(), path_parse_results);
            if (ret == -1) {
                LOG_F(WARNING, "[TrackerDetector][Runner] Save cover path fail!");
            } else {
                auto & dirname = path_parse_results["dirname"];
                auto & stem = path_parse_results["stem"];
                std::string cover_save_path = dirname + "/" + stem + "." + constants::cover_save_suffix;
                std::string command = "ffmpeg -y -i " + video_save_path.str() + " -ss 1 -frames:v 1 " + cover_save_path;
                system(command.c_str());
            }
        }
        pclose(fp);
        return state;
    }

    Detector * TrackerDetector::init_func(void * args) {
        Json::Value params = *(Json::Value *)args;
        std::string model_path = params["model"].asString();
        std::string device_name = params["device"].asString();
        const int device_id = params["device_id"].asInt();
        return new TrackerDetector(model_path, device_name, device_id);
    } 

}