#include "browser/webview_security_policy.h"

#include <gtest/gtest.h>

TEST(WebViewSecurityPolicyTest, ResourceSchemesFailClosed) {
  using cx::browser::WebViewSecurityPolicy;
  EXPECT_TRUE(WebViewSecurityPolicy::IsAllowedResourceUrl(
      L"https://example.test/app.js"));
  EXPECT_TRUE(WebViewSecurityPolicy::IsAllowedResourceUrl(
      L"blob:https://example.test/id"));
  EXPECT_TRUE(WebViewSecurityPolicy::IsAllowedResourceUrl(
      L"data:image/png;base64,AA=="));
  EXPECT_FALSE(WebViewSecurityPolicy::IsAllowedResourceUrl(
      L"file:///C:/secret.txt"));
  EXPECT_FALSE(WebViewSecurityPolicy::IsAllowedResourceUrl(
      L"javascript:alert(1)"));
  EXPECT_FALSE(WebViewSecurityPolicy::IsAllowedResourceUrl(
      L"cx-custom:payload"));
}

TEST(WebViewSecurityPolicyTest, OriginsAreCanonicalAndCredentialFree) {
  using cx::browser::WebViewSecurityPolicy;
  EXPECT_EQ(
      WebViewSecurityPolicy::OriginFromUrl(
          L"HTTPS://Example.TEST:443/path"),
      std::optional<std::wstring>(L"https://example.test"));
  EXPECT_FALSE(
      WebViewSecurityPolicy::OriginFromUrl(
          L"https://user@example.test/").has_value());
  EXPECT_FALSE(
      WebViewSecurityPolicy::OriginFromUrl(
          L"file:///C:/local.txt").has_value());
}

TEST(WebViewSecurityPolicyTest, DetectsCrossOriginNavigation) {
  using cx::browser::WebViewSecurityPolicy;
  EXPECT_FALSE(WebViewSecurityPolicy::IsCrossOrigin(
      L"https://example.test/a",
      L"https://EXAMPLE.test:443/b"));
  EXPECT_TRUE(WebViewSecurityPolicy::IsCrossOrigin(
      L"https://example.test/a",
      L"https://other.test/b"));
  EXPECT_FALSE(WebViewSecurityPolicy::IsCrossOrigin(
      L"about:blank",
      L"https://example.test/"));
}
