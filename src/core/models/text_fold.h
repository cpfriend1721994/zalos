#pragma once

#include <QString>

namespace Zalos::Core::Models {

inline QString foldForSearch(const QString& text) {
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing) continue;
        if (c == QChar(0x0111) || c == QChar(0x0110)) out.append(QLatin1Char('d'));
        else out.append(c.toLower());
    }
    return out;
}

}
