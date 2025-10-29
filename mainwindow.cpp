#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <QtMath>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QRadioButton>
#include <QSpinBox>
#include <QFileDialog>
#include <QSettings>
#include <QPixmap>
#include <QGridLayout>
#include <QImageReader>
#include <QProxyStyle>
#include <QAbstractItemView>
#include <QListView>
#include <QTreeView>
#include <QTimer>
#include <QEvent>
#include <QLabel>
#include <QMovie>
#include <QButtonGroup>
#include <QObject>

namespace {

// (inside your existing anonymous namespace)
static QImage cropToContentSmart(const QImage &src, int alphaThreshold = 8)
{
    if (src.isNull()) return src;

    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();

    bool hasAlpha = img.hasAlphaChannel();
    if (hasAlpha) {
        int minX = w, minY = h, maxX = -1, maxY = -1;
        const uchar *bits = img.bits();
        const int stride = img.bytesPerLine();
        for (int y = 0; y < h; ++y) {
            const uchar* row = bits + y * stride;
            for (int x = 0; x < w; ++x) {
                const uchar a = row[x * 4 + 3];
                if (a > alphaThreshold) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        if (maxX >= minX && maxY >= minY) {
            return img.copy(QRect(QPoint(minX, minY), QPoint(maxX, maxY)));
        }
        return img;
    }

    const QColor border = QColor::fromRgba(img.pixel(0, 0));
    auto close = [](const QColor &a, const QColor &b, int tol=2){
        return std::abs(a.red()   - b.red())   <= tol &&
               std::abs(a.green() - b.green()) <= tol &&
               std::abs(a.blue()  - b.blue())  <= tol;
    };

    int left = 0, right = w - 1, top = 0, bottom = h - 1;

    for (; left < w; ++left) {
        bool allBorder = true;
        for (int y = 0; y < h; ++y)
            if (!close(QColor::fromRgba(img.pixel(left, y)), border)) { allBorder = false; break; }
        if (!allBorder) break;
    }
    for (; right >= left; --right) {
        bool allBorder = true;
        for (int y = 0; y < h; ++y)
            if (!close(QColor::fromRgba(img.pixel(right, y)), border)) { allBorder = false; break; }
        if (!allBorder) break;
    }
    for (; top < h; ++top) {
        bool allBorder = true;
        for (int x = left; x <= right; ++x)
            if (!close(QColor::fromRgba(img.pixel(x, top)), border)) { allBorder = false; break; }
        if (!allBorder) break;
    }
    for (; bottom >= top; --bottom) {
        bool allBorder = true;
        for (int x = left; x <= right; ++x)
            if (!close(QColor::fromRgba(img.pixel(x, bottom)), border)) { allBorder = false; break; }
        if (!allBorder) break;
    }

    if (right >= left && bottom >= top)
        return img.copy(QRect(QPoint(left, top), QPoint(right, bottom)));
    return img;
}

// --- NEW: find the bounding box of non-transparent pixels ---
// Returns an empty QRect if the image is fully transparent or invalid.
// alphaThreshold: treat pixels with alpha <= threshold as transparent (default 8).
static QRect cropToOpaqueBounds(const QImage &img, int alphaThreshold = 8)
{
    if (img.isNull())
        return QRect();

    // If there’s no alpha channel, treat the whole image as opaque.
    if (!img.hasAlphaChannel())
        return QRect(0, 0, img.width(), img.height());

    int minX = img.width();
    int minY = img.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) > alphaThreshold) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        // No opaque pixels found
        return QRect();
    }

    // QRect takes x, y, width, height (inclusive bounds -> +1)
    return QRect(minX, minY, (maxX - minX + 1), (maxY - minY + 1));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setupBackfaceRadiosSimple();


    // Load placeholder from file
    if (ui->lblPreview) {
        QPixmap placeholder(":/icons/gifstew.png"); // if using Qt resources
        // OR
        // QPixmap placeholder("/path/to/placeholder.png"); // direct file path

        if (!placeholder.isNull()) {
            ui->lblPreview->setPixmap(placeholder);
            ui->lblPreview->setAlignment(Qt::AlignCenter);
            ui->lblPreview->setScaledContents(false);
        } else {
            ui->lblPreview->setText("Preview");
            ui->lblPreview->setAlignment(Qt::AlignCenter);
        }
    }


    // Style radio buttons for dark mode visibility
    auto *grp = new QButtonGroup(this);
    grp->setExclusive(false);

    if (ui->radioSpin)      { grp->addButton(ui->radioSpin);      ui->radioSpin->setAutoExclusive(false); }
    if (ui->radioOscillate) { grp->addButton(ui->radioOscillate); ui->radioOscillate->setAutoExclusive(false); }
    if (ui->radioYawSpin)   { grp->addButton(ui->radioYawSpin);   ui->radioYawSpin->setAutoExclusive(false); }
    if (ui->radioFlipUD)    { grp->addButton(ui->radioFlipUD);    ui->radioFlipUD->setAutoExclusive(false); }
    if (ui->radioGlobe)     { grp->addButton(ui->radioGlobe);     ui->radioGlobe->setAutoExclusive(false); }

    connectUiActions();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// --- Helper: find ImageMagick binary ("magick" preferred; fall back to "convert")
static QString findImageMagick()
{
    // Try v7 "magick" first
    QStringList candidates = { "magick", "convert" };
    for (const QString &bin : candidates) {
        QString which = QStandardPaths::findExecutable(bin);
        if (!which.isEmpty()) return which;
    }
    return QString();
}

// --- Helper: draw 'src' centered on a square canvas to prevent clipping when rotated
static QImage makeSquareCanvas(const QImage &src, int sizePx, const QColor &bg)
{
    QImage canvas(sizePx, sizePx, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(bg);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QSize scaled = src.size().scaled(QSize(sizePx, sizePx), Qt::KeepAspectRatio);
    const QPoint topLeft( (sizePx - scaled.width())/2, (sizePx - scaled.height())/2 );
    p.drawImage(QRect(topLeft, scaled), src);
    p.end();
    return canvas;
}

// --- Helper: write frames as PNGs to framesDir (frame_0000.png, frame_0001.png, …)
static bool writeFrames(const QList<QImage> &frames, const QString &framesDir, QString *errOut)
{
    QDir().mkpath(framesDir);
    for (int i = 0; i < frames.size(); ++i) {
        const QString fn = QString("%1/frame_%2.png")
                               .arg(framesDir)
                               .arg(i, 4, 10, QChar('0'));
        if (!frames[i].save(fn, "PNG")) {
            if (errOut) *errOut = QString("Failed saving %1").arg(fn);
            return false;
        }
    }
    return true;
}

// --- Helper: assemble frames into an animated GIF using ImageMagick
// fps → delay=100/fps (centiseconds per frame). 'optimize' lets ImageMagick reduce size.
static bool assembleGif(const QString &magickBin,
                        const QString &framesDir,
                        int fps,
                        const QString &outGif,
                        bool optimize,
                        QString *errOut)
{
    if (fps <= 0) fps = 12;
    const int delayCs = qMax(1, 100 / fps); // centiseconds per frame, min 1

    // Build arguments depending on whether we have "magick" or "convert"
    QStringList args;
    const bool isMagick7 = QFileInfo(magickBin).fileName() == "magick";

    // Input glob
    const QString glob = QDir(framesDir).filePath("frame_*.png");

    if (isMagick7) {
        // magick -delay XX -dispose Background -layers Optimize frame_*.png -loop 0 out.gif
        args << "-delay" << QString::number(delayCs)
             << "-dispose" << "Background";
        if (optimize) args << "-layers" << "Optimize";
        args << glob
             << "-loop" << "0"
             << outGif;
    } else {
        // convert (IM6)
        args << "-delay" << QString::number(delayCs)
             << "-dispose" << "Background";
        if (optimize) args << "-layers" << "Optimize";
        args << glob
             << "-loop" << "0"
             << outGif;
    }

    QProcess p;
    if (isMagick7) {
        p.start(magickBin, args);
    } else {
        // convert has no wrapper "magick", so we just run it directly
        p.start(magickBin, args);
    }
    if (!p.waitForStarted(15000)) {
        if (errOut) *errOut = "Failed to start ImageMagick.";
        return false;
    }
    p.waitForFinished(-1);
    const int exitCode = p.exitCode();
    if (exitCode != 0) {
        if (errOut) *errOut = QString("ImageMagick failed (exit %1): %2")
                                  .arg(exitCode, 0, 10)
                                  .arg(QString::fromUtf8(p.readAllStandardError()));
        return false;
    }
    return true;
}

// Decide if we’re seeing the back for a given rotation angle.
// Back is visible when cosine is negative -> angle in (90°, 270°)
static inline bool isBackVisible(qreal angleDeg)
{
    qreal a = std::fmod(angleDeg, 360.0);
    if (a < 0) a += 360.0;
    return (a > 90.0 && a < 270.0);
}

// Resolve front & back QImages for this run (will auto-simulate the back if enabled or missing).
bool MainWindow::resolveFrontBackImages(const QString &frontPath,
                                        const QString &maybeBackPath,
                                        QImage &frontOut,
                                        QImage &backOut,
                                        QString *errOut)
{
    if (frontPath.isEmpty()) {
        if (errOut) *errOut = tr("No front image selected.");
        return false;
    }

    QImage front(frontPath);
    if (front.isNull()) {
        if (errOut) *errOut = tr("Failed to load front image: %1").arg(frontPath);
        return false;
    }

    // IMPORTANT: pass the required 3rd argument (simulateBack)
    const bool simulateBack = simulateBacksideEnabled();
    QString resolvedBack = ensureBackImageForRun(frontPath,
                                                 maybeBackPath,
                                                 simulateBack,
                                                 errOut);

    // If we didn’t get a back image (either not provided or simulation failed),
    // fall back to single-sided rendering by using the front as the back.
    // This keeps animations working instead of hard-failing.
    QImage back;
    if (!resolvedBack.isEmpty() && QFileInfo::exists(resolvedBack)) {
        back.load(resolvedBack);
        if (back.isNull()) {
            if (errOut) *errOut = tr("Failed to load back image: %1").arg(resolvedBack);
            // fallback: use front as back
            back = front;
        }
    } else {
        back = front;
    }

    // Normalize sizes so face swaps don’t “jump”
    if (back.size() != front.size()) {
        back = back.scaled(front.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    frontOut = std::move(front);
    backOut  = std::move(back);
    return true;
}

// Pick which face to use at a given angle (works for both yaw-spin and pitch-flip).
const QImage &MainWindow::pickFaceForAngle(const QImage &front,
                                           const QImage &back,
                                           qreal angleDeg) const
{
    return isBackVisible(angleDeg) ? back : front;
}

// Helper: Sample texture using bilinear interpolation at spherical coordinates
// NOW with front/back hemisphere support
QRgb MainWindow::sampleTexture(const QImage &frontTexture,
                               const QImage &backTexture,
                               qreal lon,
                               qreal lat,
                               const QColor &fallbackColor)
{
    // Determine which hemisphere we're in
    const bool useFront = (lon >= -M_PI/2.0 && lon <= M_PI/2.0);
    const QImage &texture = useFront ? frontTexture :
                            (!backTexture.isNull() ? backTexture : frontTexture);

    // If no texture available, use fallback color
    if (texture.isNull()) return fallbackColor.rgba();

    // Adjust longitude for back texture (remap to its own 0-width range)
    qreal adjustedLon = lon;
    if (!useFront && !backTexture.isNull()) {
        adjustedLon = lon - M_PI;
        if (adjustedLon < -M_PI) adjustedLon += 2.0 * M_PI;
    }

    // Convert lon/lat to texture coordinates
    const qreal u = (adjustedLon + M_PI) / (2.0 * M_PI);  // 0 to 1
    const qreal v = (lat + M_PI/2.0) / M_PI;              // 0 to 1

    // Map to pixel coordinates
    const qreal x = u * (texture.width() - 1);
    const qreal y = v * (texture.height() - 1);

    // Bilinear interpolation
    const int x0 = qFloor(x);
    const int y0 = qFloor(y);
    const int x1 = qMin(x0 + 1, texture.width() - 1);
    const int y1 = qMin(y0 + 1, texture.height() - 1);

    const qreal fx = x - x0;
    const qreal fy = y - y0;

    const QRgb c00 = texture.pixel(x0, y0);
    const QRgb c10 = texture.pixel(x1, y0);
    const QRgb c01 = texture.pixel(x0, y1);
    const QRgb c11 = texture.pixel(x1, y1);

    // Check if this pixel has meaningful content (not just padding)
    // If the texture has alpha, respect it; otherwise check if it's the fallback color
    const int a00 = qAlpha(c00);
    const int a10 = qAlpha(c10);
    const int a01 = qAlpha(c01);
    const int a11 = qAlpha(c11);

    // If all corners are fully transparent, use fallback
    if (texture.hasAlphaChannel() && a00 == 0 && a10 == 0 && a01 == 0 && a11 == 0) {
        return fallbackColor.rgba();
    }

    // Interpolate each channel
    const int r = qRound(
        qRed(c00) * (1-fx) * (1-fy) +
        qRed(c10) * fx * (1-fy) +
        qRed(c01) * (1-fx) * fy +
        qRed(c11) * fx * fy
    );
    const int g = qRound(
        qGreen(c00) * (1-fx) * (1-fy) +
        qGreen(c10) * fx * (1-fy) +
        qGreen(c01) * (1-fx) * fy +
        qGreen(c11) * fx * fy
    );
    const int b = qRound(
        qBlue(c00) * (1-fx) * (1-fy) +
        qBlue(c10) * fx * (1-fy) +
        qBlue(c01) * (1-fx) * fy +
        qBlue(c11) * fx * fy
    );
    const int a = qRound(
        a00 * (1-fx) * (1-fy) +
        a10 * fx * (1-fy) +
        a01 * (1-fx) * fy +
        a11 * fx * fy
    );

    return qRgba(r, g, b, a);
}

// Main globe generation function with backside and rotation axis support
QImage MainWindow::renderGlobeFrame(const QImage &frontTexture,
                                   const QImage &backTexture,
                                   qreal rotationDegrees,
                                   int sizePx,
                                   const QColor &frameBg,
                                   const QColor &globeSurfaceColor,
                                   bool enableLighting,
                                   int rotationAxis)
{
    QImage frame(sizePx, sizePx, QImage::Format_ARGB32_Premultiplied);
    frame.fill(frameBg);  // Fill canvas with transparent (or chosen frame color)

    if (frontTexture.isNull()) return frame;

    const qreal radius = sizePx / 2.0;
    const qreal centerX = sizePx / 2.0;
    const qreal centerY = sizePx / 2.0;

    const qreal rotRad = qDegreesToRadians(rotationDegrees);

    // Render sphere using orthographic projection
    for (int py = 0; py < sizePx; ++py) {
        for (int px = 0; px < sizePx; ++px) {
            const qreal x = px - centerX;
            const qreal y = py - centerY;

            const qreal distSq = x*x + y*y;
            if (distSq > radius*radius) continue;

            const qreal z = qSqrt(radius*radius - distSq);

            qreal nx = x / radius;
            qreal ny = y / radius;
            qreal nz = z / radius;

            // Apply rotation based on axis (your existing rotation code stays the same)
            if (rotationAxis == 0) {
                const qreal cosR = qCos(rotRad);
                const qreal sinR = qSin(rotRad);
                const qreal newNx = nx * cosR - nz * sinR;
                const qreal newNz = nx * sinR + nz * cosR;
                nx = newNx;
                nz = newNz;
            }
            else if (rotationAxis == 1) {
                const qreal cosR = qCos(rotRad);
                const qreal sinR = qSin(rotRad);
                const qreal newNy = ny * cosR - nz * sinR;
                const qreal newNz = ny * sinR + nz * cosR;
                ny = newNy;
                nz = newNz;
            }
            else if (rotationAxis == 2) {
                const qreal cosR = qCos(rotRad);
                const qreal sinR = qSin(rotRad);
                qreal newNx = nx * cosR - nz * sinR;
                qreal newNz = nx * sinR + nz * cosR;

                const qreal cosR2 = qCos(rotRad * 0.5);
                const qreal sinR2 = qSin(rotRad * 0.5);
                const qreal newNy = ny * cosR2 - newNz * sinR2;
                newNz = ny * sinR2 + newNz * cosR2;

                nx = newNx;
                ny = newNy;
                nz = newNz;
            }

            const qreal lat = qAsin(ny);
            const qreal lon = qAtan2(nx, nz);

            // Sample texture - use globeSurfaceColor for areas without texture

        QRgb texColor = sampleTexture(frontTexture, backTexture, lon, lat, globeSurfaceColor);

        // Apply lighting (preserve alpha channel!)
        if (enableLighting) {
            // Old (caused dull/grey back):
            // const qreal lightDot = nz;
            // const qreal brightness = qBound(0.3, lightDot, 1.0);

            // New: symmetric + slightly brighter floor so the back isn't greyed out
            const qreal lightDot = nz;
            const qreal brightness = qBound(0.75, qAbs(lightDot), 1.0);

            const int r = qRound(qRed(texColor)   * brightness);
            const int g = qRound(qGreen(texColor) * brightness);
            const int b = qRound(qBlue(texColor)  * brightness);
            const int a = qAlpha(texColor); // PRESERVE alpha
            texColor = qRgba(r, g, b, a);
        }

        frame.setPixel(px, py, texColor);
        }
    }

    return frame;
}

// Simple L/R spin (yaw) with backside support.
// Arguments you likely already pass in elsewhere:
//  - frontPath, backPath: user-chosen paths (back may be empty)
//  - outDir: where to write frames/GIF (this uses writeFrames on framesDir)
//  - fps, durationSec: timing
//  - maxAngleDeg: half-rotation amplitude (e.g., 180 for full spin, 90 for sway)
//  - cycles: how many full cycles to perform
bool MainWindow::generateSpinGif(const QString &frontPath,
                                 const QString &backPath,
                                 const QString &outDir,
                                 int fps,
                                 int durationSec,
                                 qreal maxAngleDeg,
                                 int cycles,
                                 QString *errOut)
{
    QImage front, back;
    if (!resolveFrontBackImages(frontPath, backPath, front, back, errOut))
        return false;

    const int totalFrames = qMax(1, fps * durationSec);
    QList<QImage> frames;
    frames.reserve(totalFrames);

    // Base canvas: keep output size consistent
    const QSize canvasSize = front.size();

    // We'll sweep angle over cycles using a sine wave for smooth looping
    // t in [0,1) -> angle = sin(t * 2π * cycles) * maxAngleDeg
    for (int i = 0; i < totalFrames; ++i) {
        const qreal t = static_cast<qreal>(i) / totalFrames;
        const qreal angle = std::sin(t * 2.0 * M_PI * cycles) * maxAngleDeg;

        // Choose which face to show at this yaw
        const QImage &src = pickFaceForAngle(front, back, angle);

        // Horizontal squash to fake perspective: scale factor ~ |cos(angle)|
        const qreal cosA = std::abs(std::cos(qDegreesToRadians(angle)));
        const int w = qMax(1, int(canvasSize.width() * cosA));
        const int h = canvasSize.height();

        QImage frame(canvasSize, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);

        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // Optionally add a subtle vertical scale for depth feeling
        const qreal yScale = 0.98 + 0.02 * cosA; // ~1.0 at face-on, ~0.98 at edge-on
        const int targetH = int(h * yScale);

        // Center the squashed face on canvas
        QRect target((canvasSize.width() - w) / 2,
                     (canvasSize.height() - targetH) / 2,
                     w, targetH);

        p.drawImage(target, src);
        p.end();

        frames.push_back(std::move(frame));
    }

    // Write frames (your existing helper)
    const QString framesDir = outDir; // If you use a subdir, adjust here
    if (!writeFrames(frames, framesDir, errOut)) {
        if (errOut && errOut->isEmpty())
            *errOut = tr("Failed writing spin frames.");
        return false;
    }
    return true;
}

// U/D flip (pitch) with backside support.
// Similar parameters to generateSpinGif; flip amplitude is maxAngleDeg (e.g., 180 for full flips).
bool MainWindow::generateFlipGif(const QString &frontPath,
                                 const QString &backPath,
                                 const QString &outDir,
                                 int fps,
                                 int durationSec,
                                 qreal maxAngleDeg,
                                 int cycles,
                                 QString *errOut)
{
    QImage front, back;
    if (!resolveFrontBackImages(frontPath, backPath, front, back, errOut))
        return false;

    const int totalFrames = qMax(1, fps * durationSec);
    QList<QImage> frames;
    frames.reserve(totalFrames);

    const QSize canvasSize = front.size();

    for (int i = 0; i < totalFrames; ++i) {
        const qreal t = static_cast<qreal>(i) / totalFrames;
        const qreal angle = std::sin(t * 2.0 * M_PI * cycles) * maxAngleDeg;

        // Choose which face to show at this pitch
        const QImage &src = pickFaceForAngle(front, back, angle);

        // Vertical squash ~ |cos(angle)|
        const qreal cosA = std::abs(std::cos(qDegreesToRadians(angle)));
        const int w = canvasSize.width();
        const int h = qMax(1, int(canvasSize.height() * cosA));

        QImage frame(canvasSize, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);

        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // Optional slight x-scale for depth feeling
        const qreal xScale = 0.98 + 0.02 * cosA;
        const int targetW = int(w * xScale);

        QRect target((canvasSize.width() - targetW) / 2,
                     (canvasSize.height() - h) / 2,
                     targetW, h);

        p.drawImage(target, src);
        p.end();

        frames.push_back(std::move(frame));
    }

    const QString framesDir = outDir;
    if (!writeFrames(frames, framesDir, errOut)) {
        if (errOut && errOut->isEmpty())
            *errOut = tr("Failed writing flip frames.");
        return false;
    }
    return true;
}

// Call this once in your ctor AFTER ui->setupUi(this);
void MainWindow::setupBackfaceRadiosSimple()
{
    QRadioButton *rNormal = this->findChild<QRadioButton*>("radioSimBackface");
    QRadioButton *rUpside = this->findChild<QRadioButton*>("radioSimBackfaceUpsideDown");
    if (!rNormal && !rUpside) return;

    // Let each radio be independently toggleable
    if (rNormal) rNormal->setAutoExclusive(false);
    if (rUpside) rUpside->setAutoExclusive(false);

    // Restore last saved mode (0=off, 1=normal, 2=upside)
    {
        QSettings s("MyCompany", "GifMaker");
        const int mode = s.value("backfaceMode", 0).toInt();
        if (mode == 1 && rNormal) rNormal->setChecked(true);
        else if (mode == 2 && rUpside) rUpside->setChecked(true);
        // mode 0: leave both unchecked
    }

    // When one turns ON, turn the other OFF and save the mode
    if (rNormal) {
        connect(rNormal, &QRadioButton::toggled, this, [this, rUpside](bool on){
            if (on && rUpside) rUpside->setChecked(false);
            QSettings s("MyCompany", "GifMaker");
            s.setValue("backfaceMode", on ? 1 : (rUpside && rUpside->isChecked() ? 2 : 0));
            if (on) appendLog(tr("Backside mode: Normal"));
            else if (!(rUpside && rUpside->isChecked())) appendLog(tr("Backside mode: OFF"));
        });
    }
    if (rUpside) {
        connect(rUpside, &QRadioButton::toggled, this, [this, rNormal](bool on){
            if (on && rNormal) rNormal->setChecked(false);
            QSettings s("MyCompany", "GifMaker");
            s.setValue("backfaceMode", on ? 2 : (rNormal && rNormal->isChecked() ? 1 : 0));
            if (on) appendLog(tr("Backside mode: Upside-down"));
            else if (!(rNormal && rNormal->isChecked())) appendLog(tr("Backside mode: OFF"));
        });
    }
}



// === Simulated backside support for GIFStew ===
// Drop-in replacement: do NOT auto-create any status-bar widgets.
// Only read the in-settings satellite button or saved setting.
// Read the radio-style control from the UI (no auto-created widgets)
// Use radios if present; otherwise fall back to saved mode
bool MainWindow::simulateBacksideEnabled() const
{
    if (auto r = this->findChild<QRadioButton*>("radioSimBackface"))
        if (r->isChecked()) return true;
    if (auto r = this->findChild<QRadioButton*>("radioSimBackfaceUpsideDown"))
        if (r->isChecked()) return true;

    QSettings s("MyCompany", "GifMaker");
    return s.value("backfaceMode", 0).toInt() != 0; // 0=off
}

// Auto-connected because it matches on_<objectName>_toggled(bool)
void MainWindow::on_radioSimBackface_toggled(bool checked)
{
    QSettings s("MyCompany", "GifMaker");
    s.setValue("simulateBackside", checked);
    appendLog(checked ? tr("Backside (inverted) enabled") : tr("Backside (inverted) disabled"));
}


QImage MainWindow::makeBacksideFrom(const QImage &front, bool *ok, QString *errOut)
{
    if (ok) *ok = false;
    if (front.isNull()) {
        if (errOut) *errOut = QStringLiteral("Front image is empty.");
        return {};
    }

    // Mirror horizontally (like viewing the back of a sticker)
    QImage base = front.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage mirrored = base.mirrored(true, false);

    // Subtle desaturation + slight darken
    const qreal sat = 0.85;  // reduce colorfulness a bit
    const qreal dim = 0.90;  // darken a touch

    for (int y = 0; y < mirrored.height(); ++y) {
        QRgb *scan = reinterpret_cast<QRgb*>(mirrored.scanLine(y));
        for (int x = 0; x < mirrored.width(); ++x) {
            const QRgb px = scan[x];
            const int a = qAlpha(px);
            int r = qRed(px), g = qGreen(px), b = qBlue(px);

            const int gray = int(0.299 * r + 0.587 * g + 0.114 * b);
            r = int(gray + (r - gray) * sat);
            g = int(gray + (g - gray) * sat);
            b = int(gray + (b - gray) * sat);

            r = int(r * dim); if (r > 255) r = 255;
            g = int(g * dim); if (g > 255) g = 255;
            b = int(b * dim); if (b > 255) b = 255;

            scan[x] = qRgba(r, g, b, a);
        }
    }

    if (ok) *ok = true;
    return mirrored;
}

QString MainWindow::ensureBackImageForRun(const QString &frontPath,
                                          const QString &explicitBackPath,
                                          bool simulateBack,
                                          QString *errOut)
{
    // If user supplied a back image and we’re not forcing simulation, use it
    if (!explicitBackPath.trimmed().isEmpty() && !simulateBack) {
        if (!QFileInfo::exists(explicitBackPath)) {
            if (errOut) *errOut = QStringLiteral("Back image does not exist: %1").arg(explicitBackPath);
            return {};
        }
        return explicitBackPath;
    }

    // Simulate if requested, or if no explicit back provided (your choice)
    if (!simulateBack && explicitBackPath.trimmed().isEmpty()) {
        return {}; // single-sided
    }

    if (frontPath.trimmed().isEmpty() || !QFileInfo::exists(frontPath)) {
        if (errOut) *errOut = QStringLiteral("Front image not set or missing; cannot simulate backside.");
        return {};
    }

    QImage front(frontPath);
    bool ok = false;
    QString mkErr;
    QImage back = makeBacksideFrom(front, &ok, &mkErr);
    if (!ok || back.isNull()) {
        if (errOut) *errOut = mkErr.isEmpty() ? QStringLiteral("Failed to generate backside.") : mkErr;
        return {};
    }

    // Save to temp file so downstream code can just open a path
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempDir);
    const QString outPath = QDir(tempDir).filePath(QStringLiteral("gifstew_back_sim.png"));
    if (!back.save(outPath, "PNG")) {
        if (errOut) *errOut = QStringLiteral("Could not save simulated backside to: %1").arg(outPath);
        return {};
    }
    return outPath;
}

void MainWindow::setupBackfaceRadios()
{
    // your radio names (use whichever you actually have)
    QRadioButton *rNormal = this->findChild<QRadioButton*>("radioSimBackface");
    QRadioButton *rUpside = this->findChild<QRadioButton*>("radioSimBackfaceFlip");
    if (!rUpside) rUpside = this->findChild<QRadioButton*>("radioSimBackfaceUpsideDown");

    if (!rNormal && !rUpside) return; // nothing to wire

    // create a group to keep them mutually exclusive
    auto group = this->findChild<QButtonGroup*>("grpBackfaceMode");
    if (!group) {
        group = new QButtonGroup(this);
        group->setObjectName("grpBackfaceMode");
        group->setExclusive(true);
    } else {
        // clear old membership just in case
        for (auto b : group->buttons()) group->removeButton(b);
    }

    if (rNormal) group->addButton(rNormal, 1);  // 1 = normal backside
    if (rUpside) group->addButton(rUpside, 2);  // 2 = upside-down backside

    // restore saved state (0=off, 1=normal, 2=upside)
    {
        QSettings s("MyCompany", "GifMaker");
        const int mode = s.value("backfaceMode", 0).toInt();
        if      (mode == 1 && rNormal) rNormal->setChecked(true);
        else if (mode == 2 && rUpside) rUpside->setChecked(true);
        // mode 0 => none checked (leave both unchecked)
    }

    auto hook = [this, group](QRadioButton *rb, int id) {
        if (!rb) return;

        // allow "toggle off" when clicking the already-checked radio
        connect(rb, &QRadioButton::clicked, this, [this, group, rb, id]() {
            if (rb->isChecked()) {
                // clicked while already selected -> unselect everything
                group->setExclusive(false);
                rb->setChecked(false);
                group->setExclusive(true);

                QSettings s("MyCompany", "GifMaker");
                s.setValue("backfaceMode", 0);
                appendLog(tr("Backside mode: OFF"));
            }
            // if it wasn’t checked (i.e., user selected it from the other), do nothing here;
            // the toggled(true) handler below will persist the new mode.
        });

        // persist when it becomes checked
        connect(rb, &QRadioButton::toggled, this, [this, id](bool on) {
            if (!on) return;
            QSettings s("MyCompany", "GifMaker");
            s.setValue("backfaceMode", id);
            appendLog(id == 1 ? tr("Backside mode: Normal") : tr("Backside mode: Upside-down"));
        });
    };

    hook(rNormal, 1);
    hook(rUpside, 2);
}

// --- Main worker: spin (full 360°) over 'durationSec' at 'fps', output GIF.
//    - srcImagePath: input still image
//    - outGifPath:   absolute path to animated GIF
//    - fps:          frames per second (e.g. 12, 24)
//    - durationSec:  total duration (e.g. 2 → 2 seconds)
//    - sizePx:       canvas size (square); image keeps aspect and is centered
//    - bg:           background color (use Qt::transparent for alpha, or a solid color)
// Returns true on success; on failure returns false and sets *errOut.
bool MainWindow::generateSpinGif(const QString &srcImagePath,
                                 const QString &outGifPath,
                                 int fps,
                                 int durationSec,
                                 int sizePx,
                                 const QColor &bg,
                                 QString *errOut)
{
    if (!QFileInfo::exists(srcImagePath)) { if (errOut) *errOut="Source image does not exist."; return false; }
    if (fps <= 0 || durationSec <= 0)     { if (errOut) *errOut="FPS and duration must be > 0."; return false; }

    const QString magick = findImageMagick();
    if (magick.isEmpty()) { if (errOut) *errOut="ImageMagick not found (sudo apt install imagemagick)."; return false; }

    // Resolve/auto-simulate the back image if toggle is on or no explicit back provided
    const QString userBackPath = ui->editBackPath ? ui->editBackPath->text().trimmed() : QString();
    QString simErr;
    const bool wantSim = simulateBacksideEnabled();
    const QString resolvedBack = ensureBackImageForRun(srcImagePath, userBackPath, wantSim, &simErr);
    if (wantSim && resolvedBack.isEmpty() && !simErr.isEmpty()) {
        appendLog(QStringLiteral("Backside simulation failed: %1").arg(simErr));
        // continue single-sided if we must
    }

    QImage src(srcImagePath);
    if (src.isNull()) { if (errOut) *errOut="Failed to load source image."; return false; }

    QImage back;
    const bool haveBack = !resolvedBack.isEmpty()
                       && QFileInfo::exists(resolvedBack)
                       && back.load(resolvedBack);

    if (sizePx < 32) sizePx = qMax(32, sizePx);
    const QImage frontBase = makeSquareCanvas(src,  sizePx, bg);
    const QImage backBase  = haveBack ? makeSquareCanvas(back, sizePx, bg) : frontBase;

    const QSize canvasSize = frontBase.size();
    const int totalFrames = fps * durationSec;
    QList<QImage> frames; frames.reserve(totalFrames);

    // Yaw spin: we sweep 0..360 degrees. When cos < 0, show the backside.
    // Horizontal scale ~ |cos| with an epsilon so it never vanishes.
    const qreal eps = 0.08;

    for (int i=0;i<totalFrames;++i){
        const qreal t   = (qreal)i / (qreal)totalFrames;
        const qreal deg = 360.0 * t;
        const qreal rad = qDegreesToRadians(deg);
        const qreal c   = qCos(rad);

        const bool showBack = (c < 0.0);
        const qreal sxAbs   = (1.0 - eps) * std::abs(c) + eps; // width “thickness”

        const QImage &face = showBack ? backBase : frontBase;

        QImage frame(canvasSize, QImage::Format_ARGB32_Premultiplied);
        frame.fill(bg);
        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform,true);

        // Optional slight vertical scale for depth feel near edge-on
        const qreal yScale = 0.98 + 0.02 * std::abs(c);
        const int targetW  = qMax(1, int(canvasSize.width() * sxAbs));
        const int targetH  = qMax(1, int(canvasSize.height() * yScale));

        const QRect target((canvasSize.width()  - targetW)/2,
                           (canvasSize.height() - targetH)/2,
                           targetW, targetH);

        p.drawImage(target, face);
        p.end();

        frames.push_back(std::move(frame));
    }

    QTemporaryDir tmp("gif_spin_XXXXXX");
    if (!tmp.isValid()){ if (errOut) *errOut="Could not create temp directory."; return false; }
    const QString framesDir = QDir(tmp.path()).filePath("frames");
    if (!writeFrames(frames, framesDir, errOut)) return false;
    return assembleGif(magick, framesDir, fps, outGifPath, true, errOut);
}

// --- Variation: rotate back-and-forth (oscillate) by +/-maxDegrees
bool MainWindow::generateOscillateGif(const QString &srcImagePath,
                                      const QString &outGifPath,
                                      int fps,
                                      int durationSec,
                                      int sizePx,
                                      qreal maxDegrees,
                                      const QColor &bg,
                                      QString *errOut)
{
    if (!QFileInfo::exists(srcImagePath)) { if (errOut) *errOut="Source image does not exist."; return false; }
    if (fps <= 0 || durationSec <= 0)     { if (errOut) *errOut="FPS and duration must be > 0."; return false; }

    const QString magick = findImageMagick();
    if (magick.isEmpty()) { if (errOut) *errOut="ImageMagick not found (sudo apt install imagemagick)."; return false; }

    QImage src(srcImagePath); if (src.isNull()) { if (errOut) *errOut="Failed to load source image."; return false; }

    const int totalFrames = fps * durationSec;
    const QImage base = makeSquareCanvas(src, qMax(32,sizePx), bg);

    const QPointF center(base.width()/2.0, base.height()/2.0);
    const QRectF  dst(0.0, 0.0, base.width(), base.height());

    QList<QImage> frames; frames.reserve(totalFrames);

    for (int i=0;i<totalFrames;++i){
        const qreal t   = (qreal)i / (qreal)totalFrames;
        const qreal deg = maxDegrees * qSin(2.0*M_PI*t);

        QImage frame(base.size(), QImage::Format_ARGB32_Premultiplied);
        frame.fill(bg);
        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform,true);

        QTransform tr;                     // ← clean start
        tr.translate(center.x(), center.y());
        tr.rotate(deg);                    // ← only Z rotation, no shear
        tr.translate(-center.x(), -center.y());
        p.setTransform(tr);

        p.drawImage(dst, base, base.rect());
        p.end();
        frames.push_back(std::move(frame));
    }

    QTemporaryDir tmp("gif_osc_XXXXXX"); if (!tmp.isValid()){ if (errOut) *errOut="Could not create temp directory."; return false; }
    const QString framesDir = QDir(tmp.path()).filePath("frames");
    if (!writeFrames(frames, framesDir, errOut)) return false;
    return assembleGif(magick, framesDir, fps, outGifPath, true, errOut);
}

bool MainWindow::generateYawSpinGif(const QString &frontImagePath,
                                    const QString &outGifPath,
                                    int fps,
                                    int durationSec,
                                    int sizePx,
                                    qreal rotations,          // interpreted as # of full rotations
                                    const QColor &bg,
                                    QString *errOut)
{
    const QString backImagePath = ui->editBackPath ? ui->editBackPath->text().trimmed() : QString();
    const bool cropContent      = ui->checkCropToContent && ui->checkCropToContent->isChecked();

    // Validate
    if (!QFileInfo::exists(frontImagePath)) { if (errOut) *errOut = "Front image does not exist."; return false; }
    if (fps <= 0 || durationSec <= 0)       { if (errOut) *errOut = "FPS and duration must be > 0."; return false; }
    if (sizePx < 32) sizePx = 256;
    if (rotations < 0) rotations = 0;

    const QString magick = findImageMagick();
    if (magick.isEmpty()) { if (errOut) *errOut = "ImageMagick not found (install imagemagick)."; return false; }

    // Load
    QImage front(frontImagePath);
    if (front.isNull()) { if (errOut) *errOut = "Failed to load front image."; return false; }

    QImage back;
    const bool haveBack = !backImagePath.isEmpty() && QFileInfo::exists(backImagePath) && back.load(backImagePath);

    // Optional crop-to-content
    if (cropContent) {
        front = cropToContentSmart(front);
        if (haveBack) back = cropToContentSmart(back);
    }

    // Square-pad to desired size with bg, keeping aspect
    const QImage frontBase = makeSquareCanvas(front, sizePx, bg);
    const QImage backBase  = haveBack ? makeSquareCanvas(back,  sizePx, bg) : frontBase;

    const int totalFrames = fps * durationSec;
    if (totalFrames < 1) { if (errOut) *errOut = "Total frames computed < 1."; return false; }

    const QPointF center(frontBase.width()/2.0, frontBase.height()/2.0);
    const QRectF  dstRect(0.0, 0.0, frontBase.width(), frontBase.height());

    QList<QImage> frames; frames.reserve(totalFrames);
    const qreal eps = 0.08;

    for (int i = 0; i < totalFrames; ++i) {
        const qreal t      = (qreal)i / (qreal)totalFrames;
        const qreal phiDeg = rotations * 360.0 * t;
        const qreal c      = qCos(qDegreesToRadians(phiDeg));

        const bool showBack = (c < 0.0);
        const qreal sxAbs   = (1.0 - eps) * std::abs(c) + eps;
        const QImage &face  = showBack ? backBase : frontBase;

        QImage frame(frontBase.size(), QImage::Format_ARGB32_Premultiplied);
        frame.fill(bg);

        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        QTransform tr;
        tr.translate(center.x(), center.y());
        tr.scale(sxAbs, 1.0);   // no shear → no tilt
        tr.translate(-center.x(), -center.y());
        p.setTransform(tr);

        p.drawImage(dstRect, face, face.rect());
        p.end();

        frames.push_back(std::move(frame));
    }

    QTemporaryDir tmp("gif_yaw_XXXXXX");
    if (!tmp.isValid()) { if (errOut) *errOut = "Could not create temp directory."; return false; }
    const QString framesDir = QDir(tmp.path()).filePath("frames");
    if (!writeFrames(frames, framesDir, errOut)) return false;
    return assembleGif(magick, framesDir, fps, outGifPath, true, errOut);
}

bool MainWindow::generateFlipGif(const QString &frontImagePath,
                                 const QString &outGifPath,
                                 int fps,
                                 int durationSec,
                                 int sizePx,
                                 bool animate,
                                 int cycles,
                                 const QColor &bg,
                                 QString *errOut)
{
    const QString userBackPath = ui->editBackPath ? ui->editBackPath->text().trimmed() : QString();

    if (!QFileInfo::exists(frontImagePath)) { if (errOut) *errOut="Front image does not exist."; return false; }
    if (fps <= 0 || durationSec <= 0)       { if (errOut) *errOut="FPS and duration must be > 0."; return false; }
    if (sizePx < 32) sizePx = 256;

    const QString magick = findImageMagick();
    if (magick.isEmpty()) { if (errOut) *errOut="ImageMagick not found (install imagemagick)."; return false; }

    // Resolve/auto-simulate the back image when toggle is ON or missing explicit back
    QString simErr;
    const bool wantSim = simulateBacksideEnabled();
    const QString resolvedBack = ensureBackImageForRun(frontImagePath, userBackPath, wantSim, &simErr);
    if (wantSim && resolvedBack.isEmpty() && !simErr.isEmpty()) {
        appendLog(QStringLiteral("Backside simulation failed: %1").arg(simErr));
        // continue single-sided if we must
    }

    QImage front(frontImagePath);
    if (front.isNull()) { if (errOut) *errOut="Failed to load front image."; return false; }

    QImage back;
    const bool haveBack = !resolvedBack.isEmpty()
                          && QFileInfo::exists(resolvedBack)
                          && back.load(resolvedBack);

    // Square canvases
    const QImage frontBase = makeSquareCanvas(front, sizePx, bg);
    const QImage backBase  = haveBack ? makeSquareCanvas(back,  sizePx, bg) : frontBase;

    // Should the *flip* show the backside upside down?
    const auto upsideDownSelected = [this]() -> bool {
        if (auto r = this->findChild<QRadioButton*>("radioSimBackfaceUpsideDown"))
            return r->isChecked();
        QSettings s("MyCompany", "GifMaker");
        return s.value("backfaceMode", 0).toInt() == 2; // 2 = upside-down
    };
    const QImage backForFlip = upsideDownSelected() ? backBase.mirrored(true, true) : backBase;

    const QPointF center(frontBase.width()/2.0, frontBase.height()/2.0);
    const QRectF  dst(0.0, 0.0, frontBase.width(), frontBase.height());

    // Non-animated “flip”: just show back (respecting upside-down if selected), else front
    if (!animate) {
        const QImage &face = haveBack ? backForFlip : frontBase;

        QImage frame(frontBase.size(), QImage::Format_ARGB32_Premultiplied);
        frame.fill(bg);
        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform,true);
        p.drawImage(dst, face, face.rect());
        p.end();

        QList<QImage> frames; frames.push_back(std::move(frame));
        QTemporaryDir tmp("gif_flip_XXXXXX");
        if (!tmp.isValid()){ if (errOut) *errOut="Could not create temp directory."; return false; }
        const QString framesDir = QDir(tmp.path()).filePath("frames");
        if (!writeFrames(frames, framesDir, errOut)) return false;
        return assembleGif(magick, framesDir, fps, outGifPath, true, errOut);
    }

    // Animated flip with backside: vertical thickness + swap face when cos < 0
    const int totalFrames = fps * durationSec;
    if (totalFrames < 1) { if (errOut) *errOut = "Total frames computed < 1."; return false; }

    const qreal eps = 0.08; // thickness floor so it never vanishes
    QList<QImage> frames; frames.reserve(totalFrames);

    for (int i=0;i<totalFrames;++i){
        const qreal t   = (qreal)i / (qreal)totalFrames;
        const qreal phi = 2.0 * M_PI * qMax(1, cycles) * t;

        const qreal c   = qCos(phi);
        const bool showBack = (c < 0.0);
        const qreal syAbs  = (1.0 - eps) * std::abs(c) + eps; // vertical “thickness”
        const QImage &face = showBack ? backForFlip : frontBase;

        QImage frame(frontBase.size(), QImage::Format_ARGB32_Premultiplied);
        frame.fill(bg);
        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform,true);

        QTransform tr;
        tr.translate(center.x(), center.y());
        tr.scale(1.0, syAbs); // NO SHEAR → no tilt
        tr.translate(-center.x(), -center.y());
        p.setTransform(tr);

        p.drawImage(dst, face, face.rect());
        p.end();
        frames.push_back(std::move(frame));
    }

    QTemporaryDir tmp("gif_flip_XXXXXX");
    if (!tmp.isValid()){ if (errOut) *errOut="Could not create temp directory."; return false; }
    const QString framesDir = QDir(tmp.path()).filePath("frames");
    if (!writeFrames(frames, framesDir, errOut)) return false;
    return assembleGif(magick, framesDir, fps, outGifPath, true, errOut);
}

bool MainWindow::generateCompositeGif(const QString &frontImagePath,
                                      const QString &outGifPath,
                                      int fps,
                                      int durationSec,
                                      int sizePx,
                                      bool useZSpin,
                                      qreal zDegPerSec,
                                      bool useYaw,
                                      qreal maxYawRotations,   // your UI uses rotations count here
                                      bool useFlip,
                                      bool flipAnimate,
                                      int flipCycles,
                                      const QColor &bg,
                                      QString *errOut)
{
    const QString userBackPath = ui->editBackPath ? ui->editBackPath->text().trimmed() : QString();
    const bool cropContent      = ui->checkCropToContent && ui->checkCropToContent->isChecked();

    if (!QFileInfo::exists(frontImagePath)) { if (errOut) *errOut="Front image does not exist."; return false; }
    if (fps <= 0 || durationSec <= 0)       { if (errOut) *errOut="FPS and duration must be > 0."; return false; }
    if (sizePx < 32) sizePx = 256;

    const QString magick = findImageMagick();
    if (magick.isEmpty()) { if (errOut) *errOut="ImageMagick not found (install imagemagick)."; return false; }

    // Load
    QImage front(frontImagePath);
    if (front.isNull()) { if (errOut) *errOut="Failed to load front image."; return false; }

    // Resolve/auto-simulate backside
    QString simErr;
    const bool wantSim = simulateBacksideEnabled();
    const QString resolvedBack = ensureBackImageForRun(frontImagePath, userBackPath, wantSim, &simErr);
    if (wantSim && resolvedBack.isEmpty() && !simErr.isEmpty()) {
        appendLog(QStringLiteral("Backside simulation failed: %1").arg(simErr));
        // continue single-sided if needed
    }

    QImage back;
    const bool haveBack = !resolvedBack.isEmpty()
                          && QFileInfo::exists(resolvedBack)
                          && back.load(resolvedBack);

    // Optional crop-to-content
    if (cropContent) {
        front = cropToContentSmart(front);
        if (haveBack) back = cropToContentSmart(back);
    }

    // Canvases
    QImage frontBase = makeSquareCanvas(front, sizePx, bg);
    QImage backBase  = haveBack ? makeSquareCanvas(back,  sizePx, bg) : frontBase;

    // Upside-down only for FLIP backs
    const auto upsideDownSelected = [this]() -> bool {
        if (auto r = this->findChild<QRadioButton*>("radioSimBackfaceUpsideDown"))
            return r->isChecked();
        QSettings s("MyCompany", "GifMaker");
        return s.value("backfaceMode", 0).toInt() == 2; // 2 = upside-down
    };
    const QImage backForFlip = upsideDownSelected() ? backBase.mirrored(true, true) : backBase;

    const QSize  canvasSize = frontBase.size();
    const QPointF center(canvasSize.width()/2.0, canvasSize.height()/2.0);
    const QRectF  dst(0.0, 0.0, canvasSize.width(), canvasSize.height());

    // Frames
    const int totalFrames = fps * durationSec;
    if (totalFrames < 1) { if (errOut) *errOut = "Total frames computed < 1."; return false; }

    const qreal eps = 0.08; // thickness floors
    QList<QImage> frames; frames.reserve(totalFrames);

    for (int i=0;i<totalFrames;++i) {
        const qreal t01 = (qreal)i / (qreal)totalFrames;

        // Z spin angle
        const qreal zDeg = useZSpin ? (zDegPerSec * (t01 * durationSec)) : 0.0;

        // Yaw → which side due to spin?
        bool  yawBack = false;
        qreal sxAbs   = 1.0;
        if (useYaw) {
            const qreal cx = qCos(2.0 * M_PI * qMax<qreal>(0.0, maxYawRotations) * t01);
            yawBack = (cx < 0.0);
            sxAbs   = (1.0 - eps) * std::abs(cx) + eps;   // horizontal “thickness”
        }

        // Flip → which side due to flip?
        bool  flipBack = false;
        qreal syAbs    = 1.0;
        if (useFlip) {
            if (!flipAnimate) {
                flipBack = true; // static “show back”
            } else {
                const qreal phi = 2.0 * M_PI * qMax(1, flipCycles) * t01;
                const qreal cy  = qCos(phi);
                flipBack = (cy < 0.0);
                syAbs    = (1.0 - eps) * std::abs(cy) + eps; // vertical “thickness”
            }
        }

        // Back shown iff exactly one axis says “back”
        const bool showBack = (yawBack ^ flipBack);

        // Choose which back to use based on WHY we’re showing it:
        // - if back is from FLIP -> use upside-down option
        // - if back is from YAW -> use normal back
        const QImage &face = showBack
                           ? ((flipBack && !yawBack) ? backForFlip : backBase)
                           : frontBase;

        QImage frame(canvasSize, QImage::Format_ARGB32_Premultiplied);
        frame.fill(bg);

        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setRenderHint(QPainter::Antialiasing, true);

        QTransform tr;
        tr.translate(center.x(), center.y());
        if (useZSpin) tr.rotate(zDeg);
        tr.scale(sxAbs, syAbs);
        tr.translate(-center.x(), -center.y());
        p.setTransform(tr);

        p.drawImage(dst, face, face.rect());
        p.end();

        frames.push_back(std::move(frame));
    }

    QTemporaryDir tmp("gif_combo_XXXXXX");
    if (!tmp.isValid()) { if (errOut) *errOut="Could not create temp directory."; return false; }
    const QString framesDir = QDir(tmp.path()).filePath("frames");
    if (!writeFrames(frames, framesDir, errOut)) return false;
    return assembleGif(magick, framesDir, fps, outGifPath, true, errOut);
}

// Main globe generation function with backside and rotation axis support
bool MainWindow::generateGlobeGif(const QString &frontImagePath,
                                 const QString &backImagePath,
                                 const QString &outGifPath,
                                 int fps,
                                 int durationSec,
                                 int sizePx,
                                 qreal rotationSpeed,
                                 qreal zoomPercent,
                                 int rotationAxis,
                                 const QColor &globeSurfaceColor,
                                 QString *errOut)
{
    // Validate inputs
    if (frontImagePath.isEmpty()) {
        if (errOut) *errOut = "No front image path provided.";
        return false;
    }
    if (!QFileInfo::exists(frontImagePath)) {
        if (errOut) *errOut = "Front image does not exist.";
        return false;
    }
    if (fps <= 0 || durationSec <= 0) {
        if (errOut) *errOut = "FPS and duration must be > 0.";
        return false;
    }
    if (sizePx < 64) sizePx = 512;

    const QString magick = findImageMagick();
    if (magick.isEmpty()) {
        if (errOut) *errOut = "ImageMagick not found (install imagemagick).";
        return false;
    }

    // Load front texture
    QImage frontTexture(frontImagePath);
    if (frontTexture.isNull()) {
        if (errOut) *errOut = "Failed to load front texture image.";
        return false;
    }
    frontTexture = frontTexture.convertToFormat(QImage::Format_ARGB32);

    // Load back texture (optional)
    QImage backTexture;
    if (!backImagePath.isEmpty() && QFileInfo::exists(backImagePath)) {
        backTexture = QImage(backImagePath);
        if (!backTexture.isNull()) {
            backTexture = backTexture.convertToFormat(QImage::Format_ARGB32);
        }
    }

    // Apply zoom to both textures
    if (qAbs(zoomPercent - 100.0) > 0.1) {
        frontTexture = zoomImage(frontTexture, zoomPercent, globeSurfaceColor);
        if (!backTexture.isNull()) {
            backTexture = zoomImage(backTexture, zoomPercent, globeSurfaceColor);
        }
    }

    // Calculate frames
    const int totalFrames = fps * durationSec;
    if (totalFrames < 1) {
        if (errOut) *errOut = "Total frames computed < 1.";
        return false;
    }

    const qreal degreesPerFrame = (rotationSpeed * 360.0) / totalFrames;

    QList<QImage> frames;
    frames.reserve(totalFrames);

    // Always use transparent background for the frame canvas
    const QColor frameBackground = Qt::transparent;

    // Generate each frame
    for (int i = 0; i < totalFrames; ++i) {
        const qreal rotation = i * degreesPerFrame;
        QImage frame = renderGlobeFrame(frontTexture, backTexture, rotation,
                                       sizePx, frameBackground, globeSurfaceColor,
                                       true, rotationAxis);
        frames.push_back(std::move(frame));
    }

    // Write frames and assemble GIF
    QTemporaryDir tmp("gif_globe_XXXXXX");
    if (!tmp.isValid()) {
        if (errOut) *errOut = "Could not create temp directory.";
        return false;
    }

    const QString framesDir = QDir(tmp.path()).filePath("frames");
    if (!writeFrames(frames, framesDir, errOut)) return false;
    return assembleGif(magick, framesDir, fps, outGifPath, true, errOut);
}

void MainWindow::appendLog(const QString &msg)
{
    if (auto sb = this->statusBar()) {
        sb->showMessage(msg, 5000);
    }
    qDebug().noquote() << "[GIFStew]" << msg;
}


//void MainWindow::onFlipAnimateToggled(bool on)
//{
//    if (ui->spinPitchMax) ui->spinPitchMax->setEnabled(on);
//}

void MainWindow::refreshPreview(const QString &path)
{
    if (!ui->lblPreview) return;

    if (!path.isEmpty() && QFileInfo::exists(path)) {
        m_previewPixmap = QPixmap(path);
    } else {
        m_previewPixmap = QPixmap();
    }

    QPixmap scaled = m_previewPixmap.scaled(
        ui->lblPreview->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    ui->lblPreview->setPixmap(scaled);
}

// Opens a QFileDialog with an image preview panel on the right.
// Returns the chosen file or an empty string if cancelled.
// Add these includes at top of mainwindow.cpp if missing:

QString MainWindow::pickImageWithPreview(const QString &startDir)
{
    QFileDialog dlg(this, tr("Choose Source Image"), startDir);
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setAcceptMode(QFileDialog::AcceptOpen);
    dlg.setNameFilters({
        tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tif *.tiff)"),
        tr("All Files (*)")
    });
    dlg.setOption(QFileDialog::DontUseNativeDialog, true); // use Qt's dialog

    // ---- Proxy style that *disables* single-click activate (forces double-click to accept)
    class NoSingleClickProxyStyle : public QProxyStyle {
    public:
        using QProxyStyle::QProxyStyle;
        int styleHint(StyleHint hint, const QStyleOption *opt = nullptr,
                      const QWidget *widget = nullptr,
                      QStyleHintReturn *ret = nullptr) const override
        {
            if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick)
                return 0; // don't activate on single click
            return QProxyStyle::styleHint(hint, opt, widget, ret);
        }
    };

    // Apply to the dialog itself (good baseline; some views are built lazily)
    dlg.setStyle(new NoSingleClickProxyStyle(dlg.style()));

    // ---- Right-side preview panel
    auto *side = new QWidget(&dlg);
    auto *vbox = new QVBoxLayout(side);
    vbox->setContentsMargins(8, 8, 8, 8);
    vbox->setSpacing(6);

    auto *preview = new QLabel(side);
    preview->setMinimumSize(220, 180);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFrameShape(QFrame::StyledPanel);
    preview->setScaledContents(false); // we scale manually to keep aspect
    preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *info = new QLabel(side);
    info->setAlignment(Qt::AlignCenter);
    info->setWordWrap(true);
    info->setStyleSheet("color: palette(mid);");

    vbox->addWidget(preview, 1);
    vbox->addWidget(info, 0);

    if (auto *grid = qobject_cast<QGridLayout*>(dlg.layout())) {
        grid->addWidget(side, 0, grid->columnCount(), grid->rowCount(), 1);
    }

    // ---- Aspect-aware preview scaler
    class AspectPreview : public QObject {
        QLabel *lbl = nullptr;
        QPixmap px;
    public:
        explicit AspectPreview(QLabel *target, QObject *parent=nullptr)
            : QObject(parent), lbl(target) {}
        void setImage(const QImage &img) {
            if (img.isNull()) { px = QPixmap(); rescale(); return; }
            px = QPixmap::fromImage(img);
            rescale();
        }
        bool eventFilter(QObject *o, QEvent *e) override {
            if (o == lbl && e->type() == QEvent::Resize) rescale();
            return QObject::eventFilter(o, e);
        }
    private:
        void rescale() {
            if (!lbl) return;
            if (px.isNull()) { lbl->setPixmap(QPixmap()); return; }
            lbl->setPixmap(px.scaled(lbl->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    };

    auto *aspect = new AspectPreview(preview, &dlg);
    preview->installEventFilter(aspect);

    // Update preview + info on selection change
    QObject::connect(&dlg, &QFileDialog::currentChanged, &dlg,
        [&](const QString &path){
            if (path.isEmpty()) { aspect->setImage(QImage()); info->clear(); return; }
            QImageReader reader(path);
            reader.setAutoTransform(true); // honor EXIF rotation
            const QImage img = reader.read();
            if (!img.isNull()) {
                aspect->setImage(img);
                info->setText(QString("%1 × %2 • %3")
                              .arg(img.width()).arg(img.height())
                              .arg(QFileInfo(path).fileName()));
            } else {
                aspect->setImage(QImage());
                info->setText(tr("No preview"));
            }
        });

    // ---- Key bit: once internal views exist, apply the proxy style to THEM too.
    auto styleAndFocusViews = [&]() {
        const auto views = dlg.findChildren<QAbstractItemView*>();
        for (auto *v : views) {
            // Force double-click to activate on each view individually
            v->setStyle(new NoSingleClickProxyStyle(v->style()));
            v->setSelectionMode(QAbstractItemView::SingleSelection);
            v->setSelectionBehavior(QAbstractItemView::SelectRows);
            v->setEditTriggers(QAbstractItemView::NoEditTriggers);
        }
        // Focus the main list/tree and select first item so arrow keys work
        if (!views.isEmpty()) {
            auto *v = views.first();
            v->setFocus();
            if (v->model() && v->model()->rowCount() > 0)
                v->setCurrentIndex(v->model()->index(0, 0));
        }
    };

    // Run after the dialog builds its internals, and again when changing dirs
    QTimer::singleShot(0, &dlg, styleAndFocusViews);
    QObject::connect(&dlg, &QFileDialog::directoryEntered, &dlg,
        [&](){ QTimer::singleShot(0, &dlg, styleAndFocusViews); });

    const QStringList files = dlg.selectedFiles();
    if (dlg.exec() == QDialog::Accepted) {
    const QStringList files = dlg.selectedFiles();
    if (!files.isEmpty())
        return files.constFirst();
    }
    return {};
}

void MainWindow::on_btnGenerate_clicked()
{
    // Paths
    const QString src = ui->editImagePath ? ui->editImagePath->text().trimmed() : QString();
    const QString out = ui->editOutputPath ? ui->editOutputPath->text().trimmed() : QString();

    if (src.isEmpty() || !QFileInfo::exists(src)) {
        QMessageBox::warning(this, tr("Missing Image"), tr("Please choose a valid source image."));
        return;
    }
    if (out.isEmpty()) {
        QMessageBox::warning(this, tr("Missing Output"), tr("Please choose an output GIF filename."));
        return;
    }

    // Common params
    const int   fps           = ui->spinFPS      ? ui->spinFPS->value()      : 24;
    const int   sizePx        = ui->spinSizePx   ? ui->spinSizePx->value()   : 512;

    // FRACTIONAL seconds per revolution (allow < 1s/rev)
    const double durationSecF = ui->spinDuration ? qMax(0.10, ui->spinDuration->value()) : 1.0;
    // Some existing generators might still take an int duration; keep it safe (≥1)
    const int    durationSec  = qMax(1, int(std::ceil(durationSecF)));

    QColor bg = Qt::transparent;
    if (ui->comboBackground) {
        const QString c = ui->comboBackground->currentText();
        if      (c.compare("Black", Qt::CaseInsensitive) == 0) bg = Qt::black;
        else if (c.compare("White", Qt::CaseInsensitive) == 0) bg = Qt::white;
        else                                                    bg = Qt::transparent;
    }

    // Direction toggles (we made radios non-exclusive earlier)
    const bool wantZSpin = (ui->radioSpin      && ui->radioSpin->isChecked());
    const bool wantYaw   = (ui->radioYawSpin   && ui->radioYawSpin->isChecked());
    const bool wantFlip  = (ui->radioFlipUD    && ui->radioFlipUD->isChecked());
    const bool wantOsc   = (ui->radioOscillate && ui->radioOscillate->isChecked());
    const bool wantGlobe = (ui->radioGlobe && ui->radioGlobe->isChecked());
    // Mode params
    const qreal yawRotations = ui->spinYawMax   ? qMax<qreal>(0.0, ui->spinYawMax->value()) : 1.0;
    const bool  flipAnimate  = wantFlip; // checkbox removed; animate when Flip is chosen
    const int   flipCycles   = ui->spinYawMax_2 ? qMax(1, (int)ui->spinYawMax_2->value())   : 1;

    // >>> Speed derived from FRACTIONAL seconds per revolution
    const double zDegPerSec = 360.0 / durationSecF;   // e.g., 0.5s/rev => 720 deg/sec (fast!)

    QString err;
    bool ok = false;

    if (wantGlobe) {
        const qreal rotations   = ui->spinGlobeRotations ? ui->spinGlobeRotations->value() : 1.0;
        const qreal zoomPercent = ui->spinGlobeZoom      ? ui->spinGlobeZoom->value()      : 100.0;

        // NEW: resolve the back image for this run (user-provided OR simulated)
        QString simErr;
        const QString userBack  = ui->editBackPath ? ui->editBackPath->text().trimmed() : QString();
        const bool    wantSim   = simulateBacksideEnabled();
        const QString backPath  = ensureBackImageForRun(src, userBack, wantSim, &simErr);
        if (wantSim && backPath.isEmpty() && !simErr.isEmpty()) {
            appendLog(QStringLiteral("Backside simulation failed: %1").arg(simErr));
            // continue single-sided (generateGlobeGif handles empty back path)
        }

        int axis = 0;
        if (ui->comboGlobeAxis) {
            const QString axisText = ui->comboGlobeAxis->currentText();
            if (axisText.contains("Vertical", Qt::CaseInsensitive)) axis = 1;
            else if (axisText.contains("Both", Qt::CaseInsensitive)) axis = 2;
        }

        // bg is used for globe surface color, frame is always transparent
        ok = generateGlobeGif(src, backPath, out, fps, durationSec, sizePx,
                              rotations, zoomPercent, axis, bg, &err);

    } else {
        const int modeCount = int(wantZSpin) + int(wantYaw) + int(wantFlip);

        // Use the composite path for 1+ primary directions so we can honor zDegPerSec precisely.
        if (modeCount >= 1) {
            ok = generateCompositeGif(src, out, fps, durationSec /*int for legacy*/, sizePx,
                                      wantZSpin, zDegPerSec,
                                      wantYaw,   yawRotations,
                                      wantFlip,  flipAnimate, flipCycles,
                                      bg, &err);
        }
        else if (wantOsc) {
            const qreal maxDeg = ui->spinMaxDegrees ? ui->spinMaxDegrees->value() : 15.0;
            ok = generateOscillateGif(src, out, fps, durationSec /*int*/, sizePx, maxDeg, bg, &err);
        }
        else {
            QMessageBox::warning(this, tr("No Mode Selected"),
                                 tr("Choose Spin, Yaw, Flip, Oscillate, or Globe."));
            return;
        }
    }

    if (!ok) {
        QMessageBox::critical(this, tr("GIF Generation Failed"), err);
        return;
    }

    // Show the result in the preview
    showGifInPreview(out);
}

// Browse for source image → fills duration and suggests an output name
void MainWindow::on_btnBrowseImage_clicked()
{
    QString startDir = ui->editImagePath ? ui->editImagePath->text().trimmed() : QString();
    if (startDir.isEmpty()) {
        QSettings s("MyCompany", "GifMaker");
        startDir = s.value("lastImageDir").toString();
        if (startDir.isEmpty()) {
            startDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            if (startDir.isEmpty()) startDir = QDir::homePath();
        }
    } else if (QFileInfo(startDir).isFile()) {
        startDir = QFileInfo(startDir).absolutePath();
    }

    const QString file = pickImageWithPreview(startDir);
    if (file.isEmpty())
        return;

    if (ui->editImagePath)
        ui->editImagePath->setText(file);

    // Suggest an output GIF if empty
    if (ui->editOutputPath && ui->editOutputPath->text().trimmed().isEmpty()) {
        const QFileInfo fi(file);
        const QString candidate =
            QDir(fi.absolutePath()).filePath(fi.completeBaseName() + "_anim.gif");
        ui->editOutputPath->setText(candidate);
    }

    // Refresh the app's own preview area too (if you added refreshPreview earlier)
    refreshPreview(file);

    // Remember folder
    QSettings s("MyCompany", "GifMaker");
    s.setValue("lastImageDir", QFileInfo(file).absolutePath());
}

void MainWindow::on_btnBrowseBack_clicked()
{
    // Prefer starting in whatever is currently typed in the Back path,
    // otherwise fall back to the front image’s folder if available.
    QString startDir;
    if (ui->editBackPath) {
        const QString cur = ui->editBackPath->text().trimmed();
        if (!cur.isEmpty()) {
            const QFileInfo fi(cur);
            startDir = fi.isDir() ? fi.absoluteFilePath()
                                  : fi.absolutePath();
        }
    }
    if (startDir.isEmpty() && ui->editImagePath) {
        const QString cur = ui->editImagePath->text().trimmed();
        if (!cur.isEmpty()) {
            const QFileInfo fi(cur);
            startDir = fi.isDir() ? fi.absoluteFilePath()
                                  : fi.absolutePath();
        }
    }

    // Use the SAME helper your front picker uses (preview + double-click to accept).
    // If your helper takes only a directory, keep this call signature.
    // If it also takes a parent QWidget*, change to pickImageWithPreview(startDir, this);
    const QString fn = pickImageWithPreview(startDir);
    if (fn.isEmpty())
        return;

    if (ui->editBackPath)
        ui->editBackPath->setText(fn);
}

// Browse for output file → fills editOutputPath and enforces .gif
void MainWindow::on_btnBrowseOutput_clicked()
{
    QString startDir;
    const QString src = ui->spinDuration ? ui->spinDuration->text().trimmed() : QString();
    if (!src.isEmpty() && QFileInfo::exists(src))
        startDir = QFileInfo(src).absolutePath();

    if (startDir.isEmpty()) {
        QSettings s("MyCompany", "GifMaker");
        startDir = s.value("lastOutputDir").toString();
    }
    if (startDir.isEmpty())
        startDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (startDir.isEmpty())
        startDir = QDir::homePath();

    QString suggestedName = ui->editOutputPath ? ui->editOutputPath->text().trimmed() : QString();
    if (suggestedName.isEmpty()) {
        QString base = "animation";
        if (!src.isEmpty()) base = QFileInfo(src).completeBaseName() + "_anim";
        suggestedName = QDir(startDir).filePath(base + ".gif");
    }

    const QString file = QFileDialog::getSaveFileName(
        this, tr("Save Animated GIF As"), suggestedName, "GIF Image (*.gif)");
    if (file.isEmpty())
        return;

    QString out = file;
    if (!out.endsWith(".gif", Qt::CaseInsensitive))
        out += ".gif";

    QDir outDir = QFileInfo(out).dir();
    if (!outDir.exists()) outDir.mkpath(".");

    if (ui->editOutputPath)
        ui->editOutputPath->setText(out);

    QSettings s("MyCompany", "GifMaker");
    s.setValue("lastOutputDir", outDir.absolutePath());
}

// Update preview and suggest an output name when the image path text changes
void MainWindow::on_editImagePath_textChanged(const QString &path)
{
    // Suggest output if empty
    if (ui->editOutputPath && ui->editOutputPath->text().trimmed().isEmpty()) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isFile()) {
            const QString candidate =
                QDir(fi.absolutePath()).filePath(fi.completeBaseName() + "_anim.gif");
            ui->editOutputPath->setText(candidate);
        }
    }

    // ✅ Keep preview in sync while typing/pasting paths
    refreshPreview(path);
}

void MainWindow::showGifInPreview(const QString &gifPath)
{
    if (!ui->lblPreview) return;
    if (!QFileInfo::exists(gifPath)) return;

    // Clean up any previous movie
    if (auto *old = ui->lblPreview->movie()) {
        old->stop();
        old->deleteLater();
    }

    // Load GIF
    auto *movie = new QMovie(gifPath, QByteArray(), ui->lblPreview);
    if (!movie->isValid()) { delete movie; return; }

    // Show at ACTUAL SIZE (no scaling)
    ui->lblPreview->setScaledContents(false);                        // <- key line
    ui->lblPreview->setAlignment(Qt::AlignCenter);                   // looks nicer
    ui->lblPreview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Set the movie first
    ui->lblPreview->setMovie(movie);

    // Size the label to the GIF’s natural frame size
    const QSize natural = movie->frameRect().size();
    if (natural.isValid()) {
        ui->lblPreview->setMinimumSize(natural);
        ui->lblPreview->resize(natural);
    }

    // If the first frame size arrives a tick later, update once more
    // (avoid UniqueConnection here since this is a lambda)
    connect(movie, &QMovie::frameChanged, ui->lblPreview, [this, movie](int){
        const QSize s = movie->frameRect().size();
        if (s.isValid()) {
            ui->lblPreview->setMinimumSize(s);
            ui->lblPreview->resize(s);
        }
    });

    movie->start();
}


// Helper: Zoom image
// New behavior:
//   <100%  = shrink image and pad to original size (appears smaller on globe)
//   100%   = unchanged
//   >100%  = crop central region (appears larger on globe)
QImage MainWindow::zoomImage(const QImage &src, qreal zoomPercent, const QColor &padColor)
{
    if (src.isNull()) return src;

    // Treat ~100% as "no change"
    if (qAbs(zoomPercent - 100.0) < 0.1) return src;

    // Safety clamps
    if (zoomPercent <= 0.0) zoomPercent = 1.0;

    if (zoomPercent > 100.0) {
        // ZOOM IN (make features larger on the globe):
        // keep only the central portion of the texture (crop)
        // e.g. 200% -> keep 50% width/height centered
        const qreal keep = 100.0 / zoomPercent;         // fraction of original to keep
        const int newW   = qMax(1, qRound(src.width()  * keep));
        const int newH   = qMax(1, qRound(src.height() * keep));
        const int x      = (src.width()  - newW) / 2;
        const int y      = (src.height() - newH) / 2;

        // Return the cropped texture; the sampler maps the smaller image across the sphere,
        // which effectively enlarges features.
        return src.copy(x, y, newW, newH);
    } else {
        // ZOOM OUT (make features smaller on the globe):
        // scale the image down, then center it on a canvas the original size
        const qreal scale = zoomPercent / 100.0;        // e.g. 50% -> 0.5 scale
        const int newW    = qMax(1, qRound(src.width()  * scale));
        const int newH    = qMax(1, qRound(src.height() * scale));

        QImage result(src.width(), src.height(), QImage::Format_ARGB32);
        result.fill(padColor);

        QImage scaled = src.scaled(newW, newH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int x   = (src.width()  - scaled.width())  / 2;
        const int y   = (src.height() - scaled.height()) / 2;

        QPainter p(&result);
        p.drawImage(x, y, scaled);
        p.end();

        return result;
    }
}

void MainWindow::connectUiActions()
{
    constexpr auto UC = Qt::UniqueConnection;

    auto dis = [this](QObject* s){ if (s) QObject::disconnect(s, nullptr, this, nullptr); };

    // Disconnect and reconnect buttons to prevent double-firing
    dis(ui->btnBrowseImage);
    dis(ui->btnBrowseBack);
    dis(ui->btnBrowseOutput);
    dis(ui->btnGenerate);

    if (ui->btnBrowseImage)
        QObject::connect(ui->btnBrowseImage, &QPushButton::clicked,
                         this, &MainWindow::on_btnBrowseImage_clicked, UC);

    if (ui->btnBrowseBack)
        QObject::connect(ui->btnBrowseBack, &QPushButton::clicked,
                         this, &MainWindow::on_btnBrowseBack_clicked, UC);

    if (ui->btnBrowseOutput)
        QObject::connect(ui->btnBrowseOutput, &QPushButton::clicked,
                         this, &MainWindow::on_btnBrowseOutput_clicked, UC);

    if (ui->btnGenerate)
        QObject::connect(ui->btnGenerate, &QPushButton::clicked,
                         this, &MainWindow::on_btnGenerate_clicked, UC);
}
