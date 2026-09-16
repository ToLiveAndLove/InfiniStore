#include <cstdio>
#include <cstdlib>

#include "log.h"
#include "rdma.h"

static ibv_device device = {};
static ibv_context context = {};
static ibv_pd pd = {};
static int expected_port;
static int expected_index;
static int query_port_seen;
static int find_port_seen;
static int query_gid_port_seen;
static int query_gid_index_seen;

extern "C" {
ibv_device **__wrap_ibv_get_device_list(int *count) {
    static ibv_device *devices[] = {&device, nullptr};
    *count = 1;
    return devices;
}

const char *__wrap_ibv_get_device_name(ibv_device *) { return "mock"; }

ibv_context *__wrap_ibv_open_device(ibv_device *) { return &context; }

int __wrap_ibv_query_port(ibv_context *, uint8_t port, _compat_ibv_port_attr *attr) {
    query_port_seen = port;
    // verbs 的兼容入口接收实际的 ibv_port_attr 缓冲区。
    auto *port_attr = reinterpret_cast<ibv_port_attr *>(attr);
    port_attr->link_layer = IBV_LINK_LAYER_ETHERNET;
    port_attr->active_mtu = IBV_MTU_1024;
    return 0;
}

int __wrap_ibv_query_gid(ibv_context *, uint8_t port, int index, ibv_gid *gid) {
    query_gid_port_seen = port;
    query_gid_index_seen = index;
    *gid = {};
    gid->raw[15] = port;
    return 0;
}

ibv_pd *__wrap_ibv_alloc_pd(ibv_context *) { return &pd; }

int __wrap_ibv_dealloc_pd(ibv_pd *) { return 0; }

int __wrap_ibv_close_device(ibv_context *) { return 0; }
}

int ibv_find_sgid_type(ibv_context *, uint8_t port, ibv_gid_type, int) {
    find_port_seen = port;
    return expected_index;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        return 2;
    }
    expected_port = std::atoi(argv[1]);
    int hint = std::atoi(argv[2]);
    expected_index = hint < 0 ? 5 : hint;
    spdlog::stdout_color_mt(APP_NAME);
    spdlog::set_level(spdlog::level::off);

    rdma_device rdma_dev;
    int ret = open_rdma_device("mock", expected_port, "Ethernet", hint, &rdma_dev);
    if (ret != 0 || query_port_seen != expected_port || query_gid_port_seen != expected_port ||
        query_gid_index_seen != expected_index ||
        find_port_seen != (hint < 0 ? expected_port : 0) || rdma_dev.ib_port != expected_port ||
        rdma_dev.gid_index != expected_index || rdma_dev.gid.raw[15] != expected_port) {
        std::fprintf(stderr,
                     "port=%d hint=%d: ret=%d query_port=%d find_port=%d "
                     "query_gid_port=%d query_gid_index=%d\n",
                     expected_port, hint, ret, query_port_seen, find_port_seen, query_gid_port_seen,
                     query_gid_index_seen);
        return 1;
    }
    return close_rdma_device(&rdma_dev);
}
