#include "SelectedRecheck.h"
#include <QCoreApplication>
#include <QSet>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <algorithm>

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
    QSet<int> selected, pieces, affected;
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
        for(int piece=first;piece<=last;++piece) pieces.insert(piece);
    }
    RecheckPlan plan;
    plan.revision=revision;
    plan.selectedFiles=sorted(selected);
    plan.pieces=sorted(pieces);
    for(const int index : plan.pieces) {
        if(index<0 || index>=metadata.num_pieces())
            return {{},SelectedRecheckText::tr("Torrent metadata has an invalid verification piece range.")};
        const auto piece=lt::piece_index_t(index);
        for(const auto& slice : fs.map_block(piece,0,fs.piece_size(piece))) {
            if(fs.pad_file_at(slice.file_index) || slice.size==0) continue;
            affected.insert(static_cast<int>(slice.file_index));
            plan.totalBytes+=slice.size;
        }
    }
    plan.affectedFiles=sorted(affected);
    return {std::move(plan),{}};
}

} // namespace eiskalt::torrent
