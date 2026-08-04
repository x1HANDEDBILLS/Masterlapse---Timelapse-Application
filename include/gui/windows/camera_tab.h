#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QTimer>
#include <QJsonObject>

class Buffer;
class Camera;
class VideoEncoder;
class Save;

class CameraTab : public QWidget {
    Q_OBJECT
public:
    CameraTab(int tabIndex, Buffer* buf, Camera* cam, VideoEncoder* enc, Save* sav, QWidget *parent = nullptr);
    ~CameraTab();
    bool isRecording() const { return is_recording_; }
    QString getSelectedCamera() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void updateFrame();
    void openImageFolderDialog();
    void openVideoFolderDialog();
    void saveProfile();
    void loadProfile();
    void syncBackendResolution();
    void updateCalculator();

private:
    void setupUi();
    void populateCameras();
    void refreshProfileList();
    void applyJsonToUi(const QJsonObject& json);
    void updateTimestampPosition(QPointF pos);

    int tab_index_;
    Buffer* preview_buffer_;
    Camera* camera_;
    VideoEncoder* encoder_;
    Save* save_;

    bool is_recording_ = false;
    bool is_paused_ = false;
    bool dragging_ts_ = false;
    QString last_selected_camera_ = "--- Select a Camera ---";
    QTimer* timer_;

    QLabel* video_label_;
    QWidget* fs_window_ = nullptr;
    QLabel* fs_label_ = nullptr;
    
    QComboBox* cmb_camera;
    QComboBox* cmb_resolution;
    QComboBox* cmb_fps;
    QComboBox* cmb_quality;
    QComboBox* cmb_dark;
    QComboBox* cmb_interval_sec;
    QComboBox* cmb_interval_min;
    QComboBox* cmb_dur_hr;
    QComboBox* cmb_dur_min;
    QComboBox* cmb_profiles;
    QLineEdit* txt_total_frames;
    QLineEdit* txt_est_length;
    QLineEdit* txt_est_size;
    QLineEdit* txt_profile_name;
    QCheckBox* chk_keep_images;
    QCheckBox* chk_keep_videos;
    QCheckBox* chk_crash_recovery;
    QCheckBox* chk_timestamp;
    QCheckBox* chk_gopro_live;
    QLabel* lblImgPath;
    QLabel* lblVidPath;
    QLabel* status_label_;
    QLabel* dark_status_label_;
    QPushButton* btn_start_;
    QPushButton* btn_pause_;
    QPushButton* btn_stop_;
    QPushButton* btn_merge_;
    QPushButton* btn_props_;
    QPushButton* btn_set_img;
    QPushButton* btn_set_vid;
    QPushButton* btn_open_img;
    QPushButton* btn_open_vid;
    QPushButton* btn_view_live;
    QPushButton* btn_fullscreen_;
    QPushButton* btn_save_profile;
    QPushButton* btn_load_profile;
};

