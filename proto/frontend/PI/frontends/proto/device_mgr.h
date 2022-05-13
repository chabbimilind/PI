/* Copyright 2013-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#ifndef PI_FRONTENDS_PROTO_DEVICE_MGR_H_
#define PI_FRONTENDS_PROTO_DEVICE_MGR_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <grpcpp/grpcpp.h>

#include "google/rpc/status.pb.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/server/v1/config.pb.h"
#include "p4/v1/p4runtime.pb.h"

#ifdef HAVE_SHM
#ifndef MAX_VALUE_STR
#define MAX_VALUE_STR (16) // Max of 16 bytes of any value type
#endif

#ifndef MAX_FIELD_MATCHES
#define MAX_FIELD_MATCHES (5) // Max 5 field matches
#endif

#ifndef MAX_MASK_STR
#define MAX_MASK_STR (16) // Max IP mask of 16 bytes
#endif

#ifndef MAX_PARAMS
#define MAX_PARAMS (8) // Max num of action params = 8
#endif

#define CACHE_LINE_SIZE (64) // Cacheline of 64 bytes for avoiding false sharing

#ifndef SHM_SZ
#define SHM_SZ (1<<19) // 512 KB
#endif

#ifndef MAX_BATCHES
#define MAX_BATCHES (4) // Four partitions of the SHM buffer for pipelining
#endif

#ifndef MAX_SERVER_ENTRIES
#define MAX_SERVER_ENTRIES (10000UL) // Server side copy buffer
#endif
#define INVALID_SEND_CNT (0)
#define INVALID_UPDATE (-1)
#endif // HAVE_SHM


#if __has_cpp_attribute(deprecated)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++14-extensions"
#endif  // clang
#define _PI_DEPRECATED [[deprecated]]  // NOLINT(whitespace/braces)
#else
#define _PI_DEPRECATED
#endif  // deprecated

namespace pi {

namespace fe {

#ifdef HAVE_SHM
namespace local {
    
    struct FieldMatch_Exact {
    char value[MAX_VALUE_STR];
    };

    struct FieldMatch_Ternary {
    char value[MAX_VALUE_STR];
    char mask[MAX_MASK_STR];
    };

    struct FieldMatch_LPM {
    char value[MAX_VALUE_STR];
    uint32_t prefix_len_;
    };

    struct FieldMatch_Range {
    char low[MAX_VALUE_STR];
    char high[MAX_VALUE_STR];
    };

    struct FieldMatch_Optional {
    char value[MAX_VALUE_STR];
    };

    struct Action_Param  {
        char value[MAX_VALUE_STR];
        uint32_t param_id_;
    };

    struct Action  {
    Action_Param  params_[MAX_PARAMS];
    uint32_t num_params_;
    uint32_t action_id_;
    };

    struct FieldMatch {
    union {
        FieldMatch_Exact exact_;
        FieldMatch_Ternary ternary_;
        FieldMatch_LPM lpm_;
        FieldMatch_Range range_;
        FieldMatch_Optional optional_;
    };
    uint32_t field_id_;
    uint8_t underlyingType;
    };

    struct TableEntry {
    uint32_t numFields;
    FieldMatch match_[MAX_FIELD_MATCHES];
    Action action_;
    uint32_t table_id_;
    };

    struct Update {
        uint8_t type;
        TableEntry t;
    };


    grpc::Status WriteLocal(const p4::v1::WriteRequest &request);

    typedef struct TableHeaders {
        // The head pointer from where the consumer reads data
        std::atomic<uint64_t> head;
        uint8_t dummy1[CACHE_LINE_SIZE];
        // The tail pointer into which the producer puts data
        std::atomic<uint64_t> tail;
        uint8_t dummy2[CACHE_LINE_SIZE];
        // The monotonically increasing last sent count: always an even number.
        std::atomic<uint64_t> lastSent;
        // The count of elements in this WriteLocal set.
        std::atomic<uint64_t> lastSendCnt;
        // The monotonically increasing last received count (an ACK from receiver to the sender).
        // if lastRecvd + 1 == lastSent ==> the receive is complete.
        std::atomic<uint64_t> lastRecvd;
    } TableHeaders;

    const uint64_t _shmMaxEntries = (SHM_SZ - sizeof(pi::fe::local::TableHeaders))/sizeof(pi::fe::local::Update);
}
#endif // HAVE_SHM

namespace proto {

// forward declaration for PIMPL class
class DeviceMgrImp;

// the gRPC server will instantiate one DeviceMgr object per device
class DeviceMgr {
 public:
  using device_id_t = uint64_t;
  using Status = ::google::rpc::Status;
  using StreamMessageResponseCb = std::function<void(
      device_id_t, p4::v1::StreamMessageResponse *msg, void *cookie)>;

  explicit DeviceMgr(device_id_t device_id);

  ~DeviceMgr();

  // New pipeline_config_set and pipeline_config_get methods to replace init,
  // update_start and update_end
  Status pipeline_config_set(
      p4::v1::SetForwardingPipelineConfigRequest::Action action,
      const p4::v1::ForwardingPipelineConfig &config);

  Status pipeline_config_get(
      p4::v1::GetForwardingPipelineConfigRequest::ResponseType response_type,
      p4::v1::ForwardingPipelineConfig *config);

  Status write(const p4::v1::WriteRequest &request);
#ifdef HAVE_SHM
  Status writeLocal();
#endif

  Status read(const p4::v1::ReadRequest &request,
              p4::v1::ReadResponse *response) const;
  Status read_one(const p4::v1::Entity &entity,
                  p4::v1::ReadResponse *response) const;

  Status stream_message_request_handle(
      const p4::v1::StreamMessageRequest &request);

  void stream_message_response_register_cb(StreamMessageResponseCb cb,
                                           void *cookie);

  Status server_config_set(const p4::server::v1::Config &config);

  Status server_config_get(p4::server::v1::Config *config);

  _PI_DEPRECATED
  static void init(size_t max_devices);

  static Status init();

  static Status init(const p4::server::v1::Config &config);

  static Status init(const std::string &config_text,
                     const std::string &version = "v1");

  static void destroy();

 private:
  // PIMPL design
  std::unique_ptr<DeviceMgrImp> pimp;
};

}  // namespace proto

}  // namespace fe

}  // namespace pi

#if __has_cpp_attribute(deprecated)
#if defined(__clang__)
#pragma clang diagnostic pop
#endif  // clang
#endif  // deprecated

#undef _PI_DEPRECATED

#endif  // PI_FRONTENDS_PROTO_DEVICE_MGR_H_
