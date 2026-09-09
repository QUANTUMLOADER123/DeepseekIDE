#include "net/HttpClient.h"

#include <curl/curl.h>

#include <mutex>

namespace net {

namespace {
std::once_flag gInitOnce;

size_t WriteToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* s = static_cast<std::string*>(userdata);
  size_t len = size * nmemb;
  s->append(ptr, len);
  return len;
}

struct StreamCtx {
  const std::function<bool(const char*, size_t)>* onData;
  std::atomic<bool>* cancel;
};

size_t WriteStream(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* ctx = static_cast<StreamCtx*>(userdata);
  if (ctx->cancel && ctx->cancel->load()) return 0;  // обрыв по запросу
  size_t len = size * nmemb;
  bool keepGoing = (*ctx->onData)(ptr, len);
  return keepGoing ? len : 0;
}

void SetupCommon(CURL* curl, const std::string& url, const std::vector<std::string>& headers,
                 curl_slist** slist, long timeoutSec) {
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec > 0 ? timeoutSec : 180L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "DeepSeekIDE/0.1");
  for (const auto& h : headers) *slist = curl_slist_append(*slist, h.c_str());
  if (*slist) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *slist);
}
}  // namespace

void HttpClient::GlobalInit() {
  std::call_once(gInitOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpResponse HttpClient::Post(const std::string& url, const std::vector<std::string>& headers,
                              const std::string& body, long timeoutSec) {
  GlobalInit();
  HttpResponse res;
  CURL* curl = curl_easy_init();
  if (!curl) {
    res.error = "curl_easy_init() failed";
    return res;
  }
  curl_slist* slist = nullptr;
  SetupCommon(curl, url, headers, &slist, timeoutSec);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);

  CURLcode rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
  if (rc != CURLE_OK) res.error = curl_easy_strerror(rc);
  if (slist) curl_slist_free_all(slist);
  curl_easy_cleanup(curl);
  return res;
}

bool HttpClient::PostStream(const std::string& url, const std::vector<std::string>& headers,
                            const std::string& body,
                            const std::function<bool(const char*, size_t)>& onData,
                            std::atomic<bool>* cancel, HttpResponse* outMeta, long timeoutSec) {
  GlobalInit();
  HttpResponse meta;
  CURL* curl = curl_easy_init();
  if (!curl) {
    if (outMeta) outMeta->error = "curl_easy_init() failed";
    return false;
  }
  curl_slist* slist = nullptr;
  SetupCommon(curl, url, headers, &slist, timeoutSec);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));

  StreamCtx ctx{&onData, cancel};
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStream);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

  CURLcode rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &meta.status);
  if (rc != CURLE_OK) meta.error = curl_easy_strerror(rc);
  if (slist) curl_slist_free_all(slist);
  curl_easy_cleanup(curl);

  if (outMeta) *outMeta = meta;
  bool cancelled = cancel && cancel->load();
  return rc == CURLE_OK && !cancelled;
}

HttpResponse HttpClient::Get(const std::string& url, const std::vector<std::string>& headers,
                             long timeoutSec) {
  GlobalInit();
  HttpResponse res;
  CURL* curl = curl_easy_init();
  if (!curl) {
    res.error = "curl_easy_init() failed";
    return res;
  }
  curl_slist* slist = nullptr;
  SetupCommon(curl, url, headers, &slist, timeoutSec);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);

  CURLcode rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
  if (rc != CURLE_OK) res.error = curl_easy_strerror(rc);
  if (slist) curl_slist_free_all(slist);
  curl_easy_cleanup(curl);
  return res;
}

}  // namespace net
