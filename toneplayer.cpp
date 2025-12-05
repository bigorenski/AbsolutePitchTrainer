#include "toneplayer.h"

#include "trainingmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QtMath>
#include <cmath>

TonePlayer::TonePlayer(QObject *parent)
    : QObject(parent)
{
    m_format.setSampleRate(44100);
    m_format.setChannelCount(1);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_format.setSampleFormat(QAudioFormat::Int16);
    m_device = QMediaDevices::defaultAudioOutput();
    m_audioOutput = std::make_unique<QAudioSink>(m_device, m_format);
    QObject::connect(m_audioOutput.get(), &QAudioSink::stateChanged, this, &TonePlayer::handleStateChanged);

    m_mediaOutput = std::make_unique<QAudioOutput>();
    m_mediaPlayer = new QMediaPlayer(this);
    m_mediaPlayer->setAudioOutput(m_mediaOutput.get());
    QObject::connect(m_mediaPlayer, &QMediaPlayer::playbackStateChanged, this, &TonePlayer::handleMediaStateChanged);
#else
    m_format.setSampleSize(16);
    m_format.setSampleType(QAudioFormat::SignedInt);
    m_format.setByteOrder(QAudioFormat::LittleEndian);
    m_audioOutput = std::make_unique<QAudioOutput>(m_format, parent);
    QObject::connect(m_audioOutput.get(), SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateChanged(QAudio::State)));

    m_mediaPlayer = new QMediaPlayer(this);
    QObject::connect(m_mediaPlayer, SIGNAL(stateChanged(QMediaPlayer::State)), this, SLOT(handleMediaStateChanged(QMediaPlayer::State)));
#endif
}



void TonePlayer::playSample(const ToneSample &sample)
{
    if (!sample.filePath.isEmpty() && QFile::exists(sample.filePath)) {
        playFile(sample.filePath);
        return;
    }
    if (!sample.pcmData.isEmpty()) {
        playPcm(sample.pcmData);
    }
}

void TonePlayer::playPcm(const QByteArray &data)
{
    if (!m_audioOutput) {
        return;
    }

    stop();
    m_currentData = data;
    if (!m_buffer) {
        m_buffer.reset(new QBuffer(this));
    }
    m_buffer->setData(m_currentData);
    m_buffer->open(QIODevice::ReadOnly);
    m_pcmPlaying = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audioOutput->start(m_buffer.data());
#else
    m_audioOutput->start(m_buffer.data());
#endif
}

void TonePlayer::playFile(const QString &path)
{
    if (!m_mediaPlayer) {
        return;
    }
    stop();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_mediaPlayer->setSource(QUrl::fromLocalFile(path));
#else
    m_mediaPlayer->setMedia(QUrl::fromLocalFile(path));
#endif
    m_mediaPlaying = true;
    m_mediaPlayer->play();
}

void TonePlayer::stop()
{
    if (m_audioOutput) {
        m_audioOutput->stop();
    }
    if (m_buffer) {
        m_buffer->close();
    }
    m_pcmPlaying = false;
    if (m_mediaPlayer) {
        m_mediaPlayer->stop();
    }
    m_mediaPlaying = false;
}

bool TonePlayer::isPlaying() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const bool mediaActive = m_mediaPlayer && m_mediaPlayer->playbackState() == QMediaPlayer::PlayingState;
#else
    const bool mediaActive = m_mediaPlayer && m_mediaPlayer->state() == QMediaPlayer::PlayingState;
#endif
    return m_pcmPlaying || mediaActive;
}

void TonePlayer::handleStateChanged(QAudio::State state)
{
    if (state == QAudio::IdleState && m_pcmPlaying) {
        m_pcmPlaying = false;
        emit playbackFinished();
    }
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void TonePlayer::handleMediaStateChanged(QMediaPlayer::PlaybackState state)
{
    if (state == QMediaPlayer::PlaybackState::StoppedState && m_mediaPlaying) {
        m_mediaPlaying = false;
        emit playbackFinished();
    }
}
#else
void TonePlayer::handleMediaStateChanged(QMediaPlayer::State state)
{
    if (state == QMediaPlayer::StoppedState && m_mediaPlaying) {
        m_mediaPlaying = false;
        emit playbackFinished();
    }
}
#endif

ToneLibrary::ToneLibrary(QObject *parent)
    : QObject(parent)
{
    m_octaves = {4, 5, 6};
    m_sampleRoot = resolveSampleRoot();

    // Remove octaves without backing samples (fallback will still work)
    if (!m_sampleRoot.isEmpty()) {
        for (auto it = m_octaves.begin(); it != m_octaves.end();) {
            if (samplePathFor(QStringLiteral("C"), *it).isEmpty()) {
                it = m_octaves.erase(it);
            } else {
                ++it;
            }
        }
        if (m_octaves.isEmpty()) {
            m_octaves = {4, 5, 6};
        }
    }
}

ToneSample ToneLibrary::toneFor(const QString &pitch, int octave)
{
    ToneSample sample;
    const QString path = samplePathFor(pitch, octave);
    if (!path.isEmpty()) {
        sample.filePath = path;
        return sample;
    }

    const ToneSampleKey key{pitch, octave};
    if (m_pcmCache.contains(key)) {
        sample.pcmData = m_pcmCache.value(key);
        return sample;
    }

    const double freq = frequencyFor(pitch, octave);
    const QByteArray pcm = generateTone(freq, 800);
    m_pcmCache.insert(key, pcm);
    sample.pcmData = pcm;
    return sample;
}

ToneSample ToneLibrary::shepardTone()
{
    static const QStringList filenames = {
        QStringLiteral("shepard.mp3"),
        QStringLiteral("shepard.wav"),
        QStringLiteral("shepard.ogg")
    };
    if (m_shepardSample.filePath.isEmpty() && m_shepardSample.pcmData.isEmpty()) {
        if (!m_sampleRoot.isEmpty()) {
            QDir dir(m_sampleRoot);
            for (const auto &name : filenames) {
                const QString candidate = dir.filePath(name);
                if (QFile::exists(candidate)) {
                    m_shepardSample.filePath = candidate;
                    break;
                }
            }
        }
        if (m_shepardSample.filePath.isEmpty()) {
            m_shepardSample.pcmData = generateShepard(20000);
        }
    }
    return m_shepardSample;
}

QList<int> ToneLibrary::supportedOctaves() const
{
    return m_octaves;
}

QString ToneLibrary::resolveSampleRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.cd(QStringLiteral("pianoSounds"))) {
        return dir.absolutePath();
    }
    dir = QDir(QCoreApplication::applicationDirPath());
    if (dir.cdUp() && dir.cd(QStringLiteral("pianoSounds"))) {
        return dir.absolutePath();
    }
    QDir current = QDir::current();
    if (current.cd(QStringLiteral("pianoSounds"))) {
        return current.absolutePath();
    }
    return QString();
}

QString ToneLibrary::samplePathFor(const QString &pitch, int octave) const
{
    if (m_sampleRoot.isEmpty()) {
        return QString();
    }
    const QString note = sampleNameForPitch(pitch);
    const QString fileName = QStringLiteral("%1%2_mf.mp3").arg(note).arg(octave);
    const QString candidate = QDir(m_sampleRoot).filePath(fileName);
    if (QFile::exists(candidate)) {
        return candidate;
    }
    return QString();
}

QString ToneLibrary::sampleNameForPitch(const QString &pitch) const
{
    const QString normalized = pitch.trimmed().toUpper();
    static const QHash<QString, QString> kMap = {
        {QStringLiteral("C#"), QStringLiteral("Db")},
        {QStringLiteral("D#"), QStringLiteral("Eb")},
        {QStringLiteral("F#"), QStringLiteral("Gb")},
        {QStringLiteral("G#"), QStringLiteral("Ab")},
        {QStringLiteral("A#"), QStringLiteral("Bb")}
    };
    const QString token = kMap.value(normalized, normalized);
    if (token.isEmpty()) {
        return token;
    }
    QString formatted = token.toLower();
    formatted[0] = formatted[0].toUpper();
    return formatted;
}

double ToneLibrary::frequencyFor(const QString &pitch, int octave) const
{
    const auto &order = TrainingSpec::chromaticOrder();
    const int index = order.indexOf(pitch);
    if (index < 0) {
        return 440.0;
    }
    const int midi = 60 + index + (octave - 4) * 12;
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

QByteArray ToneLibrary::generateTone(double frequency, int durationMs) const
{
    const int sampleRate = 44100;
    const int sampleCount = durationMs * sampleRate / 1000;
    QByteArray buffer(sampleCount * static_cast<int>(sizeof(qint16)), Qt::Uninitialized);
    qint16 *samples = reinterpret_cast<qint16 *>(buffer.data());
    const int rampSamples = sampleRate / 10; // 100 ms ramp

    double phase = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        double envelope = 1.0;
        if (i < rampSamples) {
            envelope = static_cast<double>(i) / rampSamples;
        } else if (i > sampleCount - rampSamples) {
            envelope = static_cast<double>(sampleCount - i) / rampSamples;
        }
        const double sampleValue = (qSin(phase) + 0.4 * qSin(phase * 2.0) + 0.2 * qSin(phase * 3.0)) * envelope * 0.4;
        const double limited = qBound(-1.0, sampleValue, 1.0);
        samples[i] = static_cast<qint16>(limited * 32767.0);
        phase += 2.0 * M_PI * frequency / sampleRate;
    }
    return buffer;
}

QByteArray ToneLibrary::generateShepard(int durationMs) const
{
    const int sampleRate = 44100;
    const int sampleCount = durationMs * sampleRate / 1000;
    QByteArray buffer(sampleCount * static_cast<int>(sizeof(qint16)), Qt::Uninitialized);
    qint16 *samples = reinterpret_cast<qint16 *>(buffer.data());

    const double cycles = 5.0;
    const double samplesPerCycle = sampleCount / cycles;
    double phase = 0.0;
    double phase2 = 0.0;

    for (int i = 0; i < sampleCount; ++i) {
        const double cyclePos = std::fmod(static_cast<double>(i) / samplesPerCycle, 1.0);
        const double freq = 80.0 * std::pow(2.0, (1.0 - cyclePos) * 5.0);
        const double env = 0.3 + 0.7 * (1.0 - cyclePos);
        phase += 2.0 * M_PI * freq / sampleRate;
        phase2 += 2.0 * M_PI * freq * 0.5 / sampleRate;
        const double wave = std::sin(phase) + 0.6 * std::sin(phase2) + 0.3 * std::sin(phase * 0.5);
        const double limited = qBound(-1.0, wave * env * 0.4, 1.0);
        samples[i] = static_cast<qint16>(limited * 32767.0);
    }

    return buffer;
}
