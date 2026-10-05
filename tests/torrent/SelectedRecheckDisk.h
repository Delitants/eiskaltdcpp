#pragma once
#include <libtorrent/disk_interface.hpp>
#if __has_include(<libtorrent/eiskalt_selected_recheck.hpp>)
#include <libtorrent/eiskalt_selected_recheck.hpp>
#endif
#include <mutex>
#include <optional>

namespace selected_recheck_test {
namespace lt = libtorrent;
struct Reads {
    std::mutex mutex;
    std::vector<lt::piece_index_t> pieces;
    int writes = 0;
    std::optional<lt::storage_index_t> storage;
    lt::disk_interface* disk = nullptr;
    bool holdHashes = false;
    bool supportsFence = true;
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
        : disk(std::move(d)), reads(std::move(r)) { reads->disk = this; }
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
#if __has_include(<libtorrent/eiskalt_selected_recheck.hpp>)
#if LIBTORRENT_EISKALT_SELECTED_RECHECK_ABI >= 2
    void async_selected_recheck_fence(lt::storage_index_t s, std::function<void(lt::storage_error const&)> h) override {
        bool supported;
        { std::lock_guard l(reads->mutex); supported = reads->supportsFence; }
        if(supported) disk->async_selected_recheck_fence(s,std::move(h));
        else lt::disk_interface::async_selected_recheck_fence(s,std::move(h));
    }
#endif
#endif
    void async_rename_file(lt::storage_index_t s, lt::file_index_t i, std::string n, std::function<void(std::string const&,lt::file_index_t,lt::storage_error const&)> h) override { disk->async_rename_file(s,i,std::move(n),std::move(h)); }
    void async_delete_files(lt::storage_index_t s, lt::remove_flags_t f, std::function<void(lt::storage_error const&)> h) override { disk->async_delete_files(s,f,std::move(h)); }
    void async_set_file_priority(lt::storage_index_t s, lt::aux::vector<lt::download_priority_t,lt::file_index_t> p, std::function<void(lt::storage_error const&,lt::aux::vector<lt::download_priority_t,lt::file_index_t>)> h) override { disk->async_set_file_priority(s,std::move(p),std::move(h)); }
    void async_clear_piece(lt::storage_index_t s, lt::piece_index_t p, std::function<void(lt::piece_index_t)> h) override { disk->async_clear_piece(s,p,std::move(h)); }
    void update_stats_counters(lt::counters& c) const override { disk->update_stats_counters(c); }
    std::vector<lt::open_file_state> get_status(lt::storage_index_t s) const override { return disk->get_status(s); }
    void abort(bool w) override {
        // abort(false) runs on the native executor. Retire intercepted, already
        // completed jobs before that executor is destroyed, like the real disk.
        if(!w) reads->release();
        disk->abort(w);
    }
    void submit_jobs() override { disk->submit_jobs(); }
    void settings_updated() override { disk->settings_updated(); }
};

} // namespace selected_recheck_test
