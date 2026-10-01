#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "torrent/SelectedRecheck.h"
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <memory>

namespace {
namespace lt=libtorrent;
using namespace eiskalt::torrent;

std::shared_ptr<lt::torrent_info> metadata(lt::file_storage files,
    lt::create_flags_t flags=lt::create_torrent::v1_only, int pieceLength=16384)
{
    lt::create_torrent maker(files,pieceLength,flags);
    const auto& fs=maker.files();
    if(!(flags & lt::create_torrent::v2_only))
        for(auto p : fs.piece_range()) maker.set_hash(p,lt::sha1_hash(std::string(20,'h')));
    if(!(flags & lt::create_torrent::v1_only))
        for(auto f : fs.file_range()) {
            if(fs.pad_file_at(f)) continue;
            const auto count=(fs.file_size(f)+pieceLength-1)/pieceLength;
            for(int p=0;p<count;++p)
                maker.set_hash2(f,lt::piece_index_t::diff_type(p),lt::sha256_hash(std::string(32,'m')));
        }
    return lt::load_torrent_buffer(lt::bencode(maker.generate())).ti;
}

lt::file_storage boundaryFiles(int lastSize=32768)
{
    lt::file_storage files;
    files.add_file("fixture/A",8192);
    files.add_file("fixture/B",8192);
    files.add_file("fixture/C",lastSize);
    return files;
}

int fileId(const lt::torrent_info& ti,const std::string& name)
{
    for(auto f : ti.files().file_range())
        if(ti.files().file_path(f)==name) return static_cast<int>(f);
    FAIL("Fixture file is absent from metadata");
    return -1;
}
}

TEST_CASE("Selected scope deduplicates stable metadata IDs and shared boundaries", "[selected-recheck-plan]")
{
    auto ti=metadata(boundaryFiles());
    auto result=planSelectedRecheck(*ti,{1,0,1},77);
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.plan.has_value());
    const auto& plan=*result.plan;
    REQUIRE(plan.selectedFiles==QList<int>{0,1});
    REQUIRE(plan.affectedFiles==QList<int>{0,1});
    REQUIRE(plan.pieces==QList<int>{0});
    REQUIRE(plan.totalBytes==16384);
    REQUIRE(plan.revision==77);
    const auto single=planSelectedRecheck(*ti,{0},77);
    REQUIRE(single.plan.has_value());
    REQUIRE(single.plan->affectedFiles==plan.affectedFiles);
    REQUIRE(single.plan->pieces==plan.pieces);
    // View order is deliberately different; requests carry IDs, not visual rows.
    const QList<int> reorderedRows{2,0,1};
    const auto reordered=planSelectedRecheck(*ti,{reorderedRows[1],reorderedRows[2]},77);
    REQUIRE(reordered.plan.has_value());
    REQUIRE(reordered.plan->selectedFiles==plan.selectedFiles);
    REQUIRE(reordered.plan->pieces==plan.pieces);
}

TEST_CASE("Scope admission rejects empty invalid and padding-only selections", "[selected-recheck-plan]")
{
    auto files=boundaryFiles();
    files.add_file("fixture/.pad/16384",16384,lt::file_storage::flag_pad_file);
    auto ti=metadata(files);
    for(const auto& ids : {QList<int>{},QList<int>{-1},QList<int>{4},QList<int>{0,4},QList<int>{3}}) {
        auto result=planSelectedRecheck(*ti,ids,1);
        REQUIRE_FALSE(result.plan.has_value());
        REQUIRE_FALSE(result.error.isEmpty());
    }
}

TEST_CASE("Last partial pieces count only their actual payload once", "[selected-recheck-plan]")
{
    auto ti=metadata(boundaryFiles(18000));
    auto result=planSelectedRecheck(*ti,{2},9);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->pieces==QList<int>{1,2});
    REQUIRE(result.plan->affectedFiles==QList<int>{2});
    REQUIRE(result.plan->totalBytes==18000);
}

TEST_CASE("Disjoint selected ranges count only overlapping boundary fragments", "[selected-recheck-plan]")
{
    lt::file_storage files;
    files.add_file("sparse/A",8192);
    files.add_file("sparse/B",40960);
    files.add_file("sparse/C",8192);
    files.add_file("sparse/D",24576);
    auto ti=metadata(files);
    const auto result=planSelectedRecheck(*ti,{2,0,2},11);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->selectedFiles==QList<int>{0,2});
    REQUIRE(result.plan->pieces==QList<int>{0,3});
    REQUIRE(result.plan->affectedFiles==QList<int>{0,1,2,3});
    REQUIRE(result.plan->totalBytes==32768);
}

TEST_CASE("v2 and hybrid padding does not pull in unrelated real files", "[selected-recheck-plan]")
{
    const auto kind=GENERATE(0,1);
    auto ti=metadata(boundaryFiles(18000),kind==0 ? lt::create_torrent::v2_only : lt::create_flags_t{});
    const auto a=fileId(*ti,"fixture/A");
    const auto b=fileId(*ti,"fixture/B");
    auto result=planSelectedRecheck(*ti,{a},3);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->selectedFiles==QList<int>{a});
    REQUIRE(result.plan->affectedFiles==QList<int>{a});
    REQUIRE_FALSE(result.plan->affectedFiles.contains(b));
    REQUIRE(result.plan->pieces==QList<int>{0});
    REQUIRE(result.plan->totalBytes==8192);
    const auto c=fileId(*ti,"fixture/C");
    auto tail=planSelectedRecheck(*ti,{c},3);
    REQUIRE(tail.plan.has_value());
    REQUIRE(tail.plan->totalBytes==18000);
    REQUIRE(tail.plan->affectedFiles==QList<int>{c});
}

TEST_CASE("Zero-size real files have an explicit empty non-dividing scope", "[selected-recheck-plan]")
{
    const auto kind=GENERATE(0,1,2);
    auto files=boundaryFiles();
    files.add_file("fixture/Z",0);
    auto ti=metadata(files,kind==0 ? lt::create_torrent::v1_only : kind==1 ? lt::create_torrent::v2_only : lt::create_flags_t{});
    const auto zero=fileId(*ti,"fixture/Z");
    auto result=planSelectedRecheck(*ti,{zero},12);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->selectedFiles==QList<int>{zero});
    REQUIRE(result.plan->affectedFiles.isEmpty());
    REQUIRE(result.plan->pieces.isEmpty());
    REQUIRE(result.plan->totalBytes==0);
}

TEST_CASE("Synthetic v1 pads do not count as file bytes or affected files", "[selected-recheck-plan]")
{
    lt::file_storage files;
    files.add_file("fixture/A",8192);
    files.add_file("fixture/.pad/8192",8192,lt::file_storage::flag_pad_file);
    files.add_file("fixture/B",8192);
    auto ti=metadata(files);
    auto result=planSelectedRecheck(*ti,{0},5);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->pieces==QList<int>{0});
    REQUIRE(result.plan->affectedFiles==QList<int>{0});
    REQUIRE(result.plan->totalBytes==8192);
}

TEST_CASE("Large file offsets retain 64-bit scope arithmetic", "[selected-recheck-plan]")
{
    lt::file_storage files;
    files.add_file("big/A",(qint64(1)<<33)+7);
    files.add_file("big/B",11);
    auto ti=metadata(files,lt::create_torrent::v1_only,4*1024*1024);
    auto result=planSelectedRecheck(*ti,{1},1);
    REQUIRE(result.plan.has_value());
    REQUIRE(result.plan->pieces==QList<int>{2048});
    REQUIRE(result.plan->affectedFiles==QList<int>{0,1});
    REQUIRE(result.plan->totalBytes==18);
    const auto wide=planSelectedRecheck(*ti,{0},1);
    REQUIRE(wide.plan.has_value());
    REQUIRE(wide.plan->pieces.size()==2049);
    REQUIRE(wide.plan->affectedFiles==QList<int>{0,1});
    REQUIRE(wide.plan->totalBytes==(qint64(1)<<33)+18);
}
