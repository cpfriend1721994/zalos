#include "rich_composer.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

#include <functional>

namespace Zalos::Core::Models {

namespace {

// Markers kept in the char format so export never has to guess from rendering properties.
constexpr int kColorProp = QTextFormat::UserProperty + 1; // "c_db342e"
constexpr int kSizeProp = QTextFormat::UserProperty + 2;  // "f_13" | "f_18"
constexpr int kSmallPx = 12;
constexpr int kBigPx = 18;

QStringList keysOf(const QTextCharFormat& f) {
    QStringList keys;
    if (f.fontWeight() >= QFont::Bold) keys << QStringLiteral("b");
    if (f.fontItalic()) keys << QStringLiteral("i");
    if (f.fontUnderline()) keys << QStringLiteral("u");
    if (f.fontStrikeOut()) keys << QStringLiteral("s");
    const QString color = f.stringProperty(kColorProp);
    if (!color.isEmpty()) keys << color;
    const QString size = f.stringProperty(kSizeProp);
    if (!size.isEmpty()) keys << size;
    return keys;
}

void setKey(QTextCharFormat& f, const QString& st, bool on) {
    if (st == QLatin1String("b")) f.setFontWeight(on ? QFont::Bold : QFont::Normal);
    else if (st == QLatin1String("i")) f.setFontItalic(on);
    else if (st == QLatin1String("u")) f.setFontUnderline(on);
    else if (st == QLatin1String("s")) f.setFontStrikeOut(on);
    else if (st.startsWith(QLatin1String("c_"))) {
        if (on && st.size() == 8) {
            f.setProperty(kColorProp, st);
            f.setForeground(QColor(QLatin1Char('#') + st.mid(2)));
        } else {
            f.clearProperty(kColorProp);
            f.clearForeground();
        }
    } else if (st.startsWith(QLatin1String("f_"))) {
        if (on && (st == QLatin1String("f_13") || st == QLatin1String("f_18"))) {
            f.setProperty(kSizeProp, st);
            f.setProperty(QTextFormat::FontPixelSize, st == QLatin1String("f_13") ? kSmallPx : kBigPx);
        } else {
            f.clearProperty(kSizeProp);
            f.clearProperty(QTextFormat::FontPixelSize);
        }
    }
}

// Calls fn on every fragment piece inside [start, end).
void forEachPiece(QTextDocument* doc, int start, int end,
                  const std::function<void(int from, int to, const QTextCharFormat&)>& fn) {
    for (QTextBlock b = doc->findBlock(start); b.isValid() && b.position() < end; b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            const int from = qMax(start, frag.position());
            const int to = qMin(end, frag.position() + frag.length());
            if (from < to) fn(from, to, frag.charFormat());
        }
    }
}

bool allHave(QTextDocument* doc, int start, int end, const QString& st) {
    bool any = false, all = true;
    forEachPiece(doc, start, end, [&](int, int, const QTextCharFormat& f) {
        any = true;
        if (!keysOf(f).contains(st)) all = false;
    });
    return any && all;
}

QString listKey(const QTextBlock& b) {
    const QTextList* list = b.textList();
    if (!list) return {};
    return list->format().style() == QTextListFormat::ListDecimal ? QStringLiteral("lst_2") : QStringLiteral("lst_1");
}

} // namespace

const QStringList& RichComposer::colorStyles() {
    static const QStringList colors{"c_db342e", "c_f27806", "c_f7b503", "c_15a85f"};
    return colors;
}

bool RichComposer::isKnownStyle(const QString& st) {
    static const QStringList simple{"b", "i", "u", "s", "f_13", "f_18", "lst_1", "lst_2"};
    return simple.contains(st) || colorStyles().contains(st);
}

RichComposer::RichComposer(QObject* parent) : QObject(parent) {}

void RichComposer::attach(QQuickTextDocument* document) {
    attachDocument(document ? document->textDocument() : nullptr);
}

void RichComposer::attachDocument(QTextDocument* document) {
    m_document = document;
    refresh(0, 0);
}

bool RichComposer::apply(const QString& st, int start, int end) {
    QTextDocument* doc = document();
    if (!doc) return false;
    QTextCursor c(doc);
    const int max = doc->characterCount() - 1;
    start = qBound(0, qMin(start, end), max);
    end = qBound(0, qMax(start, end), max);
    if (start == end) {
        c.setPosition(start);
        c.select(QTextCursor::WordUnderCursor);
        if (!c.hasSelection() && start > 0) { // cursor right after a word
            c.setPosition(start - 1);
            c.select(QTextCursor::WordUnderCursor);
        }
        if (!c.hasSelection()) return false;
        start = c.selectionStart();
        end = c.selectionEnd();
    }
    // Toggle for b/i/u/s and for a colour/size already on the whole range; otherwise set it.
    bool on = true;
    if (st == QLatin1String("c_") || st == QLatin1String("f_")) on = false;
    else if (allHave(doc, start, end, st)) on = false;
    const bool exclusiveColor = st.startsWith(QLatin1String("c_"));
    const bool exclusiveSize = st.startsWith(QLatin1String("f_"));

    QList<std::pair<std::pair<int, int>, QTextCharFormat>> pieces;
    forEachPiece(doc, start, end, [&](int from, int to, const QTextCharFormat& f) {
        QTextCharFormat next = f;
        if (exclusiveColor) setKey(next, QStringLiteral("c_"), false);
        if (exclusiveSize) setKey(next, QStringLiteral("f_"), false);
        if (on || (!exclusiveColor && !exclusiveSize)) setKey(next, st, on);
        pieces.append({{from, to}, next});
    });
    c.beginEditBlock(); // one undo step
    for (const auto& p : std::as_const(pieces)) {
        QTextCursor pc(doc);
        pc.setPosition(p.first.first);
        pc.setPosition(p.first.second, QTextCursor::KeepAnchor);
        pc.setCharFormat(p.second);
    }
    c.endEditBlock();
    refresh(start, end);
    return true;
}

void RichComposer::toggleList(const QString& st, int start, int end) {
    QTextDocument* doc = document();
    if (!doc || (st != QLatin1String("lst_1") && st != QLatin1String("lst_2"))) return;
    const int max = doc->characterCount() - 1;
    start = qBound(0, qMin(start, end), max);
    end = qBound(0, qMax(start, end), max);
    QList<QTextBlock> blocks;
    for (QTextBlock b = doc->findBlock(start); b.isValid(); b = b.next()) {
        blocks << b;
        if (b.position() + b.length() > end) break;
    }
    if (blocks.isEmpty()) return;
    const bool remove = listKey(blocks.first()) == st;
    QTextCursor c(doc);
    c.beginEditBlock();
    for (const QTextBlock& b : std::as_const(blocks)) {
        if (QTextList* l = b.textList()) l->remove(b);
        QTextCursor bc(b);
        QTextBlockFormat bf = b.blockFormat();
        bf.setIndent(0);
        bc.setBlockFormat(bf);
    }
    if (!remove) {
        QTextCursor first(blocks.first());
        QTextListFormat lf;
        lf.setStyle(st == QLatin1String("lst_2") ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
        lf.setIndent(1);
        QTextList* list = first.createList(lf);
        for (int i = 1; i < blocks.size(); ++i) list->add(blocks.at(i));
    }
    c.endEditBlock();
    refresh(start, end);
}

void RichComposer::clearFormatting(int start, int end) {
    QTextDocument* doc = document();
    if (!doc) return;
    const int max = doc->characterCount() - 1;
    if (start == end) {
        start = 0;
        end = max;
    }
    start = qBound(0, qMin(start, end), max);
    end = qBound(0, qMax(start, end), max);
    QTextCursor c(doc);
    c.beginEditBlock();
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    c.setCharFormat(QTextCharFormat());
    for (QTextBlock b = doc->findBlock(start); b.isValid() && b.position() <= end; b = b.next()) {
        if (QTextList* l = b.textList()) l->remove(b);
        QTextCursor bc(b);
        QTextBlockFormat bf = b.blockFormat();
        bf.setIndent(0);
        bc.setBlockFormat(bf);
    }
    c.endEditBlock();
    refresh(start, start);
}

void RichComposer::refresh(int start, int end) {
    QVariantMap state{{"b", false}, {"i", false}, {"u", false}, {"s", false},
                      {"color", QString()}, {"size", QString()}, {"list", QString()}};
    if (QTextDocument* doc = document()) {
        const int max = doc->characterCount() - 1;
        int s = qBound(0, qMin(start, end), max);
        int e = qBound(0, qMax(start, end), max);
        if (s == e && s > 0) s -= 1; // caret: the character before it decides, like any editor
        if (s < e) {
            for (const QString& key : {QStringLiteral("b"), QStringLiteral("i"), QStringLiteral("u"), QStringLiteral("s")})
                state.insert(key, allHave(doc, s, e, key));
            for (const QString& color : colorStyles())
                if (allHave(doc, s, e, color)) state.insert("color", color);
            for (const QString& size : {QStringLiteral("f_13"), QStringLiteral("f_18")})
                if (allHave(doc, s, e, size)) state.insert("size", size);
        }
        state.insert("list", listKey(doc->findBlock(qBound(0, start, max))));
    }
    if (state == m_active) return;
    m_active = state;
    emit activeChanged();
}

bool RichComposer::hasFormatting() const {
    return !styles().isEmpty();
}

QVariantList RichComposer::styles() const {
    return exportStyles(document());
}

QVariantList RichComposer::exportStyles(const QTextDocument* document) {
    QVariantList out;
    if (!document) return out;
    QHash<QString, std::pair<int, int>> open; // style -> [start, end)
    auto flush = [&out](const QString& st, const std::pair<int, int>& r) {
        if (r.second > r.first) out.append(QVariantMap{{"start", r.first}, {"len", r.second - r.first}, {"st", st}});
    };
    for (QTextBlock b = document->begin(); b.isValid(); b = b.next()) {
        const QString list = listKey(b);
        if (!list.isEmpty() && b.length() > 1) flush(list, {b.position(), b.position() + b.length() - 1});
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            const int from = frag.position();
            const int to = from + frag.length();
            const QStringList keys = keysOf(frag.charFormat());
            // Close ranges that do not continue into this fragment.
            for (auto o = open.begin(); o != open.end();) {
                if (!keys.contains(o.key()) || o->second != from) {
                    flush(o.key(), *o);
                    o = open.erase(o);
                } else {
                    ++o;
                }
            }
            for (const QString& k : keys) {
                auto o = open.find(k);
                if (o == open.end()) open.insert(k, {from, to});
                else o->second = to;
            }
        }
    }
    for (auto o = open.cbegin(); o != open.cend(); ++o) flush(o.key(), *o);
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        return x.value("start").toInt() != y.value("start").toInt() ? x.value("start").toInt() < y.value("start").toInt()
                                                                    : x.value("st").toString() < y.value("st").toString();
    });
    return out;
}

void RichComposer::applyStyles(QTextDocument* document, const QVariantList& styles) {
    if (!document) return;
    RichComposer helper;
    helper.attachDocument(document);
    for (const QVariant& v : styles) {
        const QVariantMap m = v.toMap();
        const QString st = m.value("st").toString();
        const int start = m.value("start").toInt();
        const int end = start + m.value("len").toInt();
        if (end <= start || !isKnownStyle(st)) continue;
        if (st.startsWith(QLatin1String("lst_"))) {
            helper.toggleList(st, start, end);
        } else {
            // Set (not toggle): only where it is missing.
            QTextCursor c(document);
            QList<std::pair<std::pair<int, int>, QTextCharFormat>> pieces;
            forEachPiece(document, start, end, [&](int from, int to, const QTextCharFormat& f) {
                QTextCharFormat next = f;
                setKey(next, st, true);
                pieces.append({{from, to}, next});
            });
            for (const auto& p : std::as_const(pieces)) {
                c.setPosition(p.first.first);
                c.setPosition(p.first.second, QTextCursor::KeepAnchor);
                c.setCharFormat(p.second);
            }
        }
    }
}

} // namespace Zalos::Core::Models
