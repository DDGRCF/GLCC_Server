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
        LOG_F(INFO, "[ObjectDetector] Release ObjectDetector");
    }

    int ObjectDetector::dect(cv::Mat & img, std::vector<Object> & objects, float score_thre) {
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
        mmdeploy_detector_release_result(bboxes, res_count, 1);
        return 0;
    }

    cv::Scalar ObjectDetector::get_color() {
        return Scalar(rand() % 255, rand() % 255, rand() % 255);
    }

    int ObjectDetector::run(void * args, 
            std::function<void(void *)> cancel_func,
            std::function<void(void *)> deal_func) {
        int ret, state;
        detector_run_context_t * context = (detector_run_context_t *) args; 
        const std::string video_path = context->video_path;
        const std::string upload_path = context->upload_path;
        const Json::Value extra_config = context->vis_params;
        const float score_thre = extra_config["score_thre"].asFloat();
        const int into_contour_time_gap_second = extra_config["into_contour_time_gap_second"].asInt();
        const int out_contour_time_gap_second = extra_config["out_contour_time_gap_second"].asInt();
        const bool imshow_result_image = extra_config["imshow_result_image"].asBool();
        std::vector<std::string> class_names;
        for (int i = 0; i < (int)extra_config["class_names"].size(); i++) {
            class_names.emplace_back(extra_config["class_names"].asString());
        }
        // video
        cv::Mat frame;
        cv::VideoCapture capture;
        ret = capture.open(video_path);
        if (!ret) {
            LOG_F(ERROR, "[ObjectDetector][Runner] Open %s failed!", video_path.c_str());
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
            LOG_F(ERROR, "[ObjectDetector][Runner] Couldn't open process pipe with command: %s", command);
            state = -1;
            if (cancel_func != nullptr) {
                cancel_func(nullptr);
            }
            capture.release();
            cv::destroyAllWindows();
            pclose(fp);
            return -1;
        }

        LOG_F(INFO, "\n[ObjectDetector][Runner]\n"
            "Read the video from %s: \n"
            "width: %d | height: %d | fps: %d.\n"
            "Push the video to %s"
            "Extra config: %s",
            video_path.c_str(), 
            width, height, fps, 
            upload_path.c_str(),
            extra_config.toStyledString().c_str());

        // time to recorder
        int into_recoder_time_gap = into_contour_time_gap_second * 1000;
        int out_recoder_time_gap = out_contour_time_gap_second * 1000;
        std::unordered_map<std::string, bool> is_in_contour = {};
        std::unordered_map<std::string, std::chrono::system_clock::time_point> into_contour_time_point = {};
        std::unordered_map<std::string, std::chrono::system_clock::time_point> out_contour_time_point = {};

        std::stringstream video_save_path;
        cv::VideoWriter video_writer;
        int video_type = (int)capture.get(CAP_PROP_FOURCC);

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

            std::vector<Object> objects;
            ret = dect(frame, objects, score_thre);
            if (ret == -1) {
                LOG_F(ERROR, "[ObjectDetector][Runner] Dect image failed!");
                state = -1;
                break;
            }

            for (auto & object : objects) {
                Scalar color = get_color();
                auto tl = object.rect.tl(); auto br = object.rect.br();
                auto ctr = (tl + br) / 2;
                cv::putText(frame, cv::format("%s: %.3f", class_names[object.label].c_str(), object.prob), cv::Point(tl.x, tl.y - 5),
                    0, 0.6, cv::Scalar(0, 0, 255), 2, LINE_AA);
                cv::rectangle(frame, object.rect, color, 2);
                cv::circle(frame, cv::Point(ctr.x, ctr.y), 10, color, -1);
            }

            if (is_put_lattice) {
                auto time_now = std::chrono::system_clock::now();
                for (auto & item: contour_list) {
                    auto & name = item.first;
                    auto & contour = item.second;
                    int ret = -1;
                    for (auto & object : objects) {
                        auto tl = object.rect.tl(); auto br = object.rect.br();
                        auto ctr = (tl + br) / 2;
                        ret = cv::pointPolygonTest(contour, ctr, false);
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
                                    video_save_path.clear();
                                    video_save_path.str("");
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
                        cv::addWeighted(frame, 0.9, tmp, 0.1, 0, frame);
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
                            LOG_F(WARNING, "[ObjectDetector][Runner] Save cover path fail!");
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
                LOG_F(ERROR, "[ObjectDetector][Runner] Write push pipe failed");
                state = -1;
                break;
            }
            if (imshow_result_image) {
                cv::imshow(video_path, frame);
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
        LOG_F(INFO, "[TrackerDetector][Runner] Release TrackerDetector");
    }

    int TrackerDetector::dect(cv::Mat & img, std::vector<Object> & objects, float score_thre) {
        int ret;
        mm_mat_t mat{img.data, img.rows, img.cols, 3, MM_BGR, MM_INT8};
        mm_detect_t * bboxes;
        int * res_count;
        ret = mmdeploy_detector_apply(detector, &mat, 1, & bboxes, &res_count);
        if (ret != MM_SUCCESS) {
            LOG_F(ERROR, "[TrackerDetector][DECT] Apply detector failed! Code: %d", (int)ret);
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
        mmdeploy_detector_release_result(bboxes, res_count, 1);
        return 0;
    }

    int TrackerDetector::run(void * args, 
            std::function<void (void *)> cancel_func,
            std::function<void (void *)> deal_func) {
        int ret, state;
        detector_run_context_t * context = (detector_run_context_t *) args; 
        const std::string video_path = context->video_path;
        const std::string upload_path = context->upload_path;
        const Json::Value extra_config = context->vis_params;
        const float score_thre = extra_config["score_thre"].asFloat();
        const int tracker_buffer = extra_config["tracker_buffer"].asInt();
        const int into_contour_time_gap_second = extra_config["into_contour_time_gap_second"].asInt();
        const int out_contour_time_gap_second = extra_config["out_contour_time_gap_second"].asInt();
        const bool imshow_result_image = extra_config["imshow_result_image"].asBool();
        const float wh_ratio_thre_to_show = extra_config["wh_ratio_thre_to_show"].asFloat();
        const float wh_multiply_thre_to_show = extra_config["wh_multiply_thre_to_show"].asFloat();
        std::vector<std::string> class_names;
        for (int i = 0; i < (int)extra_config["class_names"].size(); i++) {
            class_names.emplace_back(extra_config["class_names"][i].asString());
        }

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

        // command
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

        LOG_F(INFO, "\n[TrackerDetector][Runner]\n"
            "Read the video from %s: \n"
            "width: %d | height: %d | fps: %d.\n"
            "Push the video to %s\n"
            "Extra config: %s",
            video_path.c_str(), 
            width, height, fps, 
            upload_path.c_str(),
            extra_config.toStyledString().c_str());

        // byteTracker
        BYTETracker tracker(fps, tracker_buffer);
        int num_frames = 0;

        // time to recorder
        int into_recoder_time_gap = into_contour_time_gap_second * 1000;
        int out_recoder_time_gap = out_contour_time_gap_second * 1000;
        std::unordered_map<std::string, bool> is_in_contour = {};
        std::unordered_map<std::string, std::chrono::system_clock::time_point> into_contour_time_point = {};
        std::unordered_map<std::string, std::chrono::system_clock::time_point> out_contour_time_point = {};

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
            ret = dect(frame, objects, score_thre);
            if (ret == -1) {
                LOG_F(ERROR, "[TrackerDetector][Runner] Dect image failed!");
                state = -1;
                break;
            }

            std::vector<STrack> stracks = tracker.update(objects);
            std::vector<STrack> stracks_show;

            for (auto & strack : stracks) {
                auto & tlwh = strack.tlwh;
                auto xyah = strack.to_xyah();
                bool wh_ratio = tlwh[2] / tlwh[3] > wh_ratio_thre_to_show;
                if (tlwh[2] * tlwh[3] > wh_multiply_thre_to_show && !wh_ratio) {
                    stracks_show.emplace_back(strack);
                    Scalar color = tracker.get_color(strack.track_id);
                    cv::putText(frame, cv::format("id:%d: %.3f", strack.track_id, strack.score), cv::Point(tlwh[0], tlwh[1] - 5),
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
                    int ret = -1;
                    for (auto & strack : stracks_show) {
                        auto xyah = strack.to_xyah();
                        cv::Point ctr(xyah[0], xyah[1]);
                        ret = cv::pointPolygonTest(contour, ctr, false);
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
                                    video_save_path.clear();
                                    video_save_path.str("");
                                    video_save_path << resource_dir << "/" 
                                       << std::put_time(localtime(&now), constants::file_time_format.c_str())
                                       << ".mp4";
                                    video_writer.open(video_save_path.str(), video_type, fps, frame.size());
                                    if (deal_func != nullptr && video_writer.isOpened()) {
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
                        cv::addWeighted(frame, 0.9, tmp, 0.1, 0, frame);
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
                LOG_F(ERROR, "[TrackerDetector][Runner] Write push pipe failed");
                state = -1;
                break;
            }
            if (imshow_result_image) {
                cv::imshow(video_path, frame);
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