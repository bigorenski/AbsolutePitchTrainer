#include "pitchtraining.h"
#include "ui_pitchtraining.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QVBoxLayout>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QTabWidget>
#include <QtCore/qoverload.h>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr int kTokenCostForDouble = 10;
constexpr int kSessionMinimumSeconds = 15 * 60;
}

PitchTraining::PitchTraining(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PitchTraining)
{
    ui->setupUi(this);
    qApp->installEventFilter(this);
    buildUi();
    applyTheme();

    m_profileManager.load();
    refreshProfileControls();
    applyActiveProfile();

    m_responseTimer = new QTimer(this);
    m_responseTimer->setSingleShot(true);
    connect(m_responseTimer, &QTimer::timeout, this, &PitchTraining::handleResponseTimeout);

    m_responseProgressTimer = new QTimer(this);
    m_responseProgressTimer->setInterval(40);
    connect(m_responseProgressTimer, &QTimer::timeout, this, &PitchTraining::updateResponseTimeBar);

    connect(&m_tonePlayer, &TonePlayer::playbackFinished, this, &PitchTraining::handlePlaybackFinished);

    connect(m_startLevelButton, &QPushButton::clicked, this, &PitchTraining::handleStartLevel);
    connect(m_startTrialButton, &QPushButton::clicked, this, &PitchTraining::handleStartTrial);
    connect(m_sampleButton, &QPushButton::clicked, this, &PitchTraining::handleSampleButton);
    connect(m_sessionButton, &QPushButton::clicked, this, &PitchTraining::handleSessionToggle);
    connect(m_newProfileButton, &QPushButton::clicked, this, &PitchTraining::handleCreateProfile);
    connect(m_deleteProfileButton, &QPushButton::clicked, this, &PitchTraining::handleDeleteProfile);
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PitchTraining::handleProfileSelection);
    connect(m_helpButton, &QPushButton::clicked, this, &PitchTraining::handleShowInstructions);

    setMinimumSize(1000, 680);
    resize(1180, 760);
    setWindowState(windowState() | Qt::WindowMaximized);
}

PitchTraining::~PitchTraining()
{
    concludeSessionIfNeeded();
    m_state.save();
    if (qApp) {
        qApp->removeEventFilter(this);
    }
    delete ui;
}

void PitchTraining::buildUi()
{
    auto *central = ui->centralwidget;
    central->setObjectName("mainSurface");
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(14, 11, 14, 11);
    layout->setSpacing(9);

    if (!m_titleAboutButton && menuBar()) {
        m_titleAboutButton = new QToolButton(this);
        m_titleAboutButton->setText(tr("About"));
        m_titleAboutButton->setAutoRaise(true);
        m_titleAboutButton->setCursor(Qt::PointingHandCursor);
        connect(m_titleAboutButton, &QToolButton::clicked, this, &PitchTraining::handleShowAbout);
        menuBar()->setCornerWidget(m_titleAboutButton, Qt::TopRightCorner);
    }

    if (auto *bar = menuBar()) {
        bar->clear();
        bar->setVisible(false);
    }


    auto *heroFrame = new QFrame(central);
    heroFrame->setObjectName("heroFrame");
    auto *heroLayout = new QVBoxLayout(heroFrame);
    heroLayout->setContentsMargins(14, 12, 14, 12);
    heroLayout->setSpacing(3);
    const QString heroCaptionText = tr("Pitch mastery program").toUpper();
    auto *heroCaption = new QLabel(heroCaptionText, heroFrame);
    heroCaption->setObjectName("heroCaption");
    heroLayout->addWidget(heroCaption);
    m_levelLabel = new QLabel(tr("Welcome to the AP training"), heroFrame);
    m_levelLabel->setObjectName("levelLabel");
    heroLayout->addWidget(m_levelLabel);
    m_requirementLabel = new QLabel(heroFrame);
    m_requirementLabel->setWordWrap(true);
    m_requirementLabel->setObjectName("requirementLabel");
    heroLayout->addWidget(m_requirementLabel);
    auto *statsRow = new QHBoxLayout();
    statsRow->setContentsMargins(0, 4, 0, 0);
    statsRow->setSpacing(8);
    const auto createStatBubble = [&](const QString &title, QLabel **valueLabel) {
        auto *statLayout = new QVBoxLayout();
        statLayout->setSpacing(2);
        auto *titleLabel = new QLabel(title, heroFrame);
        titleLabel->setObjectName("statTitle");
        auto *value = new QLabel(tr("--"), heroFrame);
        value->setObjectName("statValue");
        value->setWordWrap(false);
        *valueLabel = value;
        statLayout->addWidget(titleLabel);
        statLayout->addWidget(value);
        statsRow->addLayout(statLayout);
    };
    createStatBubble(tr("Tokens"), &m_tokensLabel);
    createStatBubble(tr("Streak"), &m_streakLabel);
    createStatBubble(tr("Hours trained"), &m_hoursLabel);
    createStatBubble(tr("Session"), &m_sessionLabel);
    heroLayout->addLayout(statsRow);
    auto *aboutLink = new QLabel(heroFrame);
    aboutLink->setObjectName(QStringLiteral("heroAboutLink"));
    aboutLink->setText(tr("<a style=\"color:#0a58ca;\" href=\"#about\">About this trainer</a>"));
    aboutLink->setTextFormat(Qt::RichText);
    aboutLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    aboutLink->setOpenExternalLinks(false);
    connect(aboutLink, &QLabel::linkActivated, this, [this](const QString &) {
        handleShowAbout();
    });
    heroLayout->addWidget(aboutLink);
    layout->addWidget(heroFrame);

    auto *profileFrame = new QFrame(central);
    profileFrame->setObjectName("sectionFrame");
    auto *profileLayout = new QVBoxLayout(profileFrame);
    profileLayout->setContentsMargins(14, 12, 14, 12);
    profileLayout->setSpacing(6);
    auto *profileTitle = new QLabel(tr("Profiles"), profileFrame);
    profileTitle->setObjectName("sectionTitle");
    profileLayout->addWidget(profileTitle);
    auto *profileRow = new QHBoxLayout();
    profileRow->setContentsMargins(0, 6, 0, 0);
    profileRow->setSpacing(6);
    m_profileCombo = new QComboBox(profileFrame);
    m_profileCombo->setFocusPolicy(Qt::NoFocus);
    m_profileCombo->setMinimumHeight(24);
    m_profileCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    profileRow->addWidget(m_profileCombo, 1);
    m_newProfileButton = new QPushButton(tr("New"), profileFrame);
    m_newProfileButton->setFocusPolicy(Qt::NoFocus);
    m_newProfileButton->setMinimumHeight(24);
    profileRow->addWidget(m_newProfileButton);
    m_deleteProfileButton = new QPushButton(tr("Delete"), profileFrame);
    m_deleteProfileButton->setFocusPolicy(Qt::NoFocus);
    m_deleteProfileButton->setMinimumHeight(24);
    profileRow->addWidget(m_deleteProfileButton);
    profileLayout->addLayout(profileRow);
    layout->addWidget(profileFrame);

    auto *controlFrame = new QFrame(central);
    controlFrame->setObjectName("sectionFrame");
    controlFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *controlLayout = new QVBoxLayout(controlFrame);
    controlLayout->setContentsMargins(14, 12, 14, 12);
    controlLayout->setSpacing(8);
    auto *controlTitle = new QLabel(tr("Level controls"), controlFrame);
    controlTitle->setObjectName("sectionTitle");
    controlLayout->addWidget(controlTitle);
    auto *controlHint = new QLabel(tr("Use the buttons below or press 1 / note keys to keep the session moving."), controlFrame);
    controlHint->setObjectName("sectionSubtitle");
    controlHint->setWordWrap(true);
    controlLayout->addWidget(controlHint);
    auto *controlGrid = new QGridLayout();
    controlGrid->setContentsMargins(0, 2, 0, 0);
    controlGrid->setHorizontalSpacing(6);
    controlGrid->setVerticalSpacing(6);
    m_startLevelButton = new QPushButton(tr("Start next level/special"), controlFrame);
    m_startLevelButton->setFocusPolicy(Qt::NoFocus);
    m_startLevelButton->setProperty("primary", true);
    m_startLevelButton->setProperty("sessionRequired", true);
    m_startLevelButton->setMinimumHeight(26);
    m_startLevelButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_startTrialButton = new QPushButton(tr("Hear next tone (1)"), controlFrame);
    m_startTrialButton->setEnabled(false);
    m_startTrialButton->setFocusPolicy(Qt::NoFocus);
    m_startTrialButton->setMinimumHeight(26);
    m_startTrialButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_sampleButton = new QPushButton(tr("Preview samples"), controlFrame);
    m_sampleButton->setEnabled(false);
    m_sampleButton->setFocusPolicy(Qt::NoFocus);
    m_sampleButton->setMinimumHeight(24);
    m_sampleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_doubleButton = new QPushButton(tr("Arm double bonus (-10 tokens)"), controlFrame);
    m_doubleButton->setEnabled(false);
    m_doubleButton->setFocusPolicy(Qt::NoFocus);
    m_doubleButton->setMinimumHeight(24);
    m_doubleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_sessionButton = new QPushButton(tr("Start 15-min session"), controlFrame);
    m_sessionButton->setFocusPolicy(Qt::NoFocus);
    m_sessionButton->setProperty("accent", true);
    m_sessionButton->setMinimumHeight(26);

    connect(m_doubleButton, &QPushButton::clicked, [this]() {
        if (!m_levelActive || !m_currentSpec.tokensAllowed) {
            updateFeedback(tr("Bonus unavailable right now"), false);
            return;
        }
        if (!m_state.consumeTokens(kTokenCostForDouble)) {
            updateFeedback(tr("You need %1 tokens").arg(kTokenCostForDouble), false);
            return;
        }
        m_doubleArmed = true;
        refreshStateLabels();
        updateFeedback(tr("Double bonus armed for the next correct answer"), true);
    });

    controlLayout->addWidget(m_sessionButton);
    controlGrid->addWidget(m_startLevelButton, 0, 0);
    controlGrid->addWidget(m_startTrialButton, 0, 1);
    controlGrid->addWidget(m_sampleButton, 1, 0);
    controlGrid->addWidget(m_doubleButton, 1, 1);
    controlGrid->setColumnStretch(0, 1);
    controlGrid->setColumnStretch(1, 1);
    controlLayout->addLayout(controlGrid);
    layout->addWidget(controlFrame);

    auto *statusFrame = new QFrame(central);
    statusFrame->setObjectName("sectionFrame");
    auto *statusLayout = new QVBoxLayout(statusFrame);
    statusLayout->setContentsMargins(14, 10, 14, 10);
    statusLayout->setSpacing(4);
    m_statusLabel = new QLabel(tr("Click \"Start next level\" to begin."), statusFrame);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setObjectName("statusLabel");
    m_helpButton = new QPushButton(tr("How the tests work"), statusFrame);
    m_helpButton->setProperty("link", true);
    m_helpButton->setFocusPolicy(Qt::NoFocus);
    m_helpButton->setCursor(Qt::PointingHandCursor);
    statusLayout->addWidget(m_statusLabel);
    auto *helpRow = new QHBoxLayout();
    helpRow->setContentsMargins(0, 0, 0, 0);
    helpRow->addWidget(m_helpButton, 0, Qt::AlignLeft);
    helpRow->addStretch();
    statusLayout->addLayout(helpRow);
    layout->addWidget(statusFrame);

    auto *progressFrame = new QFrame(central);
    progressFrame->setObjectName("sectionFrame");
    auto *progressLayout = new QVBoxLayout(progressFrame);
    progressLayout->setContentsMargins(14, 10, 14, 10);
    progressLayout->setSpacing(6);
    m_trialProgress = new QProgressBar(progressFrame);
    m_trialProgress->setRange(0, 20);
    m_trialProgress->setObjectName("trialProgress");
    progressLayout->addWidget(m_trialProgress);
    m_responseProgress = new QProgressBar(progressFrame);
    m_responseProgress->setTextVisible(true);
    m_responseProgress->setFormat(tr("Time left"));
    m_responseProgress->setMinimum(0);
    m_responseProgress->setMaximum(100);
    m_responseProgress->setObjectName("responseProgress");
    m_responseProgress->setValue(0);
    progressLayout->addWidget(m_responseProgress);
    layout->addWidget(progressFrame);

    m_feedbackLabel = new QLabel(central);
    m_feedbackLabel->setWordWrap(true);
    m_feedbackLabel->setObjectName("feedbackLabel");
    m_feedbackLabel->setProperty("positive", true);
    layout->addWidget(m_feedbackLabel);

    m_specialContainer = new QFrame(central);
    m_specialContainer->setObjectName("specialCard");
    auto *specialLayout = new QVBoxLayout(m_specialContainer);
    specialLayout->setContentsMargins(14, 12, 14, 12);
    specialLayout->setSpacing(8);
    auto *specialLabel = new QLabel(tr("Special exercise: Focus on your target pitch"), m_specialContainer);
    specialLabel->setWordWrap(true);
    specialLabel->setObjectName("sectionSubtitle");
    specialLayout->addWidget(specialLabel);
    auto *specialButtons = new QHBoxLayout();
    specialButtons->setSpacing(8);
    m_specialTargetButton = new QPushButton(tr("Target"), m_specialContainer);
    m_specialTargetButton->setFocusPolicy(Qt::NoFocus);
    m_specialTargetButton->setProperty("accent", true);
    m_specialTargetButton->setMinimumHeight(26);
    m_specialOtherButton = new QPushButton(tr("Other"), m_specialContainer);
    m_specialOtherButton->setFocusPolicy(Qt::NoFocus);
    m_specialOtherButton->setProperty("outline", true);
    m_specialOtherButton->setMinimumHeight(26);
    connect(m_specialTargetButton, &QPushButton::clicked, [this]() { handleSpecialResponse(true); });
    connect(m_specialOtherButton, &QPushButton::clicked, [this]() { handleSpecialResponse(false); });
    specialButtons->addWidget(m_specialTargetButton);
    specialButtons->addWidget(m_specialOtherButton);
    specialLayout->addLayout(specialButtons);
    layout->addWidget(m_specialContainer);
    m_specialContainer->hide();

    m_keyboardHintLabel = new QLabel(central);
    m_keyboardHintLabel->setWordWrap(true);
    m_keyboardHintLabel->setObjectName("hintLabel");
    m_keyboardHintLabel->setText(tr("Keyboard shortcuts: press note letters (A-G) for notes, hold Ctrl for sharps, Backspace for \"Other\", and 1 for \"Hear next tone\"."));
    layout->addWidget(m_keyboardHintLabel);

    auto *interactionTabs = new QTabWidget(central);
    interactionTabs->setObjectName("interactionTabs");
    interactionTabs->setTabPosition(QTabWidget::South);
    interactionTabs->setDocumentMode(true);
    interactionTabs->setMinimumHeight(220);
    interactionTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *responsePage = new QWidget(interactionTabs);
    auto *responsePageLayout = new QVBoxLayout(responsePage);
    responsePageLayout->setContentsMargins(0, 0, 0, 0);
    responsePageLayout->setSpacing(0);
    auto *responseFrame = new QFrame(responsePage);
    responseFrame->setObjectName("sectionFrame");
    auto *responseLayout = new QVBoxLayout(responseFrame);
    responseLayout->setContentsMargins(14, 10, 14, 10);
    responseLayout->setSpacing(8);
    auto *responseTitle = new QLabel(tr("Choose the note you heard"), responseFrame);
    responseTitle->setObjectName("sectionTitle");
    responseLayout->addWidget(responseTitle);
    m_responseContainer = new QWidget(responseFrame);
    m_responseContainer->setObjectName("responseContainer");
    m_responseContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_responseContainer->setMinimumHeight(160);
    auto *grid = new QGridLayout(m_responseContainer);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(10);
    m_responseButtons = new QButtonGroup(this);
    m_responseButtons->setExclusive(true);
    connect(m_responseButtons, &QButtonGroup::idClicked, this, &PitchTraining::handleResponse);
    responseLayout->addWidget(m_responseContainer);
    responsePageLayout->addWidget(responseFrame);
    interactionTabs->addTab(responsePage, tr("Tone pad"));

    auto *logPage = new QWidget(interactionTabs);
    auto *logPageLayout = new QVBoxLayout(logPage);
    logPageLayout->setContentsMargins(0, 0, 0, 0);
    logPageLayout->setSpacing(0);
    auto *logFrame = new QFrame(logPage);
    logFrame->setObjectName("sectionFrame");
    auto *logLayout = new QVBoxLayout(logFrame);
    logLayout->setContentsMargins(18, 14, 18, 14);
    logLayout->setSpacing(10);
    auto *logTitle = new QLabel(tr("Trial log"), logFrame);
    logTitle->setObjectName("sectionTitle");
    logLayout->addWidget(logTitle);
    m_trialLogList = new QListWidget(logFrame);
    m_trialLogList->setObjectName("trialLog");
    m_trialLogList->setSelectionMode(QAbstractItemView::NoSelection);
    m_trialLogList->setAlternatingRowColors(true);
    logLayout->addWidget(m_trialLogList);
    logPageLayout->addWidget(logFrame);
    interactionTabs->addTab(logPage, tr("Trial history"));

    layout->addWidget(interactionTabs);
    layout->setStretchFactor(interactionTabs, 1);

    resetTrialLog();
    refreshStartLevelButton();
}

void PitchTraining::applyTheme()
{
    const QString style = QStringLiteral(R"(
QMainWindow#PitchTraining {
    background-color: #f4f6fb;
}
QWidget#mainSurface {
    background-color: #f4f6fb;
}
QScrollArea {
    background-color: transparent;
    border: none;
}
QScrollArea > QWidget > QWidget {
    background-color: transparent;
}
QLabel {
    color: #0f172a;
    font-size: 13px;
}
QLabel#heroCaption {
    color: #475569;
    font-size: 11px;
    font-weight: 600;
}
QLabel#heroAboutLink {
    color: #0a58ca;
    font-size: 14px;
}
QLabel#heroAboutLink:hover {
    color: #0a58ca;
    text-decoration: underline;
}
QLabel#levelLabel {
    font-size: 22px;
    font-weight: 700;
    color: #0b1120;
}
QLabel#requirementLabel {
    color: #1f2933;
    font-size: 13px;
}
QFrame#heroFrame,
QFrame#sectionFrame,
QFrame#statCard,
QFrame#specialCard {
    border-radius: 12px;
    border: 1px solid #cfd8e3;
    background-color: #ffffff;
}
QWidget#responseContainer {
    border-radius: 10px;
    border: 1px solid #cbd5f5;
    background-color: #eef2ff;
}
QLabel#sectionTitle {
    font-size: 16px;
    font-weight: 600;
    color: #0f172a;
}
QLabel#sectionSubtitle {
    font-size: 12px;
    color: #475569;
}
QLabel#statusLabel {
    font-size: 14px;
}
QLabel#statTitle {
    font-size: 11px;
    color: #64748b;
    letter-spacing: 0.05em;
}
QLabel#statValue {
    font-size: 13px;
    font-weight: 600;
    color: #0f172a;
}
QLabel#feedbackLabel {
    border-radius: 5px;
    padding: 10px 12px;
    font-weight: 600;
    border: 1px solid #fbbf24;
    background-color: #fff7e6;
    color: #92400e;
}
QLabel#feedbackLabel[positive="true"] {
    background-color: #ecfdf5;
    color: #065f46;
    border-color: #34d399;
}
QLabel#feedbackLabel[positive="false"] {
    background-color: #fef2f2;
    color: #991b1b;
    border-color: #fca5a5;
}
QLabel#hintLabel {
    font-size: 13px;
    color: #0f172a;
    padding: 8px 12px;
    border-radius: 6px;
    background-color: #e0f2fe;
}
QProgressBar {
    background-color: #e5e7eb;
    border: 1px solid #cfd8e3;
    border-radius: 5px;
    padding: 2px;
    color: #0f172a;
    height: 17px;
}
QProgressBar::chunk {
    border-radius: 4px;
    background-color: #2563eb;
}
QProgressBar#responseProgress::chunk {
    background-color: #dc2626;
}
QPushButton {
    background-color: #f9fafb;
    border: 1px solid #94a3b8;
    border-radius: 4px;
    color: #0f172a;
    font-weight: 600;
    font-size: 13px;
    padding: 6px 12px;
}
QPushButton:hover:!disabled {
    background-color: #e2e8f0;
}
QPushButton:disabled {
    color: #94a3b8;
    border-color: #e2e8f0;
    background-color: #f4f4f5;
}
QPushButton[sessionRequired="true"]:disabled {
    background-color: #dfe3ea;
    border-color: #cbd5f5;
    color: #94a3b8;
}
QPushButton[noteButton="true"] {
    font-size: 15px;
    font-weight: 600;
    letter-spacing: 0.01em;
    background-color: #ffffff;
    color: #0f172a;
    border: 1px solid #1d4ed8;
    border-radius: 6px;
    padding: 6px 10px;
}
QPushButton[noteButton="true"]:hover {
    background-color: #e0f2fe;
}
QPushButton[noteButton="true"]:disabled {
    background-color: #f1f5f9;
    border-color: #cbd5f5;
    color: #94a3b8;
}
QPushButton[primary="true"] {
    background-color: #1d4ed8;
    color: #ffffff;
    border-color: #1d4ed8;
    padding: 6px 14px;
}
QPushButton[primary="true"]:hover {
    background-color: #1e40af;
}
QPushButton[accent="true"] {
    background-color: #047857;
    color: #ffffff;
    border-color: #047857;
}
QPushButton[accent="true"]:hover {
    background-color: #036749;
}
QPushButton[outline="true"] {
    background-color: transparent;
    border-color: #0f172a;
    color: #0f172a;
}
QPushButton[outline="true"]:hover {
    background-color: #e2e8f0;
}
QPushButton[link="true"] {
    background-color: transparent;
    border: none;
    color: #0a58ca;
    font-size: 14px;
    font-weight: 600;
    padding: 0;
}
QPushButton[link="true"]:hover {
    text-decoration: underline;
}
QComboBox {
    background-color: #ffffff;
    border: 1px solid #94a3b8;
    border-radius: 5px;
    padding: 5px 26px 5px 10px;
    color: #0f172a;
    font-size: 13px;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background-color: #ffffff;
    color: #0f172a;
    selection-background-color: #e0f2fe;
    selection-color: #0c4a6e;
    border: 1px solid #cbd5f5;
    font-size: 13px;
}
QListWidget#trialLog {
    background-color: #ffffff;
    border: 1px solid #d1d5db;
    border-radius: 8px;
    padding: 8px;
    color: #111827;
}
QListWidget#trialLog::item {
    padding: 6px 8px;
}
QListWidget#trialLog::item:alternate {
    background-color: #f3f4f6;
}
QListWidget#trialLog::item:selected {
    background-color: #dbeafe;
    color: #1e3a8a;
}
QScrollBar:vertical, QScrollBar:horizontal {
    background: transparent;
    width: 10px;
    margin: 4px;
}
QScrollBar::handle {
    background: #cbd5f5;
    border-radius: 5px;
}
QScrollBar::handle:hover {
    background: #94a3b8;
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0px;
    height: 0px;
}
QTabWidget#interactionTabs::pane {
    border: none;
}
QTabWidget#interactionTabs > QTabBar {
    alignment: center;
}
QTabBar::tab {
    background: transparent;
    border: none;
    color: #4b5563;
    padding: 8px 16px;
    margin: 0 6px;
    font-weight: 500;
}
QTabBar::tab:selected {
    color: #1d4ed8;
    border-bottom: 2px solid #1d4ed8;
}
QTabBar::tab:hover:!selected {
    color: #111827;
}
    )");
    setStyleSheet(style);
}

QMessageBox::StandardButton PitchTraining::showMessage(QMessageBox::Icon icon,
                                                       const QString &title,
                                                       const QString &text,
                                                       QMessageBox::StandardButtons buttons,
                                                       QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(this);
    box.setIcon(icon);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(buttons);
    box.setDefaultButton(defaultButton);
    box.setPalette(QApplication::style()->standardPalette());
    box.setStyleSheet(QStringLiteral(
        "QLabel { color: #0f172a; }"
        "QLabel a { color: #0a58ca; }"
        "QLabel a:visited { color: #0a58ca; }"
        "QPushButton { color: #0f172a; }"));
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

void PitchTraining::refreshProfileControls()
{
    if (!m_profileCombo) {
        return;
    }
    const auto profiles = m_profileManager.profiles();
    QSignalBlocker blocker(m_profileCombo);
    m_profileCombo->clear();
    int activeIndex = -1;
    for (const auto &profile : profiles) {
        m_profileCombo->addItem(profile.name, profile.id);
        if (profile.id == m_profileManager.activeProfileId()) {
            activeIndex = m_profileCombo->count() - 1;
        }
    }
    if (activeIndex >= 0) {
        m_profileCombo->setCurrentIndex(activeIndex);
    }
    if (m_deleteProfileButton) {
        m_deleteProfileButton->setEnabled(profiles.size() > 1);
    }
}

void PitchTraining::applyActiveProfile()
{
    m_state.setProfileDirectory(m_profileManager.activeProfileDirectory());
    m_state.load();
    if (m_responseTimer) {
        m_responseTimer->stop();
    }
    if (m_responseProgressTimer) {
        m_responseProgressTimer->stop();
    }
    m_tonePlayer.stop();
    resetLevelState();
    m_levelActive = false;
    m_waitingForShepard = false;
    m_specialContext = SpecialContext{};
    m_mode = SessionMode::Idle;
    if (m_startTrialButton) {
        m_startTrialButton->setEnabled(false);
    }
    if (m_sampleButton) {
        m_sampleButton->setEnabled(false);
    }
    if (m_doubleButton) {
        m_doubleButton->setEnabled(false);
    }
    if (m_trialProgress) {
        m_trialProgress->setValue(0);
    }
    if (m_feedbackLabel) {
        m_feedbackLabel->clear();
    }
    setResponseEnabled(false, false);
    refreshStateLabels();
    updateLevelDescription();
    rebuildResponseButtons();
    refreshStartLevelButton();
}

bool PitchTraining::switchProfile(const QString &profileId)
{
    if (profileId.isEmpty() || profileId == m_profileManager.activeProfileId()) {
        return false;
    }
    if (!m_profileManager.setActiveProfile(profileId)) {
        return false;
    }
    applyActiveProfile();
    refreshProfileControls();
    return true;
}

void PitchTraining::resetTrialLog()
{
    m_trialLogHasEntries = false;
    if (!m_trialLogList) {
        return;
    }
    m_trialLogList->clear();
    auto *placeholder = new QListWidgetItem(tr("No trials yet. Press \"Hear next tone\" to begin."));
    placeholder->setFlags(Qt::NoItemFlags);
    placeholder->setForeground(QColor(QStringLiteral("#94a3b8")));
    m_trialLogList->addItem(placeholder);
}

void PitchTraining::appendTrialLogEntry(int trialNumber, const QString &description, bool positive)
{
    if (!m_trialLogList) {
        return;
    }
    if (!m_trialLogHasEntries) {
        m_trialLogList->clear();
        m_trialLogHasEntries = true;
    }
    auto *item = new QListWidgetItem(tr("Trial %1: %2").arg(trialNumber).arg(description));
    item->setForeground(QColor(positive ? QStringLiteral("#1b5e20") : QStringLiteral("#b71c1c")));
    m_trialLogList->addItem(item);
    m_trialLogList->scrollToBottom();
}

void PitchTraining::clearActiveResponses()
{
    if (!m_responseButtons) {
        return;
    }
    const bool wasExclusive = m_responseButtons->exclusive();
    if (wasExclusive) {
        m_responseButtons->setExclusive(false);
    }
    const auto buttons = m_responseButtons->buttons();
    for (auto *button : buttons) {
        if (button) {
            button->setChecked(false);
        }
    }
    if (wasExclusive) {
        m_responseButtons->setExclusive(true);
    }
}

void PitchTraining::setResponseEnabled(bool levelEnabled, bool specialEnabled)
{
    if (m_responseContainer) {
        m_responseContainer->setEnabled(levelEnabled);
    }
    if (m_specialContainer) {
        m_specialContainer->setEnabled(specialEnabled);
    }
    clearActiveResponses();
}

void PitchTraining::setControlsEnabled(bool enabled)
{
    if (ui && ui->centralwidget) {
        ui->centralwidget->setEnabled(enabled);
    }
}

void PitchTraining::refreshStartLevelButton()
{
    if (!m_startLevelButton) {
        return;
    }
    const bool canStart = m_sessionActive && !m_levelActive && !m_specialContext.active && !m_waitingForShepard;
    m_startLevelButton->setEnabled(canStart);
}

void PitchTraining::updateResponseTimeBar()
{
    if (!m_responseProgress) {
        return;
    }
    if (m_currentResponseWindowMs <= 0 || !(m_levelActive || m_specialContext.active)) {
        if (m_responseProgressTimer) {
            m_responseProgressTimer->stop();
        }
        m_responseProgress->setMaximum(100);
        m_responseProgress->setValue(0);
        m_responseProgress->setFormat(tr("Time left"));
        return;
    }
    const int elapsed = m_trialTimer.isValid() ? m_trialTimer.elapsed() : 0;
    const int remaining = qMax(0, m_currentResponseWindowMs - elapsed);
    m_responseProgress->setMaximum(m_currentResponseWindowMs);
    m_responseProgress->setValue(remaining);
    m_responseProgress->setFormat(tr("Time left: %1 s").arg(remaining / 1000.0, 0, 'f', 1));
    if (remaining <= 0) {
        if (m_responseProgressTimer) {
            m_responseProgressTimer->stop();
        }
    } else {
        // keep bar visible; values already updated
    }
}

QString PitchTraining::pitchFromKeyEvent(QKeyEvent *event, bool &isOther) const
{
    isOther = false;
    if (!event) {
        return {};
    }
    const int key = event->key();
    QString pitch;
    switch (key) {
    case Qt::Key_C:
        pitch = QStringLiteral("C");
        break;
    case Qt::Key_D:
        pitch = QStringLiteral("D");
        break;
    case Qt::Key_E:
        pitch = QStringLiteral("E");
        break;
    case Qt::Key_F:
        pitch = QStringLiteral("F");
        break;
    case Qt::Key_G:
        pitch = QStringLiteral("G");
        break;
    case Qt::Key_A:
        pitch = QStringLiteral("A");
        break;
    case Qt::Key_B:
        pitch = QStringLiteral("B");
        break;
    case Qt::Key_Backspace:
        isOther = true;
        break;
    default:
        break;
    }
    if (!pitch.isEmpty() && (event->modifiers() & Qt::ControlModifier)) {
        pitch += QLatin1Char('#');
    }
    return pitch;
}

bool PitchTraining::handleLevelKeyResponse(const QString &pitch, bool isOther)
{
    if (!m_levelActive || !m_responseButtons) {
        return false;
    }
    if (!isOther && pitch.isEmpty()) {
        return false;
    }
    for (auto *button : m_responseButtons->buttons()) {
        if (!button) {
            continue;
        }
        const bool isOutButton = button->property("outOfBound").toBool();
        if (isOther && isOutButton) {
            button->click();
            return true;
        }
        if (!isOther && !pitch.isEmpty()) {
            const QString buttonPitch = button->property("pitch").toString();
            if (!buttonPitch.isEmpty() && buttonPitch.compare(pitch, Qt::CaseInsensitive) == 0) {
                button->click();
                return true;
            }
        }
    }
    return false;
}

bool PitchTraining::handleSpecialKeyResponse(const QString &pitch, bool isOther)
{
    if (!m_specialContext.active) {
        return false;
    }
    if (isOther) {
        handleSpecialResponse(false);
        return true;
    }
    if (!pitch.isEmpty() && pitch.compare(m_specialContext.targetPitch, Qt::CaseInsensitive) == 0) {
        handleSpecialResponse(true);
        return true;
    }
    return false;
}

bool PitchTraining::handleShortcutKey(QKeyEvent *event)
{
    if (!event) {
        return false;
    }
    QWidget *activeWindow = QApplication::activeWindow();
    if (activeWindow && activeWindow != this) {
        return false;
    }

    const int key = event->key();
    if (key == Qt::Key_1) {
        if (m_startTrialButton && m_startTrialButton->isEnabled()) {
            handleStartTrial();
        }
        event->accept();
        return true;
    }

    const bool canRespond = (m_levelActive && m_responseContainer && m_responseContainer->isEnabled()) ||
                            (m_specialContext.active && m_specialContainer && m_specialContainer->isEnabled());
    const auto consumesShortcut = [](int k) {
        switch (k) {
        case Qt::Key_Space:
        case Qt::Key_Backspace:
        case Qt::Key_1:
        case Qt::Key_Control:
        case Qt::Key_Shift:
        case Qt::Key_Alt:
        case Qt::Key_A:
        case Qt::Key_B:
        case Qt::Key_C:
        case Qt::Key_D:
        case Qt::Key_E:
        case Qt::Key_F:
        case Qt::Key_G:
            return true;
        default:
            return false;
        }
    };

    if (!canRespond) {
        if (consumesShortcut(key)) {
            event->accept();
            return true;
        }
        return false;
    }

    bool isOther = false;
    const QString pitch = pitchFromKeyEvent(event, isOther);
    if (!isOther && pitch.isEmpty()) {
        if (consumesShortcut(key)) {
            event->accept();
            return true;
        }
        return false;
    }

    bool handled = false;
    if (m_mode == SessionMode::Level && m_levelActive && m_responseContainer && m_responseContainer->isEnabled()) {
        handled = handleLevelKeyResponse(pitch, isOther);
    } else if (m_mode == SessionMode::SpecialExercise && m_specialContext.active && m_specialContainer && m_specialContainer->isEnabled()) {
        handled = handleSpecialKeyResponse(pitch, isOther);
    }

    if (handled) {
        event->accept();
    }
    return handled;
}

void PitchTraining::keyPressEvent(QKeyEvent *event)
{
    if (handleShortcutKey(event)) {
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool PitchTraining::eventFilter(QObject *watched, QEvent *event)
{
    if (event && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (handleShortcutKey(keyEvent)) {
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void PitchTraining::refreshStateLabels()
{
    m_tokensLabel->setText(QString::number(m_state.tokens()));
    m_streakLabel->setText(tr("%1 day(s)").arg(m_state.streakCount()));
    m_hoursLabel->setText(tr("%1 h").arg(QString::number(m_state.countedTrainingHours(), 'f', 2)));
    if (m_sessionActive) {
        const int secs = static_cast<int>(m_sessionTimer.elapsed() / 1000);
        m_sessionLabel->setText(tr("%1 min active").arg(secs / 60));
    } else {
        m_sessionLabel->setText(tr("Idle"));
    }
}

void PitchTraining::updateLevelDescription()
{
    const auto spec = TrainingSpec::specForIndex(m_state.currentLevelIndex());
    m_levelLabel->setText(tr("Level %1 of %2 • Stage %3")
                              .arg(spec.globalIndex + 1)
                              .arg(TrainingSpec::totalLevelCount())
                              .arg(spec.stageIndex));
    m_requirementLabel->setText(tr("Accuracy ≥ %1% • Response window %2 ms • Tokens %3 • Feedback %4")
                                    .arg(static_cast<int>(spec.passAccuracy * 100))
                                    .arg(spec.responseWindowMs)
                                    .arg(spec.tokensAllowed ? tr("enabled") : tr("disabled"))
                                    .arg(spec.feedback ? tr("on") : tr("off")));
}

void PitchTraining::rebuildResponseButtons()
{
    if (!m_responseContainer->layout()) {
        m_responseContainer->setLayout(new QGridLayout());
    }
    auto *grid = qobject_cast<QGridLayout *>(m_responseContainer->layout());
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    while (auto *item = grid->takeAt(0)) {
        if (auto *widget = item->widget()) {
            m_responseButtons->removeButton(qobject_cast<QAbstractButton *>(widget));
            widget->deleteLater();
        }
        delete item;
    }
    const auto pitches = TrainingSpec::stagePitchSet(TrainingSpec::specForIndex(m_state.currentLevelIndex()).stageIndex);
    const int columns = 4;
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
    int row = 0;
    int column = 0;
    int id = 0;
    for (const auto &pitch : pitches) {
        auto *btn = new QPushButton(pitch, m_responseContainer);
        btn->setProperty("pitch", pitch);
        btn->setCheckable(true);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setProperty("noteButton", true);
        btn->setMinimumHeight(30);
        grid->addWidget(btn, row, column);
        m_responseButtons->addButton(btn, id++);
        if (++column >= columns) {
            column = 0;
            ++row;
        }
    }
    if (column != 0) {
        ++row;
        column = 0;
    }
    auto *oob = new QPushButton(tr("Other"), m_responseContainer);
    oob->setProperty("outOfBound", true);
    oob->setCheckable(true);
    oob->setFocusPolicy(Qt::NoFocus);
    oob->setProperty("noteButton", true);
    oob->setMinimumHeight(30);
    grid->addWidget(oob, row, 0, 1, columns);
    m_responseButtons->addButton(oob, id);
}

void PitchTraining::handleStartLevel()
{
    if (m_levelActive || m_specialContext.active) {
        return;
    }

    if (!m_sessionActive) {
        updateFeedback(tr("Start a 15-min session before beginning a level."), false);
        if (m_sessionButton) {
            m_sessionButton->setFocus();
        }
        refreshStartLevelButton();
        return;
    }

    if (shouldRunSpecialExercise()) {
        startSpecialExercise();
        return;
    }

    const auto spec = TrainingSpec::specForIndex(m_state.currentLevelIndex());
    if (spec.globalIndex == TrainingSpec::totalLevelCount() - 1 &&
        m_state.finalLevelConsecutivePasses() >= 3 &&
        !m_state.trainingCompleted()) {
        const auto last = m_state.finalLevelCooldownStart();
        const auto now = QDateTime::currentDateTimeUtc();
        if (!last.isValid() || last.secsTo(now) < 12 * 3600) {
            updateFeedback(tr("Wait at least 12 h after the third clear before the final attempt."), false);
            return;
        }
    }

    startLevelInternal();
}

void PitchTraining::startLevelInternal()
{
    m_currentSpec = TrainingSpec::specForIndex(m_state.currentLevelIndex());
    m_trainingPitches = TrainingSpec::stagePitchSet(m_currentSpec.stageIndex);
    m_outOfBoundsPitches = TrainingSpec::outOfBoundsForStage(m_currentSpec.stageIndex);
    rebuildResponseButtons();
    resetLevelState();
    setResponseEnabled(false, false);
    m_mode = SessionMode::Level;
    m_levelActive = true;
    m_requiredTrials = m_currentSpec.trialCount;
    m_trialProgress->setRange(0, m_requiredTrials);
    m_trialProgress->setValue(0);
    m_statusLabel->setText(tr("Level in progress. Press \"Hear next tone\" to hear a tone."));
    m_startTrialButton->setEnabled(true);
    m_sampleButton->setEnabled(true);
    m_doubleButton->setEnabled(m_currentSpec.tokensAllowed);
    scheduleShepardIfNeeded();
    updateLevelDescription();
    refreshStartLevelButton();
}

void PitchTraining::resetLevelState()
{
    m_trialLog.clear();
    m_trialsCompleted = 0;
    m_correctTrials = 0;
    m_effectiveBonus = 0.0;
    m_doubleArmed = false;
    m_randomDouble = false;
    m_waitingForShepard = false;
    m_samplesQueued = false;
    m_sampleQueue.clear();
    m_currentTrial = TrialData{};
    if (m_feedbackLabel) {
        m_feedbackLabel->clear();
    }
    m_currentResponseWindowMs = 0;
    if (m_responseProgressTimer) {
        m_responseProgressTimer->stop();
    }
    if (m_responseProgress) {
        m_responseProgress->setMaximum(100);
        m_responseProgress->setValue(0);
        m_responseProgress->setFormat(tr("Time left"));
    }
    resetTrialLog();
}

bool PitchTraining::shouldRunSpecialExercise() const
{
    const auto spec = TrainingSpec::specForIndex(m_state.currentLevelIndex());
    return m_state.levelsSinceSpecial() >= 15 && spec.stageIndex >= 5;
}

void PitchTraining::scheduleShepardIfNeeded()
{
    const bool shouldPlayShepard = (m_mode == SessionMode::Level && !m_currentSpec.feedback);
    if (shouldPlayShepard) {
        playShepardTone();
    }
}

void PitchTraining::playShepardTone()
{
    m_waitingForShepard = true;
    m_playbackContext = PlaybackContext::Shepard;
    m_startTrialButton->setEnabled(false);
    m_statusLabel->setText(tr("Memory reset: Shepard tone playing for 20 s"));
    updateFeedback(tr("Memory reset tone playing..."), true);
    setControlsEnabled(false);
    refreshStartLevelButton();
    m_tonePlayer.playSample(m_toneLibrary.shepardTone());
}

void PitchTraining::handleStartTrial()
{
    if (!(m_levelActive || m_specialContext.active) || m_waitingForShepard) {
        return;
    }
    prepareNextTrial();
}

void PitchTraining::prepareNextTrial()
{
    if (m_startTrialButton) {
        m_startTrialButton->setEnabled(false);
    }
    setResponseEnabled(false, false);
    m_playbackContext = PlaybackContext::Trial;
    m_currentTrial = TrialData{};

    if (m_mode == SessionMode::SpecialExercise) {
        const bool playTarget = QRandomGenerator::global()->bounded(2) == 0;
        if (playTarget || m_outOfBoundsPitches.isEmpty()) {
            m_currentTrial.presentedPitch = m_specialContext.targetPitch;
            m_currentTrial.outOfBounds = false;
        } else {
            const auto fallback = m_outOfBoundsPitches.at(QRandomGenerator::global()->bounded(m_outOfBoundsPitches.size()));
            m_currentTrial.presentedPitch = fallback;
            m_currentTrial.outOfBounds = true;
        }
    } else {
        QVector<QString> pool = m_trainingPitches;
        for (const auto &name : m_outOfBoundsPitches) {
            if (!pool.contains(name)) {
                pool.append(name);
            }
        }
        const int index = QRandomGenerator::global()->bounded(pool.size());
        m_currentTrial.presentedPitch = pool.at(index);
        m_currentTrial.outOfBounds = m_outOfBoundsPitches.contains(m_currentTrial.presentedPitch);
        m_randomDouble = m_currentSpec.tokensAllowed && QRandomGenerator::global()->bounded(80) == 0;
        if (m_randomDouble) {
            updateFeedback(tr("Lucky double bonus ready!"), true);
        }
    }

    const auto octaves = m_toneLibrary.supportedOctaves();
    if (!octaves.isEmpty()) {
        const int octaveIndex = QRandomGenerator::global()->bounded(octaves.size());
        m_currentTrial.octave = octaves.at(octaveIndex);
    }
    m_tonePlayer.playSample(m_toneLibrary.toneFor(m_currentTrial.presentedPitch, m_currentTrial.octave));
    m_trialTimer.restart();
    const int window = m_currentSpec.responseWindowMs;
    m_responseTimer->start(window);
    m_currentResponseWindowMs = window;
    if (m_responseProgress) {
        m_responseProgress->setMaximum(window);
        m_responseProgress->setValue(window);
        m_responseProgress->setFormat(tr("Time left: %1 s").arg(window / 1000.0, 0, 'f', 1));
        // already visible
    }
    if (m_responseProgressTimer) {
        m_responseProgressTimer->start();
    }
    updateResponseTimeBar();
    m_statusLabel->setText(tr("Tone presented. Identify it."));
    m_responseContainer->setVisible(m_mode == SessionMode::Level);
    m_specialContainer->setVisible(m_mode == SessionMode::SpecialExercise);
    if (m_mode == SessionMode::SpecialExercise) {
        setResponseEnabled(false, true);
    } else {
        setResponseEnabled(true, false);
    }
}

void PitchTraining::handleResponse(int buttonId)
{
    if (!m_levelActive || m_mode != SessionMode::Level) {
        return;
    }
    auto *button = m_responseButtons->button(buttonId);
    if (!button) {
        return;
    }
    const bool isOut = button->property("outOfBound").toBool();
    const QString pitch = button->property("pitch").toString();
    m_currentTrial.response = isOut ? QStringLiteral("OUT") : pitch;
    finishCurrentTrial();
}

void PitchTraining::handleSpecialResponse(bool isTarget)
{
    if (!m_specialContext.active) {
        return;
    }
    m_currentTrial.response = isTarget ? m_specialContext.targetPitch : QStringLiteral("OUT");
    finishCurrentTrial();
}

void PitchTraining::handleResponseTimeout()
{
    finishCurrentTrial(true);
}

void PitchTraining::finishCurrentTrial(bool timedOut)
{
    if (!(m_levelActive || m_specialContext.active)) {
        return;
    }
    m_responseTimer->stop();
    m_currentTrial.timedOut = timedOut;
    m_currentTrial.responseTimeMs = m_trialTimer.isValid() ? static_cast<int>(m_trialTimer.elapsed()) : 0;
    const int trialNumber = m_trialsCompleted + 1;

    bool correct = false;
    if (!timedOut) {
        if (m_mode == SessionMode::SpecialExercise) {
            const bool targetTone = !m_currentTrial.outOfBounds;
            const bool answeredTarget = (m_currentTrial.response == m_specialContext.targetPitch);
            correct = (targetTone && answeredTarget) || (!targetTone && m_currentTrial.response == QStringLiteral("OUT"));
        } else {
            if (m_currentTrial.outOfBounds) {
                correct = m_currentTrial.response == QStringLiteral("OUT");
            } else {
                correct = m_currentTrial.response == m_currentTrial.presentedPitch;
                if (!correct) {
                    const auto &order = TrainingSpec::chromaticOrder();
                    const int played = order.indexOf(m_currentTrial.presentedPitch);
                    const int answered = order.indexOf(m_currentTrial.response);
                    if (played >= 0 && answered >= 0 && std::abs(played - answered) == 1) {
                        m_currentTrial.semitoneError = true;
                    }
                }
            }
        }
    }

    m_currentTrial.correct = correct;
    if (correct && m_mode != SessionMode::SpecialExercise) {
        ++m_correctTrials;
    }

    if (correct) {
        bool appliedBonus = false;
        if (m_doubleArmed) {
            m_effectiveBonus += 1.0;
            appliedBonus = true;
            m_currentTrial.usedDouble = true;
            m_doubleArmed = false;
        }
        if (m_randomDouble && m_currentSpec.tokensAllowed && !appliedBonus) {
            m_effectiveBonus += 1.0;
            appliedBonus = true;
            m_currentTrial.luckyDouble = true;
        }
    } else {
        m_doubleArmed = false;
    }
    m_randomDouble = false;

    if (!correct && timedOut) {
        updateFeedback(tr("Time up!"), false);
    } else if (!correct) {
        updateFeedback(tr("Incorrect."), false);
    } else {
        updateFeedback(tr("Correct"), true);
    }
    for (auto *button : m_responseButtons->buttons()) {
        if (button) {
            button->setChecked(false);
        }
    }
    m_currentResponseWindowMs = 0;
    if (m_responseProgressTimer) {
        m_responseProgressTimer->stop();
    }
    if (m_responseProgress) {
        m_responseProgress->setMaximum(100);
        m_responseProgress->setValue(0);
        m_responseProgress->setFormat(tr("Time left"));
    }
    setResponseEnabled(false, false);

    const QString actualDisplay = m_currentTrial.outOfBounds ? tr("other") : m_currentTrial.presentedPitch;
    const QString responseDisplay = m_currentTrial.response.isEmpty()
                                       ? tr("none")
                                       : (m_currentTrial.response == QStringLiteral("OUT") ? tr("other") : m_currentTrial.response);
    QString logText;
    bool positiveLog = correct && !timedOut;
    if (timedOut) {
        logText = tr("Time expired (target %1)").arg(actualDisplay);
        positiveLog = false;
    } else if (correct) {
        logText = tr("Correct (%1)").arg(actualDisplay);
    } else {
        logText = tr("Incorrect (target %1, answered %2)").arg(actualDisplay, responseDisplay);
    }
    if (m_mode == SessionMode::SpecialExercise) {
        logText = tr("[Special] %1").arg(logText);
    }
    appendTrialLogEntry(trialNumber, logText, positiveLog);

    m_trialLog.append(m_currentTrial);
    ++m_trialsCompleted;
    updateProgress();

    const int needed = (m_mode == SessionMode::SpecialExercise) ? m_specialContext.totalTrials : m_requiredTrials;
    if (m_trialsCompleted >= needed) {
        if (m_mode == SessionMode::SpecialExercise) {
            resolveSpecialExercise();
        } else {
            resolveLevelCompletion();
        }
    } else {
        m_startTrialButton->setEnabled(true);
    }
}

void PitchTraining::updateProgress()
{
    const int maximum = (m_mode == SessionMode::SpecialExercise) ? m_specialContext.totalTrials : m_requiredTrials;
    m_trialProgress->setMaximum(maximum);
    m_trialProgress->setValue(m_trialsCompleted);
}

void PitchTraining::resolveLevelCompletion()
{
    const double actualAccuracy = m_requiredTrials == 0 ? 0.0 : static_cast<double>(m_correctTrials) / m_requiredTrials;
    const double effectiveAccuracy = m_requiredTrials == 0 ? 0.0 : qMin(1.0, (m_correctTrials + m_effectiveBonus) / m_requiredTrials);
    const bool passed = effectiveAccuracy >= m_currentSpec.passAccuracy;

    if (passed) {
        updateFeedback(tr("Level passed at %1% accuracy.").arg(static_cast<int>(effectiveAccuracy * 100)), true);
    } else {
        updateFeedback(tr("Level failed (%1% accuracy). Keep going!").arg(static_cast<int>(effectiveAccuracy * 100)), false);
    }

    static const QVector<double> kTokenThresholds = {0.60, 0.75, 0.90};
    int earned = 0;
    for (double threshold : kTokenThresholds) {
        if (actualAccuracy >= threshold) {
            ++earned;
        }
    }
    if (earned > 0) {
        m_state.addTokens(earned);
    }

    m_state.incrementLevelAttempts();
    recordSummary(false, actualAccuracy, passed);
    m_state.incrementLevelsSinceSpecial();
    m_state.markActivity();

    m_levelActive = false;
    m_mode = SessionMode::Idle;
    m_startTrialButton->setEnabled(false);
    m_sampleButton->setEnabled(false);
    m_doubleButton->setEnabled(false);
    setResponseEnabled(false, false);
    refreshStartLevelButton();

    int nextLevel = m_state.currentLevelIndex();
    if (passed) {
        const auto &specs = TrainingSpec::levelSpecs();
        const double achieved = actualAccuracy;
        nextLevel = qMin(m_state.currentLevelIndex() + 1, TrainingSpec::totalLevelCount() - 1);
        for (int idx = m_state.currentLevelIndex() + 1; idx < specs.size(); ++idx) {
            const auto &candidate = specs.at(idx);
            if (candidate.stageIndex != m_currentSpec.stageIndex) {
                nextLevel = idx;
                break;
            }
            if (candidate.feedback != m_currentSpec.feedback) {
                break;
            }
            nextLevel = idx;
            if (achieved < candidate.passAccuracy) {
                break;
            }
        }
    }
    m_state.setCurrentLevelIndex(nextLevel);

    if (m_currentSpec.globalIndex == TrainingSpec::totalLevelCount() - 1) {
        if (passed) {
            auto passes = m_state.finalLevelConsecutivePasses() + 1;
            m_state.setFinalLevelConsecutivePasses(passes);
            if (passes == 3) {
                m_state.setFinalLevelCooldownStart(QDateTime::currentDateTimeUtc());
                updateFeedback(tr("Final level cleared three times. Wait 12 h, then clear it once more."), true);
            } else if (passes > 3) {
                const auto last = m_state.finalLevelCooldownStart();
                if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) >= 12 * 3600) {
                    m_state.setTrainingCompleted(true);
                    updateFeedback(tr("Congratulations! Training sequence completed."), true);
                }
            }
        } else {
            m_state.setFinalLevelConsecutivePasses(0);
            m_state.setFinalLevelCooldownStart(QDateTime());
        }
    }

    refreshStateLabels();
    updateLevelDescription();
    m_state.save();
}

void PitchTraining::recordSummary(bool specialExercise, double accuracy, bool passed)
{
    LevelSummary summary;
    summary.levelIndex = m_currentSpec.globalIndex;
    summary.accuracy = accuracy;
    summary.passed = passed;
    summary.specialExercise = specialExercise;
    summary.completedAt = QDateTime::currentDateTime();

    for (const auto &trial : m_trialLog) {
        const QString key = trial.outOfBounds ? QStringLiteral("OUT") : trial.presentedPitch;
        auto stats = summary.perPitch.value(key);
        ++stats.totalTrials;
        if (trial.correct) {
            ++stats.correctTrials;
        }
        summary.perPitch.insert(key, stats);
    }

    m_state.recordLevelSummary(summary);
    m_trialLog.clear();
}

void PitchTraining::startSpecialExercise()
{
    m_currentSpec = TrainingSpec::specForIndex(m_state.currentLevelIndex());
    m_specialContext = SpecialContext{};
    m_specialContext.active = true;
    m_specialContext.feedbackPhase = true;
    m_specialContext.totalTrials = 12;
    m_specialContext.secondPhasePending = true;
    m_mode = SessionMode::SpecialExercise;
    m_levelActive = false;

    const QString weakest = m_state.leastAccuratePitch();
    if (!weakest.isEmpty()) {
        m_specialContext.targetPitch = weakest;
    } else if (!m_trainingPitches.isEmpty()) {
        m_specialContext.targetPitch = m_trainingPitches.first();
    } else {
        m_specialContext.targetPitch = QStringLiteral("F");
    }

    m_statusLabel->setText(tr("Special exercise: lock onto pitch %1").arg(m_specialContext.targetPitch));
    m_specialTargetButton->setText(tr("This is %1").arg(m_specialContext.targetPitch));
    m_specialOtherButton->setText(tr("Not %1").arg(m_specialContext.targetPitch));
    m_specialContainer->show();
    m_responseContainer->hide();
    m_startTrialButton->setEnabled(true);
    resetLevelState();
    setResponseEnabled(false, false);
    refreshStartLevelButton();
}

void PitchTraining::resolveSpecialExercise()
{
    if (m_specialContext.feedbackPhase && m_specialContext.secondPhasePending) {
        m_specialContext.feedbackPhase = false;
        m_specialContext.totalTrials = 22;
        m_specialContext.secondPhasePending = false;
        m_trialsCompleted = 0;
        m_trialLog.clear();
        m_currentResponseWindowMs = 0;
        if (m_responseProgressTimer) {
            m_responseProgressTimer->stop();
        }
        if (m_responseProgress) {
            m_responseProgress->setMaximum(100);
            m_responseProgress->setValue(0);
            m_responseProgress->setFormat(tr("Time left"));
        }
        resetTrialLog();
        m_statusLabel->setText(tr("Special exercise phase 2: no feedback."));
        m_startTrialButton->setEnabled(true);
        setResponseEnabled(false, false);
        return;
    }

    recordSummary(true, 0.0, true);
    m_specialContext = SpecialContext{};
    m_mode = SessionMode::Idle;
    m_state.resetLevelsSinceSpecial();
    m_waitingForShepard = false;
    m_specialContainer->hide();
    m_responseContainer->show();
    m_startTrialButton->setEnabled(false);
    m_statusLabel->setText(tr("Special exercise done. Resume main training."));
    setResponseEnabled(false, false);
    m_state.save();
    refreshStartLevelButton();
}

void PitchTraining::handlePlaybackFinished()
{
    if (m_playbackContext == PlaybackContext::Sample) {
        playNextSample();
    } else if (m_playbackContext == PlaybackContext::Shepard) {
        m_waitingForShepard = false;
        m_playbackContext = PlaybackContext::None;
        m_statusLabel->setText(tr("Memory reset complete. Start the trials."));
        m_startTrialButton->setEnabled(true);
        setControlsEnabled(true);
        refreshStartLevelButton();
    } else {
        m_playbackContext = PlaybackContext::None;
    }
}

void PitchTraining::handleSampleButton()
{
    if (!m_levelActive) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Pick a pitch to preview"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Select a pitch to hear all of its octaves."), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *scroll = new QScrollArea(&dialog);
    auto *inner = new QWidget(scroll);
    auto *innerLayout = new QVBoxLayout(inner);
    for (const auto &pitch : m_trainingPitches) {
        auto *btn = new QPushButton(pitch, inner);
        connect(btn, &QPushButton::clicked, &dialog, [this, &dialog, pitch]() {
            enqueueSamplePlayback(pitch);
            dialog.accept();
        });
        innerLayout->addWidget(btn);
    }
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    layout->addWidget(scroll);
    dialog.exec();
    if (!m_currentSpec.feedback && !m_waitingForShepard) {
        playShepardTone();
    }
}

void PitchTraining::enqueueSamplePlayback(const QString &pitch)
{
    m_sampleQueue.clear();
    const auto octaves = m_toneLibrary.supportedOctaves();
    for (int octave : octaves) {
        m_sampleQueue.append(QStringLiteral("%1@%2").arg(pitch).arg(octave));
    }
    std::mt19937 engine(QRandomGenerator::global()->generate());
    std::shuffle(m_sampleQueue.begin(), m_sampleQueue.end(), engine);
    playNextSample();
}

void PitchTraining::playNextSample()
{
    if (m_sampleQueue.isEmpty()) {
        m_samplesQueued = false;
        m_playbackContext = PlaybackContext::None;
        m_statusLabel->setText(tr("Sample playback finished."));
        return;
    }
    m_samplesQueued = true;
    m_playbackContext = PlaybackContext::Sample;
    const auto entry = m_sampleQueue.takeFirst();
    const auto parts = entry.split('@');
    if (parts.size() != 2) {
        return;
    }
    const QString pitch = parts.first();
    const int octave = parts.last().toInt();
    m_statusLabel->setText(tr("Sample: %1 (octave %2)").arg(pitch).arg(octave));
    m_tonePlayer.playSample(m_toneLibrary.toneFor(pitch, octave));
}

void PitchTraining::handleSessionToggle()
{
    if (m_sessionActive) {
        concludeSessionIfNeeded();
    } else {
        m_sessionActive = true;
        m_sessionStart = QDateTime::currentDateTime();
        m_sessionTimer.start();
        m_sessionButton->setText(tr("End session"));
    }
    refreshStateLabels();
    refreshStartLevelButton();
}

void PitchTraining::handleProfileSelection(int index)
{
    if (!m_profileCombo) {
        return;
    }
    if (index < 0 || index >= m_profileCombo->count()) {
        return;
    }
    const QString id = m_profileCombo->itemData(index).toString();
    if (id.isEmpty() || id == m_profileManager.activeProfileId()) {
        return;
    }
    concludeSessionIfNeeded();
    m_state.save();
    if (!switchProfile(id)) {
        updateFeedback(tr("Unable to switch profile."), false);
        refreshProfileControls();
        return;
    }
    updateFeedback(tr("Switched to profile %1").arg(m_profileCombo->itemText(index)), true);
}

void PitchTraining::handleCreateProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Create profile"), tr("Profile name"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    if (m_profileManager.profileNameExists(name)) {
        showMessage(QMessageBox::Information, tr("Create profile"), tr("A profile with that name already exists."));
        return;
    }
    QString newId;
    if (!m_profileManager.createProfile(name, &newId)) {
        showMessage(QMessageBox::Warning, tr("Create profile"), tr("Unable to create the profile."));
        return;
    }
    if (!switchProfile(newId)) {
        refreshProfileControls();
        return;
    }
    updateFeedback(tr("Profile %1 created.").arg(m_profileManager.activeProfile().name), true);
}

void PitchTraining::handleDeleteProfile()
{
    const auto profiles = m_profileManager.profiles();
    if (profiles.size() <= 1) {
        showMessage(QMessageBox::Information, tr("Delete profile"), tr("At least one profile must remain."));
        return;
    }
    if (!m_profileCombo) {
        return;
    }
    const int index = m_profileCombo->currentIndex();
    if (index < 0) {
        return;
    }
    const QString id = m_profileCombo->itemData(index).toString();
    if (id.isEmpty()) {
        return;
    }
    const QString name = m_profileCombo->itemText(index);
    const auto reply = showMessage(QMessageBox::Question,
                                   tr("Delete profile"),
                                   tr("Delete profile \"%1\"? This cannot be undone.").arg(name),
                                   QMessageBox::Yes | QMessageBox::No,
                                   QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }
    concludeSessionIfNeeded();
    m_state.save();
    if (!m_profileManager.deleteProfile(id)) {
        showMessage(QMessageBox::Warning, tr("Delete profile"), tr("Unable to delete the profile."));
        refreshProfileControls();
        return;
    }
    refreshProfileControls();
    applyActiveProfile();
    updateFeedback(tr("Profile %1 deleted.").arg(name), false);
}

void PitchTraining::handleShowInstructions()
{
    const QString text = tr("Training levels present 20 randomized piano tones drawn from the current pitch set plus nearby 'out-of-bound' distractors. You have to label each tone within the response window; semitone errors and responses after the timer are counted as incorrect. Once you clear a block of 24 levels for the current pitch set, the next chromatic pitch is added.\n\nSpecial exercises appear after every 15 attempted levels once at least five pitches are active. They focus on the weakest pitch with a short feedback block followed by a no-feedback block.\n\nWhen you run pre/post tests (outside of this trainer) they mirror the paper: no feedback, tones spaced more than an octave apart, and a 5-second response limit. Use the sample button here if you want to rehearse the reference tones before starting a level.");
    showMessage(QMessageBox::Information, tr("How the training and tests work"), text);
}

void PitchTraining::handleShowAbout()
{
    const QString text = tr("<p>PitchTraining was built by <a style=\"color:#0a58ca;\" href=\"https://github.com/bigorenski\">Lucas Bigorenski</a> based on the paper <em>\"Learning fast and accurate absolute pitch judgment in adulthood\"</em>.</p>");
    showMessage(QMessageBox::Information, tr("About PitchTraining"), text);
}

void PitchTraining::concludeSessionIfNeeded()
{
    if (!m_sessionActive) {
        return;
    }
    m_sessionActive = false;
    m_sessionButton->setText(tr("Start 15-min session"));
    const qint64 elapsedSeconds = m_sessionTimer.elapsed() / 1000;
    if (elapsedSeconds >= kSessionMinimumSeconds) {
        m_state.addCountedSeconds(static_cast<double>(elapsedSeconds));
        updateFeedback(tr("Session logged: %1 minutes counted.").arg(elapsedSeconds / 60), true);
    } else if (elapsedSeconds > 0) {
        showMessage(QMessageBox::Information,
                    tr("Short session"),
                    tr("Only sessions longer than 15 min count. You logged %1 minutes.").arg(elapsedSeconds / 60.0, 0, 'f', 1));
    }
    refreshStateLabels();
    m_state.save();
    refreshStartLevelButton();
}

void PitchTraining::updateFeedback(const QString &text, bool positive)
{
    if (!m_feedbackLabel) {
        return;
    }
    m_feedbackLabel->setText(text);
    m_feedbackLabel->setProperty("positive", positive);
    m_feedbackLabel->style()->unpolish(m_feedbackLabel);
    m_feedbackLabel->style()->polish(m_feedbackLabel);
    m_feedbackLabel->update();
}
