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

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <condition_variable>
#include <memory>
#include <optional>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

#include "grpc_broker.grpc.pb.h"
#include "grpc_controller.grpc.pb.h"
#include "grpc_stdio.grpc.pb.h"
#include "cty.h"
#include "schema.h"
#include "server-cert.h"
#include "workaround-example.h"
#include "tfplugin5.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

ABSL_FLAG(uint16_t, port, 0,
          "Server port for the service (0 = pick a free port, or one inside "
          "PLUGIN_MIN_PORT..PLUGIN_MAX_PORT when Core sets them)");
ABSL_FLAG(bool, debug, false,
          "Debug mode: do not require the go-plugin magic cookie, so the binary "
          "can be started by hand or under a debugger");

// go-plugin's magic cookie: a fixed, public marker that tells the plugin it was
// started by a host program and not directly by a person.
constexpr char kMagicCookieKey[] = "TF_PLUGIN_MAGIC_COOKIE";
constexpr char kMagicCookieValue[] =
    "d602bf8f470bc67ca7faa0386276bbdd4330efaf76d1a219cb4d6991ca9872b2";

// Provider protocol major versions this binary serves (package tfplugin5).
constexpr int kSupportedProtocolVersions[] = {5};

// The OpenTofu provider protocol (tfplugin5). The rules (what is valid, how a change is planned and applied) are in
// workaround-example.cpp, and the value encoding is in cty.cpp; this class translates between them and the protobuf
// messages. A problem with a request is answered with an error diagnostic, which Core shows to the user, and not with a
// gRPC error, which would look like a crash.
class ProviderServiceImpl final : public tfplugin5::Provider::Service {
  using Diagnostics = google::protobuf::RepeatedPtrField<tfplugin5::Diagnostic>;

  // --- diagnostics ---------------------------------------------------------------------------------------------------

  static void SetPath(const workaround::AttributePath& path, tfplugin5::AttributePath* out) {
    for (const workaround::PathStep& step : path) {
      tfplugin5::AttributePath::Step* proto = out->add_steps();
      switch (step.kind) {
        case workaround::PathStep::Kind::kAttribute: proto->set_attribute_name(step.name); break;
        case workaround::PathStep::Kind::kKey: proto->set_element_key_string(step.name); break;
        case workaround::PathStep::Kind::kIndex: proto->set_element_key_int(step.index); break;
      }
    }
  }

  static void AddDiagnostics(const std::vector<workaround::Diagnostic>& diagnostics, Diagnostics* out) {
    for (const workaround::Diagnostic& diagnostic : diagnostics) {
      tfplugin5::Diagnostic* proto = out->Add();
      proto->set_severity(diagnostic.severity == workaround::Diagnostic::Severity::kError
                              ? tfplugin5::Diagnostic::ERROR
                              : tfplugin5::Diagnostic::WARNING);
      proto->set_summary(diagnostic.summary);
      proto->set_detail(diagnostic.detail);
      if (!diagnostic.attribute.empty()) SetPath(diagnostic.attribute, proto->mutable_attribute());
    }
  }

  static bool HasError(const std::vector<workaround::Diagnostic>& diagnostics) {
    for (const workaround::Diagnostic& diagnostic : diagnostics) {
      if (diagnostic.severity == workaround::Diagnostic::Severity::kError) return true;
    }
    return false;
  }

  static void AddError(Diagnostics* out, std::string summary, std::string detail) {
    AddDiagnostics({workaround::Diagnostic{workaround::Diagnostic::Severity::kError, std::move(summary),
                                           std::move(detail), {}}},
                   out);
  }

  // --- values --------------------------------------------------------------------------------------------------------

  // An empty DynamicValue is null. On a value that does not fit the type, adds an error and returns nothing.
  static std::optional<cty::Value> Decode(const tfplugin5::DynamicValue& dynamic, const cty::Type& type,
                                          const char* what, Diagnostics* out) {
    if (dynamic.msgpack().empty() && dynamic.json().empty()) return cty::Value::Null();
    absl::StatusOr<cty::Value> value =
        dynamic.msgpack().empty() ? cty::DecodeJson(dynamic.json(), type) : cty::DecodeMsgpack(dynamic.msgpack(), type);
    if (!value.ok()) {
      AddError(out, absl::StrCat("Cannot read ", what), std::string(value.status().message()));
      return std::nullopt;
    }
    return *std::move(value);
  }

  static bool Encode(const cty::Value& value, const cty::Type& type, const char* what,
                     tfplugin5::DynamicValue* dynamic, Diagnostics* out) {
    absl::StatusOr<std::string> bytes = cty::EncodeMsgpack(value, type);
    if (!bytes.ok()) {
      AddError(out, absl::StrCat("Cannot write ", what), std::string(bytes.status().message()));
      return false;
    }
    dynamic->set_msgpack(*std::move(bytes));
    return true;
  }

  static tfplugin5::Schema ToProto(const schema::Block& block) {
    tfplugin5::Schema proto;
    proto.set_version(block.version);
    tfplugin5::Schema::Block* proto_block = proto.mutable_block();
    proto_block->set_description(block.description);
    for (const schema::Attribute& attribute : block.attributes) {
      tfplugin5::Schema::Attribute* proto_attribute = proto_block->add_attributes();
      proto_attribute->set_name(attribute.name);
      proto_attribute->set_type(attribute.type->ToJson());
      proto_attribute->set_description(attribute.description);
      proto_attribute->set_required(attribute.required);
      proto_attribute->set_optional(attribute.optional);
      proto_attribute->set_computed(attribute.computed);
      proto_attribute->set_sensitive(attribute.sensitive);
    }
    return proto;
  }

  // The only resource type there is. Anything else is the caller's mistake.
  static const schema::Block* ResourceSchema(const std::string& type_name, Diagnostics* out) {
    if (type_name == workaround::kExampleType) return &workaround::ExampleSchema();
    AddError(out, "Unknown resource type", absl::StrCat("This provider has no resource type \"", type_name, "\"."));
    return nullptr;
  }

  // A short random text for new ids, e.g. "3fa9c01b".
  static std::string RandomSuffix() {
    std::random_device random;
    return absl::StrFormat("%08x", random());
  }

 public:
  Status GetSchema(ServerContext*, const tfplugin5::GetProviderSchema::Request*,
                   tfplugin5::GetProviderSchema::Response* response) override {
    *response->mutable_provider() = ToProto(workaround::ProviderSchema());
    (*response->mutable_resource_schemas())[workaround::kExampleType] = ToProto(workaround::ExampleSchema());
    return Status::OK;
  }

  // In protocol 5 this is "validate the provider block". Core uses the prepared config as the config from then on.
  Status PrepareProviderConfig(
      ServerContext*, const tfplugin5::PrepareProviderConfig::Request* request,
      tfplugin5::PrepareProviderConfig::Response* response) override {
    cty::TypePtr type = workaround::ProviderSchema().ImpliedType();
    std::optional<cty::Value> config = Decode(request->config(), *type, "the provider configuration",
                                              response->mutable_diagnostics());
    if (!config) return Status::OK;
    AddDiagnostics(workaround::ValidateProviderConfig(*config), response->mutable_diagnostics());
    *response->mutable_prepared_config() = request->config();
    return Status::OK;
  }

  Status ValidateResourceTypeConfig(
      ServerContext*, const tfplugin5::ValidateResourceTypeConfig::Request* request,
      tfplugin5::ValidateResourceTypeConfig::Response* response) override {
    Diagnostics* diagnostics = response->mutable_diagnostics();
    const schema::Block* block = ResourceSchema(request->type_name(), diagnostics);
    if (block == nullptr) return Status::OK;
    std::optional<cty::Value> config = Decode(request->config(), *block->ImpliedType(), "the configuration", diagnostics);
    if (config) AddDiagnostics(workaround::ValidateExample(*config), diagnostics);
    return Status::OK;
  }

  Status ValidateDataSourceConfig(
      ServerContext*, const tfplugin5::ValidateDataSourceConfig::Request* request,
      tfplugin5::ValidateDataSourceConfig::Response* response) override {
    AddError(response->mutable_diagnostics(), "Unknown data source type",
             absl::StrCat("This provider has no data source type \"", request->type_name(), "\"."));
    return Status::OK;
  }

  Status Configure(ServerContext*, const tfplugin5::Configure::Request* request,
                   tfplugin5::Configure::Response* response) override {
    Diagnostics* diagnostics = response->mutable_diagnostics();
    cty::TypePtr type = workaround::ProviderSchema().ImpliedType();
    std::optional<cty::Value> config = Decode(request->config(), *type, "the provider configuration", diagnostics);
    if (!config) return Status::OK;
    std::vector<workaround::Diagnostic> problems = workaround::ValidateProviderConfig(*config);
    AddDiagnostics(problems, diagnostics);
    if (HasError(problems)) return Status::OK;
    std::lock_guard<std::mutex> lock(settings_mutex_);
    settings_ = workaround::SettingsFrom(*config);
    return Status::OK;
  }

  // Core hands back the state it stored, as JSON, together with the schema version it was written with.
  Status UpgradeResourceState(
      ServerContext*, const tfplugin5::UpgradeResourceState::Request* request,
      tfplugin5::UpgradeResourceState::Response* response) override {
    Diagnostics* diagnostics = response->mutable_diagnostics();
    const schema::Block* block = ResourceSchema(request->type_name(), diagnostics);
    if (block == nullptr) return Status::OK;
    if (request->version() != block->version) {
      AddError(diagnostics, "Unsupported state version",
               absl::StrCat("The stored state has schema version ", request->version(),
                            "; this provider can only read version ", block->version, "."));
      return Status::OK;
    }
    if (request->raw_state().json().empty()) {
      AddError(diagnostics, "Unsupported state format", "The stored state is not JSON (a legacy flatmap state).");
      return Status::OK;
    }
    cty::TypePtr type = block->ImpliedType();
    absl::StatusOr<cty::Value> state = cty::DecodeJson(request->raw_state().json(), *type);
    if (!state.ok()) {
      AddError(diagnostics, "Cannot read the stored state", std::string(state.status().message()));
      return Status::OK;
    }
    Encode(*state, *type, "the upgraded state", response->mutable_upgraded_state(), diagnostics);
    return Status::OK;
  }

  // Everything lives in the state; there is no outside system that could have drifted. So the refreshed state is
  // the state we were given.
  Status ReadResource(ServerContext*, const tfplugin5::ReadResource::Request* request,
                      tfplugin5::ReadResource::Response* response) override {
    Diagnostics* diagnostics = response->mutable_diagnostics();
    const schema::Block* block = ResourceSchema(request->type_name(), diagnostics);
    if (block == nullptr) return Status::OK;
    cty::TypePtr type = block->ImpliedType();
    std::optional<cty::Value> state = Decode(request->current_state(), *type, "the current state", diagnostics);
    if (state) Encode(*state, *type, "the new state", response->mutable_new_state(), diagnostics);
    response->set_private_(request->private_());
    return Status::OK;
  }

  Status PlanResourceChange(
      ServerContext*, const tfplugin5::PlanResourceChange::Request* request,
      tfplugin5::PlanResourceChange::Response* response) override {
    Diagnostics* diagnostics = response->mutable_diagnostics();
    const schema::Block* block = ResourceSchema(request->type_name(), diagnostics);
    if (block == nullptr) return Status::OK;
    cty::TypePtr type = block->ImpliedType();
    std::optional<cty::Value> prior = Decode(request->prior_state(), *type, "the prior state", diagnostics);
    std::optional<cty::Value> proposed =
        Decode(request->proposed_new_state(), *type, "the proposed new state", diagnostics);
    if (!prior || !proposed) return Status::OK;

    workaround::PlanResult plan = workaround::PlanExample(*prior, *proposed);
    AddDiagnostics(plan.diagnostics, diagnostics);
    if (!Encode(plan.planned, *type, "the planned state", response->mutable_planned_state(), diagnostics)) {
      return Status::OK;
    }
    for (const workaround::AttributePath& path : plan.requires_replace) {
      SetPath(path, response->add_requires_replace());
    }
    response->set_planned_private(request->prior_private());
    return Status::OK;
  }

  Status ApplyResourceChange(
      ServerContext*, const tfplugin5::ApplyResourceChange::Request* request,
      tfplugin5::ApplyResourceChange::Response* response) override {
    Diagnostics* diagnostics = response->mutable_diagnostics();
    const schema::Block* block = ResourceSchema(request->type_name(), diagnostics);
    if (block == nullptr) return Status::OK;
    cty::TypePtr type = block->ImpliedType();
    std::optional<cty::Value> prior = Decode(request->prior_state(), *type, "the prior state", diagnostics);
    std::optional<cty::Value> planned = Decode(request->planned_state(), *type, "the planned state", diagnostics);
    if (!prior || !planned) return Status::OK;

    workaround::ProviderSettings settings;
    {
      std::lock_guard<std::mutex> lock(settings_mutex_);
      settings = settings_;
    }
    std::vector<workaround::Diagnostic> problems;
    cty::Value applied = workaround::ApplyExample(*prior, *planned, settings, RandomSuffix(), &problems);
    AddDiagnostics(problems, diagnostics);
    if (HasError(problems)) return Status::OK;
    Encode(applied, *type, "the new state", response->mutable_new_state(), diagnostics);
    response->set_private_(request->planned_private());
    return Status::OK;
  }

  Status Stop(ServerContext*, const tfplugin5::Stop::Request*, tfplugin5::Stop::Response*) override {
    return Status::OK;
  }

 private:
  std::mutex settings_mutex_;
  workaround::ProviderSettings settings_;  // set by Configure; read by Apply (RPCs can run on several threads)
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

// Everything Core tells us through the environment before the handshake.
struct StartupConfig {
  int protocol_version = 0;  // the major version to advertise
  uint16_t min_port = 0;     // both 0: no range was requested
  uint16_t max_port = 0;
  std::string client_cert_pem;  // Core's certificate; empty means plaintext (no mutual TLS)
};

// Reads an optional port variable. Returns false (after a message) if it is set
// but is not a port number.
static bool ReadPortVariable(const char* name, uint16_t* port, bool* is_set) {
  const char* text = std::getenv(name);
  *is_set = text != nullptr && *text != '\0';
  if (!*is_set) return true;
  uint32_t value = 0;
  if (!absl::SimpleAtoi(text, &value) || value == 0 || value > 65535) {
    std::cerr << name << "=\"" << text << "\" is not a valid port number" << std::endl;
    return false;
  }
  *port = static_cast<uint16_t>(value);
  return true;
}

// Checks the environment the way go-plugin's server does. On failure it prints
// why to stderr and returns nothing, so main() can exit with status 1 before any
// handshake line is written.
std::optional<StartupConfig> CheckStartup(bool debug) {
  StartupConfig config;

  // 1. Magic cookie.
  const char* cookie = std::getenv(kMagicCookieKey);
  if (!debug && (cookie == nullptr || std::string(cookie) != kMagicCookieValue)) {
    std::cerr << "This binary is a plugin. These are not meant to be executed "
                 "directly.\nPlease execute the program that consumes these "
                 "plugins, which will load any plugins automatically.\n"
                 "(To run it by hand, pass --debug.)"
              << std::endl;
    return std::nullopt;
  }

  // 2. Protocol version: the highest one that both sides support. The order of
  // Core's list does not matter, and entries that are not numbers are skipped.
  const char* offered = std::getenv("PLUGIN_PROTOCOL_VERSIONS");
  std::vector<int> core_versions;
  if (offered != nullptr) {
    for (std::string_view entry : absl::StrSplit(offered, ',')) {
      int version = 0;
      if (absl::SimpleAtoi(entry, &version)) core_versions.push_back(version);
    }
  }
  for (int ours : kSupportedProtocolVersions) {
    if (std::find(core_versions.begin(), core_versions.end(), ours) != core_versions.end()) {
      config.protocol_version = std::max(config.protocol_version, ours);
    }
  }
  if (config.protocol_version == 0) {
    if (debug && core_versions.empty()) {
      // Started by hand: there is no Core to negotiate with.
      config.protocol_version = *std::max_element(std::begin(kSupportedProtocolVersions),
                                                  std::end(kSupportedProtocolVersions));
    } else {
      std::cerr << "No protocol version in common. Core offered PLUGIN_PROTOCOL_VERSIONS=\""
                << (offered ? offered : "") << "\"; this provider serves version 5." << std::endl;
      return std::nullopt;
    }
  }

  // 3. Mutual TLS. Core hands us its own certificate (public part only) and expects us to answer only clients that
  // present it. Without one Core runs in plaintext mode (TF_DISABLE_PLUGIN_TLS), and so do we.
  const char* client_cert = std::getenv("PLUGIN_CLIENT_CERT");
  if (client_cert != nullptr && *client_cert != '\0') {
    config.client_cert_pem = client_cert;
    if (!IsPemCertificate(config.client_cert_pem)) {
      std::cerr << "PLUGIN_CLIENT_CERT is set but does not hold a PEM certificate" << std::endl;
      return std::nullopt;
    }
  }

  // 4. Port range. Core may restrict which TCP ports a plugin may listen on.
  bool has_min = false, has_max = false;
  if (!ReadPortVariable("PLUGIN_MIN_PORT", &config.min_port, &has_min) ||
      !ReadPortVariable("PLUGIN_MAX_PORT", &config.max_port, &has_max)) {
    return std::nullopt;
  }
  if (has_min || has_max) {
    if (!has_min) config.min_port = 1;
    if (!has_max) config.max_port = 65535;
    if (config.min_port > config.max_port) {
      std::cerr << "PLUGIN_MIN_PORT (" << config.min_port << ") is greater than PLUGIN_MAX_PORT ("
                << config.max_port << ")" << std::endl;
      return std::nullopt;
    }
  }
  return config;
}

// Returns 0 on a clean exit, 1 if the server could not be started.
int RunServer(uint16_t port, const StartupConfig& config) {
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
  // Transport security, chosen like go-plugin does: by whether Core sent its certificate.
  //  - PLUGIN_CLIENT_CERT set: mutual TLS. We make a fresh certificate and key for this process, present it, and
  //    answer only clients that present Core's certificate (required AND verified against it).
  //  - not set (Core runs with TF_DISABLE_PLUGIN_TLS, or --debug by hand): plaintext.
  std::shared_ptr<grpc::ServerCredentials> credentials;
  std::string handshake_certificate;  // field 6 of the handshake line; empty for plaintext
  if (config.client_cert_pem.empty()) {
    credentials = grpc::InsecureServerCredentials();
  } else {
    std::string error;
    std::optional<ServerCertificate> certificate = GenerateServerCertificate(&error);
    if (!certificate) {
      std::cerr << error << std::endl;
      return 1;
    }
    grpc::SslServerCredentialsOptions options(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    options.pem_root_certs = config.client_cert_pem;  // the only certificate a client may have
    options.pem_key_cert_pairs.push_back({certificate->key_pem, certificate->cert_pem});
    credentials = grpc::SslServerCredentials(options);
    handshake_certificate = certificate->handshake_field;
  }
  // Loopback only. A server cannot be re-bound once started, so each attempt builds a fresh one.
  auto try_start = [&](int candidate) {
    std::string address = absl::StrFormat("127.0.0.1:%d", candidate);
    selected_port = 0;
    ServerBuilder builder;
    builder.AddListeningPort(address, credentials, &selected_port);
    // Register the provider service through which Core communicates with us.
    builder.RegisterService(&provider_service);
    builder.RegisterService(&controller_service);
    builder.RegisterService(&stdio_service);
    builder.RegisterService(&broker_service);
    // Finally assemble the server.
    server = builder.BuildAndStart();
    return server && selected_port != 0;
  };
  bool started = false;
  if (port != 0 || config.max_port == 0) {
    // An explicit --port, or no range from Core: port 0 lets gRPC pick a free one.
    started = try_start(port);
  } else {
    // Try every port of the range once, beginning at a random one so that
    // several providers started together do not all fight for the lowest port.
    const int count = config.max_port - config.min_port + 1;
    std::random_device random;
    const int first = std::uniform_int_distribution<int>(0, count - 1)(random);
    for (int i = 0; i < count && !started; ++i) {
      started = try_start(config.min_port + (first + i) % count);
    }
  }
  if (!started) {
    if (port == 0 && config.max_port != 0) {
      std::cerr << "No free port in PLUGIN_MIN_PORT..PLUGIN_MAX_PORT (" << config.min_port
                << ".." << config.max_port << ")" << std::endl;
    } else {
      std::cerr << "Failed to start server on 127.0.0.1:" << port << std::endl;
    }
    return 1;
  }
  std::cerr << "Server listening on 127.0.0.1:" << selected_port << " (pid " << getpid() << ", "
            << (handshake_certificate.empty() ? "plaintext" : "mutual TLS") << ")" << std::endl;
  // go-plugin's ping asks the standard gRPC health service about the service
  // name "plugin". The default health service only knows names it was told
  // about, so without this Core's ping gets NOT_FOUND.
  if (auto* health = server->GetHealthCheckService()) {
    health->SetServingStatus("plugin", true);
  }
  // Handshake: only after the server is listening, since Core connects as soon
  // as it reads this line. The line must be the first thing written to stdout.
  std::cout << absl::StrFormat("1|%d|tcp|127.0.0.1:%d|grpc|%s\n", config.protocol_version,
                               selected_port, handshake_certificate)
            << std::flush;
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
  // Use std::jthread for resiliency and remove join()
  if (shutdown_thread.joinable()) shutdown_thread.join();
  return 0;
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // Check the environment first: a rejected start must not print a handshake.
  std::optional<StartupConfig> config = CheckStartup(absl::GetFlag(FLAGS_debug));
  if (!config) return 1;
  return RunServer(absl::GetFlag(FLAGS_port), *config);
}
