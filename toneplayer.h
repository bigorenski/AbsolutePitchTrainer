#ifndef TONEPLAYER_H
#define TONEPLAYER_H

#include <QObject>
#include <QAudio>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QScopedPointer>
#include <memory>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioSink>
#include <QMediaDevices>
#endif
#include <QMediaPlayer>

struct ToneSample {
    QString filePath;
    QByteArray pcmData;

    bool isValid() const { return !filePath.isEmpty() || !pcmData.isEmpty(); }
};

class TonePlayer : public QObject
{
    Q_OBJECT
public:
    explicit TonePlayer(QObject *parent = nullptr);

    void playSample(const ToneSample &sample);
    void playPcm(const QByteArray &data);
    void stop();
    bool isPlaying() const;

signals:
    void playbackFinished();

private slots:
    void handleStateChanged(QAudio::State state);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void handleMediaStateChanged(QMediaPlayer::PlaybackState state);
#else
    void handleMediaStateChanged(QMediaPlayer::State state);
#endif

private:
    void playFile(const QString &path);

    QAudioFormat m_format;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioDevice m_device;
    std::unique_ptr<QAudioSink> m_audioOutput;
    std::unique_ptr<QAudioOutput> m_mediaOutput;
#else
    std::unique_ptr<QAudioOutput> m_audioOutput;
#endif
    QMediaPlayer *m_mediaPlayer = nullptr;
    QByteArray m_currentData;
    QScopedPointer<QBuffer> m_buffer;
    bool m_pcmPlaying = false;
    bool m_mediaPlaying = false;
};

struct ToneSampleKey {
    QString pitch;
    int octave = 4;

    friend bool operator==(const ToneSampleKey &a, const ToneSampleKey &b)
    {
        return a.pitch == b.pitch && a.octave == b.octave;
    }
};

inline uint qHash(const ToneSampleKey &key, uint seed = 0)
{
    auto combined = qHash(key.pitch, seed) ^ qHash(key.octave, seed);
    return static_cast<uint>(combined);
}

class ToneLibrary : public QObject
{
    Q_OBJECT
public:
    explicit ToneLibrary(QObject *parent = nullptr);

    ToneSample toneFor(const QString &pitch, int octave);
    ToneSample shepardTone();
    QList<int> supportedOctaves() const;

private:
    QString resolveSampleRoot() const;
    QString samplePathFor(const QString &pitch, int octave) const;
    QString sampleNameForPitch(const QString &pitch) const;
    QByteArray generateTone(double frequency, int durationMs) const;
    QByteArray generateShepard(int durationMs) const;
    double frequencyFor(const QString &pitch, int octave) const;

    QString m_sampleRoot;
    QHash<ToneSampleKey, QByteArray> m_pcmCache;
    ToneSample m_shepardSample;
    QList<int> m_octaves;
};

#endif // TONEPLAYER_H
