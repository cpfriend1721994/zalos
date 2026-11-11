#pragma once

namespace Zalos::Core::Sync::V2 {

enum class FallbackTrigger { None, PullRejected211, ConfirmTimeoutAfterV2 };

inline FallbackTrigger decideFallback(int pullErrorCode, bool confirmTimedOut, bool sawSyncV2) {
    if (pullErrorCode == 211) return FallbackTrigger::PullRejected211;
    if (confirmTimedOut && sawSyncV2) return FallbackTrigger::ConfirmTimeoutAfterV2;
    return FallbackTrigger::None;
}

}  // namespace Zalos::Core::Sync::V2
