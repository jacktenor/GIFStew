#include "mainwindow.h"
#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

// --- NEW: install Fusion + white indicators for checkboxes/radios ---
static void installFusionAndIndicatorStyles(QApplication &app)
{
    // 1) Force Fusion so QSS reliably applies on Ubuntu/Qt6
    app.setStyle(QStyleFactory::create("Fusion"));

    // 2) High-contrast, white indicators + visible borders
    const char *qss = R"qss(
/* Make checkbox and radio indicators big enough and visible on dark bg */
QCheckBox::indicator,
QRadioButton::indicator {
    width: 10px;
    height: 10px;
    border: 2px solid #AAAAAA;      /* crisp white outline */
    background: #AAAAAA;
}

/* Keep the radio circular; checkbox stays square */
QRadioButton::indicator {
    border-radius: 6px;
}

/* Checked states — use a clear accent so it's obvious */
QCheckBox::indicator:checked {
    background: #0000ff;             /*  fill when checked */
}

QRadioButton::indicator:checked {
    background: #0000ff;
}

/* Hover/focus feedback so they pop on dark UI */
QCheckBox::indicator:hover,
QRadioButton::indicator:hover {

     width: 12px;
    height: 12px;
}

QCheckBox::indicator:focus,
QRadioButton::indicator:focus {
    outline: none;
    box-shadow: 0 0 0 2px rgba(255,255,255,0.35);
}

/* Disabled fallback */
QCheckBox::indicator:disabled,
QRadioButton::indicator:disabled {
    border-color: #AAAAAA;
    background: rgba(255,255,255,0.5);
}

)qss";

    app.setStyleSheet(qss);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // --- call the helper before creating any widgets ---
    installFusionAndIndicatorStyles(app);

    // Use an icon embedded via .qrc (see #2 below)
    app.setWindowIcon(QIcon(":/icons/appicon.png"));

    MainWindow w;
    w.show();
    return app.exec();
}
