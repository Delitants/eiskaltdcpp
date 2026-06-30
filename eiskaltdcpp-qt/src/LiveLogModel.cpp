#include "LiveLogModel.h"

#include <QCoreApplication>
#include <QDateTime>

#include <algorithm>

LiveLogModel::LiveLogModel(QObject* parent) : QAbstractTableModel(parent)
{
}

int LiveLogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(visibleRows.size());
}

int LiveLogModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LiveLogModel::data(const QModelIndex& index, int role) const
{
    if(!index.isValid() || role != Qt::DisplayRole ||
        index.row() < 0 || static_cast<size_t>(index.row()) >= visibleRows.size()) {
        return {};
    }

    const auto& entry = entries[visibleRows[static_cast<size_t>(index.row())]];
    switch(index.column()) {
    case Time:
        return QDateTime::fromSecsSinceEpoch(entry.timestamp).toString("yyyy-MM-dd HH:mm:ss");
    case Category:
        return categoryName(entry.area);
    case Message:
        return QString::fromStdString(entry.message);
    default:
        return {};
    }
}

QVariant LiveLogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch(section) {
    case Time:
        return tr("Time");
    case Category:
        return tr("Category");
    case Message:
        return tr("Message");
    default:
        return {};
    }
}

void LiveLogModel::appendEntry(const dcpp::LogEntry& entry)
{
    if(entry.sequence <= lastSequence()) {
        return;
    }

    if(entries.size() == MAX_ENTRIES) {
        beginResetModel();
        entries.erase(entries.begin());
        entries.push_back(entry);
        rebuildVisibleRows();
        endResetModel();
        return;
    }

    if(isVisible(entry)) {
        const int row = static_cast<int>(visibleRows.size());
        beginInsertRows(QModelIndex(), row, row);
        entries.push_back(entry);
        visibleRows.push_back(entries.size() - 1);
        endInsertRows();
    } else {
        entries.push_back(entry);
    }
}

void LiveLogModel::appendNewEntries(const dcpp::LogManager::EntryList& newEntries)
{
    for(const auto& entry : newEntries) {
        if(entry.sequence > lastSequence()) {
            appendEntry(entry);
        }
    }
}

void LiveLogModel::replaceEntries(const dcpp::LogManager::EntryList& newEntries)
{
    beginResetModel();
    sequenceFloor = 0;
    entries.assign(newEntries.begin(), newEntries.end());
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.sequence < right.sequence;
    });
    entries.erase(std::unique(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.sequence == right.sequence;
    }), entries.end());
    if(entries.size() > MAX_ENTRIES) {
        entries.erase(entries.begin(), entries.end() - MAX_ENTRIES);
    }
    rebuildVisibleRows();
    endResetModel();
}

void LiveLogModel::clear()
{
    beginResetModel();
    entries.clear();
    visibleRows.clear();
    sequenceFloor = 0;
    endResetModel();
}

void LiveLogModel::clearThroughSequence(quint64 sequence)
{
    beginResetModel();
    entries.clear();
    visibleRows.clear();
    sequenceFloor = std::max(sequenceFloor, sequence);
    endResetModel();
}

void LiveLogModel::setCategoryMask(quint32 categoryMask)
{
    if(mask == categoryMask) {
        return;
    }

    beginResetModel();
    mask = categoryMask & allCategoryMask();
    rebuildVisibleRows();
    endResetModel();
}

void LiveLogModel::refreshTranslations()
{
    emit headerDataChanged(Qt::Horizontal, Time, Message);
    if(!visibleRows.empty()) {
        emit dataChanged(index(0, Category), index(rowCount() - 1, Category),
            { Qt::DisplayRole });
    }
}

quint32 LiveLogModel::categoryMask() const
{
    return mask;
}

quint64 LiveLogModel::lastSequence() const
{
    return entries.empty() ? sequenceFloor : std::max(sequenceFloor, static_cast<quint64>(entries.back().sequence));
}

quint32 LiveLogModel::categoryBit(dcpp::LogManager::Area area)
{
    const auto bit = static_cast<unsigned>(area);
    return bit < dcpp::LogManager::LAST ? quint32(1) << bit : 0;
}

quint32 LiveLogModel::allCategoryMask()
{
    return (quint32(1) << dcpp::LogManager::LAST) - 1;
}

QString LiveLogModel::categoryName(dcpp::LogManager::Area area)
{
    switch(area) {
    case dcpp::LogManager::CHAT:
        return QCoreApplication::translate("LiveLogModel", "Chat");
    case dcpp::LogManager::PM:
        return QCoreApplication::translate("LiveLogModel", "Private messages");
    case dcpp::LogManager::DOWNLOAD:
        return QCoreApplication::translate("LiveLogModel", "Downloads");
    case dcpp::LogManager::FINISHED_DOWNLOAD:
        return QCoreApplication::translate("LiveLogModel", "Finished downloads");
    case dcpp::LogManager::UPLOAD:
        return QCoreApplication::translate("LiveLogModel", "Uploads");
    case dcpp::LogManager::SYSTEM:
        return QCoreApplication::translate("LiveLogModel", "System");
    case dcpp::LogManager::STATUS:
        return QCoreApplication::translate("LiveLogModel", "Status");
    case dcpp::LogManager::SPY:
        return QCoreApplication::translate("LiveLogModel", "Search spy");
    case dcpp::LogManager::CMD_DEBUG:
        return QCoreApplication::translate("LiveLogModel", "Command debug");
    default:
        return {};
    }
}

bool LiveLogModel::isVisible(const dcpp::LogEntry& entry) const
{
    return (mask & categoryBit(entry.area)) != 0;
}

void LiveLogModel::rebuildVisibleRows()
{
    visibleRows.clear();
    visibleRows.reserve(entries.size());
    for(size_t i = 0; i < entries.size(); ++i) {
        if(isVisible(entries[i])) {
            visibleRows.push_back(i);
        }
    }
}
