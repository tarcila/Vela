// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <string>
#include <vector>
#include "catch.hpp"
#include "vsr/scene/Scene.hpp"

namespace chess {

// Independently worked, binary-exact square oracle. Never call the scene's
// matrix/conversion helpers to construct expected destination coordinates.
struct Move
{
  const char *name;
  std::array<double, 3> world;
  std::array<double, 3> local;
};
inline const std::array<Move, 4> MOVES{{{"white f-pawn",
                                            {1.859375, .2890625, -2.8125},
                                            {-.09375, .03125, -.09375}},
    {"white g-pawn", {1.953125, .2890625, -2.6875}, {-.15625, .03125, -.03125}},
    {"black e-pawn", {2.046875, .2890625, -2.9375}, {-.03125, .03125, .03125}},
    {"black queen", {1.953125, .40625, -2.5625}, {-.21875, .125, -.03125}}}};

struct Fixture
{
  vsr::scene::Scene &scene;
  vsr::scene::LayerNodeRef board;
  std::vector<vsr::scene::LayerNodeRef> pieces;
  std::vector<vsr::scene::LayerNodeRef> references;
  std::vector<vsr::scene::LayerNodeData> original;
  std::vector<vsr::scene::LayerNodeData> originalReferences;
  std::vector<vsr::scene::SurfaceRef> surfaces;
  vsr::scene::LayerNodeData originalBoard;

  explicit Fixture(vsr::scene::Scene &s) : scene(s)
  {
    auto *layer = scene.defaultLayer();
    board = layer->root()->insert_last_child(
        vsr::scene::LayerNodeData(layer, "board"));
    auto matrix = vsr::math::IDENTITY_MAT4;
    // Rows: [0,0,1.5,2], [0,1.25,0,.25], [-2,0,0,-3], [0,0,0,1].
    matrix[0] = {0, 0, -2, 0};
    matrix[1] = {0, 1.25f, 0, 0};
    matrix[2] = {1.5f, 0, 0, 0};
    matrix[3] = {2, .25f, -3, 1};
    board->value().setAsTransform(matrix);
    board->value().setCurrentValueAsDefault();
    originalBoard = board->value();
    const char *back[] = {"rook",
        "knight",
        "bishop",
        "queen",
        "king",
        "bishop",
        "knight",
        "rook"};
    for (int side = 0; side < 2; ++side) {
      auto surface = scene.createSurface();
      surface->setName(side ? "shared black piece" : "shared white piece");
      surfaces.push_back(surface);
      for (int row = 0; row < 2; ++row) {
        for (int file = 0; file < 8; ++file) {
          const bool pawn = row == 1;
          std::string name = side ? "black " : "white ";
          name +=
              pawn ? std::string(1, char('a' + file)) + "-pawn" : back[file];
          if (!pawn && file != 3 && file != 4)
            name += std::string(" ") + char('a' + file);
          auto node = board->insert_last_child(
              vsr::scene::LayerNodeData(layer, name.c_str()));
          const int rank = side ? (pawn ? 7 : 8) : (pawn ? 2 : 1);
          const vsr::math::float3 position(.21875f - .0625f * file,
              pawn ? .03125f : .125f,
              -.21875f + .0625f * (rank - 1));
          if (side) {
            auto srt = vsr::math::mat3{};
            srt[0] = {1, 1, 1};
            srt[1] = {0, 0, 0};
            srt[2] = position;
            node->value().setAsTransform(srt);
          } else {
            auto m = vsr::math::IDENTITY_MAT4;
            m[1].x = .125f; // Exact matrix representation, not SRT.
            m[3] = {position.x, position.y, position.z, 1};
            node->value().setAsTransform(m);
          }
          node->value().setCurrentValueAsDefault();
          pieces.push_back(node);
          original.push_back(node->value());
          auto ref = scene.insertChildObjectNode(node, surface);
          references.push_back(ref);
          originalReferences.push_back(ref->value());
        }
      }
    }
    scene.signalLayerStructureChanged(layer);
    REQUIRE(pieces.size() == 32);
  }

  static void same(
      const vsr::scene::LayerNodeData &a, const vsr::scene::LayerNodeData &b)
  {
    CHECK(a.name() == b.name());
    CHECK(a.isEnabled() == b.isEnabled());
    CHECK(a.type() == b.type());
    CHECK(a.getValueRaw() == b.getValueRaw());
    CHECK(a.hasDefault() == b.hasDefault());
    CHECK(a.isDefaultValue() == b.isDefaultValue());
    if (a.isTransform()) {
      for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
          CHECK(a.getTransformSRT()[c][r] == b.getTransformSRT()[c][r]);
      auto defaultA = a;
      auto defaultB = b;
      defaultA.setToDefaultValue();
      defaultB.setToDefaultValue();
      CHECK(defaultA.getValueRaw() == defaultB.getValueRaw());
    }
  }

  void unchanged(bool restored) const
  {
    same(board->value(), originalBoard);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
      bool moved = false;
      for (const auto &move : MOVES)
        moved |= pieces[i]->value().name() == move.name;
      if (restored || !moved)
        same(pieces[i]->value(), original[i]);
      same(references[i]->value(), originalReferences[i]);
      CHECK(references[i]->value().getObject() == surfaces[i / 16].data());
    }
    CHECK(surfaces[0]->name() == "shared white piece");
    CHECK(surfaces[1]->name() == "shared black piece");
  }
};
} // namespace chess
