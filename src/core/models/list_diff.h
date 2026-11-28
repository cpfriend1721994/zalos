#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <utility>

namespace Zalos::Core::Models {

template <typename Item>
struct ListDiffOps {
    std::function<void(QVector<Item>&&)> reset;
    std::function<void(int, int, const std::function<void()>&)> removeRows;
    std::function<void(int, int, const std::function<void()>&)> moveRow;
    std::function<void(int, int, const std::function<void()>&)> insertRows;
    std::function<void(int, const QVector<int>&)> changed;
};

template <typename Item, typename DiffFn>
void applyKeyedListDiff(QVector<Item>& cur, QVector<Item> next, DiffFn diffRoles, const ListDiffOps<Item>& ops) {
    auto fullReset = [&]() { ops.reset(std::move(next)); };
    if (cur.isEmpty() || next.isEmpty()) return fullReset();

    QSet<QString> curIds;
    curIds.reserve(cur.size());
    for (const auto& item : cur) {
        if (item.id.isEmpty() || curIds.contains(item.id)) return fullReset();
        curIds.insert(item.id);
    }
    QSet<QString> nextIds;
    nextIds.reserve(next.size());
    for (const auto& item : next) {
        if (item.id.isEmpty() || nextIds.contains(item.id)) return fullReset();
        nextIds.insert(item.id);
    }

    int i = cur.size() - 1;
    while (i >= 0) {
        if (nextIds.contains(cur[i].id)) {
            --i;
            continue;
        }
        const int end = i;
        while (i > 0 && !nextIds.contains(cur[i - 1].id)) --i;
        const int first = i;
        ops.removeRows(first, end, [&]() { cur.remove(first, end - first + 1); });
        --i;
    }

    QHash<QString, int> position;
    position.reserve(cur.size());
    for (int k = 0; k < cur.size(); ++k) position.insert(cur[k].id, k);

    for (int pos = 0; pos < next.size();) {
        if (pos < cur.size() && cur[pos].id == next[pos].id) {
            const QVector<int> roles = diffRoles(cur[pos], next[pos]);
            cur[pos] = std::move(next[pos]);
            if (!roles.isEmpty()) ops.changed(pos, roles);
            ++pos;
            continue;
        }
        const auto found = position.constFind(next[pos].id);
        if (found != position.cend() && found.value() > pos) {
            const int from = found.value();
            ops.moveRow(from, pos, [&]() { cur.move(from, pos); });
            for (int k = pos; k <= from; ++k) position.insert(cur[k].id, k);
            const QVector<int> roles = diffRoles(cur[pos], next[pos]);
            cur[pos] = std::move(next[pos]);
            if (!roles.isEmpty()) ops.changed(pos, roles);
            ++pos;
            continue;
        }
        int insertEnd = pos + 1;
        while (insertEnd < next.size() && !position.contains(next[insertEnd].id)) ++insertEnd;
        const int count = insertEnd - pos;
        const int at = pos;
        ops.insertRows(at, insertEnd - 1, [&]() {
            cur.insert(at, count, Item{});
            for (int k = 0; k < count; ++k) cur[at + k] = std::move(next[at + k]);
        });
        for (int k = at; k < cur.size(); ++k) position.insert(cur[k].id, k);
        pos = insertEnd;
    }
}

}
