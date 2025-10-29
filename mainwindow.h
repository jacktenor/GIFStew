#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QImage>
#include <QColor>
#include <QPixmap>

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    QPixmap m_previewPixmap;

    // Generators you’re calling from .cpp
    bool generateSpinGif(const QString &srcImagePath,
                         const QString &outGifPath,
                         int fps, int durationSec, int sizePx,
                         const QColor &bg, QString *errOut);

    bool generateOscillateGif(const QString &srcImagePath,
                              const QString &outGifPath,
                              int fps, int durationSec, int sizePx,
                              qreal maxDegrees,
                              const QColor &bg, QString *errOut);

    bool generateYawSpinGif(const QString &srcImagePath,
                            const QString &outGifPath,
                            int fps, int durationSec, int sizePx,
                            qreal maxYawDeg,
                            const QColor &bg, QString *errOut);

    bool generateFlipGif(const QString &srcImagePath,
                         const QString &outGifPath,
                         int fps, int durationSec, int sizePx,
                         bool animate, int cycles,
                         const QColor &bg, QString *errOut);

    bool generateCompositeGif(const QString &srcImagePath,
                              const QString &outGifPath,
                              int fps, int durationSec, int sizePx,
                              bool useZSpin, qreal zDegPerSec,
                              bool useYaw, qreal maxYawDeg,
                              bool useFlip, bool flipAnimate, int flipCycles,
                              const QColor &bg, QString *errOut);

    // (Optional) globe helpers if your .cpp really uses them
    bool generateGlobeGif(const QString &frontImagePath,
                          const QString &backImagePath,
                          const QString &outGifPath,
                          int fps, int durationSec, int sizePx,
                          qreal rotationSpeed, qreal zoomPercent,
                          int rotationAxis,
                          const QColor &globeSurfaceColor,
                          QString *errOut);

    QImage renderGlobeFrame(const QImage &frontTexture,
                            const QImage &backTexture,
                            qreal rotationDegrees, int sizePx,
                            const QColor &frameBg,
                            const QColor &globeSurfaceColor,
                            bool enableLighting,
                            int rotationAxis);

    QImage renderGlobeFrame(const QImage &frontTexture,
                            const QImage &backTexture,
                            qreal rotationDegrees, int sizePx,
                            const QColor &bg, bool enableLighting, int rotationAxis);

    QRgb sampleTexture(const QImage &frontTexture,
                       const QImage &backTexture,
                       qreal lon, qreal lat);

    QRgb sampleTexture(const QImage &frontTexture,
                       const QImage &backTexture,
                       qreal lon, qreal lat,
                       const QColor &fallbackColor);

    // UI helpers
    QImage  zoomImage(const QImage &src, qreal zoomPercent, const QColor &padColor);
    QImage  cropCenterPercent(const QImage &src, qreal percentToKeep);
    void    refreshPreview(const QString &path);
    QString pickImageWithPreview(const QString &startDir);
    void    showGifInPreview(const QString &gifPath);

QString ensureBackImageForRun(const QString &frontPath,
                                          const QString &explicitBackPath,
                                          bool simulateBack,
                                          QString *errOut);

bool resolveFrontBackImages(const QString &frontPath,
                                        const QString &maybeBackPath,
                                        QImage &frontOut,
                                        QImage &backOut,
                                        QString *errOut);

const QImage &pickFaceForAngle(const QImage &front,
                                           const QImage &back,
                                           qreal angleDeg) const;

bool generateSpinGif(const QString &frontPath,
                                 const QString &backPath,
                                 const QString &outDir,
                                 int fps,
                                 int durationSec,
                                 qreal maxAngleDeg,
                                 int cycles,
                                 QString *errOut);

bool generateFlipGif(const QString &frontPath,
                                 const QString &backPath,
                                 const QString &outDir,
                                 int fps,
                                 int durationSec,
                                 qreal maxAngleDeg,
                                 int cycles,
                                 QString *errOut);

bool simulateBacksideEnabled() const;
QImage makeBacksideFrom(const QImage &front, bool *ok, QString *errOut);
void appendLog(const QString &msg);
void on_radioSimBackface_toggled(bool checked);
void setupBackfaceRadios();
void setupBackfaceRadiosSimple();

private slots:
    void on_btnBrowseImage_clicked();
    void on_btnBrowseBack_clicked();
    void on_btnBrowseOutput_clicked();
    void on_btnGenerate_clicked();
    void on_editImagePath_textChanged(const QString &path);
    void connectUiActions();
    // void onFlipAnimateToggled(bool on); // enable if implemented
};

#endif // MAINWINDOW_H
