#ifndef PROFILEMANAGER_H
#define PROFILEMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

struct UserProfile
{
    QString id;
    QString name;
    QDateTime createdAt;
    QDateTime lastActiveAt;
};

class ProfileManager
{
public:
    ProfileManager();

    bool load();
    bool save() const;

    QVector<UserProfile> profiles() const;
    QString activeProfileId() const;
    UserProfile activeProfile() const;

    bool setActiveProfile(const QString &id);
    bool createProfile(const QString &name, QString *outId = nullptr);
    bool deleteProfile(const QString &id);
    bool profileNameExists(const QString &name) const;

    QString profileDirectory(const QString &id) const;
    QString activeProfileDirectory() const;
    QString rootPath() const;

private:
    bool ensureRoot() const;
    QString metadataPath() const;
    QString legacyStatePath() const;
    QString generateId() const;
    UserProfile *findProfile(const QString &id);
    const UserProfile *findProfileConst(const QString &id) const;
    void ensureDefaultProfile();
    void maybeImportLegacyState(const QString &profileId) const;
    void recordLastActive(const QString &id);
    void seedDebugProfiles();
    void seedProfileState(const QString &profileId, int levelIndex, int tokens, double countedHours, int levelsSinceSpecial, bool completed);

    QVector<UserProfile> m_profiles;
    QString m_activeId;
    QString m_rootPath;
};

#endif // PROFILEMANAGER_H
