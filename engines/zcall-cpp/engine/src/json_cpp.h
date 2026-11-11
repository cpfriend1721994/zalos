// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>

namespace zcall {

QByteArray jsonStringify(const QJsonValue& v);
QString jsonCpp(const QJsonValue& v);

}
