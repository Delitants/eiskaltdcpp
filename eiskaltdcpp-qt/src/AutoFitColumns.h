#pragma once

#include <QObject>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

class QTreeView;

class AutoFitColumns : public QObject {
    Q_OBJECT
public:
    explicit AutoFitColumns(QTreeView *view);
    QVariantMap saveState() const;
    bool restoreState(const QVariantMap &state, const QByteArray &legacyHeader = {});
    void fitToContents(int column = -1);

signals:
    void layoutChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void scheduleFit();
    void fit();
    QTreeView *view;
    QTimer pending;
    QMap<int, int> manualWidths;
    QSet<int> shrinkColumns;
    QByteArray beforeReset;
    bool internalChange = false;
    int modelChanges = 0;
};
