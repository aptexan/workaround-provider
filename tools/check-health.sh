#!/bin/bash
# Is the provider's gRPC health service answering? go-plugin's "are you alive?" ping asks it about the name "plugin".
# Also: an ordinary call works, and a call to a method that does not exist is refused cleanly. No OpenTofu needed.
. "$(dirname "$0")/lib.sh"
start_provider

# Requests, written out byte by byte:  00 | 4-byte length | protobuf message.
# HealthCheckRequest{service="plugin"} is  0a 06 'plugin'  (field 1, 6 bytes), so its length is 8.
ASK_PLUGIN='\x00\x00\x00\x00\x08\x0a\x06plugin'
ASK_EMPTY='\x00\x00\x00\x00\x00'                                  # service "" = the server as a whole
ASK_UNKNOWN='\x00\x00\x00\x00\x11\x0a\x0fno-such-service'         # 15-character name
NO_FIELDS='\x00\x00\x00\x00\x00'                                  # a message with no fields
SERVING='00 00 00 00 02 08 01'                                    # reply: length 2, status field = 1 (SERVING)

expect "health: 'plugin' succeeds"        "$(printf "$ASK_PLUGIN" | grpc_call /grpc.health.v1.Health/Check)" 0
expect "health: 'plugin' is SERVING"      "$(reply_hex)" "$SERVING"
expect "health: '' succeeds"              "$(printf "$ASK_EMPTY" | grpc_call /grpc.health.v1.Health/Check)" 0
expect "health: '' is SERVING"            "$(reply_hex)" "$SERVING"
expect "health: unknown name is NOT_FOUND (5)" "$(printf "$ASK_UNKNOWN" | grpc_call /grpc.health.v1.Health/Check)" 5
expect "GetSchema succeeds"               "$(printf "$NO_FIELDS" | grpc_call /tfplugin5.Provider/GetSchema)" 0
expect "unknown method is UNIMPLEMENTED (12)" "$(printf "$NO_FIELDS" | grpc_call /tfplugin5.Provider/NoSuchMethod)" 12
summary
