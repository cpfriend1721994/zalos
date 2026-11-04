#pragma once

#include <QJSEngine>
#include <QtQml/qqml.h>
#include <QtQml/qqmlregistration.h>

#include "network/zalo_client.h"
#include "platform/system_tray.h"
#include "platform/theme_manager.h"

class QQmlEngine;

namespace Zalos::QmlSingletons {

template <typename T>
T* existingInstance() {
    if (!T::s_instance) new T();
    QJSEngine::setObjectOwnership(T::s_instance, QJSEngine::CppOwnership);
    return T::s_instance;
}

struct ZaloClientSingleton {
    Q_GADGET
    QML_FOREIGN(Zalos::Core::Network::ZaloClient)
    QML_NAMED_ELEMENT(ZaloClient)
    QML_SINGLETON
public:
    static Zalos::Core::Network::ZaloClient* create(QQmlEngine*, QJSEngine*) {
        return existingInstance<Zalos::Core::Network::ZaloClient>();
    }
};

struct ThemeManagerSingleton {
    Q_GADGET
    QML_FOREIGN(Zalos::Core::Platform::ThemeManager)
    QML_NAMED_ELEMENT(ThemeManager)
    QML_SINGLETON
public:
    static Zalos::Core::Platform::ThemeManager* create(QQmlEngine*, QJSEngine*) {
        return existingInstance<Zalos::Core::Platform::ThemeManager>();
    }
};

struct TrayControllerSingleton {
    Q_GADGET
    QML_FOREIGN(Zalos::Core::Platform::SystemTrayController)
    QML_NAMED_ELEMENT(TrayController)
    QML_SINGLETON
public:
    static Zalos::Core::Platform::SystemTrayController* create(QQmlEngine*, QJSEngine*) {
        return existingInstance<Zalos::Core::Platform::SystemTrayController>();
    }
};

}
