#pragma once
#include <QMainWindow>
#include <vector>
#include "buffer/buffer.h"
#include "camera/camera.h"
#include "process/process.h"
#include "video-encoder/video-encoder.h"
#include "save/save.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::vector<Buffer*>& previewBuffers, std::vector<Camera*>& cameras, std::vector<VideoEncoder*>& encoders, std::vector<Save*>& saves, QWidget *parent = nullptr);
    ~MainWindow();
protected:
    void closeEvent(QCloseEvent *event) override;
};
