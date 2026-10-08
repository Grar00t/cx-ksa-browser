#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#ifndef CX_SOURCE_DIR
#error CX_SOURCE_DIR must identify the checked-out repository root
#endif

namespace {

std::string ReadRepositoryFile(std::string_view relative_path) {
  const auto path =
      std::filesystem::path(CX_SOURCE_DIR) /
      std::filesystem::path(relative_path);
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    ADD_FAILURE() << "Cannot read source contract file: "
                  << path.string();
    return {};
  }
  return std::string(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

void ExpectToken(
    const std::string& source,
    std::string_view token,
    std::string_view control) {
  EXPECT_NE(source.find(token), std::string::npos)
      << "Missing hardening control: " << control
      << " (expected token: " << token << ")";
}

}  // namespace

TEST(HardeningAudit, WebViewSettingsAreExplicitAndFailClosed) {
  const auto source = ReadRepositoryFile("src/app_window.cpp");
  ExpectToken(source, "put_IsStatusBarEnabled", "disable status bar");
  ExpectToken(source, "put_IsPasswordAutosaveEnabled", "disable password autosave");
  ExpectToken(source, "put_IsGeneralAutofillEnabled", "disable general autofill");
  ExpectToken(source, "put_AreDevToolsEnabled", "gate DevTools");
  ExpectToken(source, "put_AreDefaultContextMenusEnabled", "gate context menus");
  ExpectToken(source, "put_IsReputationCheckingRequired", "explicit SmartScreen opt-in");
}

TEST(HardeningAudit, SensitiveWebViewEventsHaveDenyByDefaultHandlers) {
  const auto source = ReadRepositoryFile("src/app_window.cpp");
  ExpectToken(source, "add_PermissionRequested", "permission deny/prompt handler");
  ExpectToken(source, "add_DownloadStarting", "download deny/prompt handler");
  ExpectToken(source, "AddWebResourceRequestedFilter", "resource interception filter");
  ExpectToken(source, "add_WebResourceRequested", "resource deny/prompt handler");
}

TEST(HardeningAudit, NewWindowsRequireExplicitConsent) {
  const auto source = ReadRepositoryFile("src/app_window.cpp");
  ExpectToken(source, "ConfirmNewWindow", "per-origin popup confirmation");
}

TEST(HardeningAudit, AgentPermissionGrantsBindTabAndOrigin) {
  const auto header = ReadRepositoryFile("src/agent/permissions.h");
  ExpectToken(header, "tab_id", "tab-bound grants");
  ExpectToken(header, "origin", "origin-bound grants");
}

TEST(HardeningAudit, McpIdentityIsHashAndVersionPinned) {
  const auto header = ReadRepositoryFile("src/mcp/allowlist_manager.h");
  ExpectToken(header, "sha256", "executable hash pin");
  ExpectToken(header, "version", "server version pin");
}

TEST(HardeningAudit, JsonRpcIsParsedAndStrictlyValidated) {
  const auto source = ReadRepositoryFile("src/mcp/mcp_client.cpp");
  ExpectToken(source, "ParseJsonRpcRequest", "JSON-RPC parser");
  ExpectToken(source, "RejectUnknownFields", "strict unknown-field rejection");
  ExpectToken(source, "ValidateToolArguments", "per-tool argument schema");
}

TEST(HardeningAudit, RateLimitsBindToolAndTab) {
  const auto header = ReadRepositoryFile("src/mcp/rate_limiter.h");
  ExpectToken(header, "tool_id", "per-tool rate limiting");
  ExpectToken(header, "tab_id", "per-tab rate limiting");
}

TEST(HardeningAudit, AuditLogIsHashChainedAndRedacted) {
  const auto header = ReadRepositoryFile("src/agent/agent_core.h");
  const auto source = ReadRepositoryFile("src/agent/agent_core.cpp");
  ExpectToken(header, "previous_hash", "hash-chain state");
  ExpectToken(source, "RedactSecrets", "systematic secret redaction");
  ExpectToken(source, "ComputeEntryHash", "tamper-evident entry hash");
}

TEST(HardeningAudit, LocalDataHasAclPortableExportAndWipeControls) {
  const auto database = ReadRepositoryFile("src/storage/database.cpp");
  const auto main = ReadRepositoryFile("src/main.cpp");
  ExpectToken(database, "SetOwnerOnlyAcl", "owner-only local-data ACL");
  ExpectToken(database, "RejectReparsePoint", "reparse-point defense");
  ExpectToken(main, "portable_mode", "portable data placement");
  ExpectToken(main, "ExportAllLocalData", "one-click full export");
  ExpectToken(main, "WipeAllLocalData", "one-click full wipe");
}

TEST(HardeningAudit, LocalModelAdapterIsLoopbackOnly) {
  const auto root = std::filesystem::path(CX_SOURCE_DIR);
  EXPECT_TRUE(std::filesystem::exists(
      root / "src/model/local_model_adapter.cpp"))
      << "Missing optional local model adapter";
  const auto main = ReadRepositoryFile("src/main.cpp");
  ExpectToken(main, "IsLoopbackEndpoint", "loopback endpoint enforcement");
  ExpectToken(main, "DisableAgentOnModelFailure", "no fallback when local model fails");
}

TEST(HardeningAudit, PageContentIsDataAndSensitiveActionsNeedConfirmation) {
  const auto source = ReadRepositoryFile("src/agent/agent_core.cpp");
  ExpectToken(source, "UntrustedPageContent", "page content data boundary");
  ExpectToken(source, "ConfirmSensitiveAction", "sensitive action confirmation");
  ExpectToken(source, "RejectCredentialFields", "credential-field exclusion");
}

TEST(HardeningAudit, BuildPinsHashesAndEnablesMitigations) {
  const auto cmake = ReadRepositoryFile("CMakeLists.txt");
  ExpectToken(cmake, "EXPECTED_HASH", "download hash verification");
  ExpectToken(cmake, "/GS", "stack protection");
  ExpectToken(cmake, "/guard:cf", "Control Flow Guard");
  ExpectToken(cmake, "/DYNAMICBASE", "ASLR");
  ExpectToken(cmake, "/NXCOMPAT", "DEP");
  ExpectToken(cmake, "/WX", "warnings as errors");
}

TEST(HardeningAudit, CiScansDependenciesAndSecrets) {
  const auto ci = ReadRepositoryFile(".github/workflows/ci.yml");
  ExpectToken(ci, "dependency-review-action", "dependency review");
  ExpectToken(ci, "gitleaks", "secret scanning");
}
