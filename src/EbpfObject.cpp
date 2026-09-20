#include "EbpfObject.hpp"
#include "es_build_config.h"
#include "es_tc_legacy.skel.h"
#if ES_HAS_TCX
#include "es_tc_tcx.skel.h"
#endif
#include <bpf/libbpf.h>
#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace ElephantShrew {
namespace {
void Check(int rc, const char* what) {
    if (rc != 0) throw std::system_error(-rc, std::generic_category(), what);
}
template<class Skel, class Open, class Load, class Destroy>
LoadedObserver LoadTyped(const ObjectOptions& o, Open open, Load load, Destroy destroy) {
    std::shared_ptr<Skel> s(open(), destroy);
    if (!s) throw std::system_error(errno ? errno : ENOMEM,
                                  std::generic_category(), "open observer skeleton");
    s->rodata->sample_every = o.sample_every;
    s->rodata->snapshot_bytes = o.snapshot_bytes;
    s->rodata->classify = o.classify;
    s->rodata->flow_enabled = o.flow_enabled;
    s->rodata->select_l4_proto = o.select_l4_proto;
    Check(bpf_map__set_max_entries(s->maps.events, o.ring_buffer_bytes), "resize ring");
    Check(bpf_map__set_max_entries(s->maps.flows, o.flow_enabled ? o.max_flows : 1), "resize flows");
    Check(bpf_program__set_autoload(s->progs.es_ingress, o.ingress), "select ingress");
    Check(bpf_program__set_autoload(s->progs.es_egress, o.egress), "select egress");
    Check(load(s.get()), "load observer; inspect libbpf/verifier diagnostics");
    LoadedObserver result;
    result.owner = s;
    result.ingress = o.ingress ? s->progs.es_ingress : nullptr;
    result.egress = o.egress ? s->progs.es_egress : nullptr;
    result.events_fd = bpf_map__fd(s->maps.events);
    result.counters_fd = bpf_map__fd(s->maps.counters);
    result.flows_fd = bpf_map__fd(s->maps.flows);
    return result;
}
}
LoadedObserver LoadObserver(const ObjectOptions& o, TcApi api) {
    if (!o.ingress && !o.egress) throw std::invalid_argument("no TC direction selected");
    if (api == TcApi::Legacy)
        return LoadTyped<es_tc_legacy>(o, es_tc_legacy__open,
                                       es_tc_legacy__load, es_tc_legacy__destroy);
#if ES_HAS_TCX
    if (api == TcApi::Tcx)
        return LoadTyped<es_tc_tcx>(o, es_tc_tcx__open,
                                    es_tc_tcx__load, es_tc_tcx__destroy);
#endif
    throw std::runtime_error("requested TC attachment API was not built");
}
} // namespace ElephantShrew
