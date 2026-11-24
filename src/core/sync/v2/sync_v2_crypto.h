#pragma once

#include <QByteArray>
#include <QString>

namespace Zalos::Core::Sync::V2 {

struct KeyPair {
    QByteArray pub;
    QByteArray priv;
};

struct SessionRecord {
    QByteArray sessionId;
    QByteArray key;
    quint64 recordId{0};
};

struct MetadataPlain {
    QByteArray plaintext;
    SessionRecord session;
    QByteArray senderIdentity;
};

enum class CryptoStatus { Ok, BadInput, AuthFailed, Unsupported };

KeyPair generateKeyPair();

QByteArray calculateAgreement(const QByteArray& priv32, const QByteArray& pub32Or33);

CryptoStatus openMessage(const SessionRecord& record, const QByteArray& sealed, QByteArray* plaintext);

CryptoStatus openMetadata(const QByteArray& envelope, const QByteArray& receiverIkPub,
                          const QByteArray& receiverIkPriv, const QByteArray& receiverEkPriv,
                          MetadataPlain* out);

QString cryptoStatusName(CryptoStatus status);

}
