#include "net/HttpClient.h"

// HTTP-клиент на cpp-httplib (vendored header). Никаких внешних зависимостей:
// libcurl больше не нужен. https:// работает, если httplib собран с
// OpenSSL (-DCPPHTTPLIB_OPENSSL_SUPPORT и линковка ssl+crypto). Для наших
// нужд (CDP /json/* по 127.0.0.1) и http достаточно.

#include "cpp-httplib/httplib.h"

namespace net {

namespace {

struct Url {
  std::string host;
  int port = 0;
  std::string path;
  bool https = false;
};

bool SplitUrl(const std::string& url, Url& out) {
  const std::string::size_type p = url.find("://");
  if (p == std::string::npos) return false;
  const std::string scheme = url.substr(0, p);
  out.https = (scheme == "https");
  if (scheme != "http" && !out.https) return false;
  std::string rest = url.substr(p + 3);
  const auto slash = rest.find('/');
  const std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
  out.path = slash == std::string::npos ? "/" : rest.substr(slash);
  const auto colon = hostport.rfind(':');
  if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
    out.host = hostport.substr(0, colon);
    out.port = atoi(hostport.c_str() + colon + 1);
  } else {
    out.host = hostport;
    out.port = out.https ? 443 : 80;
  }
  return !out.host.empty();
}

httplib::Headers ToHeaders(const std::vector<std::string>& headers) {
  httplib::Headers hs;
  for (const auto& h : headers) {
    const auto colon = h.find(':');
    if (colon == std::string::npos) continue;
    std::string name = h.substr(0, colon);
    std::string value = h.substr(colon + 1);
    while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(value.begin());
    hs.emplace(std::move(name), std::move(value));
  }
  return hs;
}

}  // namespace

void HttpClient::GlobalInit() {}  // совместимость: ничего не нужно

HttpResponse HttpClient::Get(const std::string& url, const std::vector<std::string>& headers,
                             long timeoutSec) {
  HttpResponse out;
  Url u;
  if (!SplitUrl(url, u)) {
    out.error = "битый URL: " + url;
    return out;
  }
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
  if (u.https) {
    out.error = "https требует сборку с OpenSSL: " + url;
    return out;
  }
#endif
  httplib::Client cli(u.host, u.port);
  cli.set_connection_timeout(timeoutSec > 30 ? 30 : timeoutSec, 0);
  cli.set_read_timeout(timeoutSec, 0);
  auto res = cli.Get(u.path.c_str(), ToHeaders(headers));
  if (!res) {
    out.error = httplib::to_string(res.error());
    return out;
  }
  out.status = res->status;
  out.body = std::move(res->body);
  return out;
}

HttpResponse HttpClient::Post(const std::string& url, const std::vector<std::string>& headers,
                              const std::string& body, long timeoutSec) {
  HttpResponse out;
  Url u;
  if (!SplitUrl(url, u)) {
    out.error = "битый URL: " + url;
    return out;
  }
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
  if (u.https) {
    out.error = "https требует сборку с OpenSSL: " + url;
    return out;
  }
#endif
  httplib::Client cli(u.host, u.port);
  cli.set_connection_timeout(30, 0);
  cli.set_read_timeout(timeoutSec, 0);
  cli.set_write_timeout(60, 0);
  auto res = cli.Post(u.path.c_str(), ToHeaders(headers), body, "application/json");
  if (!res) {
    out.error = httplib::to_string(res.error());
    return out;
  }
  out.status = res->status;
  out.body = std::move(res->body);
  return out;
}

HttpResponse HttpClient::Put(const std::string& url, const std::vector<std::string>& headers,
                             long timeoutSec) {
  HttpResponse out;
  Url u;
  if (!SplitUrl(url, u)) {
    out.error = "битый URL: " + url;
    return out;
  }
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
  if (u.https) {
    out.error = "https требует сборку с OpenSSL: " + url;
    return out;
  }
#endif
  httplib::Client cli(u.host, u.port);
  cli.set_connection_timeout(10, 0);
  cli.set_read_timeout(timeoutSec, 0);
  auto res = cli.Put(u.path.c_str(), ToHeaders(headers), std::string(), "application/json");
  if (!res) {
    out.error = httplib::to_string(res.error());
    return out;
  }
  out.status = res->status;
  out.body = std::move(res->body);
  return out;
}

}  // namespace net
