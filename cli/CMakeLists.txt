include(${CMAKE_SOURCE_DIR}/cmake/ProtobufGRPCGenerateCpp.cmake)

add_executable(gs++-cli
    ${CMAKE_CURRENT_SOURCE_DIR}/gs++-cli.cpp
    ${CMAKE_SOURCE_DIR}/src/util.cpp
    ${CMAKE_SOURCE_DIR}/src/transaction.cpp
    ${CMAKE_SOURCE_DIR}/src/slp_transaction.cpp
    ${CMAKE_SOURCE_DIR}/src/sha2.cpp
    ${CMAKE_SOURCE_DIR}/src/slp_validator.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(gs++-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
    ${PROTO_SRC_DIR}
)

target_link_libraries(gs++-cli
    gRPC::grpc++_reflection
    protobuf::libprotobuf
    absl::variant
    absl::flat_hash_map
    absl::node_hash_map
    base64
)

target_compile_options(gs++-cli PRIVATE -Wall)
