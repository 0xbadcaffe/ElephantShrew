#ifndef ELEPHANTSHREW_TC_ATTACHMENT_HPP
#define ELEPHANTSHREW_TC_ATTACHMENT_HPP
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <system_error>

namespace ElephantShrew {
class TcAttachment {
    bpf_tc_hook hook_{};
    std::uint32_t handle_{}, priority_{}, program_id_{};
    bool attached_{};
public:
    TcAttachment() = default;
    TcAttachment(const TcAttachment&) = delete;
    TcAttachment& operator=(const TcAttachment&) = delete;
    ~TcAttachment() noexcept {
        const int rc = Detach();
        if (rc != 0) std::fprintf(stderr, "TC cleanup failed: %d\n", rc);
    }
    void Attach(int ifindex, bpf_tc_attach_point direction, int prog_fd,
                std::uint32_t handle, std::uint32_t priority) {
        if (attached_) throw std::logic_error("TC attachment already active");
        if (ifindex <= 0 || prog_fd < 0 || handle == 0 || priority == 0 || priority > 65535)
            throw std::invalid_argument("invalid TC attachment identity");
        bpf_prog_info info{};
        std::uint32_t len = sizeof(info);
        if (bpf_obj_get_info_by_fd(prog_fd, &info, &len) < 0)
            throw std::system_error(errno, std::generic_category(), "read program ID");
        hook_ = {};
        hook_.sz = sizeof(hook_);
        hook_.ifindex = ifindex;
        hook_.attach_point = direction;
        int rc = bpf_tc_hook_create(&hook_);
        if (rc != 0 && rc != -EEXIST)
            throw std::system_error(-rc, std::generic_category(), "create clsact");
        bpf_tc_opts opts{};
        opts.sz = sizeof(opts);
        opts.prog_fd = prog_fd;
        opts.handle = handle;
        opts.priority = priority;
        // No replacement flag. An occupied identity is an error.
        rc = bpf_tc_attach(&hook_, &opts);
        if (rc != 0)
            throw std::system_error(-rc, std::generic_category(), "attach TC filter");
        handle_ = opts.handle;
        priority_ = opts.priority;
        program_id_ = info.id;
        attached_ = true; // No throwing operations after ownership becomes active.
    }
    int Detach() noexcept {
        if (!attached_) return 0;
        bpf_tc_opts query{};
        query.sz = sizeof(query);
        query.handle = handle_;
        query.priority = priority_;
        int rc = bpf_tc_query(&hook_, &query);
        if (rc == -ENOENT || rc == -ENODEV) { attached_ = false; return 0; }
        if (rc != 0) return rc;
        if (query.prog_id != program_id_) {
            attached_ = false; // The old program is no longer at our slot.
            return -ESTALE;    // Do not delete the new owner's program.
        }
        // Detach requires fresh opts: prog_fd/prog_id/flags must remain zero.
        bpf_tc_opts detach{};
        detach.sz = sizeof(detach);
        detach.handle = handle_;
        detach.priority = priority_;
        rc = bpf_tc_detach(&hook_, &detach);
        if (rc == 0 || rc == -ENOENT || rc == -ENODEV) {
            attached_ = false;
            return 0;
        }
        return rc;
    }
};
} // namespace ElephantShrew
#endif
