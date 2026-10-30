/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <chrono>
#include <functional>
#include <iostream>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/strings/str_format.h"

#include "grpc_broker.grpc.pb.h"
#include "grpc_controller.grpc.pb.h"
#include "grpc_stdio.grpc.pb.h"
#include "tfplugin5.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

ABSL_FLAG(uint16_t, port, 0, "Server port for the service (0 = pick a free-port)");

// Minimal implementation of the OpenTofu provider protocol (tfplugin5).
// It exposes one resource type, "workaround_example", which has no attributes.
// RPCs that are not overridden here return UNIMPLEMENTED.
class ProviderServiceImpl final : public tfplugin5::Provider::Service {
  // msgpack encoding of an empty object (a fixmap with zero entries).
  static constexpr char kEmptyObject[] = "\x80";

  static void SetEmptyObject(tfplugin5::DynamicValue* value) {
    value->set_msgpack(kEmptyObject, 1);
  }

  Status GetSchema(ServerContext*, const tfplugin5::GetProviderSchema::Request*,
                   tfplugin5::GetProviderSchema::Response* response) override {
    // Provider block and the resource both have an empty block (no attributes).
    response->mutable_provider()->mutable_block();
    (*response->mutable_resource_schemas())["workaround_example"].mutable_block();
    return Status::OK;
  }

  Status PrepareProviderConfig(
      ServerContext*, const tfplugin5::PrepareProviderConfig::Request* request,
      tfplugin5::PrepareProviderConfig::Response* response) override {
    *response->mutable_prepared_config() = request->config();
    return Status::OK;
  }

  Status ValidateResourceTypeConfig(
      ServerContext*, const tfplugin5::ValidateResourceTypeConfig::Request*,
      tfplugin5::ValidateResourceTypeConfig::Response*) override {
    return Status::OK;
  }

  Status ValidateDataSourceConfig(
      ServerContext*, const tfplugin5::ValidateDataSourceConfig::Request*,
      tfplugin5::ValidateDataSourceConfig::Response*) override {
    return Status::OK;
  }

  Status Configure(ServerContext*, const tfplugin5::Configure::Request*,
                   tfplugin5::Configure::Response*) override {
    return Status::OK;
  }

  Status UpgradeResourceState(
      ServerContext*, const tfplugin5::UpgradeResourceState::Request*,
      tfplugin5::UpgradeResourceState::Response* response) override {
    SetEmptyObject(response->mutable_upgraded_state());
    return Status::OK;
  }

  Status ReadResource(ServerContext*,
                      const tfplugin5::ReadResource::Request* request,
                      tfplugin5::ReadResource::Response* response) override {
    *response->mutable_new_state() = request->current_state();
    return Status::OK;
  }

  Status PlanResourceChange(
      ServerContext*, const tfplugin5::PlanResourceChange::Request* request,
      tfplugin5::PlanResourceChange::Response* response) override {
    *response->mutable_planned_state() = request->proposed_new_state();
    return Status::OK;
  }

  Status ApplyResourceChange(
      ServerContext*, const tfplugin5::ApplyResourceChange::Request* request,
      tfplugin5::ApplyResourceChange::Response* response) override {
    // Echo the planned state: an empty object on create, null on destroy.
    *response->mutable_new_state() = request->planned_state();
    return Status::OK;
  }

  Status Stop(ServerContext*, const tfplugin5::Stop::Request*,
              tfplugin5::Stop::Response*) override {
    return Status::OK;
  }
};

// go-plugin's GRPCController: Core calls Shutdown when it is done with the
// plugin. Without it Core waits ~2 s and then kills the process.
class ControllerServiceImpl final : public plugin::GRPCController::Service {
 public:
  explicit ControllerServiceImpl(std::function<void()> on_shutdown)
      : on_shutdown_(std::move(on_shutdown)) {}

  Status Shutdown(ServerContext*, const plugin::Empty*, plugin::Empty*) override {
    on_shutdown_();
    return Status::OK;
  }

 private:
  std::function<void()> on_shutdown_;
};

// Keeps track of the long-lived streams (stdio, broker) so that they can be
// cancelled on shutdown. Server::Shutdown() would otherwise wait for them until
// its deadline, and Core only allows ~2 s for the process to exit.
class StreamTracker {
 public:
  // Registers a running stream. Returns false if shutdown has already begun.
  bool Add(ServerContext* context) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return false;
    streams_.insert(context);
    return true;
  }

  void Remove(ServerContext* context) {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.erase(context);
  }

  // Blocks until shutdown begins or the client cancels the stream.
  void WaitUntilDone(ServerContext* context) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_ && !context->IsCancelled()) {
      stopped_.wait_for(lock, std::chrono::milliseconds(100));
    }
  }

  void CancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    for (ServerContext* context : streams_) context->TryCancel();
    stopped_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable stopped_;
  std::set<ServerContext*> streams_;
  bool stopping_ = false;
};

// go-plugin's GRPCStdio: Core opens this stream right after connecting and
// mirrors whatever arrives into its own log. We keep logging on the original
// stderr (which Core already captures), so the stream stays open and idle.
class StdioServiceImpl final : public plugin::GRPCStdio::Service {
 public:
  explicit StdioServiceImpl(StreamTracker& tracker) : tracker_(tracker) {}

  Status StreamStdio(ServerContext* context, const google::protobuf::Empty*,
                     grpc::ServerWriter<plugin::StdioData>*) override {
    if (!tracker_.Add(context)) return Status::OK;
    tracker_.WaitUntilDone(context);
    tracker_.Remove(context);
    return Status::OK;
  }

 private:
  StreamTracker& tracker_;
};

// go-plugin's GRPCBroker: used to negotiate extra connections between the two
// sides. The provider never needs one, so we accept the stream, ignore what
// Core sends and end when Core closes it or when we shut down.
class BrokerServiceImpl final : public plugin::GRPCBroker::Service {
 public:
  explicit BrokerServiceImpl(StreamTracker& tracker) : tracker_(tracker) {}

  Status StartStream(
      ServerContext* context,
      grpc::ServerReaderWriter<plugin::ConnInfo, plugin::ConnInfo>* stream) override {
    if (!tracker_.Add(context)) return Status::OK;
    plugin::ConnInfo ignored;
    while (stream->Read(&ignored)) {
    }
    tracker_.Remove(context);
    return Status::OK;
  }

 private:
  StreamTracker& tracker_;
};

void RunServer(uint16_t port) {
  // Loopback only; with port 0 gRPC picks a free port and reports it back.
  std::string server_address = absl::StrFormat("127.0.0.1:%d", port);
  int selected_port = 0;
  ProviderServiceImpl provider_service;
  std::unique_ptr<Server> server;
  // Server::Shutdown() waits for running RPCs, including the Shutdown RPC that
  // triggers it, so it must run on another thread rather than in the handler.
  std::once_flag shutdown_once;
  std::thread shutdown_thread;
  StreamTracker streams;
  StdioServiceImpl stdio_service(streams);
  BrokerServiceImpl broker_service(streams);
  ControllerServiceImpl controller_service([&] {
    std::call_once(shutdown_once, [&] {
      streams.CancelAll();
      shutdown_thread = std::thread([&] {
        server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
      });
    });
  });

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  // Core must run with TF_DISABLE_PLUGIN_TLS=1 until mutual TLS is implemented.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(),
                           &selected_port);
  // Register the provider service through which Core communicates with us.
  builder.RegisterService(&provider_service);
  builder.RegisterService(&controller_service);
  builder.RegisterService(&stdio_service);
  builder.RegisterService(&broker_service);
  // Finally assemble the server.
  server = builder.BuildAndStart();
  if (!server || selected_port == 0) {
    std::cerr << "Failed to start server on " << server_address << std::endl;
    return;
  }
  std::cerr << "Server listening on 127.0.0.1:" << selected_port << std::endl;
  // go-plugin's ping asks the standard gRPC health service about the service
  // name "plugin". The default health service only knows names it was told
  // about, so without this Core's ping gets NOT_FOUND.
  if (auto* health = server->GetHealthCheckService()) {
    health->SetServingStatus("plugin", true);
  }
  // Handshake: only after the server is listening, since Core connects as soon
  // as it reads this line. The line must be the first thing written to stdout.
  std::cout << absl::StrFormat("1|5|tcp|127.0.0.1:%d|grpc|\n", selected_port)
            << std::flush;
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
  // Use std::jthread for resiliency and remove join()
  if (shutdown_thread.joinable()) shutdown_thread.join();
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  RunServer(absl::GetFlag(FLAGS_port));
  return 0;
}
