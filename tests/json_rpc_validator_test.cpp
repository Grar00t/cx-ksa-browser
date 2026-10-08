#include "mcp/json_rpc_validator.h"

#include <gtest/gtest.h>

TEST(JsonRpcValidatorTest, AcceptsStrictRequestShapes) {
  std::string method;
  EXPECT_TRUE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"read"}})",
      &method));
  EXPECT_EQ(method, "tools/call");
}

TEST(JsonRpcValidatorTest, RejectsMalformedAndUnknownFields) {
  std::string method;
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      "not-json", &method));
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":1,"method":"ping","extra":true})",
      &method));
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"1.0","id":1,"method":"ping"})",
      &method));
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":1,"method":"ping","method":"again"})",
      &method));
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":null,"method":"ping"})",
      &method));
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":-true,"method":"ping"})",
      &method));
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":1,"method":"ping","params":true})",
      &method));
}

TEST(JsonRpcValidatorTest, RejectsExcessiveNesting) {
  std::string json =
      R"({"jsonrpc":"2.0","id":1,"method":"ping","params":)";
  json.append(18, '[');
  json += "0";
  json.append(18, ']');
  json += "}";
  std::string method;
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      json, &method));
}
