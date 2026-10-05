#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#if __has_include(<libtorrent/eiskalt_selected_recheck.hpp>)
#include <libtorrent/eiskalt_selected_recheck.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/pread_disk_io.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/alert_types.hpp>
#include <QCoreApplication>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <chrono>
#include <mutex>
#include <optional>
#include <future>
#include <thread>

#include "SelectedRecheckDisk.h"
#include "PartfileWriteFault.h"
namespace {
namespace lt = libtorrent;
using namespace std::chrono_literals;
using selected_recheck_test::Reads;
using selected_recheck_test::CountingDisk;

template<class Predicate> bool wait(Predicate p) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    do { if(p()) return true; std::this_thread::sleep_for(5ms); } while(std::chrono::steady_clock::now() < end);
    return p();
}
void put(QString const& p, int size, char c) {
    QFile file(p);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(QByteArray(size,c)) == size);
}
lt::settings_pack settings() {
    lt::settings_pack p;
    p.set_str(lt::settings_pack::listen_interfaces, "");
    p.set_bool(lt::settings_pack::enable_dht,false);
    p.set_bool(lt::settings_pack::enable_lsd,false);
    p.set_bool(lt::settings_pack::enable_upnp,false);
    p.set_bool(lt::settings_pack::enable_natpmp,false);
    return p;
}
struct Fixture {
    QTemporaryDir temp;
    std::shared_ptr<Reads> reads = std::make_shared<Reads>();
    std::unique_ptr<lt::session> session;
    lt::torrent_handle handle;
    Fixture(lt::create_flags_t flags = lt::create_torrent::v1_only, bool omitLayers = false, bool allPresent = true, bool skipBoundaryFile = false, int lastSize = 32768, bool v1Pad = false, int diskBackend = 0, bool skipLastFile = false) {
        REQUIRE(temp.isValid());
        REQUIRE(QDir().mkpath(temp.path()+"/fixture"));
        put(temp.path()+"/fixture/A",8192,'a');
        put(temp.path()+"/fixture/B",8192,'b');
        put(temp.path()+"/fixture/C",lastSize,'c');
        lt::file_storage fs;
        fs.add_file("fixture/A",8192);
        if(v1Pad) fs.add_file("fixture/.pad/8192",8192,lt::file_storage::flag_pad_file);
        fs.add_file("fixture/B",8192); fs.add_file("fixture/C",lastSize);
        lt::create_torrent maker(fs,16384,flags);
        lt::error_code ec;
        lt::set_piece_hashes(maker,temp.path().toStdString(),ec);
        REQUIRE_FALSE(ec);
        auto entry = maker.generate();
        if(omitLayers) entry.dict().erase("piece layers");
        auto params = lt::load_torrent_buffer(lt::bencode(entry));
        if(skipBoundaryFile) {
            REQUIRE(QFile::remove(temp.path()+"/fixture/B"));
            params.file_priorities={lt::default_priority,lt::dont_download,lt::default_priority};
        }
        if(skipLastFile) {
            REQUIRE(QFile::remove(temp.path()+"/fixture/C"));
            params.file_priorities={lt::default_priority,lt::dont_download,lt::dont_download};
        }
        params.save_path = temp.path().toStdString();
        params.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::seed_mode);
        params.flags |= lt::torrent_flags::paused | lt::torrent_flags::no_verify_files;
        if(skipBoundaryFile) params.flags &= ~lt::torrent_flags::no_verify_files;
        params.have_pieces.resize(params.ti->num_pieces(),allPresent);
        lt::session_params sp(settings());
        sp.disk_io_constructor = [r=reads, diskBackend](lt::io_context& io,lt::settings_interface const& p,lt::counters& c) {
            auto disk = diskBackend == 1 ? lt::posix_disk_io_constructor(io,p,c) :
                diskBackend == 2 ? lt::pread_disk_io_constructor(io,p,c) : lt::default_disk_io_constructor(io,p,c);
            return std::make_unique<CountingDisk>(std::move(disk),r);
        };
        session = std::make_unique<lt::session>(std::move(sp));
        handle = session->add_torrent(params,ec);
        REQUIRE_FALSE(ec);
        REQUIRE(wait([&]{auto s=handle.status().state; return s == lt::torrent_status::seeding || s==lt::torrent_status::downloading;}));
    }
    void prepare(std::uint64_t token = 1) {
        lt::prepare_selected_recheck(handle,token);
        REQUIRE(wait([&]{return lt::query_selected_recheck(handle,token).phase == lt::selected_recheck_phase::quiesced;}));
    }
    void writeFragment(lt::piece_index_t piece, int offset, QByteArray data) {
        auto done = std::make_shared<std::promise<lt::storage_error>>();
        auto result = done->get_future();
        boost::asio::post(session->get_context(), [r=reads, done, piece, offset, data=std::move(data)] {
            r->disk->async_write(*r->storage, {piece, offset, int(data.size())}, data.constData(), {},
                [done, data](lt::storage_error const& error) { done->set_value(error); }, {});
            r->disk->submit_jobs();
        });
        REQUIRE(result.wait_for(5s) == std::future_status::ready);
        REQUIRE_FALSE(result.get());
    }
};
}
#endif

TEST_CASE("Selected recheck requires the linked engine capability", "[selected-recheck-dependency]") {
#if __has_include(<libtorrent/eiskalt_selected_recheck.hpp>)
    REQUIRE(LIBTORRENT_EISKALT_SELECTED_RECHECK_ABI == 2);
    REQUIRE(libtorrent::eiskalt_selected_recheck_abi() == 2);
#else
    FAIL("Linked libtorrent does not provide selected rechecking");
#endif
}

#if __has_include(<libtorrent/eiskalt_selected_recheck.hpp>)
TEST_CASE("Selected recheck reads the boundary piece and preserves unrelated availability", "[selected-recheck-dependency]") {
    auto const kind = GENERATE(0,1,2);
    Fixture fixture(kind==0 ? lt::create_torrent::v1_only : kind==1 ? lt::create_torrent::v2_only : lt::create_flags_t{});
    auto handle = fixture.handle;
    auto reads = fixture.reads;
    auto priorities = handle.get_file_priorities();
    fixture.prepare();
    put(fixture.temp.path()+"/fixture/A",8192,'x');
    lt::start_selected_recheck(handle,{1,{lt::piece_index_t(0)}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(handle,1).phase == lt::selected_recheck_phase::complete;}));
    auto result = lt::query_selected_recheck(handle,1);
    REQUIRE(result.failed == std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
    REQUIRE(result.checked_bytes == (kind==0 ? 16384 : 8192));
    REQUIRE_FALSE(handle.have_piece(lt::piece_index_t(0)));
    REQUIRE(handle.have_piece(lt::piece_index_t(1)));
    REQUIRE(handle.have_piece(lt::piece_index_t(2)));
    REQUIRE(handle.get_file_priorities() == priorities);
    REQUIRE(bool(handle.status().flags & lt::torrent_flags::paused));
    const auto checkpoint = handle.get_resume_data(lt::torrent_handle::save_info_dict);
    REQUIRE(checkpoint.have_pieces.size() == handle.torrent_file()->num_pieces());
    REQUIRE_FALSE(checkpoint.have_pieces[lt::piece_index_t(0)]);
    REQUIRE(checkpoint.have_pieces[lt::piece_index_t(1)]);
    REQUIRE(checkpoint.have_pieces[lt::piece_index_t(2)]);
    { std::lock_guard l(reads->mutex);
      REQUIRE(reads->pieces == std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
      REQUIRE(reads->writes == 0);
    }
    lt::finish_selected_recheck(handle,1,false);
}

TEST_CASE("Stopped selected-check finalization does not resolve web seeds", "[selected-recheck-dependency][selected-recheck-webseed]") {
    static int argc = 1;
    static char name[] = "selected-recheck-network-fixture";
    static char *argv[] = {name, nullptr};
    static QCoreApplication application(argc, argv);
    Fixture f;
    QTcpServer webSeed;
    REQUIRE(webSeed.listen(QHostAddress::LocalHost));
    lt::settings_pack logging;
    logging.set_int(lt::settings_pack::alert_mask,
        int(static_cast<std::uint32_t>(lt::alert_category::torrent_log | lt::alert_category::error)));
    f.session->apply_settings(logging);
    f.handle.add_url_seed("http://localhost:" + std::to_string(webSeed.serverPort()) + "/fixture/");
    f.prepare();
    std::vector<lt::alert *> alerts;
    f.session->pop_alerts(&alerts);
    put(f.temp.path() + "/fixture/A", 8192, 'x');
    lt::start_selected_recheck(f.handle, {1, {lt::piece_index_t(0)}});
    REQUIRE(wait([&] { return lt::query_selected_recheck(f.handle, 1).phase == lt::selected_recheck_phase::complete; }));
    lt::finish_selected_recheck(f.handle, 1, false);
    const auto status = f.handle.status();
    CHECK(bool(status.flags & lt::torrent_flags::paused));
    CHECK_FALSE(f.handle.have_piece(lt::piece_index_t(0)));
    f.session->pop_alerts(&alerts);
    REQUIRE_FALSE(alerts.empty());
    for (const auto *alert : alerts) {
        const auto message = alert->message();
        INFO(message);
        REQUIRE(message.find("resolving web seed") == std::string::npos);
    }
    lt::settings_pack active;
    active.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    f.session->apply_settings(active);
    REQUIRE(wait([&] { return f.session->listen_port() != 0; }));
    f.handle.resume();
    REQUIRE(wait([&] {
        f.session->pop_alerts(&alerts);
        return std::any_of(alerts.begin(), alerts.end(), [](const auto *alert) {
            return alert->message().find("resolving web seed") != std::string::npos;
        });
    }));
}

TEST_CASE("Selected seed-to-download transition refreshes known seed candidacy", "[selected-recheck-dependency][selected-recheck-candidates]") {
    Fixture f(lt::create_torrent::v1_only, false, false);
    REQUIRE(QDir().mkpath(f.temp.path() + "/seed/fixture"));
    for (const auto *name : {"A", "B", "C"})
        REQUIRE(QFile::copy(f.temp.path() + "/fixture/" + name, f.temp.path() + "/seed/fixture/" + name));
    auto config = settings();
    config.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    lt::session seed{lt::session_params(config)};
    lt::add_torrent_params params;
    params.ti = std::make_shared<lt::torrent_info>(*f.handle.torrent_file());
    params.save_path = (f.temp.path() + "/seed").toStdString();
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    const auto source = seed.add_torrent(params);
    REQUIRE(wait([&] { return source.status().is_seeding && seed.listen_port(); }));
    f.session->apply_settings(config);
    REQUIRE(wait([&] { return f.session->listen_port() != 0; }));
    f.handle.resume();
    f.handle.connect_peer({lt::make_address("127.0.0.1"), std::uint16_t(seed.listen_port())});
    REQUIRE(wait([&] { return f.handle.status().is_finished; }));
    f.handle.pause();
    REQUIRE(wait([&] { return bool(f.handle.status().flags & lt::torrent_flags::paused) && f.handle.status().num_peers == 0; }));
    REQUIRE(f.handle.status().list_seeds == 1);
    lt::settings_pack refresh;
    refresh.set_int(lt::settings_pack::max_failcount, 4);
    f.session->apply_settings(refresh);
    REQUIRE(wait([&] { return f.handle.status().connect_candidates == 0; }));
    f.prepare();
    put(f.temp.path() + "/fixture/A", 8192, 'x');
    lt::start_selected_recheck(f.handle, {1, {lt::piece_index_t(0)}});
    REQUIRE(wait([&] { return lt::query_selected_recheck(f.handle, 1).phase == lt::selected_recheck_phase::complete; }));
    lt::finish_selected_recheck(f.handle, 1, false);
    const auto status = f.handle.status();
    CHECK_FALSE(status.is_finished);
    CHECK(bool(status.flags & lt::torrent_flags::paused));
    REQUIRE(status.list_seeds == 1);
    CHECK(status.connect_candidates == 1);
    CHECK(int(status.queue_position) >= 0);
}

TEST_CASE("Correct selected pieces restore availability on read-only payloads", "[selected-recheck-dependency]") {
    auto const kind = GENERATE(0,1,2);
    Fixture f(kind==0 ? lt::create_torrent::v1_only : kind==1 ? lt::create_torrent::v2_only : lt::create_flags_t{});
    REQUIRE(QFile::setPermissions(f.temp.path()+"/fixture/A",QFile::ReadOwner));
    REQUIRE(QFile::setPermissions(f.temp.path()+"/fixture/B",QFile::ReadOwner));
    f.prepare();
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(0),lt::piece_index_t(0)}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase == lt::selected_recheck_phase::complete;}));
    REQUIRE(lt::query_selected_recheck(f.handle,1).passed == std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
    REQUIRE(f.handle.have_piece(lt::piece_index_t(0)));
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->writes == 0); REQUIRE(f.reads->pieces.size()==1); }
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("Unchanged downloaded bytes cannot substitute for missing authenticated v2 hashes", "[selected-recheck-dependency]") {
    Fixture f(lt::create_torrent::v2_only,true);
    f.prepare();
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(2)}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase == lt::selected_recheck_phase::complete;}));
    auto result=lt::query_selected_recheck(f.handle,1);
    REQUIRE(result.passed.empty());
    REQUIRE(result.failed.empty());
    REQUIRE(result.unavailable==std::vector<lt::piece_index_t>{lt::piece_index_t(2)});
    REQUIRE_FALSE(f.handle.have_piece(lt::piece_index_t(2)));
    REQUIRE(f.handle.have_piece(lt::piece_index_t(3)));
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("Cancel drains admitted hashes and leaves unscheduled pieces unavailable", "[selected-recheck-dependency]") {
    Fixture f;
    f.prepare();
    { std::lock_guard l(f.reads->mutex); f.reads->holdHashes=true; }
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(0),lt::piece_index_t(1),lt::piece_index_t(2)}});
    REQUIRE(wait([&]{std::lock_guard l(f.reads->mutex); return f.reads->pending.size()==1;}));
    lt::cancel_selected_recheck(f.handle,1);
    REQUIRE(lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::progress);
    // Callback delivery must occur on the session executor, like a real disk completion.
    boost::asio::post(f.session->get_context(),[r=f.reads]{r->release();});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::canceled;}));
    auto result=lt::query_selected_recheck(f.handle,1);
    REQUIRE(result.passed==std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
    REQUIRE(result.unavailable==std::vector<lt::piece_index_t>{lt::piece_index_t(1),lt::piece_index_t(2)});
    REQUIRE_FALSE(f.handle.have_piece(lt::piece_index_t(1)));
    REQUIRE_FALSE(f.handle.have_piece(lt::piece_index_t(2)));
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->pieces==std::vector<lt::piece_index_t>{lt::piece_index_t(0)}); }
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("Stale and invalid operations cannot resume or replace the active check", "[selected-recheck-dependency]") {
    Fixture f;
    f.prepare();
    lt::start_selected_recheck(f.handle,{2,{lt::piece_index_t(0)}});
    lt::finish_selected_recheck(f.handle,2,true);
    f.handle.resume();
    f.handle.set_flags(lt::torrent_flags::auto_managed);
    REQUIRE(lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::quiesced);
    REQUIRE(bool(f.handle.status().flags & lt::torrent_flags::paused));
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(-1)}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::failed;}));
    REQUIRE(f.handle.have_piece(lt::piece_index_t(0)));
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->pieces.empty()); }
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("Stopped local-only checking does not scan missing unrelated pieces", "[selected-recheck-dependency]") {
    Fixture f(lt::create_torrent::v1_only,false,false);
    f.prepare();
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(0)}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::complete;}));
    REQUIRE(f.handle.have_piece(lt::piece_index_t(0)));
    REQUIRE_FALSE(f.handle.have_piece(lt::piece_index_t(1)));
    REQUIRE_FALSE(f.handle.have_piece(lt::piece_index_t(2)));
    REQUIRE(f.session->listen_port()==0);
    REQUIRE(f.handle.status().num_peers==0);
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->pieces==std::vector<lt::piece_index_t>{lt::piece_index_t(0)}); REQUIRE(f.reads->writes==0); }
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("Missing selected payload stays missing without creating files", "[selected-recheck-dependency]") {
    Fixture f;
    f.prepare();
    REQUIRE(QFile::remove(f.temp.path()+"/fixture/A"));
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(0)}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::complete;}));
    REQUIRE(lt::query_selected_recheck(f.handle,1).failed==std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
    REQUIRE_FALSE(QFile::exists(f.temp.path()+"/fixture/A"));
    REQUIRE_FALSE(f.handle.have_piece(lt::piece_index_t(0)));
    REQUIRE(f.handle.have_piece(lt::piece_index_t(1)));
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->writes==0); }
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("An unrelated torrent transfers while a selected check is held", "[selected-recheck-dependency]") {
    Fixture f;
    f.prepare();
    { std::lock_guard l(f.reads->mutex); f.reads->holdHashes=true; }
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(0)}});
    REQUIRE(wait([&]{std::lock_guard l(f.reads->mutex); return f.reads->pending.size()==1;}));
    auto config=settings(); config.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");
    f.session->apply_settings(config);
    REQUIRE(wait([&]{return f.session->listen_port()!=0;}));
    put(f.temp.path()+"/other",262144,'t');
    lt::file_storage fs; fs.add_file("other",262144);
    lt::create_torrent maker(fs,16384,lt::create_torrent::v1_only);
    lt::error_code ec; lt::set_piece_hashes(maker,f.temp.path().toStdString(),ec); REQUIRE_FALSE(ec);
    auto seed=lt::load_torrent_buffer(lt::bencode(maker.generate()));
    seed.save_path=f.temp.path().toStdString();
    seed.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed | lt::torrent_flags::seed_mode);
    seed.flags |= lt::torrent_flags::no_verify_files;
    seed.have_pieces.resize(seed.ti->num_pieces(),true);
    auto sh=f.session->add_torrent(seed,ec); REQUIRE_FALSE(ec);
    REQUIRE(wait([&]{return sh.status().state==lt::torrent_status::seeding;}));
    lt::session downloader(config);
    REQUIRE(wait([&]{return downloader.listen_port()!=0;}));
    auto dp=seed; dp.save_path=(f.temp.path()+"/download").toStdString();
    dp.flags &= ~lt::torrent_flags::no_verify_files;
    REQUIRE(QDir().mkpath(QString::fromStdString(dp.save_path)));
    dp.have_pieces.resize(dp.ti->num_pieces(),false);
    dp.have_pieces.clear_all();
    auto dh=downloader.add_torrent(dp,ec); REQUIRE_FALSE(ec);
    REQUIRE(wait([&]{return dh.status().state==lt::torrent_status::downloading;}));
    dh.connect_peer({lt::make_address("127.0.0.1"),std::uint16_t(f.session->listen_port())});
    const bool transferred=wait([&]{return dh.status().is_seeding;});
    if(!transferred) {
        std::vector<lt::alert*> alerts;
        downloader.pop_alerts(&alerts);
        for(auto* a : alerts) UNSCOPED_INFO(a->message());
        auto ds=dh.status();
        UNSCOPED_INFO("download state=" << int(ds.state) << " peers=" << ds.num_peers << " bytes=" << ds.total_done << " error=" << ds.errc.message());
        UNSCOPED_INFO("seed peers=" << sh.status().num_peers << " seed paused=" << bool(sh.status().flags & lt::torrent_flags::paused));
    }
    REQUIRE(transferred);
    REQUIRE(lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::progress);
    boost::asio::post(f.session->get_context(),[r=f.reads]{r->release();});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::complete;}));
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("A skipped boundary fragment is checked through native partfile storage", "[selected-recheck-dependency]") {
    Fixture f(lt::create_torrent::v1_only,false,false,true);
    // Populate native storage as a prior download would, before starting any check.
    std::vector<char> data(16384,'a'); std::fill(data.begin()+8192,data.end(),'b');
    f.handle.add_piece(lt::piece_index_t(0),data.data());
    REQUIRE(wait([&]{return f.handle.have_piece(lt::piece_index_t(0));}));
    REQUIRE_FALSE(QFile::exists(f.temp.path()+"/fixture/B"));
    f.prepare();
    // Restore a stopped job without full file verification, including its partfile.
    auto resume=f.handle.get_resume_data(lt::torrent_handle::save_info_dict);
    resume.flags |= lt::torrent_flags::paused | lt::torrent_flags::no_verify_files;
    resume.flags &= ~lt::torrent_flags::auto_managed;
    f.session.reset();
    { std::lock_guard l(f.reads->mutex); f.reads->storage.reset(); }
    lt::session_params sp(settings());
    sp.disk_io_constructor = [r=f.reads](lt::io_context& io,lt::settings_interface const& p,lt::counters& c) {
        return std::make_unique<CountingDisk>(lt::default_disk_io_constructor(io,p,c),r);
    };
    f.session=std::make_unique<lt::session>(std::move(sp));
    lt::error_code ec; f.handle=f.session->add_torrent(resume,ec); REQUIRE_FALSE(ec);
    REQUIRE(wait([&]{return f.handle.status().state==lt::torrent_status::downloading;}));
    f.prepare(2);
    { std::lock_guard l(f.reads->mutex); f.reads->pieces.clear(); f.reads->writes=0; }
    lt::start_selected_recheck(f.handle,{2,{lt::piece_index_t(0)}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,2).phase==lt::selected_recheck_phase::complete;}));
    REQUIRE(lt::query_selected_recheck(f.handle,2).passed==std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
    REQUIRE_FALSE(QFile::exists(f.temp.path()+"/fixture/B"));
    REQUIRE(f.handle.get_file_priorities()[1]==lt::dont_download);
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->writes==0); REQUIRE(f.reads->pieces==std::vector<lt::piece_index_t>{lt::piece_index_t(0)}); }
    lt::finish_selected_recheck(f.handle,2,false);
}

TEST_CASE("Storage fences reload persisted partfile maps before selected hashing", "[selected-recheck-dependency][selected-recheck-partmap]") {
    const auto backend = GENERATE(0, 1, 2);
    INFO("disk backend=" << backend);
    Fixture f(lt::create_torrent::v1_only, false, false, true, 32768, false, backend);
    std::vector<char> data(16384, 'a');
    std::fill(data.begin() + 8192, data.end(), 'b');
    f.handle.add_piece(lt::piece_index_t(0), data.data());
    REQUIRE(wait([&] { return f.handle.have_piece(lt::piece_index_t(0)); }));
    f.prepare(11);
    lt::cancel_selected_recheck(f.handle, 11);
    REQUIRE(wait([&] { return lt::query_selected_recheck(f.handle, 11).phase == lt::selected_recheck_phase::canceled; }));
    lt::finish_selected_recheck(f.handle, 11, false);
    const auto names = QDir(f.temp.path()).entryList({"*.parts"}, QDir::Files | QDir::Hidden);
    REQUIRE(names.size() == 1);
    QFile backing(f.temp.path() + '/' + names.first());
    REQUIRE(backing.open(QIODevice::ReadWrite));
    REQUIRE(backing.seek(8));
    REQUIRE(backing.read(4) != QByteArray(4, char(0xff)));
    REQUIRE(backing.seek(8));
    REQUIRE(backing.write(QByteArray(4, char(0xff))) == 4);
    backing.close();
    REQUIRE(backing.open(QIODevice::ReadOnly));
    const auto before = backing.readAll();
    backing.close();
    f.prepare(12);
    { std::lock_guard l(f.reads->mutex); f.reads->pieces.clear(); f.reads->writes = 0; }
    lt::start_selected_recheck(f.handle, {12, {lt::piece_index_t(0)}});
    REQUIRE(wait([&] { return lt::query_selected_recheck(f.handle, 12).phase == lt::selected_recheck_phase::complete; }));
    const auto result = lt::query_selected_recheck(f.handle, 12);
    CHECK(result.passed.empty());
    CHECK(result.failed == std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
    CHECK_FALSE(f.handle.have_piece(lt::piece_index_t(0)));
    CHECK(bool(f.handle.status().flags & lt::torrent_flags::paused));
    { std::lock_guard l(f.reads->mutex); CHECK(f.reads->writes == 0); CHECK(f.reads->pieces == std::vector<lt::piece_index_t>{lt::piece_index_t(0)}); }
    REQUIRE(backing.open(QIODevice::ReadOnly));
    CHECK(backing.readAll() == before);
    lt::finish_selected_recheck(f.handle, 12, false);
}

TEST_CASE("Failed partfile metadata fences retain dirty slots and fail preparation", "[selected-recheck-dependency][selected-recheck-partmap][selected-recheck-flush-failure]") {
    const auto backend = GENERATE(0, 1, 2);
#ifdef EISKALT_TEST_INTERPOSE_PARTFILE_PWRITE
    std::vector<int> faultKinds{0, EIO, ENOSPC};
    if(backend == 1) { faultKinds.push_back(-EIO); faultKinds.push_back(-ENOSPC); }
    const auto fault = GENERATE_COPY(Catch::Generators::from_range(faultKinds));
#else
    const auto fault = 0;
#endif
    INFO("disk backend=" << backend);
    INFO("metadata fault=" << fault);
    Fixture f(lt::create_torrent::v1_only, false, false, true, 32768, false, backend, true);
    f.writeFragment(lt::piece_index_t(0), 8192, QByteArray(8192, 'b'));
    const auto priorities = f.handle.get_file_priorities();
    const auto names = QDir(f.temp.path()).entryList({"*.parts"}, QDir::Files | QDir::Hidden);
    REQUIRE(names.size() == 1);
    const auto partPath = f.temp.path() + '/' + names.first();
    QFile backing(partPath);
    REQUIRE(backing.open(QIODevice::ReadOnly));
    const auto before = backing.readAll();
    backing.close();
    REQUIRE(before.size() > 1024);
    // There is fragment payload, but no persisted header for its dirty slot.
    REQUIRE(before.left(8) == QByteArray(8, char(0)));
    const auto permissions = QFile::permissions(partPath);
#ifdef EISKALT_TEST_INTERPOSE_PARTFILE_PWRITE
    REQUIRE(backing.open(QIODevice::ReadOnly));
    selected_recheck_test::ScopedPartfileWriteFault writeFault(backing.handle(), fault);
    backing.close();
#endif
    if(!fault) {
        REQUIRE(QFile::setPermissions(partPath, QFileDevice::ReadOwner));
        REQUIRE_FALSE(backing.open(QIODevice::ReadWrite));
    }
    for (const auto token : {21u, 22u}) {
        { std::lock_guard l(f.reads->mutex); f.reads->pieces.clear(); f.reads->writes = 0; }
        lt::prepare_selected_recheck(f.handle, token);
        REQUIRE(wait([&] {
            const auto phase = lt::query_selected_recheck(f.handle, token).phase;
            return phase == lt::selected_recheck_phase::failed || phase == lt::selected_recheck_phase::quiesced;
        }));
        const auto result = lt::query_selected_recheck(f.handle, token);
        CHECK(result.phase == lt::selected_recheck_phase::failed);
        CHECK(result.error == (fault ? boost::system::errc::errc_t(fault < 0 ? -fault : fault) : boost::system::errc::permission_denied));
        CHECK(result.passed.empty());
        CHECK(result.failed.empty());
        CHECK(result.checked_bytes == 0);
        lt::start_selected_recheck(f.handle, {token, {lt::piece_index_t(0)}});
        lt::cancel_selected_recheck(f.handle, token);
        REQUIRE(wait([&] {
            const auto phase = lt::query_selected_recheck(f.handle, token).phase;
            return phase == lt::selected_recheck_phase::failed || phase == lt::selected_recheck_phase::canceled || phase == lt::selected_recheck_phase::complete;
        }));
        lt::finish_selected_recheck(f.handle, token, true);
        CHECK(bool(f.handle.status().flags & lt::torrent_flags::paused));
        CHECK_FALSE(f.handle.have_piece(lt::piece_index_t(0)));
        CHECK(f.handle.get_file_priorities() == priorities);
        { std::lock_guard l(f.reads->mutex); CHECK(f.reads->pieces.empty()); CHECK(f.reads->writes == 0); }
    }
    REQUIRE(backing.open(QIODevice::ReadOnly));
    CHECK(backing.readAll() == before);
    backing.close();
#ifdef EISKALT_TEST_INTERPOSE_PARTFILE_PWRITE
    if(fault) {
        CHECK(writeFault.failures() >= 2);
    }
    writeFault.clear();
#endif
    REQUIRE(QFile::setPermissions(partPath, permissions));
    // A different skipped piece must not reuse the first fragment's dirty slot.
    f.writeFragment(lt::piece_index_t(1), 0, QByteArray(16384, 'c'));
    f.prepare(23);
    { std::lock_guard l(f.reads->mutex); f.reads->pieces.clear(); f.reads->writes = 0; }
    lt::start_selected_recheck(f.handle, {23, {lt::piece_index_t(0), lt::piece_index_t(1)}});
    REQUIRE(wait([&] { return lt::query_selected_recheck(f.handle, 23).phase == lt::selected_recheck_phase::complete; }));
    CHECK(lt::query_selected_recheck(f.handle, 23).passed == std::vector<lt::piece_index_t>{lt::piece_index_t(0), lt::piece_index_t(1)});
    CHECK(f.handle.get_file_priorities() == priorities);
    CHECK_FALSE(QFile::exists(f.temp.path() + "/fixture/B"));
    CHECK_FALSE(QFile::exists(f.temp.path() + "/fixture/C"));
    { std::lock_guard l(f.reads->mutex); CHECK(f.reads->writes == 0); }
    lt::finish_selected_recheck(f.handle, 23, false);
}

#if LIBTORRENT_EISKALT_SELECTED_RECHECK_ABI >= 2
TEST_CASE("A custom disk without the durable fence rejects preparation", "[selected-recheck-dependency][selected-recheck-fence-required]") {
    Fixture f;
    const auto priorities = f.handle.get_file_priorities();
    { std::lock_guard lock(f.reads->mutex); f.reads->supportsFence = false; f.reads->pieces.clear(); f.reads->writes = 0; }
    lt::prepare_selected_recheck(f.handle, 31);
    REQUIRE(wait([&] { return lt::query_selected_recheck(f.handle, 31).phase == lt::selected_recheck_phase::failed; }));
    CHECK(lt::query_selected_recheck(f.handle, 31).error == boost::system::errc::operation_not_supported);
    lt::start_selected_recheck(f.handle, {31, {lt::piece_index_t(0)}});
    lt::finish_selected_recheck(f.handle, 31, true);
    CHECK(bool(f.handle.status().flags & lt::torrent_flags::paused));
    CHECK(f.handle.get_file_priorities() == priorities);
    CHECK(f.handle.have_piece(lt::piece_index_t(0)));
    { std::lock_guard lock(f.reads->mutex); CHECK(f.reads->pieces.empty()); CHECK(f.reads->writes == 0); }
}
#endif

TEST_CASE("An empty piece scope finishes without disk reads or changing availability", "[selected-recheck-dependency]") {
    auto const kind=GENERATE(0,1,2);
    Fixture f(kind==0 ? lt::create_torrent::v1_only : kind==1 ? lt::create_torrent::v2_only : lt::create_flags_t{},false,true,false,0);
    f.prepare();
    lt::start_selected_recheck(f.handle,{1,{}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::complete;}));
    REQUIRE(lt::query_selected_recheck(f.handle,1).checked_bytes==0);
    REQUIRE(lt::query_selected_recheck(f.handle,1).total_bytes==0);
    REQUIRE(f.handle.have_piece(lt::piece_index_t(0)));
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->pieces.empty()); REQUIRE(f.reads->writes==0); }
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("A final partial piece uses native v1 v2 and hybrid padding rules", "[selected-recheck-dependency]") {
    auto const kind=GENERATE(0,1,2);
    Fixture f(kind==0 ? lt::create_torrent::v1_only : kind==1 ? lt::create_torrent::v2_only : lt::create_flags_t{},false,true,false,18000);
    const auto ti=f.handle.torrent_file();
    const auto piece=ti->layout().last_piece();
    f.prepare();
    lt::start_selected_recheck(f.handle,{1,{piece}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::complete;}));
    const auto result=lt::query_selected_recheck(f.handle,1);
    REQUIRE(result.passed==std::vector<lt::piece_index_t>{piece});
    REQUIRE(result.checked_bytes==1616);
    REQUIRE(result.total_bytes==1616);
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->pieces==std::vector<lt::piece_index_t>{piece}); REQUIRE(f.reads->writes==0); }
    lt::finish_selected_recheck(f.handle,1,false);
}

TEST_CASE("A storage permission error fails closed rather than completing successfully", "[selected-recheck-dependency]") {
    Fixture f;
    f.prepare();
    { std::lock_guard l(f.reads->mutex); f.reads->hashError=make_error_code(boost::system::errc::permission_denied); }
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(0)}});
    REQUIRE(wait([&]{const auto phase=lt::query_selected_recheck(f.handle,1).phase; return phase==lt::selected_recheck_phase::complete || phase==lt::selected_recheck_phase::failed;}));
    const auto result=lt::query_selected_recheck(f.handle,1);
    REQUIRE(result.phase==lt::selected_recheck_phase::failed);
    REQUIRE(result.error==boost::system::errc::permission_denied);
    REQUIRE(result.passed.empty());
    REQUIRE(result.failed.empty());
    REQUIRE(result.unavailable==std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
    REQUIRE_FALSE(f.handle.have_piece(lt::piece_index_t(0)));
    lt::finish_selected_recheck(f.handle,1,true);
    REQUIRE(bool(f.handle.status().flags & lt::torrent_flags::paused));
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->writes==0); }
}

TEST_CASE("Synthetic v1 padding is not counted as checked payload bytes", "[selected-recheck-dependency]") {
    Fixture f(lt::create_torrent::v1_only,false,true,false,32768,true);
    f.prepare();
    lt::start_selected_recheck(f.handle,{1,{lt::piece_index_t(0)}});
    REQUIRE(wait([&]{return lt::query_selected_recheck(f.handle,1).phase==lt::selected_recheck_phase::complete;}));
    const auto result=lt::query_selected_recheck(f.handle,1);
    REQUIRE(result.passed==std::vector<lt::piece_index_t>{lt::piece_index_t(0)});
    REQUIRE(result.checked_bytes==8192);
    REQUIRE(result.total_bytes==8192);
    { std::lock_guard l(f.reads->mutex); REQUIRE(f.reads->pieces==std::vector<lt::piece_index_t>{lt::piece_index_t(0)}); REQUIRE(f.reads->writes==0); }
    lt::finish_selected_recheck(f.handle,1,false);
}
#endif
