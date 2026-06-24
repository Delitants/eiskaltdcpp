#pragma once

#include <QAbstractTableModel>

#include "dcpp/LogManager.h"

#include <vector>

class LiveLogModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        Time,
        Category,
        Message,
        ColumnCount
    };

    explicit LiveLogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;

    void appendEntry(const dcpp::LogEntry& entry);
    void appendNewEntries(const dcpp::LogManager::EntryList& entries);
    void replaceEntries(const dcpp::LogManager::EntryList& entries);
    void clear();
    void clearThroughSequence(quint64 sequence);
    void setCategoryMask(quint32 mask);
    void refreshTranslations();
    quint32 categoryMask() const;
    quint64 lastSequence() const;

    static quint32 categoryBit(dcpp::LogManager::Area area);
    static quint32 allCategoryMask();
    static QString categoryName(dcpp::LogManager::Area area);

private:
    static constexpr size_t MAX_ENTRIES = 5000;

    bool isVisible(const dcpp::LogEntry& entry) const;
    void rebuildVisibleRows();

    std::vector<dcpp::LogEntry> entries;
    std::vector<size_t> visibleRows;
    quint32 mask = allCategoryMask();
    quint64 sequenceFloor = 0;
};
