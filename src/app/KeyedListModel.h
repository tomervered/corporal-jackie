#pragma once
// A list for QML views that changes in place. A view whose model is a plain
// array rebuilds every delegate whenever the array is replaced, so a list
// that gains a page, or a home row that finishes loading, would throw away
// every poster on the screen and ask for each one again. Given a new array,
// this model updates the entries that changed and adds or removes at the
// end; only a list that changed some other way is rebuilt.
//
// Each delegate gets its entry as the `entry` role. Entries are matched by
// the value under keyRole.

#include <QAbstractListModel>
#include <QVariantList>

namespace cj {

class KeyedListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantList values READ values WRITE setValues NOTIFY valuesChanged)
    Q_PROPERTY(QString keyRole READ keyRole WRITE setKeyRole NOTIFY keyRoleChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY valuesChanged)

public:
    using QAbstractListModel::QAbstractListModel;

    QVariantList values() const { return m_values; }
    void setValues(const QVariantList& values);
    QString keyRole() const { return m_keyRole; }
    void setKeyRole(const QString& role);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    static constexpr int kEntryRole = Qt::UserRole + 1;

signals:
    void valuesChanged();
    void keyRoleChanged();

private:
    QString keyOf(const QVariant& value) const;

    QVariantList m_values;
    QString m_keyRole = QStringLiteral("key");
};

} // namespace cj
