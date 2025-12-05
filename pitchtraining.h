#ifndef PITCHTRAINING_H
#define PITCHTRAINING_H

#include <QButtonGroup>
#include <QDateTime>
#include <QElapsedTimer>
#include <QComboBox>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QKeyEvent>
#include <QEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QAction>
#include <QProgressBar>
#include <QStringList>
#include <QPushButton>
#include <QTimer>
#include <QVector>

#include "profilemanager.h"
#include "toneplayer.h"
#include "trainingmodel.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class PitchTraining;
}
QT_END_NAMESPACE

class PitchTraining : public QMainWindow
{
    Q_OBJECT

public:
    PitchTraining(QWidget *parent = nullptr);
    ~PitchTraining();

private slots:
    void handleStartLevel();
    void handleStartTrial();
    void handleResponse(int buttonId);
    void handleSpecialResponse(bool isTarget);
    void handleResponseTimeout();
    void handlePlaybackFinished();
    void handleSampleButton();
    void handleSessionToggle();
    void handleProfileSelection(int index);
    void handleCreateProfile();
    void handleDeleteProfile();
    void handleShowInstructions();
    void handleShowAbout();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class PlaybackContext {
        None,
        Trial,
        Sample,
        Shepard
    };

    enum class SessionMode {
        Idle,
        Level,
        SpecialExercise
    };

    struct TrialData {
        QString presentedPitch;
        int octave = 4;
        bool outOfBounds = false;
        QString response;
        bool correct = false;
        bool semitoneError = false;
        bool timedOut = false;
        int responseTimeMs = 0;
        bool usedDouble = false;
        bool luckyDouble = false;
    };

    struct SpecialContext {
        bool active = false;
        QString targetPitch;
        bool feedbackPhase = true;
        int totalTrials = 0;
        int completedTrials = 0;
        bool secondPhasePending = false;
    };

    void buildUi();
    void refreshStateLabels();
    void updateLevelDescription();
    void rebuildResponseButtons();
    void resetLevelState();
    void maybeScheduleSpecialExercise();
    bool shouldRunSpecialExercise() const;
    void startLevelInternal();
    void resolveLevelCompletion();
    void recordSummary(bool specialExercise, double accuracy, bool passed);
    void startSpecialExercise();
    void continueSpecialExercise();
    void resolveSpecialExercise();
    void prepareNextTrial();
    void finishCurrentTrial(bool timedOut = false);
    void setControlsEnabled(bool enabled);
    void updateFeedback(const QString &text, bool positive);
    void updateProgress();
    void scheduleShepardIfNeeded();
    void playShepardTone();
    void shepardFinished();
    void enqueueSamplePlayback(const QString &pitch);
    void playNextSample();
    void concludeSessionIfNeeded();
    void addTrainingTimeForSession();
    void refreshProfileControls();
    void applyActiveProfile();
    bool switchProfile(const QString &profileId);
    void resetTrialLog();
    void appendTrialLogEntry(int trialNumber, const QString &description, bool positive);
    void setResponseEnabled(bool levelEnabled, bool specialEnabled);
    void updateResponseTimeBar();
    QString pitchFromKeyEvent(QKeyEvent *event, bool &isOther) const;
    void clearActiveResponses();
    bool handleLevelKeyResponse(const QString &pitch, bool isOther);
    bool handleSpecialKeyResponse(const QString &pitch, bool isOther);
    QMessageBox::StandardButton showMessage(QMessageBox::Icon icon,
                                            const QString &title,
                                            const QString &text,
                                            QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                            QMessageBox::StandardButton defaultButton = QMessageBox::Ok);
    void refreshStartLevelButton();
    void applyTheme();
    bool handleShortcutKey(QKeyEvent *event);

    Ui::PitchTraining *ui;

    TrainingState m_state;
    ProfileManager m_profileManager;
    ToneLibrary m_toneLibrary;
    TonePlayer m_tonePlayer;

    LevelSpec m_currentSpec;
    QVector<QString> m_trainingPitches;
    QVector<QString> m_outOfBoundsPitches;
    QVector<TrialData> m_trialLog;
    QElapsedTimer m_trialTimer;
    QTimer *m_responseTimer = nullptr;
    QTimer *m_responseProgressTimer = nullptr;
    bool m_levelActive = false;
    bool m_waitingForShepard = false;
    bool m_doubleArmed = false;
    bool m_randomDouble = false;
    bool m_samplesQueued = false;
    QStringList m_sampleQueue;
    int m_trialsCompleted = 0;
    int m_requiredTrials = 0;
    int m_correctTrials = 0;
    double m_effectiveBonus = 0.0;
    int m_currentResponseWindowMs = 0;
    TrialData m_currentTrial;
    SpecialContext m_specialContext;
    SessionMode m_mode = SessionMode::Idle;
    PlaybackContext m_playbackContext = PlaybackContext::None;

    // Session tracking
    bool m_sessionActive = false;
    QDateTime m_sessionStart;
    QElapsedTimer m_sessionTimer;

    // UI elements
    QLabel *m_levelLabel = nullptr;
    QLabel *m_requirementLabel = nullptr;
    QLabel *m_tokensLabel = nullptr;
    QLabel *m_streakLabel = nullptr;
    QLabel *m_hoursLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_feedbackLabel = nullptr;
    QLabel *m_sessionLabel = nullptr;
    QLabel *m_keyboardHintLabel = nullptr;
    QComboBox *m_profileCombo = nullptr;
    QPushButton *m_startLevelButton = nullptr;
    QPushButton *m_startTrialButton = nullptr;
    QPushButton *m_sampleButton = nullptr;
    QPushButton *m_doubleButton = nullptr;
    QPushButton *m_sessionButton = nullptr;
    QPushButton *m_helpButton = nullptr;
    QToolButton *m_titleAboutButton = nullptr;
    QPushButton *m_newProfileButton = nullptr;
    QPushButton *m_deleteProfileButton = nullptr;
    QProgressBar *m_trialProgress = nullptr;
    QProgressBar *m_responseProgress = nullptr;
    QListWidget *m_trialLogList = nullptr;
    QButtonGroup *m_responseButtons = nullptr;
    QWidget *m_responseContainer = nullptr;
    QWidget *m_specialContainer = nullptr;
    QPushButton *m_specialTargetButton = nullptr;
    QPushButton *m_specialOtherButton = nullptr;
    bool m_trialLogHasEntries = false;
};

#endif // PITCHTRAINING_H
#include <QToolButton>
