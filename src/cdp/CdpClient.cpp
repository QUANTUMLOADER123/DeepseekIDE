#include "cdp/CdpClient.h"

#include <nlohmann/json.hpp>

bool CdpClient::Connect(const std::string& wsUrl, std::string& err, int timeoutMs) {
  if (!mWs.Connect(wsUrl, err, timeoutMs)) return false;
  // Жизненный тест: Runtime.enable (необязателен, но подтверждает живой таргет)
  std::string v, e;
  if (!Evaluate("1+1", v, e, timeoutMs)) {
    Close();
    err = e.empty() ? "таргет CDP не отвечает" : e;
    return false;
  }
  return true;
}

bool CdpClient::Evaluate(const std::string& expr, std::string& valueOut, std::string& err,
                         int timeoutMs) {
  const int id = mNextId++;
  nlohmann::json req = {
      {"id", id},
      {"method", "Runtime.evaluate"},
      {"params",
       {{"expression", expr},
        {"returnByValue", true},
        {"userGesture", true},
        {"replMode", true}}}};
  if (!mWs.SendText(req.dump())) {
    err = "WebSocket разорван (отправка)";
    return false;
  }
  // Ждём ответ именно с нашим id; события (method) пропускаем.
  std::string msg;
  if (!mWs.RecvText(msg, timeoutMs)) {
    err = "WebSocket разорван/таймаут (получение)";
    return false;
  }
  auto j = nlohmann::json::parse(msg, nullptr, false);
  if (j.is_discarded()) {
    err = "CDP: нечитаемый JSON в ответе";
    return false;
  }
  if (j.contains("error")) {
    err = "CDP error: " + j["error"].value("message", std::string("?"));
    return false;
  }
  if (j.value("id", -1) != id) {
    // События без id проталкиваем дополнительным recv
    while (j.value("id", -1) != id) {
      if (!mWs.RecvText(msg, timeoutMs)) {
        err = "WebSocket разорван/таймаут (ожидание ответа)";
        return false;
      }
      j = nlohmann::json::parse(msg, nullptr, false);
      if (j.is_discarded()) continue;
      if (j.contains("error")) {
        err = "CDP error: " + j["error"].value("message", std::string("?"));
        return false;
      }
    }
  }
  const auto& rr = j["result"];
  const auto& inner = rr["result"];
  if (rr.value("subtype", std::string{}) == "error" || rr.value("isError", false) ||
      rr.contains("exceptionDetails")) {
    std::string desc;
    if (rr.contains("exceptionDetails"))
      desc = rr["exceptionDetails"]
                 .value("text", std::string("JS-исключение"));
    err = "JS evaluate error: " + (desc.empty() ? inner.dump() : desc);
    return false;
  }
  if (inner.is_object() && inner.contains("value")) {
    const auto& v = inner["value"];
    if (v.is_string()) valueOut = v.get<std::string>();
    else valueOut = v.dump();
    return true;
  }
  if (inner.value("type", std::string{}) == "undefined") {
    valueOut.clear();
    return true;
  }
  valueOut = inner.dump();
  return true;
}

void CdpClient::Close() { mWs.Close(); }
