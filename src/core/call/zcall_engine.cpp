#include "zcall_engine.h"

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <QDateTime>
#include <QProcess>
#include <QDebug>

namespace Zalos::Core::Call {

ZCallEngine::ZCallEngine(QObject* parent)
    : QObject(parent) {
    m_durationTimer = new QTimer(this);
    connect(m_durationTimer, &QTimer::timeout, this, &ZCallEngine::onDurationTick);
}

void ZCallEngine::setState(CallState state) {
    if (m_state != state) {
        m_state = state;
        emit callStateChanged(static_cast<int>(m_state));

        if (m_state == CallState::Connected) {
            m_durationSeconds = 0;
            emit durationChanged(0);
            m_durationTimer->start(1000);
        } else if (m_state == CallState::Idle || m_state == CallState::Ended || m_state == CallState::Declined) {
            m_durationTimer->stop();
            m_isScreenSharing = false;
            emit screenShareStateChanged();
        }
    }
}

void ZCallEngine::setMutedMic(bool muted) {
    if (m_isMutedMic != muted) {
        m_isMutedMic = muted;
        emit muteStateChanged();
    }
}

void ZCallEngine::setMutedSpeaker(bool muted) {
    if (m_isMutedSpeaker != muted) {
        m_isMutedSpeaker = muted;
        emit muteStateChanged();
    }
}

void ZCallEngine::setCameraActive(bool active) {
    if (m_isCameraActive != active) {
        m_isCameraActive = active;
        emit cameraStateChanged();
    }
}

void ZCallEngine::startCall(const QString& peerId, const QString& peerName, bool isVideo) {
    Q_UNUSED(peerId);
    m_currentCaller = peerName.isEmpty() ? "Người dùng Zalo" : peerName;
    m_currentCallId = QString("call-%1").arg(QDateTime::currentMSecsSinceEpoch());
    m_callType = isVideo ? CallType::Video : CallType::Voice;
    m_isCameraActive = isVideo;

    emit currentCallerChanged(m_currentCaller);
    emit currentCallIdChanged(m_currentCallId);
    emit callTypeChanged(m_callType == CallType::Voice);
    emit cameraStateChanged();

    setState(CallState::CallingOutgoing);
}

void ZCallEngine::answerCall(bool withVideo) {
    m_isCameraActive = withVideo;
    emit cameraStateChanged();
    setState(CallState::Connecting);
}

void ZCallEngine::declineCall() {
    setState(CallState::Declined);
    QTimer::singleShot(500, this, [this]() {
        setState(CallState::Idle);
    });
}

void ZCallEngine::endCall() {
    setState(CallState::Ended);
    QTimer::singleShot(500, this, [this]() {
        setState(CallState::Idle);
    });
}

void ZCallEngine::toggleMuteMic() {
    setMutedMic(!m_isMutedMic);
}

void ZCallEngine::toggleMuteSpeaker() {
    setMutedSpeaker(!m_isMutedSpeaker);
}

void ZCallEngine::toggleCamera() {
    setCameraActive(!m_isCameraActive);
}

void ZCallEngine::startScreenShare() {
    if (!m_isScreenSharing) {
        m_isScreenSharing = true;
        emit screenShareStateChanged();
    }
}

void ZCallEngine::stopScreenShare() {
    if (m_isScreenSharing) {
        m_isScreenSharing = false;
        emit screenShareStateChanged();
    }
}

void ZCallEngine::onDurationTick() {
    m_durationSeconds++;
    emit durationChanged(m_durationSeconds);
}

QByteArray ZCallEngine::deriveSrtpMasterKey(const QByteArray& zrtpSecret, const QByteArray& salt) {
    unsigned char key[SHA256_DIGEST_LENGTH];
    unsigned int keyLen = 0;

    HMAC(EVP_sha256(),
         zrtpSecret.constData(), zrtpSecret.size(),
         reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(),
         key, &keyLen);

    return QByteArray(reinterpret_cast<const char*>(key), 16);
}

namespace {
static QByteArray aes128EcbEncryptBlock(const QByteArray& key16, const QByteArray& block16) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, reinterpret_cast<const unsigned char*>(key16.constData()), nullptr);
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    QByteArray out(16, 0);
    int len = 0;
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &len,
                      reinterpret_cast<const unsigned char*>(block16.constData()), 16);
    EVP_CIPHER_CTX_free(ctx);
    return out;
}

static QByteArray aesCmExpandKdf(const QByteArray& masterKey, const QByteArray& iv16, int outLen) {
    QByteArray out;
    out.reserve(outLen);
    int counter = 0;
    while (out.size() < outLen) {
        QByteArray blk = iv16;
        blk[14] = static_cast<char>((counter >> 8) & 0xff);
        blk[15] = static_cast<char>(counter & 0xff);
        QByteArray enc = aes128EcbEncryptBlock(masterKey, blk);
        int take = std::min(16, outLen - static_cast<int>(out.size()));
        out.append(enc.left(take));
        counter++;
    }
    return out;
}

static QByteArray srtpKdf(const QByteArray& mk, const QByteArray& ms, quint8 label, int outLen) {
    QByteArray x(16, 0);
    int saltCopyLen = std::min(14, static_cast<int>(ms.size()));
    std::memcpy(x.data(), ms.constData(), saltCopyLen);
    x[7] = static_cast<char>(static_cast<quint8>(x[7]) ^ label);
    return aesCmExpandKdf(mk, x, outLen);
}
}

ZCallEngine::SrtpSessionKeys ZCallEngine::deriveSrtpSessionKeys(const QByteArray& master30) {
    SrtpSessionKeys keys;
    if (master30.size() < 30) return keys;

    QByteArray mk = master30.left(16);
    QByteArray ms = master30.mid(16, 14);

    keys.encKey  = srtpKdf(mk, ms, 0x00, 16);
    keys.authKey = srtpKdf(mk, ms, 0x01, 20);
    keys.saltKey = srtpKdf(mk, ms, 0x02, 14);
    return keys;
}

QVector<AudioDevice> ZCallEngine::getAudioDevices(bool input) {
    QVector<AudioDevice> devices;

    AudioDevice def;
    def.id = "default";
    def.name = input ? "Mặc định (Microphone Hệ thống)" : "Mặc định (Loa / Tai nghe)";
    def.isDefault = true;
    def.isInput = input;
    devices.append(def);

    QProcess proc;
    QString cmd = input ? "sources" : "sinks";
    proc.start("pactl", QStringList() << "list" << cmd << "short");
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        QString out = QString::fromUtf8(proc.readAllStandardOutput());
        QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QStringList parts = line.split('\t');
            if (parts.size() >= 2) {
                AudioDevice dev;
                dev.id = parts[0];
                dev.name = parts[1];
                dev.isDefault = false;
                dev.isInput = input;
                devices.append(dev);
            }
        }
    }
    return devices;
}

}
