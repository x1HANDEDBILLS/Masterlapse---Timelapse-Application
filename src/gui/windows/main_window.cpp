#include "gui/windows/main_window.h"
#include "gui/windows/camera_tab.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>

MainWindow::MainWindow(std::vector<Buffer*>& previewBuffers, std::vector<Camera*>& cameras, std::vector<VideoEncoder*>& encoders, std::vector<Save*>& saves, QWidget *parent)
    : QMainWindow(parent) {
    
    setWindowTitle("MasterLapse | Multi-Camera Timelapse Engine");
    resize(1200, 1000);
    setMinimumSize(1000, 800);
    
    setStyleSheet("QMainWindow { background-color: #2E2E2E; color: white; } "
                  "QTabWidget::pane { border: 1px solid #555; background: #2E2E2E; } "
                  "QTabBar::tab { background: #1E1E1E; color: #AAA; border: 1px solid #555; padding: 8px 15px; margin-right: 2px; } "
                  "QTabBar::tab:selected { background: #3A3A3A; color: white; font-weight: bold; border-bottom-color: #3A3A3A; } "
                  "QGroupBox { color: white; font-weight: bold; border: 1px solid #555; margin-top: 15px; padding-top: 15px; } "
                  "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; top: 0px; padding: 0 5px; background-color: #2E2E2E; } "
                  "QPushButton { background-color: #3C3C3C; color: white; border: 1px solid #555; padding: 6px; font-weight: bold; } "
                  "QPushButton:hover { background-color: #4C4C4C; } "
                  "QPushButton:disabled { background-color: #222222; color: #555555; border: 1px solid #333333; } "
                  "QLabel { color: white; } "
                  "QComboBox, QLineEdit { background-color: #1E1E1E; color: white; border: 1px solid #555; padding: 3px; } "
                  "QComboBox:disabled, QLineEdit:disabled { background-color: #222222; color: #555555; } "
                  "QCheckBox { color: white; } "
                  "QCheckBox:disabled { color: #555555; } "
                  "QMessageBox { background-color: #2E2E2E; color: white; } "
                  "QMessageBox QLabel { color: white; font-weight: bold; } "
                  "QMessageBox QPushButton { background-color: #D32F2F; color: white; font-weight: bold; width: 100px; padding: 5px; } ");

    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    
    QTabWidget* tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);
    
    for(int i = 0; i < 5; i++) {
        CameraTab* tab = new CameraTab(i + 1, previewBuffers[i], cameras[i], encoders[i], saves[i], this);
        tabWidget->addTab(tab, QString("Camera %1").arg(i + 1));
    }
}

MainWindow::~MainWindow() {}

void MainWindow::closeEvent(QCloseEvent *event) {
    bool any_recording = false;
    QList<CameraTab*> tabs = this->findChildren<CameraTab*>();
    for (CameraTab* tab : tabs) {
        if (tab->isRecording()) { any_recording = true; break; }
    }
    
    if (any_recording) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Recording in Progress!");
        msgBox.setText("You cannot close MasterLapse while a capture is actively running.");
        msgBox.setInformativeText("Please stop all captures across all tabs first.");
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.exec();
        event->ignore();
    } else {
        event->accept(); 
    }
}
