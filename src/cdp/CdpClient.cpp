#include "cdp/CdpClient.h"

bool CdpClient::Connect(const std::string& wsUrl, std::string& err, int timeoutMs,
                        bool probePage) {
  if (!mWs.Connect(wsUrl, err, timeoutMs)) return false;
  if (probePage) {
    std::string v, e;
    if (!Evaluate("1+1", v, e, timeoutMs)) {
      err = e.empty() ? "таргет CDP не отвечает на Runtime.evaluate"
                      : "таргет CDP: " + e;
      Close();
      return false;
    }
  }
  return true;
}

bool CdpClient::SendCmd(const std::string& method, const nlohmann::json& params,
                        nlohmann::json& respOut, std::string& err, int timeoutMs) {
  const int id = mNextId++;
  nlohmann::json req = {{"id", id}, {"method", method}, {"params", params}};
  if (!mSession.empty()) req["sessionId"] = mSession;
  if (!mWs.SendText(req.dump())) {
    err = "WebSocket разорван (отправка)";
    return false;
  }
  // Ждём ответ именно с нашим id; events (без id) пропускаем.
  for (int guard = 0; guard < 200; ++guard) {
    std::string msg;
    if (!mWs.RecvText(msg, timeoutMs)) {
      err = "WebSocket разорван/таймаут (получение)";
      return false;
    }
    auto j = nlohmann::json::parse(msg, nullptr, false);
    if (j.is_discarded()) continue;
    if (j.value("id", -1) != id) continue;  // событие/чужой ответ
    if (j.contains("error")) {
      err = "CDP " + method + " error: " + j["error"].value("message", std::string("?")) +
            " (code " + std::to_string(j["error"].value("code", 0)) + ")";
      return false;
    }
    respOut = j.value("result", nlohmann::json::object());
    return true;
  }
  err = "CDP: не дождался ответа на " + method;
  return false;
}

bool CdpClient::Evaluate(const std::string& expr, std::string& valueOut, std::string& err,
                         int timeoutMs) {
  nlohmann::json resp;
  if (!SendCmd("Runtime.evaluate",
               {{"expression", expr},
                {"returnByValue", true},
                {"userGesture", true},
                {"replMode", true}},
               resp, err, timeoutMs))
    return false;

  const std::string subtype = resp.value("subtype", std::string{});
  if (resp.value("isError", false) || subtype == "error" || resp.contains("exceptionDetails")) {
    std::string desc = "JS-исключение";
    if (resp.contains("exceptionDetails") && resp["exceptionDetails"].is_object())
      desc = resp["exceptionDetails"].value("text", desc);
    err = "JS evaluate error: " + desc;
    return false;
  }
  if (resp.contains("result") && resp["result"].is_object()) {
    const auto& inner = resp["result"];
    if (inner.contains("value")) {
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
  valueOut.clear();
  return true;
}

void CdpClient::Close() { mWs.Close(); }
