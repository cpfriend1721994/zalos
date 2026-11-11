// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace zcall {

std::optional<QByteArray> loadKey(const QString& keyFile, QString* error = nullptr);

QByteArray ipcEncrypt(const QByteArray& json, const QByteArray& key);
std::optional<QByteArray> ipcDecrypt(const QByteArray& hex, const QByteArray& key);

QVector<QByteArray> ipcChunks(const QByteArray& hex, qint64 msgId);

struct IpcFrame {
    bool isToken{false};
    QByteArray token;
    bool isError{false};
    QString error;
    QJsonValue json;
    QByteArray text;
    QByteArray raw;
};

class FrameParser {
public:
    FrameParser(QByteArray key, bool expectToken, std::function<void(const IpcFrame&)> onFrame);
    void push(const QByteArray& chunk);

private:
    QByteArray m_key;
    QByteArray m_buf;
    bool m_needToken;
    std::function<void(const IpcFrame&)> m_onFrame;
    QHash<QByteArray, QVector<std::optional<QByteArray>>> m_chunks;
};

}
