#pragma once

#include <QByteArray>
#include <QString>

namespace Zalos::Core::Sync::V2 {

struct SessionRecord {
    QByteArray sessionId;
    QByteArray key;
    quint64 recordId{0};
};

enum class CryptoStatus { Ok, BadInput, AuthFailed, Unsupported };

CryptoStatus openMessage(const SessionRecord& record, const QByteArray& sealed, QByteArray* plaintext);

CryptoStatus openMetadata(const QByteArray& envelope, QByteArray* plaintext);

QString cryptoStatusName(CryptoStatus status);

}  // namespace Zalos::Core::Sync::V2
