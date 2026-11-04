#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <QQuickTextDocument>
#include <QTextDocument>

namespace Zalos::Core::Models {

class RichComposer : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantMap active READ active NOTIFY activeChanged FINAL)

public:
    static const QStringList& colorStyles();
    static bool isKnownStyle(const QString& st);

    explicit RichComposer(QObject* parent = nullptr);

    QVariantMap active() const { return m_active; }

    Q_INVOKABLE void attach(QQuickTextDocument* document);
    void attachDocument(QTextDocument* document);

    Q_INVOKABLE bool apply(const QString& st, int start, int end);
    Q_INVOKABLE void toggleList(const QString& st, int start, int end);
    Q_INVOKABLE void clearFormatting(int start, int end);
    Q_INVOKABLE void refresh(int start, int end);
    Q_INVOKABLE bool hasFormatting() const;
    Q_INVOKABLE QVariantList styles() const;

    static QVariantList exportStyles(const QTextDocument* document);
    static void applyStyles(QTextDocument* document, const QVariantList& styles);

signals:
    void activeChanged();

private:
    QTextDocument* document() const { return m_document.data(); }

    QPointer<QTextDocument> m_document;
    QVariantMap m_active;
};

}
