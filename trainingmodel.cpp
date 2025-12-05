#include "trainingmodel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtMath>
#include <algorithm>

namespace {

constexpr int kLevelsPerStage = 24;
constexpr int kTotalPitches = 12;

QVector<QString> buildChromaticOrder()
{
    return {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}

} // namespace

QJsonObject LevelSummary::toJson() const
{
    QJsonObject obj;
    obj["levelIndex"] = levelIndex;
    obj["accuracy"] = accuracy;
    obj["passed"] = passed;
    obj["special"] = specialExercise;
    obj["completedAt"] = completedAt.toString(Qt::ISODate);

    QJsonObject perPitchObj;
    for (auto it = perPitch.constBegin(); it != perPitch.constEnd(); ++it) {
        QJsonObject stats;
        stats["total"] = it.value().totalTrials;
        stats["correct"] = it.value().correctTrials;
        perPitchObj[it.key()] = stats;
    }
    obj["perPitch"] = perPitchObj;
    return obj;
}

LevelSummary LevelSummary::fromJson(const QJsonObject &obj)
{
    LevelSummary summary;
    summary.levelIndex = obj.value("levelIndex").toInt();
    summary.accuracy = obj.value("accuracy").toDouble();
    summary.passed = obj.value("passed").toBool();
    summary.specialExercise = obj.value("special").toBool();
    summary.completedAt = QDateTime::fromString(obj.value("completedAt").toString(), Qt::ISODate);

    const auto perPitchObj = obj.value("perPitch").toObject();
    for (auto it = perPitchObj.constBegin(); it != perPitchObj.constEnd(); ++it) {
        const auto statsObj = it.value().toObject();
        PitchSummary stats;
        stats.totalTrials = statsObj.value("total").toInt();
        stats.correctTrials = statsObj.value("correct").toInt();
        summary.perPitch.insert(it.key(), stats);
    }
    return summary;
}

const QVector<QString> &TrainingSpec::chromaticOrder()
{
    static QVector<QString> order = buildChromaticOrder();
    return order;
}

QVector<QString> TrainingSpec::stagePitchSet(int stageIndex)
{
    const auto &order = chromaticOrder();
    if (stageIndex <= 0) {
        return {};
    }

    QVector<int> indices;
    indices.reserve(stageIndex);

    const int startIndex = order.indexOf("F");
    indices.append(startIndex);
    int lowest = startIndex;
    int highest = startIndex;
    bool pickLower = true;

    while (indices.size() < stageIndex && (lowest > 0 || highest < order.size() - 1)) {
        if (pickLower && lowest > 0) {
            --lowest;
            indices.append(lowest);
        } else if (!pickLower && highest < order.size() - 1) {
            ++highest;
            indices.append(highest);
        } else if (lowest > 0) {
            --lowest;
            indices.append(lowest);
        } else if (highest < order.size() - 1) {
            ++highest;
            indices.append(highest);
        }
        pickLower = !pickLower;
    }

    QVector<QString> result;
    result.reserve(indices.size());
    for (int idx : indices) {
        if (idx >= 0 && idx < order.size()) {
            result.append(order.at(idx));
        }
    }
    return result;
}

QVector<QString> TrainingSpec::outOfBoundsForStage(int stageIndex)
{
    const auto &order = chromaticOrder();
    auto trained = stagePitchSet(stageIndex);
    if (trained.isEmpty()) {
        return {};
    }

    QVector<int> trainedIdx;
    trainedIdx.reserve(trained.size());
    for (const auto &name : trained) {
        trainedIdx.append(order.indexOf(name));
    }
    std::sort(trainedIdx.begin(), trainedIdx.end());

    int lowest = trainedIdx.front();
    int highest = trainedIdx.back();

    QVector<QString> bounds;

    // lower side
    if (lowest > 0) {
        const int lowerOne = lowest - 1;
        bounds.append(order.at(lowerOne));
        if (lowerOne > 0) {
            bounds.append(order.at(lowerOne - 1));
        }
    }

    // upper side
    if (highest < order.size() - 1) {
        const int upperOne = highest + 1;
        bounds.append(order.at(upperOne));
        if (upperOne < order.size() - 1) {
            bounds.append(order.at(upperOne + 1));
        }
    }

    // remove duplicates (can happen when approaching edges)
    QSet<QString> unique;
    QVector<QString> filtered;
    for (const auto &name : bounds) {
        if (!unique.contains(name)) {
            unique.insert(name);
            filtered.append(name);
        }
    }

    return filtered;
}

QVector<LevelSpec> TrainingSpec::buildLevelSpecs()
{
    QVector<LevelSpec> specs;
    specs.reserve(kLevelsPerStage * kTotalPitches);

    const QVector<double> accuracyTargets = {
        0.20, 0.30, 0.40, 0.50, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.88, 0.90,
        0.60, 0.65, 0.70, 0.75, 0.80, 0.82, 0.85, 0.88, 0.90, 0.90, 0.90, 0.90 };

    for (int stage = 1; stage <= kTotalPitches; ++stage) {
        const int baseRt = 2028 - (stage - 1) * 80;
        for (int level = 1; level <= kLevelsPerStage; ++level) {
            LevelSpec spec;
            spec.globalIndex = static_cast<int>(specs.size());
            spec.stageIndex = stage;
            spec.levelInStage = level;
            spec.passAccuracy = accuracyTargets.at(level - 1);
            spec.trialCount = 20;
            int rtAdjustment = (level - 1) * 15;
            if (level > 12) {
                rtAdjustment += 70;
            }
            spec.responseWindowMs = qMax(1183, baseRt - rtAdjustment);
            spec.feedback = level <= 12;
            spec.tokensAllowed = level != kLevelsPerStage;
            specs.append(spec);
        }
    }

    return specs;
}

const QVector<LevelSpec> &TrainingSpec::levelSpecs()
{
    static QVector<LevelSpec> s_specs = buildLevelSpecs();
    return s_specs;
}

LevelSpec TrainingSpec::specForIndex(int idx)
{
    const auto &specs = levelSpecs();
    if (idx < 0) {
        return specs.front();
    }
    if (idx >= specs.size()) {
        return specs.back();
    }
    return specs.at(idx);
}

int TrainingSpec::totalLevelCount()
{
    return kLevelsPerStage * kTotalPitches;
}

TrainingState::TrainingState() = default;

bool TrainingState::load()
{
    resetState();

    QFile file(stateFilePath());
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const auto doc = QJsonDocument::fromJson(file.readAll());
    const auto obj = doc.object();

    m_currentLevelIndex = obj.value("currentLevel").toInt();
    m_tokens = obj.value("tokens").toInt();
    m_countedSeconds = obj.value("countedSeconds").toDouble();
    m_streakCount = obj.value("streak").toInt();
    m_levelsSinceSpecial = obj.value("levelsSinceSpecial").toInt();
    m_trainingCompleted = obj.value("trainingCompleted").toBool();
    m_finalLevelConsecutivePasses = obj.value("finalLevelPasses").toInt();
    m_totalLevelAttempts = obj.value("totalLevelAttempts").toInt();
    m_tokensSpent = obj.value("tokensSpent").toInt();

    const auto lastDateStr = obj.value("lastActivityDate").toString();
    if (!lastDateStr.isEmpty()) {
        m_lastActivityDate = QDate::fromString(lastDateStr, Qt::ISODate);
    }

    const auto cooldownStr = obj.value("finalLevelCooldown").toString();
    if (!cooldownStr.isEmpty()) {
        m_finalLevelCooldownStart = QDateTime::fromString(cooldownStr, Qt::ISODate);
    }

    const auto historyArray = obj.value("history").toArray();
    for (const auto &value : historyArray) {
        m_history.append(LevelSummary::fromJson(value.toObject()));
    }

    return true;
}

bool TrainingState::save() const
{
    QFile file(stateFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    QJsonObject obj;
    obj["currentLevel"] = m_currentLevelIndex;
    obj["tokens"] = m_tokens;
    obj["countedSeconds"] = m_countedSeconds;
    obj["streak"] = m_streakCount;
    obj["levelsSinceSpecial"] = m_levelsSinceSpecial;
    obj["trainingCompleted"] = m_trainingCompleted;
    obj["finalLevelPasses"] = m_finalLevelConsecutivePasses;
    obj["totalLevelAttempts"] = m_totalLevelAttempts;
    obj["tokensSpent"] = m_tokensSpent;
    obj["lastActivityDate"] = m_lastActivityDate.toString(Qt::ISODate);
    obj["finalLevelCooldown"] = m_finalLevelCooldownStart.toString(Qt::ISODate);

    QJsonArray historyArray;
    for (const auto &summary : m_history) {
        historyArray.append(summary.toJson());
    }
    obj["history"] = historyArray;

    QJsonDocument doc(obj);
    file.write(doc.toJson());
    return true;
}

int TrainingState::currentLevelIndex() const
{
    return m_currentLevelIndex;
}

void TrainingState::setCurrentLevelIndex(int idx)
{
    m_currentLevelIndex = qBound(0, idx, TrainingSpec::totalLevelCount() - 1);
}

int TrainingState::tokens() const
{
    return m_tokens;
}

void TrainingState::addTokens(int amount)
{
    m_tokens = qMax(0, m_tokens + amount);
}

bool TrainingState::consumeTokens(int amount)
{
    if (amount <= 0) {
        return true;
    }
    if (m_tokens < amount) {
        return false;
    }
    m_tokens -= amount;
    incrementTokensSpent(amount);
    return true;
}

double TrainingState::countedTrainingHours() const
{
    return m_countedSeconds / 3600.0;
}

void TrainingState::addCountedSeconds(double seconds)
{
    m_countedSeconds = qMax(0.0, m_countedSeconds + seconds);
}

int TrainingState::streakCount() const
{
    return m_streakCount;
}

int TrainingState::levelsSinceSpecial() const
{
    return m_levelsSinceSpecial;
}

void TrainingState::resetLevelsSinceSpecial()
{
    m_levelsSinceSpecial = 0;
}

void TrainingState::incrementLevelsSinceSpecial()
{
    ++m_levelsSinceSpecial;
}

void TrainingState::markActivity()
{
    const QDate today = QDate::currentDate();
    if (!m_lastActivityDate.isValid()) {
        m_streakCount = 1;
    } else {
        const int diff = m_lastActivityDate.daysTo(today);
        if (diff == 0) {
            // same day, keep streak
        } else if (diff == 1) {
            ++m_streakCount;
        } else if (diff > 2) {
            m_streakCount = 1;
        } else {
            m_streakCount = 1;
        }
    }
    if (!today.isValid()) {
        return;
    }
    m_lastActivityDate = today;
}

void TrainingState::recordLevelSummary(const LevelSummary &summary)
{
    m_history.append(summary);
    trimHistory();
}

QVector<LevelSummary> TrainingState::recentSummaries(int limit) const
{
    if (limit <= 0 || m_history.isEmpty()) {
        return {};
    }
    const int take = qMin(limit, m_history.size());
    QVector<LevelSummary> subset;
    subset.reserve(take);
    for (int i = m_history.size() - take; i < m_history.size(); ++i) {
        subset.append(m_history.at(i));
    }
    return subset;
}

bool TrainingState::trainingCompleted() const
{
    return m_trainingCompleted;
}

void TrainingState::setTrainingCompleted(bool done)
{
    m_trainingCompleted = done;
}

int TrainingState::finalLevelConsecutivePasses() const
{
    return m_finalLevelConsecutivePasses;
}

void TrainingState::setFinalLevelConsecutivePasses(int passes)
{
    m_finalLevelConsecutivePasses = qMax(0, passes);
}

QDateTime TrainingState::finalLevelCooldownStart() const
{
    return m_finalLevelCooldownStart;
}

void TrainingState::setFinalLevelCooldownStart(const QDateTime &dt)
{
    m_finalLevelCooldownStart = dt;
}

int TrainingState::totalLevelAttempts() const
{
    return m_totalLevelAttempts;
}

void TrainingState::incrementLevelAttempts()
{
    ++m_totalLevelAttempts;
}

int TrainingState::tokensSpent() const
{
    return m_tokensSpent;
}

void TrainingState::incrementTokensSpent(int amount)
{
    m_tokensSpent = qMax(0, m_tokensSpent + amount);
}

QString TrainingState::leastAccuratePitch() const
{
    const int window = qMin(15, m_history.size());
    if (window == 0) {
        return {};
    }
    QHash<QString, PitchSummary> aggregates;
    for (int i = m_history.size() - window; i < m_history.size(); ++i) {
        const auto &summary = m_history.at(i);
        if (summary.specialExercise) {
            continue;
        }
        for (auto it = summary.perPitch.constBegin(); it != summary.perPitch.constEnd(); ++it) {
            auto stats = aggregates.value(it.key());
            stats.totalTrials += it.value().totalTrials;
            stats.correctTrials += it.value().correctTrials;
            aggregates.insert(it.key(), stats);
        }
    }

    QString leastPitch;
    double lowestAccuracy = 2.0;
    for (auto it = aggregates.constBegin(); it != aggregates.constEnd(); ++it) {
        if (it.value().totalTrials == 0) {
            continue;
        }
        const double acc = static_cast<double>(it.value().correctTrials) / static_cast<double>(it.value().totalTrials);
        if (acc < lowestAccuracy) {
            lowestAccuracy = acc;
            leastPitch = it.key();
        }
    }
    return leastPitch;
}

QString TrainingState::stateFilePath() const
{
    const QString dirPath = resolvedProfileDir();
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dir.filePath(QStringLiteral("state.json"));
}

void TrainingState::setProfileDirectory(const QString &path)
{
    m_profileDirectory = path;
}

QString TrainingState::profileDirectory() const
{
    return resolvedProfileDir();
}

QString TrainingState::resolvedProfileDir() const
{
    if (!m_profileDirectory.isEmpty()) {
        return m_profileDirectory;
    }
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dir.absolutePath();
}

void TrainingState::trimHistory()
{
    constexpr int kMaxHistory = 80;
    while (m_history.size() > kMaxHistory) {
        m_history.removeFirst();
    }
}

void TrainingState::resetState()
{
    m_currentLevelIndex = 0;
    m_tokens = 0;
    m_countedSeconds = 0.0;
    m_streakCount = 0;
    m_lastActivityDate = QDate();
    m_levelsSinceSpecial = 0;
    m_history.clear();
    m_trainingCompleted = false;
    m_finalLevelConsecutivePasses = 0;
    m_finalLevelCooldownStart = QDateTime();
    m_totalLevelAttempts = 0;
    m_tokensSpent = 0;
}
