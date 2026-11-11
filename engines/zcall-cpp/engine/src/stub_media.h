// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "media.h"

#include <QObject>

namespace zcall {

class StubMedia : public QObject, public MediaBackend {
    Q_OBJECT

public:
    using QObject::QObject;
    void onNegotiated(const NegotiatedParams& params) override;
    void prepare(const QString& role, std::function<void(std::optional<InitReply>)> done) override;
    void start(const QString& role, const QString& rtpSerIp) override;
    void stop() override;
};

}
