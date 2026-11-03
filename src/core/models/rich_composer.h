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

// Formatting of the message composer, in Zalo's text style model (zca-js TextStyle / Zalo PC "textProperties"):
//   b i u s                       bold, italic, underline, strike-through
//   c_db342e c_f27806 c_f7b503 c_15a85f   red, orange, yellow, green text
//   f_13 f_18                     small, big text
//   lst_1 lst_2                   bulleted / numbered list (whole lines)
// The formats live in the composer's QTextDocument (real formatting, undoable with the text); styles() exports
// them as [{start, len, st}] over the plain text, which ZaloClient sends as textProperties {styles, ver: 0}.
// Exposed to QML as ZaloClient.composer; ChatArea attaches msgInput.textDocument.
class RichComposer : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // Styles active at the current selection (toolbar state): {b, i, u, s: bool, color: "c_…"|"", size: "f_…"|"",
    // list: "lst_1"|"lst_2"|""}. Refreshed by refresh(start, end).
    Q_PROPERTY(QVariantMap active READ active NOTIFY activeChanged FINAL)

public:
    static const QStringList& colorStyles();
    static bool isKnownStyle(const QString& st);

    explicit RichComposer(QObject* parent = nullptr);

    QVariantMap active() const { return m_active; }

    Q_INVOKABLE void attach(QQuickTextDocument* document);
    void attachDocument(QTextDocument* document); // tests

    // Character style over [start, end): toggles b/i/u/s; sets a colour or size (an empty colour "c_" / size "f_"
    // removes it). An empty range applies to the word under the cursor; returns false when there is none.
    Q_INVOKABLE bool apply(const QString& st, int start, int end);
    // Bulleted / numbered list on the lines touched by [start, end); the same list again removes it.
    Q_INVOKABLE void toggleList(const QString& st, int start, int end);
    // Drops every format in [start, end) (the whole text when start == end).
    Q_INVOKABLE void clearFormatting(int start, int end);
    Q_INVOKABLE void refresh(int start, int end);
    Q_INVOKABLE bool hasFormatting() const;
    // [{start, len, st}] — UTF-16 offsets into the document's plain text.
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

} // namespace Zalos::Core::Models
