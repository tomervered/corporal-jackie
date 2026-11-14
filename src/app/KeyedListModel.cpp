#include "KeyedListModel.h"

#include <QVariantMap>

#include <algorithm>

namespace cj {

QString KeyedListModel::keyOf(const QVariant& value) const
{
    return value.toMap().value(m_keyRole).toString();
}

void KeyedListModel::setKeyRole(const QString& role)
{
    if (role == m_keyRole)
        return;
    m_keyRole = role;
    emit keyRoleChanged();
}

void KeyedListModel::setValues(const QVariantList& next)
{
    const qsizetype oldSize = m_values.size();
    const qsizetype shared = std::min(oldSize, next.size());
    qsizetype same = 0;
    while (same < shared && keyOf(m_values[same]) == keyOf(next[same]) && !keyOf(next[same]).isEmpty())
        ++same;

    if (same < shared) {
        // Something other than the end changed: start over.
        beginResetModel();
        m_values = next;
        endResetModel();
        emit valuesChanged();
        return;
    }

    // The same entries in the same order, some longer or shorter at the end.
    for (qsizetype i = 0; i < same; ++i) {
        if (m_values[i] != next[i]) {
            m_values[i] = next[i];
            const QModelIndex at = index(int(i));
            emit dataChanged(at, at, {kEntryRole});
        }
    }
    if (next.size() > oldSize) {
        beginInsertRows({}, int(oldSize), int(next.size() - 1));
        m_values = next;
        endInsertRows();
    } else if (next.size() < oldSize) {
        beginRemoveRows({}, int(next.size()), int(oldSize - 1));
        m_values = next;
        endRemoveRows();
    }
    emit valuesChanged();
}

int KeyedListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_values.size());
}

QVariant KeyedListModel::data(const QModelIndex& index, int role) const
{
    if (role != kEntryRole || !index.isValid() || index.row() >= m_values.size())
        return {};
    return m_values[index.row()];
}

QHash<int, QByteArray> KeyedListModel::roleNames() const
{
    return {{kEntryRole, "entry"}};
}

} // namespace cj
