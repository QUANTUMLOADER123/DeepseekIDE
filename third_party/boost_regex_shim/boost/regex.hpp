// Минимальный shim boost/regex.hpp → std::regex.
//
// ImGuiColorTextEdit (santaclose) использует лишь малое подмножество Boost.Regex
// (boost::regex, boost::cmatch, boost::regex_search + regex_constants), которое
// почти дословно совпадает со std::regex. Этот заголовок позволяет собирать
// редактор без установки Boost. Если настоящий Boost найден в системе, CMake
// использует его вместо shim'а.
#pragma once

#include <regex>

namespace boost {
using std::cmatch;
using std::regex;
using std::regex_search;
namespace regex_constants = std::regex_constants;
}  // namespace boost
