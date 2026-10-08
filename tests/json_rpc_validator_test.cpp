#include "mcp/json_rpc_validator.h"

#include <gtest/gtest.h>

#include <vector>

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

TEST(JsonRpcValidatorTest, ExercisesSupportedJsonValueGrammar) {
  std::string method;
  EXPECT_TRUE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"method":"tools.call-1","params":{"text":"quote\" slash\\ tab\t","values":[true,false,null,-12.5e+2,{},[]]},"id":"request-1","jsonrpc":"2.0"})",
      &method));
  EXPECT_EQ(method, "tools.call-1");
  EXPECT_TRUE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":0,"method":"ping","params":[]})",
      &method));
}

TEST(JsonRpcValidatorTest, RejectsInvalidTokensAndRequiredFieldShapes) {
  const std::vector<std::string> invalid{
      R"({"jsonrpc":"2.0","id":"","method":"ping"})",
      R"({"jsonrpc":"2.0","id":1,"method":""})",
      R"({"jsonrpc":"2.0","id":1,"method":"bad method"})",
      R"({"jsonrpc":"2.0","id":1,"method":"ping",})",
      R"({"jsonrpc":"2.0","id":01,"method":"ping"})",
      R"({"jsonrpc":"2.0","id":1.,"method":"ping"})",
      R"({"jsonrpc":"2.0","id":1e,"method":"ping"})",
      R"({"jsonrpc":"2.0","id":1,"method":"ping","params":[1,]})",
      R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"x":}})",
      R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"x":"bad\q"}})",
      R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{"x":tru}})",
      R"({"jsonrpc":"2.0","id":1})",
      R"({"jsonrpc":"2.0","method":"ping"})",
      R"({"id":1,"method":"ping"})",
      R"([])"
  };
  std::string method;
  for (const auto& json : invalid) {
    EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
        json, &method)) << json;
  }

  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx","method":"ping"})",
      &method));
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":1,"method":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"})",
      &method));
  EXPECT_FALSE(cx::mcp::JsonRpcValidator::ValidateRequest(
      R"({"jsonrpc":"2.0","id":1,"method":"ping"})",
      nullptr));
}
