// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PrincipledVolumeImport.hpp"
// json
#include <json.hpp>
// std
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace vsr::io {
namespace {
using Json = nlohmann::json;
using namespace vsr::math;

enum class Kind
{
  SCALAR,
  RGB,
  BOOL,
  ID
};
struct Property
{
  const char *name;
  Kind kind;
  bool field;
};
constexpr Property PROPERTIES[] = {{"density", Kind::SCALAR, true},
    {"temperature", Kind::SCALAR, true},
    {"scattering", Kind::RGB, true},
    {"emission", Kind::RGB, true},
    {"transmissionColor", Kind::RGB, false},
    {"transmissionDistance", Kind::SCALAR, false},
    {"anisotropy", Kind::SCALAR, false},
    {"secondaryAnisotropy", Kind::SCALAR, false},
    {"secondaryWeight", Kind::SCALAR, false},
    {"blackbodyIntensity", Kind::SCALAR, false},
    {"blackbodyTint", Kind::RGB, false},
    {"visible", Kind::BOOL, false},
    {"id", Kind::ID, false}};

[[noreturn]] void error(const std::string &text)
{
  throw std::runtime_error("[principled volume] " + text);
}

const Property &property(const std::string &name)
{
  for (const auto &p : PROPERTIES)
    if (name == p.name)
      return p;
  error("unknown property '" + name + "'");
}

std::vector<float> numbers(const std::string &text)
{
  std::vector<float> out;
  std::istringstream stream(text);
  std::string token;
  while (std::getline(stream, token, ',')) {
    size_t end = 0;
    float v = std::stof(token, &end);
    if (end != token.size() || !std::isfinite(v))
      error("expected finite numeric value: '" + text + "'");
    out.push_back(v);
  }
  if (out.empty() || text.back() == ',')
    error("empty numeric value");
  return out;
}

PrincipledVolumeValue parseValue(const Property &p, const std::string &text)
{
  if (p.kind == Kind::BOOL) {
    if (text == "true")
      return true;
    if (text == "false")
      return false;
    error("expected true or false");
  }
  if (p.kind == Kind::ID) {
    if (text.empty()
        || text.find_first_not_of("0123456789") != std::string::npos)
      error("expected uint32");
    auto value = std::stoull(text);
    if (value > UINT32_MAX)
      error("id exceeds uint32");
    return uint32_t(value);
  }
  auto v = numbers(text);
  if (p.kind == Kind::RGB && v.size() == 3)
    return float3(v[0], v[1], v[2]);
  if (p.kind == Kind::SCALAR && v.size() == 1)
    return v[0];
  error("incorrect number of components");
}

void checkBounds(const box3 &b)
{
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(b.lower[i]) || !std::isfinite(b.upper[i])
        || b.lower[i] >= b.upper[i])
      error("bounds must be finite with strictly positive extents");
}

void checkValue(const std::string &name, const PrincipledVolumeValue &value)
{
  const auto &p = property(name);
  if ((p.kind == Kind::SCALAR && !std::holds_alternative<float>(value))
      || (p.kind == Kind::RGB && !std::holds_alternative<float3>(value))
      || (p.kind == Kind::BOOL && !std::holds_alternative<bool>(value))
      || (p.kind == Kind::ID && !std::holds_alternative<uint32_t>(value)))
    error(name + ": incorrect value type");
  if (p.kind == Kind::BOOL || p.kind == Kind::ID)
    return;
  float3 v;
  int count = 1;
  if (auto scalar = std::get_if<float>(&value))
    v = float3(*scalar);
  else {
    v = std::get<float3>(value);
    count = 3;
  }
  for (int i = 0; i < count; ++i) {
    float x = v[i];
    bool valid = std::isfinite(x);
    if (name == "anisotropy" || name == "secondaryAnisotropy")
      valid &= x >= -1 && x <= 1;
    else if (name == "secondaryWeight" || name == "scattering")
      valid &= x >= 0 && x <= 1;
    else if (name == "transmissionColor")
      valid &= x > 0 && x <= 1;
    else if (name != "temperature" && name != "transmissionDistance")
      valid &= x >= 0;
    if (!valid)
      error(name + ": value outside permitted range");
  }
}

Json asJson(const PrincipledVolumeValue &v)
{
  return std::visit(
      [](const auto &x) -> Json {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, float3>)
          return Json::array({x.x, x.y, x.z});
        else
          return x;
      },
      v);
}
} // namespace

void validatePrincipledVolumeOptions(const PrincipledVolumeImportOptions &o)
{
  if (o.bounds)
    checkBounds(*o.bounds);
  for (const auto &[name, source] : o.fields) {
    if (!property(name).field)
      error(name + ": field not supported");
    if (source.file.empty())
      error(name + ": empty field filename");
  }
  for (const auto &[name, v] : o.parameters)
    checkValue(name, v);
  // Cross-parameter constraints apply to complete options, not intermediate
  // assignments: transparent media may specify a nonpositive distance.
  auto scalar = [&](const char *name, float fallback) {
    auto it = o.parameters.find(name);
    return it == o.parameters.end() ? fallback : std::get<float>(it->second);
  };
  auto color = o.parameters.find("transmissionColor");
  const auto transmission = color == o.parameters.end()
      ? float3(0.36787944f)
      : std::get<float3>(color->second);
  if (scalar("transmissionDistance", 1.f) <= 0 && scalar("density", 1.f) != 0
      && (transmission.x != 1.f || transmission.y != 1.f
          || transmission.z != 1.f))
    error("transmissionDistance must be positive when extinction is nonzero");
}

void applyPrincipledVolumeAssignment(
    PrincipledVolumeImportOptions &o, const std::string &assignment)
{
  auto eq = assignment.find('=');
  if (eq == std::string::npos || eq == 0 || eq + 1 == assignment.size())
    error("expected property[:field|value]=value, got '" + assignment + "'");
  auto key = assignment.substr(0, eq);
  auto text = assignment.substr(eq + 1);
  auto colon = key.find(':');
  auto name = key.substr(0, colon);
  auto semantic = colon == std::string::npos ? "" : key.substr(colon + 1);
  if (colon != std::string::npos && semantic != "value" && semantic != "field")
    error(name + ": unknown semantic '" + semantic + "'");
  try {
    if (name == "name") {
      if (semantic == "field" || !o.name.empty())
        error("invalid or duplicate name");
      o.name = text;
      return;
    }
    if (name == "bounds") {
      if (semantic == "field" || o.bounds)
        error("invalid or duplicate bounds");
      auto v = numbers(text);
      if (v.size() != 6)
        error("bounds expects six coordinates");
      box3 bounds(float3(v[0], v[1], v[2]), float3(v[3], v[4], v[5]));
      checkBounds(bounds);
      o.bounds = bounds;
      return;
    }
    const auto &p = property(name);
    std::optional<PrincipledVolumeValue> value;
    if (semantic != "field") {
      try {
        value = parseValue(p, text);
      } catch (const std::exception &) {
        if (semantic == "value" || !p.field)
          throw;
      }
    }
    if (value) {
      checkValue(name, *value);
      if (!o.parameters.emplace(name, *value).second)
        error(name + ": duplicate value");
    } else {
      if (!p.field)
        error(name + ": field not supported");
      if (!o.fields.emplace(name, PrincipledVolumeFieldSource{text}).second)
        error(name + ": duplicate field");
    }
  } catch (const std::exception &e) {
    error(assignment + ": " + e.what());
  }
}

PrincipledVolumeImportOptions readPrincipledVolumeDescription(
    const std::filesystem::path &file)
{
  try {
    std::ifstream in(file);
    if (!in)
      error("cannot read description");
    std::vector<std::set<std::string>> keys;
    auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
      if (event == Json::parse_event_t::object_start)
        keys.emplace_back();
      if (event == Json::parse_event_t::key
          && !keys.back().insert(parsed.get<std::string>()).second)
        error("duplicate JSON key '" + parsed.get<std::string>() + "'");
      if (event == Json::parse_event_t::object_end)
        keys.pop_back();
      return true;
    };
    auto j = Json::parse(in, callback);
    if (!j.is_object())
      error("description must be an object");
    PrincipledVolumeImportOptions o;
    for (auto it = j.begin(); it != j.end(); ++it) {
      auto key = it.key();
      const auto &v = it.value();
      if (key == "name")
        o.name = v.get<std::string>();
      else if (key == "bounds") {
        if (!v.is_array() || v.size() != 6)
          error("bounds expects six numbers");
        for (auto &x : v)
          if (!x.is_number())
            error("bounds expects numbers");
        o.bounds = box3(
            float3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>()),
            float3(v[3].get<float>(), v[4].get<float>(), v[5].get<float>()));
      } else if (key == "fields") {
        if (!v.is_object())
          error("fields must be an object");
        for (auto field = v.begin(); field != v.end(); ++field) {
          if (!field.value().is_object() || field.value().size() != 1
              || !field.value().contains("file"))
            error(field.key() + ": expected {file: path}");
          auto path = std::filesystem::path(
              field.value().at("file").get<std::string>());
          if (path.empty())
            error(field.key() + ": empty filename");
          o.fields[field.key()] = {
              std::filesystem::absolute(file.parent_path() / path)
                  .lexically_normal()};
        }
      } else if (key == "parameters") {
        if (!v.is_object())
          error("parameters must be an object");
        for (auto p = v.begin(); p != v.end(); ++p) {
          auto kind = property(p.key()).kind;
          if (kind == Kind::BOOL && p.value().is_boolean())
            o.parameters[p.key()] = p.value().get<bool>();
          else if (kind == Kind::ID && p.value().is_number_integer()
              && p.value().get<double>() >= 0
              && p.value().get<double>() <= UINT32_MAX)
            o.parameters[p.key()] = p.value().get<uint32_t>();
          else if (kind == Kind::SCALAR && p.value().is_number())
            o.parameters[p.key()] = p.value().get<float>();
          else if (kind == Kind::RGB && p.value().is_array()
              && p.value().size() == 3) {
            for (const auto &x : p.value())
              if (!x.is_number())
                error(p.key() + ": expected RGB numbers");
            o.parameters[p.key()] = float3(p.value()[0].get<float>(),
                p.value()[1].get<float>(),
                p.value()[2].get<float>());
          } else
            error(p.key() + ": incorrect JSON value type");
        }
      } else
        error("unknown description member '" + key + "'");
    }
    validatePrincipledVolumeOptions(o);
    return o;
  } catch (const std::exception &e) {
    error(file.string() + ": " + e.what());
  }
}

void writePrincipledVolumeDescription(
    const PrincipledVolumeImportOptions &o, const std::filesystem::path &file)
{
  validatePrincipledVolumeOptions(o);
  Json j = Json::object();
  if (!o.name.empty())
    j["name"] = o.name;
  j["fields"] = Json::object();
  for (const auto &[name, source] : o.fields)
    j["fields"][name]["file"] = std::filesystem::absolute(source.file)
                                    .lexically_normal()
                                    .generic_string();
  j["parameters"] = Json::object();
  for (const auto &[name, v] : o.parameters)
    j["parameters"][name] = asJson(v);
  if (o.bounds) {
    auto &b = *o.bounds;
    j["bounds"] = {
        b.lower.x, b.lower.y, b.lower.z, b.upper.x, b.upper.y, b.upper.z};
  }
  std::ofstream out(file);
  if (!out || !(out << j.dump(2) << '\n'))
    error("cannot write '" + file.string() + "'");
}

} // namespace vsr::io
