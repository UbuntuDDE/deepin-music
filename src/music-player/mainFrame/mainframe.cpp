/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     ZouYa <zouya@uniontech.com>
 *
 * Maintainer: WangYu <wangyu@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mainframe.h"

#include <QDebug>
#include <QAction>
#include <QProcess>
#include <QStandardPaths>
#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QSystemTrayIcon>
#include <QTime>
#include <QShortcut>
#include <QPropertyAnimation>
#include <QDBusConnection>
#include <QDesktopWidget>
#include <QScrollArea>

#include <DUtil>
#include <DWidgetUtil>
#include <DAboutDialog>
#include <DDialog>
#include <DApplication>
#include <DFileDialog>
#include <DHiDPIHelper>
#include <DMessageManager>
#include <DFloatingMessage>
#include <DSettingsDialog>
#include <DSettings>
#include <DThemeManager>
#include <DSettingsWidgetFactory>
#include <DSettingsOption>
#include <DApplicationHelper>

#include <unistd.h>
#include "./core/musicsettings.h"
#include "./core/util/global.h"

#include "../widget/titlebarwidget.h"
#include "musicstackedwidget.h"
#include "musiccontentwidget.h"
#include "footerwidget.h"
#include "searchresult.h"
#include "musiclyricwidget.h"
#include "importwidget.h"
#include "ac-desktop-define.h"
#include "databaseservice.h"
#include "commonservice.h"
#include "shortcut.h"
#include "dequalizerdialog.h"
#include "closeconfirmdialog.h"
#include "playqueuewidget.h"
#include "subsonglistwidget.h"
#include "tabletlabel.h"
#include "comdeepiniminterface.h"
DWIDGET_USE_NAMESPACE

const QString s_PropertyViewname = "viewname";
const QString s_PropertyViewnameLyric = "lyric";
const QString s_PropertyViewnamePlay = "playList";
static DSettingsDialog *equalizer = nullptr;
using namespace Dtk::Widget;

MainFrame::MainFrame()
{
    setObjectName("MainFrame");
    Global::setAppName(tr("Music"));
    QString descriptionText = tr("Music is a local music player with beautiful design and simple functions.");
    QString acknowledgementLink = "https://www.deepin.org/acknowledgments/deepin-music#thanks";
    qApp->setProductName(QApplication::tr("Music"));
    qApp->setApplicationAcknowledgementPage(acknowledgementLink);
    qApp->setProductIcon(QIcon::fromTheme("deepin-music"));
    qApp->setApplicationDescription(descriptionText);

    this->setWindowTitle(tr("Music"));

    m_titlebarwidget = new TitlebarWidget(this);
//    m_searchResult = new SearchResult(this);
//    m_titlebarwidget->setResultWidget(m_searchResult);

    m_titlebar = new DTitlebar(this);
    m_titlebar->setFixedHeight(50);
    m_titlebar->setTitle(tr("Music"));
    m_titlebar->setIcon(QIcon::fromTheme("deepin-music"));    //titlebar->setCustomWidget(titlebarwidget, Qt::AlignLeft, false);

    m_titlebar->setCustomWidget(m_titlebarwidget);
    m_titlebar->layout()->setAlignment(m_titlebarwidget, Qt::AlignCenter);
    m_backBtn = new DPushButton(this);
    AC_SET_OBJECT_NAME(m_backBtn, AC_titleBarLeft);
    AC_SET_ACCESSIBLE_NAME(m_backBtn, AC_titleBarLeft);
    m_backBtn->setVisible(false);
    m_backBtn->setFixedSize(QSize(36, 36));

    m_selectStr = tr("Select");
    m_selectAllStr = tr("Select All");
    m_doneStr = tr("Done");
    if (CommonService::getInstance()->isTabletEnvironment()) {
        initTabletSelectBtn();
    }
    m_backBtn->setIcon(QIcon::fromTheme("left_arrow"));
    m_titlebar->addWidget(m_backBtn, Qt::AlignLeft);
    m_titlebar->resize(width(), 50);

    // ??????????????????
    connect(m_backBtn, &DPushButton::clicked,
            this, &MainFrame::slotLeftClicked);

    connect(m_titlebarwidget, &TitlebarWidget::sigSearchEditFoucusIn,
            this, &MainFrame::slotSearchEditFoucusIn);
    // ????????????
    connect(DataBaseService::getInstance(), &DataBaseService::signalImportFinished,
            this, &MainFrame::slotDBImportFinished);
    // ????????????
    connect(DataBaseService::getInstance(), &DataBaseService::signalImportFailed,
            this, &MainFrame::slotImportFailed);
    connect(CommonService::getInstance(), &CommonService::signalShowPopupMessage,
            this, &MainFrame::showPopupMessage);

    connect(DataBaseService::getInstance(), &DataBaseService::signalPlayFromFileMaganager,
            this, &MainFrame::slotPlayFromFileMaganager);

    connect(Player::getInstance()->getMpris(), &MprisPlayer::quitRequested, this, [ = ]() {
        sync();
        qApp->quit();
    });
    connect(Player::getInstance()->getMpris(), &MprisPlayer::raiseRequested, this, [ = ]() {
        qDebug() << "raiseRequested=============";
        if (isVisible()) {
            if (isMinimized()) {
                if (isFullScreen()) {
                    showFullScreen();
                } else {
                    this->titlebar()->setFocus();
                    showNormal();
                    activateWindow();
                    this->restoreGeometry(m_geometryBa);
                }
            } else {
                raise();
                activateWindow();
            }
        } else {
            this->titlebar()->setFocus();
            showNormal();
            activateWindow();
        }
    });

    connect(CommonService::getInstance(), &CommonService::signalSwitchToView, this, &MainFrame::slotViewChanged);

    if (CommonService::getInstance()->isTabletEnvironment()) {
        QDBusConnection connection = QDBusConnection::sessionBus();
        m_comDeepinImInterface = new ComDeepinImInterface("com.deepin.im", "/com/deepin/im", connection);
        if (m_comDeepinImInterface->isValid() == false) {
            qDebug() << __FUNCTION__ << "----------QDbus service can not connect";
        } else {
            qDebug() << __FUNCTION__ << "----------QDbus service connected";
            connect(m_comDeepinImInterface, &ComDeepinImInterface::imActiveChanged, this, &MainFrame::slotActiveChanged);
        }
    }
}

MainFrame::~MainFrame()
{
}

void MainFrame::initUI(bool showLoading)
{
    this->setMinimumSize(QSize(900, 600));
    this->setFocusPolicy(Qt::ClickFocus);

    m_titlebarwidget->setEnabled(showLoading);

    m_musicContentWidget = new MusicContentWidget(this);
    m_musicContentWidget->setVisible(showLoading);

    m_musicStatckedWidget = new MusicStatckedWidget(this);
    m_musicStatckedWidget->addWidget(m_musicContentWidget);
    m_musicStatckedWidget->setCurrentWidget(m_musicContentWidget);

    m_footerWidget = new FooterWidget(this);
    m_footerWidget->setVisible(showLoading);
    connect(m_footerWidget, &FooterWidget::lyricClicked, this, &MainFrame::slotLyricClicked);

    if (!showLoading) {
        m_importWidget = new ImportWidget(this);
    }
    m_musicLyricWidget = new MusicLyricWidget(this);
    m_musicLyricWidget->hide();

    AC_SET_OBJECT_NAME(m_musicLyricWidget, AC_musicLyricWidget);
    AC_SET_ACCESSIBLE_NAME(m_musicLyricWidget, AC_musicLyricWidget);

    //    m_pwidget = new QWidget(this);

    /*---------------menu&shortcut-------------------*/
    if (!CommonService::getInstance()->isTabletEnvironment()) {
        initMenuAndShortcut();
        m_newSonglistAction->setEnabled(showLoading);
    } else {
        initPadMenu();
    }
    connect(DataBaseService::getInstance(), &DataBaseService::signalAllMusicCleared,
            this, &MainFrame::slotAllMusicCleared);

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged,
            this, &MainFrame::setThemeType);

    // ?????????????????????????????????????????????
    connect(m_musicLyricWidget, &MusicLyricWidget::signalAutoHidden,
            m_footerWidget, &FooterWidget::slotLyricAutoHidden);

    connect(CommonService::getInstance(), &CommonService::signalCdaImportFinished,
            this, &MainFrame::slotCdaImportFinished);

    setThemeType(DGuiApplicationHelper::instance()->themeType());
}

MusicContentWidget *MainFrame::getMusicContentWidget()
{
    return m_musicContentWidget;
}

void MainFrame::initMenuAndShortcut()
{
    //menu
    m_newSonglistAction = new QAction(MainFrame::tr("Add playlist"), this);
    m_newSonglistAction->setEnabled(true);
    m_addMusicFiles = new QAction(MainFrame::tr("Add music"), this);
    m_equalizer = new QAction(MainFrame::tr("Equalizer"), this);
    AC_SET_OBJECT_NAME(m_equalizer, AC_equalizerAction);

    m_settings = new QAction(MainFrame::tr("Settings"), this);
    AC_SET_OBJECT_NAME(m_settings, AC_settingsAction);

    QAction *m_colorModeAction = new QAction(MainFrame::tr("Dark theme"), this);
    m_colorModeAction->setCheckable(true);
    m_colorModeAction->setChecked(2 == DGuiApplicationHelper::instance()->themeType());

    QAction *m_exit = new QAction(MainFrame::tr("Exit"), this);

    DMenu *pTitleMenu = new DMenu(this);

    AC_SET_OBJECT_NAME(pTitleMenu, AC_titleMenu);
    AC_SET_ACCESSIBLE_NAME(pTitleMenu, AC_titleMenu);

    pTitleMenu->addAction(m_newSonglistAction);
    pTitleMenu->addAction(m_addMusicFiles);
    pTitleMenu->addSeparator();
    pTitleMenu->addAction(m_equalizer);
    pTitleMenu->addAction(m_settings);
    pTitleMenu->addSeparator();

    m_titlebar->setMenu(pTitleMenu);

    connect(pTitleMenu, SIGNAL(triggered(QAction *)), this, SLOT(slotMenuTriggered(QAction *)));
    connect(m_colorModeAction, &QAction::triggered, this, &MainFrame::slotSwitchTheme);
    connect(m_exit, SIGNAL(triggered()), this, SLOT(close()));

    //short cut
    addmusicfilesShortcut = new QShortcut(this);
    addmusicfilesShortcut->setKey(QKeySequence(QLatin1String("Ctrl+O")));

    viewshortcut = new QShortcut(this);
    viewshortcut->setKey(QKeySequence(QLatin1String("Ctrl+Shift+/")));

    searchShortcut = new QShortcut(this);
    searchShortcut->setKey(QKeySequence(QLatin1String("Ctrl+F")));

    windowShortcut = new QShortcut(this);
    windowShortcut->setKey(QKeySequence(QLatin1String("Ctrl+Alt+F")));

    m_newItemShortcut = new QShortcut(this);
    m_newItemShortcut->setKey(QKeySequence(QLatin1String("Ctrl+Shift+N")));
    connect(m_newItemShortcut, &QShortcut::activated, CommonService::getInstance(), &CommonService::signalAddNewSongList);

    connect(addmusicfilesShortcut, SIGNAL(activated()), this, SLOT(slotShortCutTriggered()));
    connect(viewshortcut, SIGNAL(activated()), this, SLOT(slotShortCutTriggered()));
    connect(searchShortcut, SIGNAL(activated()), this, SLOT(slotShortCutTriggered()));
    connect(windowShortcut, SIGNAL(activated()), this, SLOT(slotShortCutTriggered()));

    //???????????????
    auto playAction = new QAction(tr("Play/Pause"), this);
    auto prevAction = new QAction(tr("Previous"), this);
    auto nextAction = new QAction(tr("Next"), this);
    auto quitAction = new QAction(tr("Exit"), this);

    auto trayIconMenu = new DMenu(this);
    trayIconMenu->addAction(playAction);
    trayIconMenu->addAction(prevAction);
    trayIconMenu->addAction(nextAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    //tray icon
    auto m_sysTrayIcon = new QSystemTrayIcon(this);
    m_sysTrayIcon->setIcon(QIcon::fromTheme("deepin-music"));
    m_sysTrayIcon->setToolTip(tr("Music"));
    m_sysTrayIcon->setContextMenu(trayIconMenu);
#ifndef  ENABLE_AUTO_UNIT_TEST
    m_sysTrayIcon->show();
#endif
    connect(playAction, &QAction::triggered,
    this, [ = ]() {
        m_footerWidget->slotPlayClick(true);
    });
    connect(prevAction, &QAction::triggered,
    this, [ = ]() {
        m_footerWidget->slotPreClick(true);
    });
    connect(nextAction, &QAction::triggered,
    this, [ = ]() {
        m_footerWidget->slotNextClick(true);
    });
    connect(quitAction, &QAction::triggered,
    this, [ = ]() {
        sync();
        qApp->quit();
    });

    connect(m_sysTrayIcon, &QSystemTrayIcon::activated,
    this, [ = ](QSystemTrayIcon::ActivationReason reason) {
        if (QSystemTrayIcon::Trigger == reason) {
            if (isVisible()) {
                if (isMinimized()) {
                    if (isFullScreen()) {
                        showFullScreen();
                    } else {
                        this->titlebar()->setFocus();
                        showNormal();
                        activateWindow();
                        this->restoreGeometry(m_geometryBa);
                    }
                } else {
                    showMinimized();
                }
            } else {
                this->titlebar()->setFocus();
                showNormal();
                activateWindow();
            }
        }
    });
}

void MainFrame::initPadMenu()
{
    // ???????????????????????????????????????????????????
    if (m_addMusicFiles || m_select) {
        return;
    }

    m_addMusicFiles = new QAction(MainFrame::tr("Add music"), this);

    m_select = new QAction(m_selectStr, this);
    m_select->setObjectName(AC_tablet_title_select);

    DMenu *pTitleMenu = new DMenu(this);

    AC_SET_OBJECT_NAME(pTitleMenu, AC_titleMenu);
    AC_SET_ACCESSIBLE_NAME(pTitleMenu, AC_titleMenu);

    pTitleMenu->addAction(m_addMusicFiles);
    pTitleMenu->addAction(m_select);

    m_titlebar->setMenu(pTitleMenu);

    connect(pTitleMenu, SIGNAL(triggered(QAction *)), this, SLOT(slotMenuTriggered(QAction *)));
}

void MainFrame::autoStartToPlay()
{
    QString strOpenPath = DataBaseService::getInstance()->getFirstSong();
    qDebug() << "autoStartToPlay:========" << strOpenPath;
    auto lastplaypage = MusicSettings::value("base.play.last_playlist").toString(); //??????????????????
    if (!strOpenPath.isEmpty()) {
        //????????????????????????
        Player::getInstance()->setCurrentPlayListHash(lastplaypage, true);
        qDebug() << "lastplaypage:========" << lastplaypage;
        return ;
    }
    auto lastMeta = MusicSettings::value("base.play.last_meta").toString();
    if (!lastMeta.isEmpty()) {
        bool bremb = MusicSettings::value("base.play.remember_progress").toBool();
        bool bautoplay = MusicSettings::value("base.play.auto_play").toBool();
        //????????????????????????&????????????
        Player::getInstance()->setCurrentPlayListHash(lastplaypage, true);
        //??????????????????????????????
        MediaMeta medmeta = DataBaseService::getInstance()->getMusicInfoByHash(lastMeta);
        //????????????????????????
        medmeta.offset = MusicSettings::value("base.play.last_position").toInt();
        if (medmeta.localPath.isEmpty())
            return;
        if (bremb) {
            Player::getInstance()->setActiveMeta(medmeta);
            //????????????
            /**
              * ????????????????????????????????????????????????????????????????????????????????????????????????
              **/
            //Player::getInstance()->loadMediaProgress(medmeta.localPath);
            // ????????????
            m_footerWidget->slotSetWaveValue(MusicSettings::value("base.play.last_position").toInt(), medmeta.length);
            //?????????????????????
            m_footerWidget->slotLoadDetector(lastMeta);
        }
        //??????????????????
        if (bautoplay) {
            slotAutoPlay(medmeta); //?????????????????????????????????
        }
    }
}

void MainFrame::showPopupMessage(const QString &songListName, int selectCount, int insertCount)
{
    QFontMetrics fm(font());
    QString name = fm.elidedText(songListName, Qt::ElideMiddle, 300);

    auto text = tr("Successfully added to \"%1\"").arg(name); //need translation
    if (selectCount - insertCount > 0) {
        if (selectCount == 1 || insertCount == 0)
            text = tr("Already added to the playlist");
        else {
            if (insertCount == 1)
                text = tr("1 song added");
            else
                text = tr("%1 songs added").arg(insertCount);
        }
    }
    if (selectCount == 0 && insertCount == 0) //cda????????????cd??????????????????????????????
        text = tr("A disc is connected");

    if (m_popupMessage == nullptr) {
        m_popupMessage = new DWidget(this);
        m_popupMessage->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_popupMessage->setVisible(true);
        m_popupMessage->move(0, 0);
    }

    // ???????????????????????????
    QList<QWidget *> oldMsgList = m_popupMessage->findChildren<QWidget *>("_d_message_float_deepin_music");
    if (oldMsgList.size() > 0) {
        oldMsgList.first()->deleteLater(); // auto delete
    }
    if (oldMsgList.size() >= 2)
        return;

    QIcon icon = QIcon::fromTheme("notify_success");
    DFloatingMessage *pDFloatingMessage = new DFloatingMessage(DFloatingMessage::MessageType::TransientType, m_popupMessage);
    pDFloatingMessage->setObjectName("_d_message_float_deepin_music");
    pDFloatingMessage->setBlurBackgroundEnabled(true);
    pDFloatingMessage->setMessage(text);
    pDFloatingMessage->setIcon(icon);
    pDFloatingMessage->setDuration(2000);
    m_popupMessage->resize(this->width(), this->height() - m_footerWidget->height());
    m_popupMessage->raise();
    DMessageManager::instance()->sendMessage(m_popupMessage, pDFloatingMessage);
}

void MainFrame::setThemeType(DGuiApplicationHelper::ColorType themeType)
{
    if (getMusicContentWidget()) {
        m_musicContentWidget->slotTheme(themeType);
    }
    if (m_footerWidget != nullptr) {
        m_footerWidget->slotTheme(themeType);
    }
}

void MainFrame::slotLeftClicked()
{
    emit CommonService::getInstance()->signalSwitchToView(PreType, "", QMap<QString, MediaMeta>());
    m_backBtn->setVisible(false);
}

void MainFrame::slotActiveChanged(bool isActive)
{
    if (CommonService::getInstance()->isTabletEnvironment()) {
        if (isActive) {
            m_musicStatckedWidget->animationToUpByInput();
        } else {
            if (m_musicStatckedWidget->pos().y() != 50) {
                m_musicStatckedWidget->animationToDownByInput();
            }
        }
    }
}

void MainFrame::slotSearchEditFoucusIn()
{
    m_titlebarwidget->slotSearchEditFoucusIn();
}

void MainFrame::slotLyricClicked()
{
    // ?????????????????????????????????
    if (m_musicLyricWidget->isHidden()) {
        m_musicLyricWidget->showAnimation();
        m_musicStatckedWidget->animationToUp();
        //??????????????????
        m_titlebarwidget->setEnabled(false);
    } else {
        m_musicLyricWidget->closeAnimation();
        m_musicStatckedWidget->animationToDown();
        //??????????????????
        m_titlebarwidget->setEnabled(true);
    }
}

void MainFrame::slotDBImportFinished(QString hash, int successCount)
{
    if (successCount <= 0) {
        if (DataBaseService::getInstance()->allMusicInfos().size() <= 0) {
            if (m_importWidget == nullptr) {
                m_importWidget = new ImportWidget(this);
            }
            m_importWidget->showImportHint();
        }
        return;
    }
    // ?????????????????????????????????
    if (m_importWidget && m_importWidget->isVisible()) {
        m_musicStatckedWidget->show();
        m_musicStatckedWidget->animationImportToDown(this->size() - QSize(0, m_footerWidget->height() + titlebar()->height()));
        // ???????????????????????????
        if (hash != "CdaRole")
            emit CommonService::getInstance()->signalSwitchToView(AllSongListType, "all");
        else
            emit CommonService::getInstance()->signalSwitchToView(CdaType, hash); //??????cd???????????????????????????????????????????????????????????????
        m_footerWidget->show();
        m_importWidget->closeAnimationToDown(this->size());
        if (CommonService::getInstance()->isTabletEnvironment() && m_select) {
            m_select->setEnabled(true);
        }
    }
    m_titlebarwidget->setEnabled(true);
    if (m_newSonglistAction) {
        m_newSonglistAction->setEnabled(true);
    }
}

void MainFrame::slotCdaImportFinished()
{
    if (m_importWidget && m_importWidget->isVisible()) {
        m_musicStatckedWidget->show();
        m_musicStatckedWidget->animationImportToDown(this->size() - QSize(0, m_footerWidget->height() + titlebar()->height()));
        // ???????????????????????????
        emit CommonService::getInstance()->signalSwitchToView(CdaType, "CdaRole"); //??????cd???????????????????????????????????????????????????????????????
        m_footerWidget->show();
        m_importWidget->closeAnimationToDown(this->size());
        if (CommonService::getInstance()->isTabletEnvironment() && m_select) {
            m_select->setEnabled(true);
        }
    }
    m_titlebarwidget->setEnabled(true);
    m_newSonglistAction->setEnabled(true);
}

void MainFrame::slotImportFailed()
{
    if (DataBaseService::getInstance()->allMusicInfos().size() <= 0) {
        m_importWidget->showImportHint();
    }
    QString message = QString(tr("Import failed, no valid music file found"));
    Dtk::Widget::DDialog warnDlg(this);
    warnDlg.setTextFormat(Qt::RichText);
    warnDlg.setObjectName("uniquewarndailog");
    warnDlg.setIcon(QIcon::fromTheme("deepin-music"));
    //warnDlg.setTitle(message);
    warnDlg.setMessage(message);
    warnDlg.addButton(tr("OK"), true, Dtk::Widget::DDialog::ButtonNormal);
    //warnDlg.setDefaultButton(0);
    if (0 == warnDlg.exec()) {
        return;
    }
}

void MainFrame::slotShortCutTriggered()
{
    QShortcut *objCut = dynamic_cast<QShortcut *>(sender()) ;
    Q_ASSERT(objCut);

    if (objCut == addmusicfilesShortcut) {
        if (m_importWidget == nullptr) {
            m_importWidget = new ImportWidget(this);
        }
        m_importWidget->slotAddMusicButtonClicked(); //open filedialog
    }

    if (objCut == viewshortcut) {
        QRect rect = window()->geometry();
        QPoint pos(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2);
        Shortcut sc;
        QStringList shortcutString;
        QString param1 = "-j=" + sc.toStr();
        QString param2 = "-p=" + QString::number(pos.x()) + "," + QString::number(pos.y());
        shortcutString << "-b" << param1 << param2;

        QProcess *shortcutViewProc = new QProcess(this);
        //????????????????????????????????????deepin-shortcut?????????????????????????????????
        shortcutViewProc->startDetached("deepin-shortcut-viewer", shortcutString);

        connect(shortcutViewProc, SIGNAL(finished(int)), shortcutViewProc, SLOT(deleteLater()));
    }

    if (objCut == searchShortcut) {
        m_titlebarwidget->slotSearchEditFoucusIn();
    }

    if (objCut == windowShortcut) {
        if (windowState() == Qt::WindowMaximized) {
            showNormal();
        } else {
            showMaximized();
        }
    }
}

static DequalizerDialog *dequalizerDialog = nullptr;
static QWidget *createEqualizerWidgetHandle(QObject *opt)
{
    Q_UNUSED(opt)
    dequalizerDialog = new DequalizerDialog(equalizer);
    return dequalizerDialog;
}

void MainFrame::slotMenuTriggered(QAction *action)
{
    Q_ASSERT(action);

    if (action == m_newSonglistAction) {
        // ????????????????????????
        if (!m_musicLyricWidget->isHidden()) {
            m_musicLyricWidget->closeAnimation();
            m_musicStatckedWidget->animationToDown();
            m_titlebarwidget->setEnabled(true);
        }

        emit CommonService::getInstance()->signalAddNewSongList();
    }

    if (action == m_addMusicFiles) {
        if (m_importWidget == nullptr) {
            m_importWidget = new ImportWidget(this);
        }
        m_importWidget->addMusic(m_importListHash);
    }

    if (action == m_equalizer) {
        DSettingsDialog *configDialog = new DSettingsDialog(this);
        equalizer = configDialog;
        configDialog->setFixedSize(720, 540);
        AC_SET_OBJECT_NAME(configDialog, AC_Dequalizer);
        AC_SET_ACCESSIBLE_NAME(configDialog, AC_Dequalizer);
        configDialog->widgetFactory()->registerWidget("equalizerWidget", createEqualizerWidgetHandle);

        Dtk::Core::DSettings *equalizerSettings = Dtk::Core::DSettings::fromJsonFile(":/data/dequalizer-settings.json");
        qDebug() << __FUNCTION__ << "" << equalizerSettings->keys();
        //?????????????????????
        Player::getInstance()->initEqualizerCfg();
        configDialog->updateSettings(equalizerSettings);
        Dtk::Widget::moveToCenter(configDialog);
        configDialog->setResetVisible(false);
        if (dequalizerDialog && static_cast<QWidget *>(dequalizerDialog->parent())) {
            QWidget *w = static_cast<QWidget *>(dequalizerDialog->parent());

            w->setContentsMargins(0, 0, 0, 0);
            qDebug() << __FUNCTION__ << "" << dequalizerDialog->height();
            if (w->findChildren<QHBoxLayout *>().size() > 0) {
                for (int i = 0; i < w->findChildren<QHBoxLayout *>().size(); i++) {
                    QHBoxLayout *h = static_cast<QHBoxLayout *>(w->findChildren<QHBoxLayout *>().at(i));
                    if (h) {
                        h->setContentsMargins(0, 0, 0, 0);
                    }
                }
            }
        }
        configDialog->exec();
        delete configDialog;
    }

    if (action == m_settings) {
        DSettingsDialog *configDialog = new DSettingsDialog(this);
        AC_SET_OBJECT_NAME(configDialog, AC_configDialog);
        AC_SET_ACCESSIBLE_NAME(configDialog, AC_configDialog);

        configDialog->updateSettings(MusicSettings::settings());
        Dtk::Widget::moveToCenter(configDialog);

        auto curAskCloseAction = MusicSettings::value("base.close.is_close").toBool();
        auto curLastPlaylist = MusicSettings::value("base.play.last_playlist").toString();
        auto curLastMeta = MusicSettings::value("base.play.last_meta").toString();
        auto curLastPosition = MusicSettings::value("base.play.last_position").toInt();

        configDialog->exec();
        delete configDialog;

        MusicSettings::sync();
        MusicSettings::setOption("base.close.is_close", curAskCloseAction);
        MusicSettings::setOption("base.play.last_playlist", curLastPlaylist);
        MusicSettings::setOption("base.play.last_meta", curLastMeta);
        MusicSettings::setOption("base.play.last_position", curLastPosition);

        //update shortcut
        m_footerWidget->updateShortcut();
        //update fade
        Player::getInstance()->setFadeInOut(MusicSettings::value("base.play.fade_in_out").toBool());
    }

    if (CommonService::getInstance()->isTabletEnvironment()) {
        if (action == m_select) {
            CommonService::getInstance()->setSelectModel(CommonService::MultSelect);
            m_tabletSelectAll->setVisible(true);
            m_tabletSelectDone->setVisible(true);
        }
    }
}

void MainFrame::slotSwitchTheme()
{
    MusicSettings::setOption("base.play.theme", DGuiApplicationHelper::instance()->themeType());
}

void MainFrame::slotAllMusicCleared()
{
    //?????????cd????????????????????????????????????
    if (Player::getInstance()->getCdaPlayList().size() > 0) {
        return;
    }
    qDebug() << "MainFrame::slotAllMusicCleared";
    // ?????????????????????????????????
    m_musicStatckedWidget->animationImportToLeft(this->size() - QSize(0, m_footerWidget->height() + titlebar()->height()));
    m_footerWidget->hide();
    if (m_importWidget == nullptr) {
        m_importWidget = new ImportWidget(this);
    }
    m_importWidget->lower();
    m_importWidget->showImportHint();
    m_importWidget->showAnimationToLeft(this->size());
    if (CommonService::getInstance()->isTabletEnvironment() && m_select) {
        m_select->setEnabled(false);
    }
    m_titlebarwidget->setEnabled(false);
    if (m_newSonglistAction) {
        m_newSonglistAction->setEnabled(false);
    }
}

void MainFrame::slotAutoPlay(const MediaMeta &meta)
{
    qDebug() << "slotAutoPlay=========";
    if (!meta.localPath.isEmpty())
        Player::getInstance()->playMeta(meta);
    else
        qDebug() << __FUNCTION__ << " at line:" << __LINE__ << " localPath is empty.";
}

void MainFrame::slotPlayFromFileMaganager()
{
    QString path = DataBaseService::getInstance()->getFirstSong();
    qDebug() << "----------openUrl:" << path << "all size:" << DataBaseService::getInstance()->allMusicInfosCount();
    if (path.isEmpty())
        return;
    //?????????????????????????????????
    MediaMeta mt = DataBaseService::getInstance()->getMusicInfoByHash(DMusic::filepathHash(path));
    if (mt.localPath.isEmpty()) {
        //?????????????????????
        qCritical() << "fail to start from file manager";
        return;
    }
    qDebug() << "----------playMeta:" << mt.localPath;
    Player::getInstance()->playMeta(mt);
    Player::getInstance()->setCurrentPlayListHash("all", true);
    // ??????????????????????????????
    emit Player::getInstance()->signalPlayListChanged();
    DataBaseService::getInstance()->setFirstSong("");
}

void MainFrame::slotViewChanged(ListPageSwitchType switchtype, const QString &hashOrSearchword, QMap<QString, MediaMeta> musicinfos)
{
    Q_UNUSED(musicinfos)
    if (switchtype != AlbumSubSongListType
            && switchtype != SingerSubSongListType
            && switchtype != SearchAlbumSubSongListType
            && switchtype != SearchSingerSubSongListType) {
        m_backBtn->setVisible(false);
    }
    // ???????????????????????????hash???
    switch (switchtype) {
    case AlbumType:
    case SingerType:
    case AllSongListType: {
        m_importListHash = "all";
        break;
    }
    case FavType: {
        m_importListHash = "fav";
        break;
    }
    case CustomType: {
        m_importListHash = hashOrSearchword;
        break;
    }
    case SearchMusicResultType:
    case SearchSingerResultType:
    case SearchAlbumResultType:
    case PreType: {
        m_importListHash = "all";
        break;
    }
    case AlbumSubSongListType:
    case SingerSubSongListType:
    case SearchAlbumSubSongListType:
    case SearchSingerSubSongListType: {
        m_backBtn->setVisible(true);
        break;
    }
    default:
        m_importListHash = "all";
        break;
    }
}

void MainFrame::showEvent(QShowEvent *event)
{
    Q_UNUSED(event)
    if (m_geometryBa.isEmpty()) {//??????????????????????????????
        this->setMinimumSize(QSize(1070, 680));
        this->resize(QSize(1070, 680));
        Dtk::Widget::moveToCenter(this);
    } else {
        QDataStream stream(m_geometryBa); //??????????????????????????????
        stream.setVersion(QDataStream::Qt_4_0);

        const quint32 magicNumber = 0x1D9D0CB;
        quint32 storedMagicNumber;
        stream >> storedMagicNumber;
        if (storedMagicNumber != magicNumber) {
            return;
        }

        const quint16 currentMajorVersion = 2;
        quint16 majorVersion = 0;
        quint16 minorVersion = 0;

        stream >> majorVersion >> minorVersion;

        if (majorVersion > currentMajorVersion) {
            return;
        }

        QRect restoredFrameGeometry;
        QRect restoredNormalGeometry;
        qint32 restoredScreenNumber;
        quint8 maximized;
        quint8 fullScreen;
        //qint32 restoredScreenWidth = 0;

        stream >> restoredFrameGeometry
               >> restoredNormalGeometry
               >> restoredScreenNumber
               >> maximized
               >> fullScreen;

        if (majorVersion > 1) {
            qint32 restoredScreenWidth = 0;
            stream >> restoredScreenWidth;
        }
        restoreGeometry(m_geometryBa);
    }
    this->setFocus();

    if (m_footerWidget) {
        m_footerWidget->setGeometry(FooterWidget::Margin, height() - FooterWidget::Height - FooterWidget::Margin,
                                    width() - FooterWidget::Margin * 2, FooterWidget::Height);
    }

    if (m_importWidget) {
        // ???????????????????????????????????????
        m_importWidget->setGeometry(0, 0, width(), height() - titlebar()->height());
    }
}

void MainFrame::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        // ???????????????????????????????????????????????????
        if (m_playQueueWidget) {
            m_playQueueWidget->stopAnimation();
        }
    }
}

void MainFrame::resizeEvent(QResizeEvent *e)
{
    DMainWindow::resizeEvent(e);
    QSize newSize = DMainWindow::size();

    auto titleBarHeight =  m_titlebar->height();
    m_titlebar->raise();
    m_titlebar->resize(newSize.width(), titleBarHeight);

    if (m_importWidget) {
        m_importWidget->setFixedSize(newSize);
    }

    if (m_footerWidget) {
        m_footerWidget->setGeometry(FooterWidget::Margin, height() - FooterWidget::Height - FooterWidget::Margin,
                                    width() - FooterWidget::Margin * 2, FooterWidget::Height);
    }

    if (m_musicStatckedWidget) {
        m_musicStatckedWidget->setGeometry(0, titlebar()->height(), width(), height() - m_footerWidget->height() - titlebar()->height());
    }

    if (m_musicContentWidget) {
        m_musicContentWidget->setGeometry(0, 0, width(), height() - m_footerWidget->height() - titlebar()->height());
    }

    // ???????????????????????????????????????
    if (m_musicLyricWidget) {
        m_musicLyricWidget->setGeometry(0, titlebar()->height(), width(), height() - titlebar()->height());
    }

    if (m_playQueueWidget) {
        m_playQueueWidget->setGeometry(PlayQueueWidget::Margin, m_footerWidget->y() - m_playQueueWidget->height() + m_footerWidget->height(),
                                       width() - PlayQueueWidget::Margin * 2, m_playQueueWidget->height());
    }

    if (m_popupMessage) {
        m_popupMessage->resize(this->width(), this->height() - m_footerWidget->height());
    }
    if (CommonService::getInstance()->isTabletEnvironment()) {
        if (this->width() > 1900) {
            CommonService::getInstance()->setIsHScreen(true);
        } else {
            CommonService::getInstance()->setIsHScreen(false);
        }
    }
}

void MainFrame::closeEvent(QCloseEvent *event)
{
    auto askCloseAction = MusicSettings::value("base.close.close_action").toInt();
    switch (askCloseAction) {
    case 0: {
        MusicSettings::setOption("base.close.is_close", false);
        break;
    }
    case 1: {
        MusicSettings::setOption("base.play.state", int(windowState()));
        MusicSettings::setOption("base.close.is_close", true);
        //?????????,stop????????????
        Player::getInstance()->stop(false);
        qApp->quit();
        break;
    }
    case 2: {
        CloseConfirmDialog ccd(this);
        ccd.setObjectName(AC_CloseConfirmDialog);


        auto clickedButtonIndex = ccd.exec();
        // 1 is confirm button
        if (1 != clickedButtonIndex) {
            event->ignore();
            return;
        }
        if (ccd.isRemember()) {
            MusicSettings::setOption("base.close.close_action", ccd.closeAction());
        }
        if (ccd.closeAction() == 1) {
            MusicSettings::setOption("base.close.is_close", true);
            //?????????,stop????????????
            Player::getInstance()->stop(false);
            qApp->quit();
        } else {
            MusicSettings::setOption("base.close.is_close", false);
        }
        break;
    }
    default:
        break;
    }

    MusicSettings::setOption("base.play.last_position", Player::getInstance()->position());
    this->setFocus();
    DMainWindow::closeEvent(event);
}

void MainFrame::hideEvent(QHideEvent *event)
{
    //??????????????????????????????????????????,note????????????????????????????????????????????????????????????
    DMainWindow::hideEvent(event);
    m_geometryBa = saveGeometry();
    qDebug() << "hideEvent=============";
}

void MainFrame::playQueueAnimation()
{
    if (m_playQueueWidget == nullptr) {
        m_playQueueWidget = new PlayQueueWidget(this);

        AC_SET_OBJECT_NAME(m_playQueueWidget, AC_PlayQueue);
        AC_SET_ACCESSIBLE_NAME(m_playQueueWidget, AC_PlayQueue);

        // ?????????????????????footer??????
        m_playQueueWidget->raise();
        m_footerWidget->raise();

        connect(m_playQueueWidget, &PlayQueueWidget::signalAutoHidden, m_footerWidget, &FooterWidget::slotPlayQueueAutoHidden);
    }

    m_playQueueWidget->playAnimation(this->size());
}

void MainFrame::initTabletSelectBtn()
{
    // ???????????????????????????????????????????????????
    if (m_tabletSelectAll || m_tabletSelectDone) {
        return;
    }

    m_tabletSelectAll = new TabletLabel(m_selectAllStr, m_titlebar, 1);
    m_tabletSelectDone = new TabletLabel(m_doneStr, m_titlebar, 0);
    DFontSizeManager::instance()->bind(m_tabletSelectAll, DFontSizeManager::T6, QFont::Medium);
    DFontSizeManager::instance()->bind(m_tabletSelectDone, DFontSizeManager::T6, QFont::Medium);
    QFontMetrics font(m_tabletSelectAll->font());
    m_tabletSelectAll->setFixedSize((font.width(m_tabletSelectAll->text()) + 22), 50);
    m_tabletSelectDone->setFixedSize((font.width(m_tabletSelectDone->text()) + 22), 50);
    m_tabletSelectAll->hide();
    m_tabletSelectDone->hide();

    m_titlebar->addWidget(m_tabletSelectAll, Qt::AlignRight | Qt::AlignVCenter);
    m_titlebar->addWidget(m_tabletSelectDone, Qt::AlignRight | Qt::AlignVCenter);
    connect(m_tabletSelectAll, &TabletLabel::signalTabletSelectAll, CommonService::getInstance(), &CommonService::signalSelectAll);
    connect(m_tabletSelectDone, &TabletLabel::signalTabletDone, this, [ = ]() {
        CommonService::getInstance()->setSelectModel(CommonService::SingleSelect);
        m_tabletSelectAll->setVisible(false);
        m_tabletSelectDone->setVisible(false);
    });
    connect(CommonService::getInstance(), &CommonService::signalSelectMode, this, [ = ](CommonService::TabletSelectMode mode) {
        if (mode == CommonService::SingleSelect) {
            m_tabletSelectAll->setVisible(false);
            m_tabletSelectDone->setVisible(false);
        }
    });
}


