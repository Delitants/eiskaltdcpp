#include "SelectedRecheck.h"
#include <QCoreApplication>
#include <QSet>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <algorithm>
#include <vector>

namespace eiskalt::torrent {
namespace {
class SelectedRecheckText {
    Q_DECLARE_TR_FUNCTIONS(SelectedRecheck)
};
QList<int> sorted(const QSet<int> &values)
{
    auto result=values.values();
    std::sort(result.begin(),result.end());
    return result;
}
}

RecheckPlanResult planSelectedRecheck(const libtorrent::torrent_info &metadata,
    const QList<int> &files, quint64 revision)
{
    namespace lt=libtorrent;
    if(!metadata.is_valid())
        return {{},SelectedRecheckText::tr("Torrent metadata is not available for file verification.")};
    if(files.isEmpty())
        return {{},SelectedRecheckText::tr("Select at least one file to verify.")};
    const auto& fs=metadata.layout();
    QSet<int> selected;
    std::vector<std::pair<int, int>> ranges;
    for(const int index : files) {
        if(index<0 || index>=fs.num_files())
            return {{},SelectedRecheckText::tr("The selected file is no longer present in Torrent metadata.")};
        const auto file=lt::file_index_t(index);
        if(fs.pad_file_at(file))
            return {{},SelectedRecheckText::tr("Torrent padding is not a selectable file.")};
        selected.insert(index);
        if(fs.file_size(file)==0) continue;
        const int first=static_cast<int>(fs.piece_index_at_file(file));
        const int last=static_cast<int>(fs.last_piece_index_at_file(file));
        if(first<0 || last<first || last>=metadata.num_pieces())
            return {{},SelectedRecheckText::tr("Torrent metadata has an invalid verification piece range.")};
        ranges.emplace_back(first,last);
    }
    std::sort(ranges.begin(),ranges.end());
    std::vector<std::pair<int, int>> merged;
    for(const auto& range : ranges) {
        if(merged.empty() || range.first>merged.back().second+1) merged.push_back(range);
        else merged.back().second=std::max(merged.back().second,range.second);
    }
    RecheckPlan plan;
    plan.revision=revision;
    plan.selectedFiles=sorted(selected);
    qsizetype pieceCount=0;
    for(const auto& range : merged) pieceCount+=range.second-range.first+1;
    plan.pieces.reserve(pieceCount);
    for(const auto& range : merged)
        for(int p=range.first;p<=range.second;++p) plan.pieces.append(p);

    // File offsets are ordered in the native layout. Intersect merged byte
    // ranges once, instead of allocating and mapping every individual piece.
    std::size_t cursor=0;
    const qint64 pieceLength=fs.piece_length();
    for(const auto file : fs.file_range()) {
        if(fs.pad_file_at(file) || fs.file_size(file)==0) continue;
        const qint64 begin=fs.file_offset(file), end=begin+fs.file_size(file);
        while(cursor<merged.size() && (qint64(merged[cursor].second)+1)*pieceLength<=begin) ++cursor;
        qint64 bytes=0;
        for(auto r=cursor;r<merged.size() && qint64(merged[r].first)*pieceLength<end;++r) {
            const auto first=std::max(begin,qint64(merged[r].first)*pieceLength);
            const auto last=std::min(end,(qint64(merged[r].second)+1)*pieceLength);
            bytes+=std::max<qint64>(0,last-first);
        }
        if(bytes==0) continue;
        plan.affectedFiles.append(static_cast<int>(file));
        plan.totalBytes+=bytes;
    }
    return {std::move(plan),{}};
}

} // namespace eiskalt::torrent
