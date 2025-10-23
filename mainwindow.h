#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qbuttongroup.h"
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

bool generateGlobeGif(const QString &frontImagePath,
                      const QString &backImagePath,
                      const QString &outGifPath,
                      int fps,
                      int durationSec,
                      int sizePx,
                      qreal rotationSpeed,
                      qreal zoomPercent,
                      int rotationAxis,
                      const QColor &globeSurfaceColor,
                      QString *errOut);
private:
    Ui::MainWindow *ui;
    QRgb sampleTexture(const QImage &frontTexture,
                       const QImage &backTexture,
                       qreal lon,
                       qreal lat);

    // Helper: render globe frame with axis control
    QImage renderGlobeFrame(const QImage &frontTexture,
                           const QImage &backTexture,
                           qreal rotationDegrees,
                           int sizePx,
                           const QColor &bg,
                           bool enableLighting,
                           int rotationAxis);  // 0=horizontal, 1=vertical, 2=both

    bool generateOscillateGif(const QString &srcImagePath,
                                      const QString &outGifPath,
                                      int fps,
                                      int durationSec,
                                      int sizePx,
                                      qreal maxDegrees,
                                      const QColor &bg,
                                      QString *errOut);

 bool generateSpinGif(const QString &srcImagePath,
                            const QString &outGifPath,
                            int fps,
                            int durationSec,
                            int sizePx,
                            const QColor &bg,
                            QString *errOut);

QRgb sampleTexture(const QImage &frontTexture,
                       const QImage &backTexture,
                       qreal lon,
                       qreal lat,
                       const QColor &fallbackColor);

    bool generateYawSpinGif(const QString &frontImagePath,
                                    const QString &outGifPath,
                                    int fps,
                                    int durationSec,
                                    int sizePx,
                                    qreal maxYawDeg,
                                    const QColor &bg,
                                    QString *errOut);

    bool generateFlipGif(const QString &frontImagePath,
                                 const QString &outGifPath,
                                 int fps,
                                 int durationSec,
                                 int sizePx,
                                 bool animate,
                                 int cycles,
                                 const QColor &bg,
                                 QString *errOut);

 bool generateCompositeGif(const QString &frontImagePath,
                                      const QString &outGifPath,
                                      int fps,
                                      int durationSec,
                                      int sizePx,
                                      bool useZSpin,
                                      qreal zDegPerSec,
                                      bool useYaw,
                                      qreal maxYawDeg,
                                      bool useFlip,
                                      bool flipAnimate,
                                      int flipCycles,
                                      const QColor &bg,
                                      QString *errOut);;

QImage renderGlobeFrame(const QImage &frontTexture,
                           const QImage &backTexture,
                           qreal rotationDegrees,
                           int sizePx,
                           const QColor &frameBg,          // canvas background
                           const QColor &globeSurfaceColor, // globe surface where no texture
                           bool enableLighting,
                           int rotationAxis);

    // Helper: zoom image by percentage (1-99=zoom in/crop, 100=original, 101-200=zoom out/pad)
QImage zoomImage(const QImage &src, qreal zoomPercent, const QColor &padColor);
QImage cropCenterPercent(const QImage &src, qreal percentToKeep);
    // Helper: sample texture at spherical coordinates (lon/lat in radians)
QPixmap m_previewPixmap;
void refreshPreview(const QString &path);
QString pickImageWithPreview(const QString &startDir);
void showGifInPreview(const QString &gifPath);

private slots:
//void onFlipAnimateToggled(bool on);
void on_btnBrowseOutput_clicked();
void on_btnBrowseImage_clicked();
void on_btnGenerate_clicked();
void on_editImagePath_textChanged(const QString &path);
void connectUiActions();
void on_btnBrowseBack_clicked();
};
#endif // MAINWINDOW_H
