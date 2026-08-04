#include <QApplication>
#include "gui/windows/main_window.h"
#include "buffer/buffer.h"
#include "camera/camera.h"
#include "process/process.h"
#include "video-encoder/video-encoder.h"
#include "save/save.h"
#include "settings/settings.h"
#include <iostream>
#include <atomic>
#include <vector>
#include <memory>
#include <string>

std::atomic<bool> keep_running{true};

int main(int argc, char *argv[]) {
    _putenv("OPENCV_VIDEOIO_PRIORITY_LIST=DSHOW,MSMF"); // Block GStreamer Spam
    std::cout << "[MasterLapse] Background Engine Live. Launching Qt6 Interface." << std::endl;
    QApplication app(argc, argv);

    Settings settings("config.txt");

    const int NUM_CAMERAS = 5;

    std::vector<std::unique_ptr<Buffer>> raw_buffers;
    std::vector<std::unique_ptr<Buffer>> process_buffers;
    std::vector<std::unique_ptr<Buffer>> encode_buffers;
    std::vector<std::unique_ptr<Buffer>> preview_buffers;

    std::vector<std::unique_ptr<Camera>> cameras;
    std::vector<std::unique_ptr<Process>> processes;
    std::vector<std::unique_ptr<Save>> saves;
    std::vector<std::unique_ptr<VideoEncoder>> encoders;

    std::vector<Buffer*> preview_ptrs;
    std::vector<Camera*> cam_ptrs;
    std::vector<VideoEncoder*> enc_ptrs;
    std::vector<Save*> save_ptrs;

    for (int i = 0; i < NUM_CAMERAS; ++i) {
        raw_buffers.push_back(std::make_unique<Buffer>());
        process_buffers.push_back(std::make_unique<Buffer>());
        encode_buffers.push_back(std::make_unique<Buffer>());
        preview_buffers.push_back(std::make_unique<Buffer>());

        // Isolate file outputs to prevent thread collision
        std::string camImageFolder = settings.config.imageFolder + "_Cam" + std::to_string(i+1);
        std::string camVideoOutput = "MasterLapse_Output_" + std::to_string(i+1) + ".avi";

        cameras.push_back(std::make_unique<Camera>(*raw_buffers[i], *preview_buffers[i], -1, settings.config.intervalMs));
        processes.push_back(std::make_unique<Process>(*raw_buffers[i], *process_buffers[i], settings.config.darkThreshold));
        saves.push_back(std::make_unique<Save>(*process_buffers[i], *encode_buffers[i], camImageFolder));
        encoders.push_back(std::make_unique<VideoEncoder>(*encode_buffers[i], camVideoOutput, settings.config.fps, cv::Size(settings.config.resWidth, settings.config.resHeight)));

        cameras[i]->start();
        processes[i]->start();
        saves[i]->start();
        encoders[i]->start();

        preview_ptrs.push_back(preview_buffers[i].get());
        cam_ptrs.push_back(cameras[i].get());
        enc_ptrs.push_back(encoders[i].get());
        save_ptrs.push_back(saves[i].get());
    }

    MainWindow w(preview_ptrs, cam_ptrs, enc_ptrs, save_ptrs);
    w.show();

    int ret = app.exec();

    keep_running = false;
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        cameras[i]->stop();
        processes[i]->stop();
        saves[i]->stop();
        encoders[i]->stop();
    }

    std::cout << "[MasterLapse] Safe shutdown complete." << std::endl;
    return ret;
}





