/****************************************
 *
 *   theShell - Desktop Environment
 *   Copyright (C) 2018 Victor Tran
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *************************************/

#define UNW_LOCAL_ONLY

#include "mainwindow.h"
#include "background.h"
#include "segfaultdialog.h"
#include "globalfilter.h"
#include "dbusevents.h"
#include "onboarding.h"
#include "tutorialwindow.h"
#include "audiomanager.h"
#include "dbussignals.h"
#include "screenrecorder.h"
#include <iostream>
//#include "dbusmenuregistrar.h"
#include <nativeeventfilter.h>
#include <QApplication>
#include <QDBusServiceWatcher>
#include <QDesktopWidget>
#include <QProcess>
#include <QThread>
#include <QUrl>
#include <QMediaPlayer>
#include <QDebug>
#include <QSettings>
#include <QInputDialog>
#include <signal.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <libunwind.h>
#include <cxxabi.h>
#include <QFile>

MainWindow* MainWin = NULL;
NativeEventFilter* NativeFilter = NULL;
DbusEvents* DBusEvents = NULL;
TutorialWindow* TutorialWin = NULL;
AudioManager* AudioMan = NULL;
QTranslator *qtTranslator, *tsTranslator;
LocationServices* locationServices = NULL;
QDBusServiceWatcher* dbusServiceWatcher = NULL;
QDBusServiceWatcher* dbusServiceWatcherSystem = NULL;
UPowerDBus* updbus = NULL;
NotificationsDBusAdaptor* ndbus = NULL;
DBusSignals* dbusSignals = NULL;
QSettings::Format desktopFileFormat;
ScreenRecorder* screenRecorder = nullptr;

#define ONBOARDING_VERSION 5

void raise_signal(QString message) {
    //Clean up required stuff

    //Delete the Native Event Filter so that keyboard bindings are cleared
    if (NativeFilter != NULL) {
        NativeFilter->deleteLater();
        NativeFilter = NULL;
    }

    SegfaultDialog* dialog;
    dialog = new SegfaultDialog(message);
    if (MainWin != NULL) {
        MainWin->close();
        MainWin->deleteLater();
    }
    dialog->exec();
    raise(SIGKILL);
}

QString getCallLocation(long pc) {
    QProcess p;
    p.start("addr2line -s -e \"" + QApplication::applicationFilePath() + "\" 0x" + QString::number(pc, 16));
    p.waitForFinished();
    return p.readAll();
}

void export_backtrace(QString header) {
    unw_cursor_t cur;
    unw_context_t ctx;

    unw_getcontext(&ctx);
    unw_init_local(&cur, &ctx);

    //Start unwinding

    QFile f(QDir::homePath() + "/.tsbacktrace");
    f.open(QFile::WriteOnly);
    f.write(header.toUtf8());


    while (unw_step(&cur) > 0) {
        unw_word_t offset;
        unw_word_t pc;
        unw_get_reg(&cur, UNW_REG_IP, &pc);
        if (pc == 0) break;

        char sym[256];
        if (unw_get_proc_name(&cur, sym, sizeof(sym), &offset) == 0) {
            char* nameptr = sym;
            int status;
            char* demangled = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
            if (status == 0) nameptr = demangled;

            f.write(QString("0x" + QString::number(pc, 16) + ": " + QString::fromLocal8Bit(nameptr) + " " + getCallLocation(pc)).toUtf8());

            std::free(demangled);
        } else {
            f.write(QString("0x" + QString::number(pc, 16) + ": ??\n").toUtf8());
        }
    }

    f.close();
}

void catch_signal(int sig) {
    if (sig == SIGSEGV) {
        export_backtrace("Signal Received: SIGSEGV (Segmentation Fault)\n\n");
    } else if (sig == SIGBUS) {
        export_backtrace("Signal Received: SIGBUS (Bus Error)\n\n");
    } else if (sig == SIGABRT) {
        export_backtrace("Signal Received: SIGABRT (Aborted)\n\n");
    } else if (sig == SIGILL) {
        export_backtrace("Signal Received: SIGILL (Illegal Instruction)\n\n");
    } else if (sig == SIGFPE) {
        export_backtrace("Signal Received: SIGFPE (Floating Point Exception)\n\n");
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void QtHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    switch (type) {
    case QtDebugMsg:
    case QtInfoMsg:
    case QtWarningMsg:
    case QtCriticalMsg:
        std::cerr << msg.toStdString() + "\n";
        break;
    case QtFatalMsg:
        std::cerr << msg.toStdString() + "\n";
        raise_signal(msg);
    }
}

int main(int argc, char *argv[])
{
    signal(SIGSEGV, *catch_signal); //Catch SIGSEGV
    signal(SIGBUS, *catch_signal); //Catch SIGBUS
    signal(SIGABRT, *catch_signal); //Catch SIGABRT
    signal(SIGILL, *catch_signal); //Catch SIGILL
    signal(SIGFPE, *catch_signal); //Catch SIGFPE

    QSettings settings("theSuite", "theShell");
    //qputenv("GTK_THEME", settings.value("theme/gtktheme", "Contemporary").toByteArray());

    QString localeName = settings.value("locale/language", "en_US").toString();
    qputenv("LANGUAGE", localeName.toUtf8());

    qInstallMessageHandler(QtHandler);

    QApplication a(argc, argv);

    a.setOrganizationName("theSuite");
    a.setOrganizationDomain("");
    a.setApplicationName("theShell");

    qDBusRegisterMetaType<QMap<QString, QVariant>>();
    qDBusRegisterMetaType<QStringList>();

    QLocale defaultLocale(localeName);
    QLocale::setDefault(defaultLocale);

    if (defaultLocale.language() == QLocale::Arabic || defaultLocale.language() == QLocale::Hebrew) {
        //Reverse the layout direction
        a.setLayoutDirection(Qt::RightToLeft);
    } else {
        //Set normal layout direction
        a.setLayoutDirection(Qt::LeftToRight);
    }

    qtTranslator = new QTranslator;
    qtTranslator->load("qt_" + defaultLocale.name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    a.installTranslator(qtTranslator);

    qDebug() << QLocale().name();

    tsTranslator = new QTranslator;
    if (defaultLocale.name() == "C") {
        tsTranslator->load(localeName, QString(SHAREDIR) + "translations");
    } else {
        tsTranslator->load(defaultLocale.name(), QString(SHAREDIR) + "translations");
    }
    a.installTranslator(tsTranslator);

    desktopFileFormat = QSettings::registerFormat("desktop", [](QIODevice &device, QSettings::SettingsMap &map) -> bool {
        QString group;
        while (!device.atEnd()) {
            QString line = device.readLine().trimmed();
            if (line.startsWith("[") && line.endsWith("]")) {
                group = line.mid(1, line.length() - 2);
            } else {
                QString key = line.left(line.indexOf("="));
                QString value = line.mid(line.indexOf("=") + 1);
                map.insert(group + "/" + key, value);
            }
        }
        return true;
    }, [](QIODevice &device, const QSettings::SettingsMap &map) -> bool {
        return false;
    }, Qt::CaseInsensitive);

    dbusServiceWatcher = new QDBusServiceWatcher();
    dbusServiceWatcher->setConnection(QDBusConnection::sessionBus());
    dbusServiceWatcherSystem = new QDBusServiceWatcher();
    dbusServiceWatcherSystem->setConnection(QDBusConnection::systemBus());

    /*QTranslator tsVnTranslator;
    tsTranslator.load(QLocale("vi_VN").name(), "/home/victor/Documents/theOSPack/theShell/translations/");
    a.installTranslator(&tsVnTranslator);*/

    bool autoStart = true;
    bool startKscreen = true;
    bool startOnboarding = false;
    bool startWm = true;
    bool tutorialDoSettings = false;
    bool sessionStarter = false;

    QStringList args = a.arguments();
    args.removeFirst();
    for (QString arg : a.arguments()) {
        if (arg == "--help" || arg == "-h") {
            qDebug() << "theShell";
            qDebug() << "Usage: theshell [OPTIONS]";
            qDebug() << "  -a, --no-autostart           Don't autostart executables";
            qDebug() << "  -k, --no-kscreen             Don't autostart KScreen";
            qDebug() << "      --no-wm                  Don't autostart the window manager";
            qDebug() << "      --onboard                Start with onboarding screen";
            qDebug() << "      --tutorial               Show all tutorials";
            qDebug() << "      --debug                  Allows you to quit theShell instead of powering off";
            qDebug() << "  -h, --help                   Show this help output";
            return 0;
        } else if (arg == "-a" || arg == "--no-autostart") {
            autoStart = false;
        } else if (arg == "-k" || arg == "--no-kscreen") {
            startKscreen = false;
        } else if (arg == "--no-wm") {
            startWm = false;
        } else if (arg == "--onboard") {
            startOnboarding = true;
        } else if (arg == "--tutorial") {
            tutorialDoSettings = true;
        } else if (arg == "--session-starter-running") {
            sessionStarter = true;
        }
    }

    QEventLoop waiter;

    if (QDBusConnection::sessionBus().interface()->registeredServiceNames().value().contains("org.thesuite.theshell")) {
        QString messageTitle = a.translate("main", "theShell already running");
        QString messageBody = a.translate("main", "theShell seems to already be running. "
                                                  "Do you wish to start theShell anyway?");
        if (sessionStarter) {
            QFile out;
            out.open(stdout, QFile::WriteOnly);
            out.write(QString("QUESTION:%1:%2").arg(messageTitle, messageBody).toLocal8Bit());
            out.flush();
            out.close();

            std::string response;
            std::cin >> response;

            if (QString::fromStdString(response).trimmed() == "no") {
                return 0;
            }
        } else {
            if (QMessageBox::warning(0, messageTitle, messageBody,
                                 QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
                return 0;
            }
        }
    }

    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerService("org.thesuite.theshell");

    QObject* notificationParent = new QObject();
    ndbus = new NotificationsDBusAdaptor(notificationParent);

    dbus.registerObject("/org/freedesktop/Notifications", "org.freedesktop.Notifications", notificationParent);
    dbus.registerService("org.freedesktop.Notifications");

    dbusSignals = new DBusSignals();

    if (startKscreen) {
        QDBusMessage kscreen = QDBusMessage::createMethodCall("org.kde.kded5", "/kded", "org.kde.kded5", "loadModule");
        QVariantList args;
        args.append("kscreen");
        kscreen.setArguments(args);
        QDBusConnection::sessionBus().call(kscreen);
    }

    {
        QDBusMessage touchpad = QDBusMessage::createMethodCall("org.kde.kded5", "/kded", "org.kde.kded5", "loadModule");
        QVariantList args;
        args.append("touchpad");
        touchpad.setArguments(args);
        QDBusConnection::sessionBus().call(touchpad);
    }

    {
        QDBusMessage sni = QDBusMessage::createMethodCall("org.kde.kded5", "/kded", "org.kde.kded5", "loadModule");
        QVariantList args;
        args.append("statusnotifierwatcher");
        sni.setArguments(args);
        QDBusConnection::sessionBus().call(sni);
    }

    QString windowManager = settings.value("startup/WindowManagerCommand", "kwin_x11").toString();

    if (startWm) {
        while (!QProcess::startDetached(windowManager)) {

            QString messageTitle = a.translate("main", "Window Manager couldn't start");
            QString messageBody = a.translate("main", "The window manager \"%1\" could not start. \n\n"
                                                      "Enter the name or path of a window manager to attempt to start a different window"
                                                      "manager, or hit 'Cancel' to start theShell without a window manager.").arg(windowManager);
            if (sessionStarter) {
                QFile out;
                out.open(stdout, QFile::WriteOnly);
                out.write(QString("PROMPT:%1:%2").arg(messageTitle, messageBody.replace("\n", "[newln]")).toLocal8Bit());
                out.flush();
                out.close();

                std::string response;
                std::cin >> response;

                windowManager = QString::fromStdString(response).trimmed();
            } else {
                windowManager = QInputDialog::getText(0, messageTitle, messageBody);
            }

            if (windowManager == "" || windowManager == "[can]") {
                break;
            }
        }
    }

    TutorialWin = new TutorialWindow(tutorialDoSettings);
    AudioMan = new AudioManager;
    screenRecorder = new ScreenRecorder;

    if (!QDBusConnection::sessionBus().interface()->registeredServiceNames().value().contains("org.kde.kdeconnect") && QFile("/usr/lib/kdeconnectd").exists()) {
        //Start KDE Connect if it is not running and it is existant on the PC
        QProcess::startDetached("/usr/lib/kdeconnectd");
    }

    QProcess btProcess;
    btProcess.start("ts-bt");
    QObject::connect(&btProcess, SIGNAL(started()), &waiter, SLOT(quit()));
    waiter.exec();
    //Wait for ts-bt to start so that the Bluetooth toggle will work properly

    locationServices = new LocationServices();

    NativeFilter = new NativeEventFilter();
    a.installNativeEventFilter(NativeFilter);

    if (settings.value("startup/lastOnboarding", 0) < ONBOARDING_VERSION || startOnboarding) {
        emit dbusSignals->Ready();
        Onboarding* onboardingWindow = new Onboarding();
        onboardingWindow->showFullScreen();
        if (onboardingWindow->exec() == QDialog::Accepted) {
            settings.setValue("startup/lastOnboarding", ONBOARDING_VERSION);
        } else {
            //Log out
            return 0;
        }
    }

    updbus = new UPowerDBus();
    MainWin = new MainWindow();

    new GlobalFilter(&a);

    qDBusRegisterMetaType<QList<QVariantMap>>();
    qDBusRegisterMetaType<QMap<QString, QVariantMap>>();

    QRect screenGeometry = QApplication::desktop()->screenGeometry();

    MainWin->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    MainWin->setGeometry(screenGeometry.x() - 1, screenGeometry.y(), screenGeometry.width() + 1, MainWin->height());
    MainWin->show();

    QTimer::singleShot(0, dbusSignals, SIGNAL(Ready()));

    return a.exec();
}

void playSound(QUrl location, bool uncompressed = false) {
    if (uncompressed) {
        QSoundEffect* sound = new QSoundEffect();
        sound->setSource(location);
        sound->play();
    } else {
        QMediaPlayer* sound = new QMediaPlayer();
        sound->setMedia(location);
        sound->play();
    }
}

QIcon getIconFromTheme(QString name, QColor textColor) {
    int averageCol = (textColor.red() + textColor.green() + textColor.blue()) / 3;

    if (averageCol <= 127) {
        return QIcon(":/icons/dark/images/dark/" + name);
    } else {
        return QIcon(":/icons/light/images/light/" + name);
    }
}

void EndSession(EndSessionWait::shutdownType type) {
    switch (type) {
        case EndSessionWait::powerOff:
        case EndSessionWait::reboot:
        case EndSessionWait::logout:
        case EndSessionWait::dummy: {
            EndSessionWait* w = new EndSessionWait(type);
            w->showFullScreen();
            break;
        }
        case EndSessionWait::suspend: {
            QList<QVariant> arguments;
            arguments.append(true);

            QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Suspend");
            message.setArguments(arguments);
            QDBusConnection::systemBus().send(message);
            break;
        }
        case EndSessionWait::hibernate: {
            QList<QVariant> arguments;
            arguments.append(true);

            QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Hibernate");
            message.setArguments(arguments);
            QDBusConnection::systemBus().send(message);
            break;
        }
        case EndSessionWait::screenOff: {
            DPMSForceLevel(QX11Info::display(), DPMSModeOff);
            break;
        }
    }

}

QString calculateSize(quint64 size) {
    QString ret;
    if (size > 1073741824) {
        ret = QString::number(((float) size / 1024 / 1024 / 1024), 'f', 2).append(" GiB");
    } else if (size > 1048576) {
        ret = QString::number(((float) size / 1024 / 1024), 'f', 2).append(" MiB");
    } else if (size > 1024) {
        ret = QString::number(((float) size / 1024), 'f', 2).append(" KiB");
    } else {
        ret = QString::number((float) size, 'f', 2).append(" B");
    }

    return ret;
}


void sendMessageToRootWindow(const char* message, Window window, long data0, long data1, long data2, long data3, long data4) {
    XEvent event;

    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.message_type = XInternAtom(QX11Info::display(), message, False);
    event.xclient.window = window;
    event.xclient.format = 32;
    event.xclient.data.l[0] = data0;
    event.xclient.data.l[1] = data1;
    event.xclient.data.l[2] = data2;
    event.xclient.data.l[3] = data3;
    event.xclient.data.l[4] = data4;

    XSendEvent(QX11Info::display(), DefaultRootWindow(QX11Info::display()), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

float getDPIScaling() {
    float currentDPI = QApplication::desktop()->logicalDpiX();
    return currentDPI / (float) 96;
}
