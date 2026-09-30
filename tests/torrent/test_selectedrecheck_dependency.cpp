#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#if __has_include(<libtorrent/eiskalt_selected_recheck.hpp>)
#include <libtorrent/eiskalt_selected_recheck.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/disk_interface.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/alert_types.hpp>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace {
namespace lt = libtorrent;
using namespace std::chrono_literals;
struct Reads {
    std::mutex mutex;
    std::vector<lt::piece_index_t> pieces;
    int writes = 0;
    std::optional<lt::storage_index_t> storage;
    bool holdHashes = false;
    lt::error_code hashError;
    std::vector<std::function<void()>> pending;
    void release() {
        std::vector<std::function<void()>> callbacks;
        { std::lock_guard l(mutex); holdHashes = false; callbacks.swap(pending); }
        for (auto& f : callbacks) f();
    }
};

// Forward real storage operations while recording payload requests at its boundary.
struct CountingDisk final : lt::disk_interface {
    std::unique_ptr<lt::disk_interface> disk;
    std::shared_ptr<Reads> reads;
    CountingDisk(std::unique_ptr<lt::disk_interface> d, std::shared_ptr<Reads> r)
        : disk(std::move(d)), reads(std::move(r)) {}
    lt::storage_holder new_torrent(lt::storage_params const& p, std::shared_ptr<void> const& t) override {
        auto ret=disk->new_torrent(p,t);
        { std::lock_guard l(reads->mutex); if(!reads->storage) reads->storage=static_cast<lt::storage_index_t>(ret); }
        return ret;
    }
    void remove_torrent(lt::storage_index_t s) override { disk->remove_torrent(s); }
    void async_read(lt::storage_index_t s, lt::peer_request const& r, std::function<void(lt::disk_buffer_holder,lt::storage_error const&)> h, lt::disk_job_flags_t f) override { { std::lock_guard l(reads->mutex); if(s==reads->storage) reads->pieces.push_back(r.piece); } disk->async_read(s,r,std::move(h),f); }
    bool async_write(lt::storage_index_t s, lt::peer_request const& r, char const* b, std::shared_ptr<lt::disk_observer> o, std::function<void(lt::storage_error const&)> h, lt::disk_job_flags_t f) override { { std::lock_guard l(reads->mutex); ++reads->writes; } return disk->async_write(s,r,b,std::move(o),std::move(h),f); }
    void async_hash(lt::storage_index_t s, lt::piece_index_t p, lt::span<lt::sha256_hash> v, lt::disk_job_flags_t f, std::function<void(lt::piece_index_t,lt::sha1_hash const&,lt::storage_error const&)> h) override {
        { std::lock_guard l(reads->mutex); if(s==reads->storage) reads->pieces.push_back(p); }
        disk->async_hash(s,p,v,f,[r=reads,h=std::move(h)](lt::piece_index_t i,lt::sha1_hash const& hash,lt::storage_error const& e) {
            auto error=e;
            { std::lock_guard l(r->mutex);
              if(r->hashError) error.ec=r->hashError;
              if(r->holdHashes) { r->pending.push_back([h,i,hash,error]{h(i,hash,error);}); return; }
            }
            h(i,hash,error);
        });
    }
    void async_hash2(lt::storage_index_t s, lt::piece_index_t p, int o, lt::disk_job_flags_t f, std::function<void(lt::piece_index_t,lt::sha256_hash const&,lt::storage_error const&)> h) override { { std::lock_guard l(reads->mutex); reads->pieces.push_back(p); } disk->async_hash2(s,p,o,f,std::move(h)); }
    void async_move_storage(lt::storage_index_t s, std::string p, lt::move_flags_t f, std::function<void(lt::status_t,std::string const&,lt::storage_error const&)> h) override { disk->async_move_storage(s,std::move(p),f,std::move(h)); }
    void async_release_files(lt::storage_index_t s, std::function<void()> h) override { disk->async_release_files(s,std::move(h)); }
    void async_check_files(lt::storage_index_t s, lt::add_torrent_params const* p, lt::aux::vector<std::string,lt::file_index_t> v, std::function<void(lt::status_t,lt::storage_error const&)> h) override { disk->async_check_files(s,p,std::move(v),std::move(h)); }
    void async_stop_torrent(lt::storage_index_t s, std::function<void()> h) override { disk->async_stop_torrent(s,std::move(h)); }
    void async_rename_file(lt::storage_index_t s, lt::file_index_t i, std::string n, std::function<void(std::string const&,lt::file_index_t,lt::storage_error const&)> h) override { disk->async_rename_file(s,i,std::move(n),std::move(h)); }
    void async_delete_files(lt::storage_index_t s, lt::remove_flags_t f, std::function<void(lt::storage_error const&)> h) override { disk->async_delete_files(s,f,std::move(h)); }
    void async_set_file_priority(lt::storage_index_t s, lt::aux::vector<lt::download_priority_t,lt::file_index_t> p, std::function<void(lt::storage_error const&,lt::aux::vector<lt::download_priority_t,lt::file_index_t>)> h) override { disk->async_set_file_priority(s,std::move(p),std::move(h)); }
    void async_clear_piece(lt::storage_index_t s, lt::piece_index_t p, std::function<void(lt::piece_index_t)> h) override { disk->async_clear_piece(s,p,std::move(h)); }
    void update_stats_counters(lt::counters& c) const override { disk->update_stats_counters(c); }
    std::vector<lt::open_file_state> get_status(lt::storage_index_t s) const override { return disk->get_status(s); }
    void abort(bool w) override { disk->abort(w); }
    void submit_jobs() override { disk->submit_jobs(); }
    void settings_updated() override { disk->settings_updated(); }
};

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
    Fixture(lt::create_flags_t flags = lt::create_torrent::v1_only, bool omitLayers = false, bool allPresent = true, bool skipBoundaryFile = false, int lastSize = 32768, bool v1Pad = false) {
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
        params.save_path = temp.path().toStdString();
        params.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::seed_mode);
        params.flags |= lt::torrent_flags::paused | lt::torrent_flags::no_verify_files;
        if(skipBoundaryFile) params.flags &= ~lt::torrent_flags::no_verify_files;
        params.have_pieces.resize(params.ti->num_pieces(),allPresent);
        lt::session_params sp(settings());
        sp.disk_io_constructor = [r=reads](lt::io_context& io,lt::settings_interface const& p,lt::counters& c) {
            return std::make_unique<CountingDisk>(lt::default_disk_io_constructor(io,p,c),r);
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
};
}
#endif

TEST_CASE("Selected recheck requires the linked engine capability", "[selected-recheck-dependency]") {
#if __has_include(<libtorrent/eiskalt_selected_recheck.hpp>)
    REQUIRE(LIBTORRENT_EISKALT_SELECTED_RECHECK_ABI == 1);
    REQUIRE(libtorrent::eiskalt_selected_recheck_abi() == 1);
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
