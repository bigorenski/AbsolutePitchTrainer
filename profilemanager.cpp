#include "profilemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include "trainingmodel.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <QDate>

namespace {
QString sanitizeName(const QString &name, int fallbackIndex)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty()) {
        return trimmed;
    }
    return QStringLiteral("Player %1").arg(fallbackIndex);
}
}

ProfileManager::ProfileManager()
{
    QDir base(QCoreApplication::applicationDirPath());
    if (!base.exists()) {
        base.mkpath(".");
    }
    m_rootPath = base.filePath(QStringLiteral("profiles"));
}

bool ProfileManager::load()
{
    if (!ensureRoot()) {
        return false;
    }

    m_profiles.clear();

    QFile file(metadataPath());
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(file.readAll());
        const auto obj = doc.object();
        m_activeId = obj.value(QStringLiteral("activeId")).toString();
        const auto arr = obj.value(QStringLiteral("profiles")).toArray();
        for (const auto &value : arr) {
            const auto profileObj = value.toObject();
            UserProfile profile;
            profile.id = profileObj.value(QStringLiteral("id")).toString();
            if (profile.id.isEmpty()) {
                continue;
            }
            profile.name = profileObj.value(QStringLiteral("name")).toString();
            profile.createdAt = QDateTime::fromString(profileObj.value(QStringLiteral("created")).toString(), Qt::ISODate);
            profile.lastActiveAt = QDateTime::fromString(profileObj.value(QStringLiteral("lastActive")).toString(), Qt::ISODate);
            if (!profile.createdAt.isValid()) {
                profile.createdAt = QDateTime::currentDateTimeUtc();
            }
            if (!profile.lastActiveAt.isValid()) {
                profile.lastActiveAt = profile.createdAt;
            }
            m_profiles.append(profile);
        }
    }

    ensureDefaultProfile();

    if (m_activeId.isEmpty() && !m_profiles.isEmpty()) {
        m_activeId = m_profiles.constFirst().id;
    }

    save();
    return true;
}

bool ProfileManager::save() const
{
    if (!ensureRoot()) {
        return false;
    }

    QJsonArray arr;
    for (const auto &profile : m_profiles) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = profile.id;
        obj[QStringLiteral("name")] = profile.name;
        obj[QStringLiteral("created")] = profile.createdAt.toString(Qt::ISODate);
        obj[QStringLiteral("lastActive")] = profile.lastActiveAt.toString(Qt::ISODate);
        arr.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("activeId")] = m_activeId;
    root[QStringLiteral("profiles")] = arr;

    QFile file(metadataPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    return true;
}

QVector<UserProfile> ProfileManager::profiles() const
{
    return m_profiles;
}

QString ProfileManager::activeProfileId() const
{
    return m_activeId;
}

UserProfile ProfileManager::activeProfile() const
{
    const auto *profile = findProfileConst(m_activeId);
    if (profile) {
        return *profile;
    }
    return {};
}

bool ProfileManager::setActiveProfile(const QString &id)
{
    auto *profile = findProfile(id);
    if (!profile) {
        return false;
    }
    m_activeId = id;
    recordLastActive(id);
    return save();
}

bool ProfileManager::createProfile(const QString &name, QString *outId)
{
    if (!ensureRoot()) {
        return false;
    }
    const QString sanitizedName = sanitizeName(name, m_profiles.size() + 1);
    for (const auto &profile : m_profiles) {
        if (profile.name.compare(sanitizedName, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }

    const QString id = generateId();
    QDir rootDir(m_rootPath);
    if (!rootDir.mkpath(id)) {
        return false;
    }

    UserProfile profile;
    profile.id = id;
    profile.name = sanitizedName;
    profile.createdAt = QDateTime::currentDateTimeUtc();
    profile.lastActiveAt = profile.createdAt;
    m_profiles.append(profile);

    maybeImportLegacyState(id);

    if (m_activeId.isEmpty()) {
        m_activeId = id;
    }

    if (outId) {
        *outId = id;
    }

    return save();
}

bool ProfileManager::deleteProfile(const QString &id)
{
    if (m_profiles.size() <= 1) {
        return false;
    }
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == id) {
            const QString dirPath = profileDirectory(id);
            QDir dir(dirPath);
            if (dir.exists()) {
                dir.removeRecursively();
            }
            m_profiles.removeAt(i);
            if (m_activeId == id) {
                m_activeId.clear();
            }
            if (m_activeId.isEmpty() && !m_profiles.isEmpty()) {
                m_activeId = m_profiles.constFirst().id;
            }
            return save();
        }
    }
    return false;
}

bool ProfileManager::profileNameExists(const QString &name) const
{
    const QString sanitizedName = sanitizeName(name, m_profiles.size() + 1);
    for (const auto &profile : m_profiles) {
        if (profile.name.compare(sanitizedName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString ProfileManager::profileDirectory(const QString &id) const
{
    if (id.isEmpty() || m_rootPath.isEmpty()) {
        return QString();
    }
    QDir rootDir(m_rootPath);
    return rootDir.filePath(id);
}

QString ProfileManager::activeProfileDirectory() const
{
    return profileDirectory(m_activeId);
}

QString ProfileManager::rootPath() const
{
    return m_rootPath;
}

bool ProfileManager::ensureRoot() const
{
    if (m_rootPath.isEmpty()) {
        return false;
    }
    QDir dir(m_rootPath);
    if (!dir.exists()) {
        return dir.mkpath(".");
    }
    return true;
}

QString ProfileManager::metadataPath() const
{
    QDir dir(m_rootPath);
    return dir.filePath(QStringLiteral("profiles.json"));
}

QString ProfileManager::legacyStatePath() const
{
    QDir base(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    return base.filePath(QStringLiteral("pitch_training_state.json"));
}

QString ProfileManager::generateId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

UserProfile *ProfileManager::findProfile(const QString &id)
{
    for (auto &profile : m_profiles) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

const UserProfile *ProfileManager::findProfileConst(const QString &id) const
{
    for (const auto &profile : m_profiles) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

void ProfileManager::ensureDefaultProfile()
{
    if (!m_profiles.isEmpty()) {
        return;
    }
    QString newId;
    createProfile(QStringLiteral("Player 1"), &newId);
    m_activeId = newId;
    seedDebugProfiles();
}

void ProfileManager::seedDebugProfiles()
{
    QString midId;
    if (createProfile(QStringLiteral("Debug - 6 pitches"), &midId)) {
        const int stageIndex = 6;
        const int levelIndex = (stageIndex - 1) * 24;
        seedProfileState(midId, levelIndex, 20, 6.0, 5, false);
    }

    QString finalId;
    if (createProfile(QStringLiteral("Debug - Final"), &finalId)) {
        seedProfileState(finalId, TrainingSpec::totalLevelCount() - 1, 40, 12.0, 24, false);
    }
}

void ProfileManager::seedProfileState(const QString &profileId, int levelIndex, int tokens, double countedHours, int levelsSinceSpecial, bool completed)
{
    const QString dirPath = profileDirectory(profileId);
    if (dirPath.isEmpty()) {
        return;
    }
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QFile stateFile(dir.filePath(QStringLiteral("state.json")));
    if (!stateFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QJsonObject obj;
    obj[QStringLiteral("currentLevel")] = levelIndex;
    obj[QStringLiteral("tokens")] = tokens;
    obj[QStringLiteral("countedSeconds")] = countedHours * 3600.0;
    obj[QStringLiteral("streak")] = 3;
    obj[QStringLiteral("levelsSinceSpecial")] = levelsSinceSpecial;
    obj[QStringLiteral("trainingCompleted")] = completed;
    obj[QStringLiteral("finalLevelPasses")] = completed ? 4 : 0;
    obj[QStringLiteral("totalLevelAttempts")] = levelIndex;
    obj[QStringLiteral("tokensSpent")] = 0;
    obj[QStringLiteral("lastActivityDate")] = QDate::currentDate().toString(Qt::ISODate);
    obj[QStringLiteral("finalLevelCooldown")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj[QStringLiteral("history")] = QJsonArray{};

    stateFile.write(QJsonDocument(obj).toJson());
}

void ProfileManager::maybeImportLegacyState(const QString &profileId) const
{
    const QString legacy = legacyStatePath();
    QFile legacyFile(legacy);
    if (!legacyFile.exists()) {
        return;
    }
    const QString targetDir = profileDirectory(profileId);
    if (targetDir.isEmpty()) {
        return;
    }
    QDir dir(targetDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    const QString targetFile = dir.filePath(QStringLiteral("state.json"));
    if (QFile::exists(targetFile)) {
        return;
    }
    legacyFile.copy(targetFile);
}

void ProfileManager::recordLastActive(const QString &id)
{
    auto *profile = findProfile(id);
    if (!profile) {
        return;
    }
    profile->lastActiveAt = QDateTime::currentDateTimeUtc();
}
