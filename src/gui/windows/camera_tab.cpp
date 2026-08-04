#include "gui/windows/camera_tab.h"
#include "buffer/buffer.h"
#include "camera/camera.h"
#include "process/process.h"
#include "video-encoder/video-encoder.h"
#include "save/save.h"
#include "process/timestamp_config.h"
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QPixmap>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QMessageBox>
#include <QProcess> 
#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QNetworkInterface>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <thread>

QString discoverGoProIp() {
    const QHostAddress &localhost = QHostAddress(QHostAddress::LocalHost);
    for (const QHostAddress &address: QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && address != localhost) {
            QString ip = address.toString();
            if (ip.startsWith("172.2")) {
                QStringList parts = ip.split(".");
                if (parts.size() == 4) return parts[0] + "." + parts[1] + "." + parts[2] + ".51";
            }
        }
    }
    return "172.24.104.51"; 
}

QString CameraTab::getSelectedCamera() const {
    return cmb_camera ? cmb_camera->currentText() : "";
}

CameraTab::CameraTab(int tabIndex, Buffer* buf, Camera* cam, VideoEncoder* enc, Save* sav, QWidget *parent)
    : QWidget(parent), tab_index_(tabIndex), preview_buffer_(buf), camera_(cam), encoder_(enc), save_(sav) {
    
    QDir().mkdir("profiles");
    setupUi();

    if (camera_) {
        populateCameras();
        QMediaDevices* hw_listener = new QMediaDevices(this);
        connect(hw_listener, &QMediaDevices::videoInputsChanged, this, &CameraTab::populateCameras);
        
        QSettings settings("MasterLapse", "Settings");
        QString savedCam = settings.value(QString("Tab_%1_Camera").arg(tab_index_), "--- Select a Camera ---").toString();
        
        cmb_camera->blockSignals(true);
        int camIndex = cmb_camera->findText(savedCam);
        if (camIndex > 0) {
            cmb_camera->setCurrentIndex(camIndex);
            last_selected_camera_ = savedCam;
            bool isVirtual = savedCam.contains("Virtual Camera");
            bool isGoPro = savedCam.contains("GoPro Hero (Network API)");
            if (isVirtual || isGoPro) {
                cmb_resolution->blockSignals(true);
                if (isVirtual) cmb_resolution->setCurrentText("1920 x 1080");
                chk_gopro_live->setEnabled(isGoPro);
                if (!isGoPro) { chk_gopro_live->setChecked(false); if(camera_) camera_->setGoProLiveView(false); }
                chk_gopro_live->setEnabled(isGoPro);
                if (!isGoPro) { chk_gopro_live->setChecked(false); if(camera_) camera_->setGoProLiveView(false); }
                if (isGoPro) cmb_resolution->setCurrentText("5568 x 4872");
                cmb_resolution->blockSignals(false);
            }
            if(camera_) camera_->setCameraIndex(camIndex - 1, isVirtual, isGoPro, discoverGoProIp().toStdString());
        } else {
            cmb_camera->setCurrentText("--- Select a Camera ---");
            last_selected_camera_ = "--- Select a Camera ---";
            if(camera_) camera_->setCameraIndex(-1, false, false, "");
        }
        cmb_camera->blockSignals(false);

        refreshProfileList();

        QString lastProfile = settings.value(QString("LastProfile_%1").arg(tab_index_), "").toString();
        if (!lastProfile.isEmpty()) {
            int index = cmb_profiles->findText(lastProfile);
            if (index != -1) { cmb_profiles->setCurrentIndex(index); loadProfile(); }
        }

        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &CameraTab::updateFrame);
        timer_->start(16); 
        syncBackendResolution(); 
        
        QTimer::singleShot(2500, this, [this]() {
            QSettings app_settings("MasterLapse", "Settings");
            QString recKey = QString("recording_in_progress_%1").arg(tab_index_);
            bool was_recording = app_settings.value(recKey, false).toBool();
            QString aviFile = QString("MasterLapse_Output_%1.avi").arg(tab_index_);
            
            if (was_recording && chk_crash_recovery->isChecked()) {
                if (QFile::exists(aviFile)) {
                    QString destFolder = lblVidPath->text(); 
                    QDir dir(destFolder); if (!dir.exists()) dir.mkpath(".");
                    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
                    QString destFile = dir.filePath(QString("Fragment_Cam%1_").arg(tab_index_) + timestamp + "_RECOVERED.avi");
                    QFile::rename(aviFile, destFile);
                }
                btn_start_->click();
            } else {
                app_settings.setValue(recKey, false);
                QFile::remove(QString("recording_%1.lock").arg(tab_index_)); 
            }
        });
    } else {
        video_label_->setText("CAMERA " + QString::number(tabIndex) + " OFFLINE\nHardware Backend Not Assigned");
        video_label_->setStyleSheet("background-color: #111; color: #555; border: 1px solid #333; font-size: 24px; font-weight: bold;");
        btn_start_->setEnabled(false);
        btn_start_->setText("Disabled");
    }
}

CameraTab::~CameraTab() {}

void CameraTab::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    video_label_ = new QLabel("LIVE HARDWARE FEED");
    video_label_->setAlignment(Qt::AlignCenter);
    video_label_->setStyleSheet("background-color: black; border: 1px solid #555;");
    video_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    video_label_->setMinimumSize(400, 225);
    video_label_->installEventFilter(this);
    mainLayout->addWidget(video_label_, 1); 
    mainLayout->addSpacing(15);

    QWidget* bottomContainer = new QWidget();
    bottomContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QHBoxLayout* bottomLayout = new QHBoxLayout(bottomContainer);
    
    QGroupBox* gbCapture = new QGroupBox("Capture Settings");
    QVBoxLayout* capLayout = new QVBoxLayout(gbCapture);
    
    QGridLayout* intervalGrid = new QGridLayout();
    intervalGrid->addWidget(new QLabel("Intervals in Seconds"), 0, 0, Qt::AlignCenter);
    intervalGrid->addWidget(new QLabel("Intervals in Minutes"), 0, 1, Qt::AlignCenter);
    
    cmb_interval_sec = new QComboBox();
    cmb_interval_sec->addItem("0.1"); cmb_interval_sec->addItem("0.5"); cmb_interval_sec->addItem("0.8"); cmb_interval_sec->addItem("0.9");
    for(int i=1; i<=60; i++) cmb_interval_sec->addItem(QString::number(i));
    
    cmb_interval_min = new QComboBox();
    for(int i=0; i<=999; i++) cmb_interval_min->addItem(QString::number(i));
    cmb_interval_sec->setCurrentText("1");
    
    intervalGrid->addWidget(cmb_interval_sec, 1, 0);
    intervalGrid->addWidget(cmb_interval_min, 1, 1);
    capLayout->addLayout(intervalGrid);
    
    btn_start_ = new QPushButton("Start Capture");
    btn_pause_ = new QPushButton("Pause Capture");
    btn_stop_ = new QPushButton("Stop Capture");
    btn_merge_ = new QPushButton("Merge Selected Videos");
    
    btn_start_->setStyleSheet("background-color: #2E7D32; color: white; font-weight: bold;");
    btn_stop_->setEnabled(false);
    btn_pause_->setEnabled(false);

    capLayout->addWidget(btn_start_); capLayout->addWidget(btn_pause_); 
    capLayout->addWidget(btn_stop_); capLayout->addWidget(btn_merge_);
    
    chk_keep_images = new QCheckBox("Don't remove Old Images"); chk_keep_videos = new QCheckBox("Don't remove Old Raw Fragments");
    chk_keep_videos->setChecked(false);
    capLayout->addWidget(chk_keep_images); capLayout->addWidget(chk_keep_videos);
    capLayout->addStretch();
    bottomLayout->addWidget(gbCapture);

    QGroupBox* gbCamera = new QGroupBox("Camera");
    QVBoxLayout* camLayout = new QVBoxLayout(gbCamera);
    QFormLayout* camForm = new QFormLayout();
    
    cmb_camera = new QComboBox(); camForm->addRow("Select Camera:", cmb_camera);
    btn_props_ = new QPushButton("Camera Properties"); camForm->addRow("", btn_props_);
    
    cmb_resolution = new QComboBox(); 
    cmb_resolution->addItem("1280 x 720"); cmb_resolution->addItem("1920 x 1080");
    cmb_resolution->addItem("2560 x 1440"); cmb_resolution->addItem("3840 x 2160"); cmb_resolution->addItem("5568 x 4872");
    cmb_resolution->setCurrentText("2560 x 1440");
    camForm->addRow("Resolution:", cmb_resolution);
    
    cmb_fps = new QComboBox(); for(int i : {10,15,24,30,60,120}) cmb_fps->addItem(QString::number(i)); cmb_fps->setCurrentText("30"); camForm->addRow("Framerate:", cmb_fps);
    
    cmb_quality = new QComboBox();
    cmb_quality->addItem("0 (Lossless)"); cmb_quality->addItem("14 (Ultra High)");
    cmb_quality->addItem("18 (High)"); cmb_quality->addItem("23 (Normal)");
    cmb_quality->setCurrentText("18 (High)");
    camForm->addRow("Video Quality (CRF):", cmb_quality);

    cmb_dark = new QComboBox(); for(int i=0; i<=255; i++) cmb_dark->addItem(QString::number(i)); cmb_dark->setCurrentText("20"); camForm->addRow("Dark Cutoff Limit:", cmb_dark);
    chk_gopro_live = new QCheckBox("API Live Framing (DISABLED - Use Webcam Mode)"); chk_gopro_live->setChecked(false); chk_gopro_live->setEnabled(false); camForm->addRow("", chk_gopro_live);
    camLayout->addLayout(camForm);
    
    QFrame* calcLine = new QFrame(); calcLine->setFrameShape(QFrame::HLine); calcLine->setStyleSheet("border: 1px solid #555; margin-top: 10px;"); camLayout->addWidget(calcLine);
    
    QLabel* lblCalcTitle = new QLabel("TIMELAPSE CALCULATOR (Estimates Only)");
    lblCalcTitle->setAlignment(Qt::AlignCenter); lblCalcTitle->setStyleSheet("color: #4DB8FF; font-weight: bold; font-size: 12px; margin-top: 5px; margin-bottom: 5px;");
    camLayout->addWidget(lblCalcTitle);
    
    QFormLayout* calcForm = new QFormLayout();
    QHBoxLayout* durLayout = new QHBoxLayout(); cmb_dur_hr = new QComboBox(); cmb_dur_min = new QComboBox();
    for(int i=0; i<=500; i++) cmb_dur_hr->addItem(QString::number(i));
    for(int i=0; i<=60; i++) cmb_dur_min->addItem(QString::number(i));
    durLayout->addWidget(cmb_dur_hr); durLayout->addWidget(new QLabel("Hr")); durLayout->addWidget(cmb_dur_min); durLayout->addWidget(new QLabel("Min"));
    calcForm->addRow("Duration:", durLayout);
    
    txt_total_frames = new QLineEdit("0"); txt_est_length = new QLineEdit("0"); txt_est_size = new QLineEdit("0 MB");
    txt_total_frames->setReadOnly(true); txt_est_length->setReadOnly(true); txt_est_size->setReadOnly(true);
    calcForm->addRow("Frames:", txt_total_frames); calcForm->addRow("Length:", txt_est_length); calcForm->addRow("Size:", txt_est_size);
    camLayout->addLayout(calcForm);
    
    QFrame* line = new QFrame(); line->setFrameShape(QFrame::HLine); line->setStyleSheet("border: 1px solid #555;"); camLayout->addWidget(line);

    QGridLayout* profileGrid = new QGridLayout();
    txt_profile_name = new QLineEdit("My_Preset_1"); btn_save_profile = new QPushButton("Save Profile");
    cmb_profiles = new QComboBox(); btn_load_profile = new QPushButton("Load Selected");
    profileGrid->addWidget(txt_profile_name, 0, 0); profileGrid->addWidget(btn_save_profile, 0, 1);
    profileGrid->addWidget(cmb_profiles, 1, 0); profileGrid->addWidget(btn_load_profile, 1, 1);
    camLayout->addLayout(profileGrid);
    camLayout->addStretch();
    bottomLayout->addWidget(gbCamera);

    QGroupBox* gbFolders = new QGroupBox("Directories & Recovery");
    QVBoxLayout* foldLayout = new QVBoxLayout(gbFolders);
    
    chk_crash_recovery = new QCheckBox("Resume on Boot (Crash Recovery)"); chk_crash_recovery->setChecked(true); foldLayout->addWidget(chk_crash_recovery);
    
    QGridLayout* btnGrid = new QGridLayout();
    btn_set_img = new QPushButton("Set Image Folder"); btn_set_vid = new QPushButton("Set Video Folder");
    btn_open_img = new QPushButton("Open Image Folder"); btn_open_vid = new QPushButton("Open Video Folder");
    btnGrid->addWidget(btn_set_img, 0, 0); btnGrid->addWidget(btn_set_vid, 0, 1);
    btnGrid->addWidget(btn_open_img, 1, 0); btnGrid->addWidget(btn_open_vid, 1, 1); foldLayout->addLayout(btnGrid);
    
    btn_view_live = new QPushButton("VIEW LIVE .AVI"); btn_view_live->setEnabled(false); 
    foldLayout->addWidget(btn_view_live);
    
    btn_fullscreen_ = new QPushButton("POP-OUT FULLSCREEN");
    btn_fullscreen_->setStyleSheet("background-color: #1976D2; color: white; font-weight: bold;");
    foldLayout->addWidget(btn_fullscreen_);
    
    foldLayout->addSpacing(15);
    
    lblImgPath = new QLabel(QString("C:\\Users\\Dalton\\Timelapse\\Camera_%1\\Images").arg(tab_index_)); 
    lblVidPath = new QLabel(QString("C:\\Users\\Dalton\\Timelapse\\Camera_%1\\Videos").arg(tab_index_));
    lblImgPath->setStyleSheet("font-size: 11px; color: #AAA; margin-bottom: 5px;"); lblVidPath->setStyleSheet("font-size: 11px; color: #AAA; margin-bottom: 15px;");
    foldLayout->addWidget(lblImgPath); foldLayout->addWidget(lblVidPath);
    
    chk_timestamp = new QCheckBox("Enable Timestamp"); chk_timestamp->setChecked(true); foldLayout->addWidget(chk_timestamp);
    
    QCheckBox* chk_lock_timestamp = new QCheckBox("Lock Timestamp");
    chk_lock_timestamp->setObjectName("chk_lock_timestamp"); chk_lock_timestamp->setChecked(true);
    foldLayout->addWidget(chk_lock_timestamp);

    foldLayout->addStretch();
    bottomLayout->addWidget(gbFolders);

    mainLayout->addWidget(bottomContainer, 0);

    QHBoxLayout* statusBarLayout = new QHBoxLayout();
    status_label_ = new QLabel("Recording: OFF"); status_label_->setStyleSheet("color: red; font-weight: bold; font-size: 14px;");
    dark_status_label_ = new QLabel("Dark Image: NO"); dark_status_label_->setStyleSheet("color: #AAAAAA; font-weight: bold; font-size: 14px; margin-left: 20px;");
    statusBarLayout->addWidget(status_label_); statusBarLayout->addWidget(dark_status_label_); statusBarLayout->addStretch();
    mainLayout->addLayout(statusBarLayout);

    connect(cmb_camera, &QComboBox::currentTextChanged, [this](const QString& newCam) {
        if (newCam == "--- Select a Camera ---" || newCam.isEmpty()) {
            last_selected_camera_ = newCam;
            if(camera_) camera_->setCameraIndex(-1, false, false, "");
            return;
        }
        
        QList<CameraTab*> allTabs = this->window()->findChildren<CameraTab*>();
        bool conflict = false;
        for (CameraTab* tab : allTabs) {
            if (tab != this && tab->getSelectedCamera() == newCam && !newCam.contains("GoPro Hero")) {
                conflict = true;
                break;
            }
        }
        
        if (conflict) {
            QMessageBox::warning(this, "Hardware Conflict", "The hardware index '" + newCam + "' is already assigned to another tab.\n\nPlease assign a different camera to this slot to prevent a system crash.");
            cmb_camera->blockSignals(true);
            cmb_camera->setCurrentText(last_selected_camera_);
            cmb_camera->blockSignals(false);
        } else {
            last_selected_camera_ = newCam;
            QSettings app_settings("MasterLapse", "Settings");
            app_settings.setValue(QString("Tab_%1_Camera").arg(tab_index_), newCam);
            
            bool isVirtual = newCam.contains("Virtual Camera");
            bool isGoPro = newCam.contains("GoPro Hero (Network API)");
            
            if (isVirtual || isGoPro) {
                cmb_resolution->blockSignals(true);
                if (isVirtual) cmb_resolution->setCurrentText("1920 x 1080");
                chk_gopro_live->setEnabled(isGoPro);
                if (!isGoPro) { chk_gopro_live->setChecked(false); if(camera_) camera_->setGoProLiveView(false); }
                chk_gopro_live->setEnabled(isGoPro);
                if (!isGoPro) { chk_gopro_live->setChecked(false); if(camera_) camera_->setGoProLiveView(false); }
                cmb_resolution->blockSignals(false);
                syncBackendResolution(); 
            }
            
            if(camera_) camera_->setCameraIndex(cmb_camera->currentIndex() - 1, isVirtual, isGoPro, discoverGoProIp().toStdString());
        }
    });

    auto toggleSettings = [this](bool state) {
        cmb_interval_sec->setEnabled(state); cmb_interval_min->setEnabled(state); cmb_camera->setEnabled(state);
        cmb_resolution->setEnabled(state); cmb_fps->setEnabled(state); cmb_quality->setEnabled(state); cmb_dark->setEnabled(state);
        cmb_dur_hr->setEnabled(state); cmb_dur_min->setEnabled(state); chk_keep_images->setEnabled(state); chk_keep_videos->setEnabled(state);
        btn_set_img->setEnabled(state); btn_set_vid->setEnabled(state); btn_save_profile->setEnabled(state); btn_load_profile->setEnabled(state);
        cmb_profiles->setEnabled(state); txt_profile_name->setEnabled(state); chk_crash_recovery->setEnabled(state);
        chk_timestamp->setEnabled(state); btn_props_->setEnabled(state); btn_merge_->setEnabled(state);
        btn_view_live->setEnabled(!state);
        QCheckBox* lockBox = this->findChild<QCheckBox*>("chk_lock_timestamp");
        if (lockBox) lockBox->setEnabled(state);
    };

    connect(chk_timestamp, &QCheckBox::toggled, [this](bool checked) { std::lock_guard<std::mutex> lock(g_ts_mutex); g_ts_config.enabled = checked; });
    connect(cmb_resolution, &QComboBox::currentTextChanged, this, [this](const QString&) { syncBackendResolution(); });
    connect(chk_gopro_live, &QCheckBox::toggled, [this](bool checked) { if(camera_) camera_->setGoProLiveView(checked); });
    connect(chk_gopro_live, &QCheckBox::toggled, [this](bool checked) { if(camera_) camera_->setGoProLiveView(checked); });
    connect(cmb_quality, &QComboBox::currentTextChanged, this, [this](const QString&){ updateCalculator(); });

    connect(chk_keep_images, &QCheckBox::toggled, [this](bool checked) { 
        if(encoder_ && save_) { encoder_->setKeepImages(checked); save_->setKeepImages(checked); }
    });

    connect(btn_start_, &QPushButton::clicked, [this, toggleSettings]() { 
        if (cmb_camera->currentText() == "--- Select a Camera ---") {
            QMessageBox::warning(this, "Hardware Missing", "Please select a valid physical or virtual camera from the dropdown before starting the capture.");
            return;
        }
        
        if (!camera_ || !encoder_ || !save_) return;
        encoder_->setKeepImages(chk_keep_images->isChecked()); save_->setKeepImages(chk_keep_images->isChecked());
        save_->setOutputFolder(lblImgPath->text().toStdString());
        
        double capSec = cmb_interval_sec->currentText().toDouble(); int capMin = cmb_interval_min->currentText().toInt();
        int ms = static_cast<int>(((capMin * 60.0) + capSec) * 1000);
        camera_->setInterval(ms);
        toggleSettings(false); camera_->setRecording(true); 
        
        QSettings app_settings("MasterLapse", "Settings"); 
        app_settings.setValue(QString("recording_in_progress_%1").arg(tab_index_), true);
        
        if (chk_crash_recovery->isChecked()) {
            QString buildDir = QDir::currentPath().replace("/", "\\");
            QString lockFileName = QString("recording_%1.lock").arg(tab_index_);
            QString batFileName = QString("watchdog_%1.bat").arg(tab_index_);
            QString vbsFileName = QString("hidden_watchdog_%1.vbs").arg(tab_index_);
            
            QFile lockFile(lockFileName);
            if (lockFile.open(QIODevice::WriteOnly)) { lockFile.write("locked"); lockFile.close(); }

            QFile batFile(batFileName);
            if (batFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&batFile);
                out << "@echo off\n:loop\n";
                out << "if not exist \"" << buildDir << "\\" << lockFileName << "\" exit\n";
                out << "tasklist /FI \"IMAGENAME eq Masterlapse.exe\" 2>NUL | find /I /N \"Masterlapse.exe\">NUL\n";
                out << "if \"%ERRORLEVEL%\"==\"1\" (\n";
                out << "    start \"\" /D \"" << buildDir << "\" \"Masterlapse.exe\"\n";
                out << ")\n";
                out << "timeout /t 5 /nobreak > NUL\n";
                out << "goto loop\n";
                batFile.close();
            }

            QFile vbsFile(vbsFileName);
            if (vbsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&vbsFile);
                out << "Set WshShell = CreateObject(\"WScript.Shell\")\n";
                out << "WshShell.Run chr(34) & \"" << buildDir << "\\" << batFileName << "\" & Chr(34), 0, False\n";
                out << "Set WshShell = Nothing\n";
                vbsFile.close();
            }

            QProcess::startDetached("wscript.exe", QStringList() << vbsFileName);
        }

        is_paused_ = false; is_recording_ = true; camera_->setPaused(false);
        btn_pause_->setEnabled(true); btn_pause_->setText("Pause Capture"); btn_pause_->setStyleSheet("background-color: #FBC02D; color: black; font-weight: bold;"); 
        status_label_->setText("Recording: ON"); status_label_->setStyleSheet("color: lime; font-weight: bold; font-size: 14px;");
        btn_start_->setEnabled(false); btn_start_->setStyleSheet(""); 
        btn_stop_->setEnabled(true); btn_stop_->setStyleSheet("background-color: #D32F2F; color: white; font-weight: bold;");
        btn_view_live->setEnabled(true); btn_view_live->setStyleSheet("background-color: #8C6D1F; color: white; font-weight: bold;");
    });

    connect(btn_pause_, &QPushButton::clicked, [this]() {
        if(!camera_) return;
        is_paused_ = !is_paused_; camera_->setPaused(is_paused_);
        if (is_paused_) {
            btn_pause_->setText("Resume Capture"); btn_pause_->setStyleSheet("background-color: #F57F17; color: white; font-weight: bold;"); 
            status_label_->setText("Recording: PAUSED"); status_label_->setStyleSheet("color: #FBC02D; font-weight: bold; font-size: 14px;");
        } else {
            btn_pause_->setText("Pause Capture"); btn_pause_->setStyleSheet("background-color: #FBC02D; color: black; font-weight: bold;"); 
            status_label_->setText("Recording: ON"); status_label_->setStyleSheet("color: lime; font-weight: bold; font-size: 14px;");
        }
    });
    
    connect(btn_stop_, &QPushButton::clicked, [this, toggleSettings]() { 
        if(!camera_ || !encoder_) return;
        camera_->setRecording(false); 
        QSettings app_settings("MasterLapse", "Settings"); 
        app_settings.setValue(QString("recording_in_progress_%1").arg(tab_index_), false);
        
        QFile::remove(QString("recording_%1.lock").arg(tab_index_)); 
        QFile::remove(QString("watchdog_%1.bat").arg(tab_index_)); 
        QFile::remove(QString("hidden_watchdog_%1.vbs").arg(tab_index_));
        
        is_paused_ = false; is_recording_ = false; camera_->setPaused(false);
        btn_pause_->setEnabled(false); btn_pause_->setText("Pause Capture"); btn_pause_->setStyleSheet(""); 
        
        QString destFolder = lblVidPath->text(); QDir dir(destFolder); if (!dir.exists()) dir.mkpath(".");
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
        QString destFile = dir.filePath(QString("Fragment_Cam%1_").arg(tab_index_) + timestamp + ".avi");
        
        encoder_->archiveSession(destFile.toStdString()); toggleSettings(true); 

        status_label_->setText("Recording: OFF"); status_label_->setStyleSheet("color: red; font-weight: bold; font-size: 14px;");
        btn_start_->setEnabled(true); btn_start_->setStyleSheet("background-color: #2E7D32; color: white; font-weight: bold;");
        btn_stop_->setEnabled(false); btn_stop_->setStyleSheet(""); 
        btn_view_live->setEnabled(false); btn_view_live->setStyleSheet(""); 
    });
    
    connect(btn_view_live, &QPushButton::clicked, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::current().filePath(QString("MasterLapse_Output_%1.avi").arg(tab_index_)))); });
    
    connect(btn_fullscreen_, &QPushButton::clicked, [this]() {
        if (!fs_window_) {
            fs_window_ = new QWidget();
            fs_window_->setWindowTitle(QString("Camera %1 - Live Feed").arg(tab_index_));
            fs_window_->setStyleSheet("background-color: black;");
            QVBoxLayout* fsLayout = new QVBoxLayout(fs_window_);
            fsLayout->setContentsMargins(0, 0, 0, 0);
            fs_label_ = new QLabel();
            fs_label_->setAlignment(Qt::AlignCenter);
            fsLayout->addWidget(fs_label_);
            fs_window_->installEventFilter(this);
        }
        fs_window_->showFullScreen();
    });

    connect(btn_props_, &QPushButton::clicked, [this]() { 
        bool isGoPro = cmb_camera->currentText().contains("GoPro Hero (Network API)");
        if (isGoPro) {
            QDialog dialog(this);
            dialog.setWindowTitle("Hero 13 Advanced ProTune Settings");
            QFormLayout form(&dialog);

            QSettings settings("MasterLapse", "GoProSettings");

            QComboBox* cmbProTune = new QComboBox();
            cmbProTune->addItem("On (Unlocks Exposure)", 1);
            cmbProTune->addItem("Off", 0);
            cmbProTune->setCurrentIndex(settings.value("ProTuneIndex", 0).toInt());
            form.addRow("ProTune Master:", cmbProTune);

            QComboBox* cmbLens = new QComboBox();
            cmbLens->addItem("Wide", 0);
            cmbLens->addItem("Linear", 4);
            cmbLens->setCurrentIndex(settings.value("LensIndex", 0).toInt());
            form.addRow("Digital Lens (FOV):", cmbLens);

            QComboBox* cmbColor = new QComboBox();
            cmbColor->addItem("GoPro Color (Vibrant)", 100);
            cmbColor->addItem("Flat", 1);
            cmbColor->addItem("Natural", 2);
            cmbColor->addItem("GP-Log", 101);
            cmbColor->setCurrentIndex(settings.value("ColorIndex", 0).toInt());
            form.addRow("Color Profile:", cmbColor);

            QComboBox* cmbSharp = new QComboBox();
            cmbSharp->addItem("High", 0);
            cmbSharp->addItem("Medium", 1);
            cmbSharp->addItem("Low", 2);
            cmbSharp->setCurrentIndex(settings.value("SharpIndex", 1).toInt());
            form.addRow("Sharpness:", cmbSharp);
            
            QComboBox* cmbWb = new QComboBox();
            cmbWb->addItem("Auto", 0);
            cmbWb->addItem("Native", 10);
            cmbWb->setCurrentIndex(settings.value("WbIndex", 0).toInt());
            form.addRow("White Balance:", cmbWb);
            
            QComboBox* cmbIsoMin = new QComboBox();
            cmbIsoMin->addItem("100", 0);
            cmbIsoMin->addItem("200", 1);
            cmbIsoMin->addItem("400", 2);
            cmbIsoMin->addItem("800", 3);
            cmbIsoMin->setCurrentIndex(settings.value("IsoMinIndex", 0).toInt());
            form.addRow("ISO Minimum:", cmbIsoMin);

            QComboBox* cmbIsoMax = new QComboBox();
            cmbIsoMax->addItem("100", 0);
            cmbIsoMax->addItem("200", 1);
            cmbIsoMax->addItem("400", 2);
            cmbIsoMax->addItem("800", 3);
            cmbIsoMax->setCurrentIndex(settings.value("IsoMaxIndex", 0).toInt());
            form.addRow("ISO Maximum:", cmbIsoMax);

            QComboBox* cmbShutter = new QComboBox();
            cmbShutter->addItem("Auto", 0);
            cmbShutter->addItem("1/240 (60Hz Anti-Flicker)", 18);
            cmbShutter->addItem("1/360", 20);
            cmbShutter->addItem("1/480 (60Hz Anti-Flicker)", 22);
            cmbShutter->addItem("1/960", 23);
            cmbShutter->setCurrentIndex(settings.value("ShutterIndex", 0).toInt());
            form.addRow("Shutter Speed:", cmbShutter);
            
            QComboBox* cmbBeeps = new QComboBox();
            cmbBeeps->addItem("Off (Mute)", 0);
            cmbBeeps->addItem("On", 1);
            cmbBeeps->setCurrentIndex(settings.value("BeepIndex", 0).toInt());
            form.addRow("Beeps:", cmbBeeps);

            QPushButton* btnApply = new QPushButton("Lock Hardware State");
            btnApply->setStyleSheet("background-color: #2E7D32; color: white; font-weight: bold;");
            form.addRow("", btnApply);

            connect(btnApply, &QPushButton::clicked, [&]() {
                // Save state to Windows Registry
                settings.setValue("ProTuneIndex", cmbProTune->currentIndex());
                settings.setValue("LensIndex", cmbLens->currentIndex());
                settings.setValue("ColorIndex", cmbColor->currentIndex());
                settings.setValue("SharpIndex", cmbSharp->currentIndex());
                settings.setValue("WbIndex", cmbWb->currentIndex());
                settings.setValue("IsoMinIndex", cmbIsoMin->currentIndex());
                settings.setValue("IsoMaxIndex", cmbIsoMax->currentIndex());
                settings.setValue("ShutterIndex", cmbShutter->currentIndex());
                settings.setValue("BeepIndex", cmbBeeps->currentIndex());

                QString ip = discoverGoProIp();
                
                // 1. FORCE THE PROTUNE UNLOCK FIRST
                QString urlProTune = QString("http://%1/gopro/camera/setting?setting=114&option=%2").arg(ip).arg(cmbProTune->currentData().toInt());
                QProcess::startDetached("curl", QStringList() << "-s" << urlProTune);
                
                // Pause backend to let camera CPU process the unlock before hammering it
                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                // 2. DISPATCH REMAINING PAYLOADS
                QString urlLens = QString("http://%1/gopro/camera/setting?setting=121&option=%2").arg(ip).arg(cmbLens->currentData().toInt());
                QString urlColor = QString("http://%1/gopro/camera/setting?setting=116&option=%2").arg(ip).arg(cmbColor->currentData().toInt());
                QString urlSharp = QString("http://%1/gopro/camera/setting?setting=117&option=%2").arg(ip).arg(cmbSharp->currentData().toInt());
                QString urlWb = QString("http://%1/gopro/camera/setting?setting=115&option=%2").arg(ip).arg(cmbWb->currentData().toInt());
                QString urlIsoMin = QString("http://%1/gopro/camera/setting?setting=102&option=%2").arg(ip).arg(cmbIsoMin->currentData().toInt());
                QString urlIsoMax = QString("http://%1/gopro/camera/setting?setting=13&option=%2").arg(ip).arg(cmbIsoMax->currentData().toInt());
                QString urlShutter = QString("http://%1/gopro/camera/setting?setting=145&option=%2").arg(ip).arg(cmbShutter->currentData().toInt());
                QString urlBeep = QString("http://%1/gopro/camera/setting?setting=221&option=%2").arg(ip).arg(cmbBeeps->currentData().toInt());
                
                QProcess::startDetached("curl", QStringList() << "-s" << urlLens);
                QProcess::startDetached("curl", QStringList() << "-s" << urlColor);
                QProcess::startDetached("curl", QStringList() << "-s" << urlSharp);
                QProcess::startDetached("curl", QStringList() << "-s" << urlWb);
                QProcess::startDetached("curl", QStringList() << "-s" << urlIsoMin);
                QProcess::startDetached("curl", QStringList() << "-s" << urlIsoMax);
                QProcess::startDetached("curl", QStringList() << "-s" << urlShutter);
                QProcess::startDetached("curl", QStringList() << "-s" << urlBeep);
                
                QMessageBox::information(&dialog, "GoPro Remote Control", "Advanced MasterLapse settings transmitted successfully.");
                dialog.accept();
            });

            dialog.exec();
        } else if (camera_) {
            camera_->requestPropertiesDialog(); 
        }
    });
connect(btn_set_img, &QPushButton::clicked, this, &CameraTab::openImageFolderDialog);
    connect(btn_set_vid, &QPushButton::clicked, this, &CameraTab::openVideoFolderDialog);
    connect(btn_open_img, &QPushButton::clicked, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(lblImgPath->text())); });
    connect(btn_open_vid, &QPushButton::clicked, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(lblVidPath->text())); });
    connect(btn_save_profile, &QPushButton::clicked, this, &CameraTab::saveProfile);
    connect(btn_load_profile, &QPushButton::clicked, this, &CameraTab::loadProfile);

    connect(cmb_interval_sec, &QComboBox::currentTextChanged, this, [this](const QString&){ updateCalculator(); });
    connect(cmb_interval_min, &QComboBox::currentTextChanged, this, [this](const QString&){ updateCalculator(); });
    connect(cmb_dur_hr, &QComboBox::currentTextChanged, this, [this](const QString&){ updateCalculator(); });
    connect(cmb_dur_min, &QComboBox::currentTextChanged, this, [this](const QString&){ updateCalculator(); });
    connect(cmb_fps, &QComboBox::currentTextChanged, this, [this](const QString&){ updateCalculator(); });
}

bool CameraTab::eventFilter(QObject *obj, QEvent *event) {
    if (obj == fs_window_) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                fs_window_->hide();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            fs_window_->hide();
            return true;
        }
        return QWidget::eventFilter(obj, event);
    }

    if (obj == video_label_) {
        QCheckBox* lockBox = this->findChild<QCheckBox*>("chk_lock_timestamp");
        bool isLocked = lockBox ? lockBox->isChecked() : false;

        if (event->type() == QEvent::MouseButtonPress) {
            if (isLocked) return QWidget::eventFilter(obj, event);
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) { dragging_ts_ = true; updateTimestampPosition(mouseEvent->pos()); return true; }
        } else if (event->type() == QEvent::MouseMove) {
            if (isLocked) return QWidget::eventFilter(obj, event);
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (dragging_ts_) { updateTimestampPosition(mouseEvent->pos()); return true; }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (isLocked) return QWidget::eventFilter(obj, event);
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) { dragging_ts_ = false; return true; }
        } else if (event->type() == QEvent::Wheel) {
            if (isLocked) return QWidget::eventFilter(obj, event);
            QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
            if (dragging_ts_) { 
                int delta = wheelEvent->angleDelta().y();
                std::lock_guard<std::mutex> lock(g_ts_mutex);
                if (delta > 0) g_ts_config.scale += 0.05; else g_ts_config.scale -= 0.05;
                if (g_ts_config.scale < 0.1) g_ts_config.scale = 0.1;
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CameraTab::updateTimestampPosition(QPointF pos) {
    QPixmap pix = video_label_->pixmap(); if (pix.isNull()) return;
    QSize labelSize = video_label_->size(); QSize pixSize = pix.size();
    double labelAspect = (double)labelSize.width() / labelSize.height(); double pixAspect = (double)pixSize.width() / pixSize.height();
    double drawWidth, drawHeight; double offsetX = 0, offsetY = 0;
    if (labelAspect > pixAspect) { drawHeight = labelSize.height(); drawWidth = drawHeight * pixAspect; offsetX = (labelSize.width() - drawWidth) / 2.0; } 
    else { drawWidth = labelSize.width(); drawHeight = drawWidth / pixAspect; offsetY = (labelSize.height() - drawHeight) / 2.0; }
    double x = pos.x() - offsetX; double y = pos.y() - offsetY;
    if (x < 0) x = 0; if (x > drawWidth) x = drawWidth; if (y < 0) y = 0; if (y > drawHeight) y = drawHeight;
    std::lock_guard<std::mutex> lock(g_ts_mutex);
    g_ts_config.rel_x = x / drawWidth; g_ts_config.rel_y = y / drawHeight;
}

void CameraTab::syncBackendResolution() {
    QString resStr = cmb_resolution->currentText(); QStringList parts = resStr.split(" x ");
    if(parts.size() == 2 && camera_ && encoder_) {
        camera_->setResolution(parts[0].toInt(), parts[1].toInt());
        encoder_->setResolution(parts[0].toInt(), parts[1].toInt());
    }
    updateCalculator();
}

void CameraTab::openImageFolderDialog() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Image Folder", "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) lblImgPath->setText(dir);
}
void CameraTab::openVideoFolderDialog() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Video Folder", "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) lblVidPath->setText(dir);
}

void CameraTab::populateCameras() {
    QString currentCam = cmb_camera->currentText();
    cmb_camera->blockSignals(true);
    cmb_camera->clear();
    cmb_camera->addItem("--- Select a Camera ---");
    
    int hw_index = 0;
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    
    for (const QCameraDevice &cameraDevice : cameras) { 
        cmb_camera->addItem(QString("[%1] ").arg(hw_index++) + cameraDevice.description()); 
    } 
    
    for (int i = 0; i < 4; i++) {
        cmb_camera->addItem(QString("[%1] Force Hidden/Virtual Camera").arg(hw_index++));
    }
    
    cmb_camera->addItem(QString("[%1] GoPro Hero (Network API)").arg(hw_index++));
    
    int index = cmb_camera->findText(currentCam);
    if (index != -1) {
        cmb_camera->setCurrentIndex(index);
    } else {
        cmb_camera->setCurrentIndex(0);
        last_selected_camera_ = "--- Select a Camera ---";
        if (camera_) camera_->setCameraIndex(-1, false, false, "");
    }
    cmb_camera->blockSignals(false);
}

void CameraTab::refreshProfileList() {
    cmb_profiles->clear(); QDir dir("profiles"); QStringList filters; filters << "*.json";
    foreach(QString file, dir.entryList(filters, QDir::Files)) { cmb_profiles->addItem(file.replace(".json", "")); }
}

void CameraTab::saveProfile() {
    QString name = txt_profile_name->text(); if(name.isEmpty()) return;
    QJsonObject json;
    json["interval_sec"] = cmb_interval_sec->currentText(); json["interval_min"] = cmb_interval_min->currentText();
    json["keep_img"] = chk_keep_images->isChecked(); json["keep_vid"] = chk_keep_videos->isChecked();
    json["fps"] = cmb_fps->currentText();
    json["quality"] = cmb_quality->currentText(); json["dark_sens"] = cmb_dark->currentText();
    json["dur_hr"] = cmb_dur_hr->currentText(); json["dur_min"] = cmb_dur_min->currentText();
    json["crash_recovery"] = chk_crash_recovery->isChecked(); 
    json["img_folder"] = lblImgPath->text(); json["vid_folder"] = lblVidPath->text(); json["timestamp"] = chk_timestamp->isChecked();
    QCheckBox* lockBox = this->findChild<QCheckBox*>("chk_lock_timestamp"); if (lockBox) json["lock_timestamp"] = lockBox->isChecked();
    { std::lock_guard<std::mutex> lock(g_ts_mutex); json["ts_rel_x"] = g_ts_config.rel_x; json["ts_rel_y"] = g_ts_config.rel_y; json["ts_scale"] = g_ts_config.scale; }
    QJsonDocument doc(json); QFile file("profiles/" + name + ".json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson()); file.close(); refreshProfileList();
        QSettings settings("MasterLapse", "Settings"); settings.setValue(QString("LastProfile_%1").arg(tab_index_), name);
    }
}

void CameraTab::loadProfile() {
    QString name = cmb_profiles->currentText(); if(name.isEmpty()) return;
    QFile file("profiles/" + name + ".json");
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll(); QJsonDocument doc = QJsonDocument::fromJson(data); applyJsonToUi(doc.object());
        txt_profile_name->setText(name); QSettings settings("MasterLapse", "Settings"); settings.setValue(QString("LastProfile_%1").arg(tab_index_), name);
    }
}

void CameraTab::applyJsonToUi(const QJsonObject& json) {
    if(json.contains("interval_sec")) cmb_interval_sec->setCurrentText(json["interval_sec"].toString());
    if(json.contains("interval_min")) cmb_interval_min->setCurrentText(json["interval_min"].toString());
    if(json.contains("keep_img")) chk_keep_images->setChecked(json["keep_img"].toBool());
    if(json.contains("keep_vid")) chk_keep_videos->setChecked(json["keep_vid"].toBool());
    if(json.contains("resolution")) cmb_resolution->setCurrentText(json["resolution"].toString()); 
    if(json.contains("fps")) cmb_fps->setCurrentText(json["fps"].toString());
    if(json.contains("quality")) cmb_quality->setCurrentText(json["quality"].toString());
    if(json.contains("dark_sens")) cmb_dark->setCurrentText(json["dark_sens"].toString());
    if(json.contains("dur_hr")) cmb_dur_hr->setCurrentText(json["dur_hr"].toString());
    if(json.contains("dur_min")) cmb_dur_min->setCurrentText(json["dur_min"].toString());
    if(json.contains("crash_recovery")) chk_crash_recovery->setChecked(json["crash_recovery"].toBool()); 
    if(json.contains("img_folder")) lblImgPath->setText(json["img_folder"].toString());
    if(json.contains("vid_folder")) lblVidPath->setText(json["vid_folder"].toString());
    if(json.contains("timestamp")) chk_timestamp->setChecked(json["timestamp"].toBool());
    QCheckBox* lockBox = this->findChild<QCheckBox*>("chk_lock_timestamp");
    if(json.contains("lock_timestamp") && lockBox) lockBox->setChecked(json["lock_timestamp"].toBool());
    if(json.contains("ts_rel_x")) { std::lock_guard<std::mutex> lock(g_ts_mutex); g_ts_config.rel_x = json["ts_rel_x"].toDouble(); g_ts_config.rel_y = json["ts_rel_y"].toDouble(); g_ts_config.scale = json["ts_scale"].toDouble(); }
    syncBackendResolution();
}

void CameraTab::updateCalculator() {
    double capSec = cmb_interval_sec->currentText().toDouble(); int capMin = cmb_interval_min->currentText().toInt();
    int durHr = cmb_dur_hr->currentText().toInt(); int durMin = cmb_dur_min->currentText().toInt();
    int fps = cmb_fps->currentText().toInt();
    int totalShootingSeconds = (durHr * 3600) + (durMin * 60);
    double captureInterval = (capMin * 60.0) + capSec; if (captureInterval <= 0) captureInterval = 0.1; 
    int totalFrames = std::floor(totalShootingSeconds / captureInterval);
    txt_total_frames->setText(QString::number(totalFrames));
    int finalVideoSeconds = (fps > 0) ? (totalFrames / fps) : 0;
    txt_est_length->setText(QString("%1 hr : %2 min : %3 sec").arg(std::floor(finalVideoSeconds / 3600)).arg(std::floor((finalVideoSeconds % 3600) / 60)).arg((finalVideoSeconds % 3600) % 60));
    QString resStr = cmb_resolution->currentText(); QStringList resParts = resStr.split(" x ");
    double width = 2560.0; double height = 1440.0;
    if(resParts.size() == 2) { width = resParts[0].toDouble(); height = resParts[1].toDouble(); }
    int crf = cmb_quality->currentText().split(" ").first().toInt();
    double compression_multiplier = 0.005; 
    if (crf == 0) compression_multiplier = 0.030; else if (crf == 14) compression_multiplier = 0.015; else if (crf == 18) compression_multiplier = 0.008; else if (crf == 23) compression_multiplier = 0.003; 
    double estMB = ((width * height * 3.0) * compression_multiplier * totalFrames) / (1024 * 1024);
    if (estMB >= 1024) { txt_est_size->setText(QString::number(estMB / 1024.0, 'f', 2) + " GB"); } else { txt_est_size->setText(QString::number(estMB, 'f', 2) + " MB"); }
}

void CameraTab::updateFrame() {
    if(!preview_buffer_) return;
    FrameData fd; bool hasNewFrame = false;
    while (preview_buffer_->try_pop(fd)) { hasNewFrame = true; }
    if (hasNewFrame && !fd.image.empty()) {
        cv::Scalar m = cv::mean(fd.image); double avg_brightness = (m[0] + m[1] + m[2]) / 3.0; int threshold = cmb_dark->currentText().toInt();
        if (avg_brightness <= threshold) { dark_status_label_->setText(QString("Dark Image: YES (Avg: %1)").arg(avg_brightness, 0, 'f', 1)); dark_status_label_->setStyleSheet("color: orange; font-weight: bold; font-size: 14px; margin-left: 20px;"); } 
        else { dark_status_label_->setText(QString("Dark Image: NO (Avg: %1)").arg(avg_brightness, 0, 'f', 1)); dark_status_label_->setStyleSheet("color: #AAAAAA; font-weight: bold; font-size: 14px; margin-left: 20px;"); }
        double rel_x, rel_y, scale; bool enabled;
        { std::lock_guard<std::mutex> lock(g_ts_mutex); rel_x = g_ts_config.rel_x; rel_y = g_ts_config.rel_y; scale = g_ts_config.scale; enabled = g_ts_config.enabled; }
        if (enabled) {
            double scale_factor = (fd.image.cols / 1280.0) * scale;
            int thickness = std::max(2, static_cast<int>(3 * scale_factor)); int shadow_thickness = thickness + std::max(1, static_cast<int>(2 * scale_factor));
            auto now = std::chrono::system_clock::now(); std::time_t now_time = std::chrono::system_clock::to_time_t(now); std::tm* local_time = std::localtime(&now_time);
            std::ostringstream timeStream; timeStream << std::put_time(local_time, "%I:%M %p %m/%d/%y"); std::string timeStr = timeStream.str();
            cv::Point textPos(static_cast<int>(fd.image.cols * rel_x), static_cast<int>(fd.image.rows * rel_y));
            cv::putText(fd.image, timeStr, textPos, cv::FONT_HERSHEY_DUPLEX, scale_factor, cv::Scalar(0, 0, 0), shadow_thickness, cv::LINE_AA);
            cv::putText(fd.image, timeStr, textPos, cv::FONT_HERSHEY_DUPLEX, scale_factor, cv::Scalar(0, 0, 255), thickness, cv::LINE_AA);
        }
        cv::Mat rgb; cv::cvtColor(fd.image, rgb, cv::COLOR_BGR2RGB); QImage qimg((const unsigned char*)(rgb.data), rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(qimg).scaled(video_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation); video_label_->setPixmap(pixmap);
        
        if (fs_window_ && fs_window_->isVisible() && fs_label_) {
            QPixmap fs_pixmap = QPixmap::fromImage(qimg).scaled(fs_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            fs_label_->setPixmap(fs_pixmap);
        }
    }
}











