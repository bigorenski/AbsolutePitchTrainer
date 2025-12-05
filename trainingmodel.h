#ifndef TRAININGMODEL_H
#define TRAININGMODEL_H

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct PitchSummary {
    int totalTrials = 0;
    int correctTrials = 0;
};

struct LevelSummary {
    int levelIndex = 0;
    double accuracy = 0.0;
    bool passed = false;
    bool specialExercise = false;
    QDateTime completedAt;
    QHash<QString, PitchSummary> perPitch;

    QJsonObject toJson() const;
    static LevelSummary fromJson(const QJsonObject &obj);
};

struct LevelSpec {
    int globalIndex = 0;
    int stageIndex = 0;
    int levelInStage = 0;
    double passAccuracy = 0.0;
    int trialCount = 0;
    int responseWindowMs = 0;
    bool feedback = false;
    bool tokensAllowed = true;
};

class TrainingSpec {
public:
    static const QVector<QString> &chromaticOrder();
    static QVector<QString> stagePitchSet(int stageIndex);
    static QVector<QString> outOfBoundsForStage(int stageIndex);
    static const QVector<LevelSpec> &levelSpecs();
    static LevelSpec specForIndex(int idx);
    static int totalLevelCount();
private:
    static QVector<LevelSpec> buildLevelSpecs();
};

class TrainingState {
public:
    TrainingState();

    bool load();
    bool save() const;

    void setProfileDirectory(const QString &path);
    QString profileDirectory() const;

    int currentLevelIndex() const;
    void setCurrentLevelIndex(int idx);

    int tokens() const;
    void addTokens(int amount);
    bool consumeTokens(int amount);

    double countedTrainingHours() const;
    void addCountedSeconds(double seconds);

    int streakCount() const;
    int levelsSinceSpecial() const;
    void resetLevelsSinceSpecial();
    void incrementLevelsSinceSpecial();

    void markActivity();

    void recordLevelSummary(const LevelSummary &summary);
    QVector<LevelSummary> recentSummaries(int limit) const;

    bool trainingCompleted() const;
    void setTrainingCompleted(bool done);

    int finalLevelConsecutivePasses() const;
    void setFinalLevelConsecutivePasses(int passes);
    QDateTime finalLevelCooldownStart() const;
    void setFinalLevelCooldownStart(const QDateTime &dt);

    int totalLevelAttempts() const;
    void incrementLevelAttempts();

    int tokensSpent() const;
    void incrementTokensSpent(int amount);

    QString leastAccuratePitch() const;

    QString stateFilePath() const;

private:
    void trimHistory();
    void resetState();

    QString resolvedProfileDir() const;

    int m_currentLevelIndex = 0;
    int m_tokens = 0;
    double m_countedSeconds = 0.0;
    int m_streakCount = 0;
    QDate m_lastActivityDate;
    int m_levelsSinceSpecial = 0;
    QVector<LevelSummary> m_history;
    bool m_trainingCompleted = false;
    int m_finalLevelConsecutivePasses = 0;
    QDateTime m_finalLevelCooldownStart;
    int m_totalLevelAttempts = 0;
    int m_tokensSpent = 0;
    QString m_profileDirectory;
};

#endif // TRAININGMODEL_H
