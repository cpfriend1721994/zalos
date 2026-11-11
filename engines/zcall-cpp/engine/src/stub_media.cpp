// SPDX-License-Identifier: BSD-3-Clause
#include "stub_media.h"

#include "js.h"
#include "log.h"

#include <QTimer>

namespace zcall {

void StubMedia::onNegotiated(const NegotiatedParams& params) {
    zlog(QStringLiteral("[media/stub] negotiated: %1 server(s), srtpMode=%2, codec= — no media sent (stub)")
             .arg(params.servers.size())
             .arg(js::string(params.srtpMode)));
}

void StubMedia::prepare(const QString&, std::function<void(std::optional<InitReply>)> done) {
    QTimer::singleShot(0, this, [done] { done(std::nullopt); });
}

void StubMedia::start(const QString& role, const QString&) {
    zlog(QStringLiteral("[media/stub] start (%1) — silent: no RTP backend").arg(role));
}

void StubMedia::stop() { zlog(QStringLiteral("[media/stub] stop")); }

}
