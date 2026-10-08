// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

TEST_CASE("Public generic edits share the one-operation recovery exception",
    "[integration][mcp][recovery-approval-generic]")
{
  const bool nodes = GENERATE(false, true);
  image::ExchangeLimits limits;
  limits.globalRetentionBytes = 9000;
  limits.hardRetentionBytes = 10000;
  Fixture f({}, true, nodes, false, 120., true, UINT64_MAX, limits, true);
  const auto session = f.attach();
  std::vector<vsr::scene::LayerNodeRef> nodeRefs;
  for (int i = 0; i < 20; ++i) {
    if (nodes) {
      auto *layer = f.scene.defaultLayer();
      nodeRefs.push_back(layer->root()->insert_last_child(
          vsr::scene::LayerNodeData(layer, "approval-node")));
    } else {
      f.scene.createObject<vsr::scene::Material>("matte")->setParameter(
          "opacity", 1.f);
    }
  }
  f.tool(session,
      "vsr_inspect",
      "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId)
          + (nodes ? ",\"kind\":\"nodes\",\"name\":\"approval-node\"}"
                   : ",\"object_type\":\"ANARI_MATERIAL\"}"));
  const auto inspected = f.await(session, 1);
  std::string edits;
  for (const auto &item : inspected.result()->get("items")->a) {
    if (!edits.empty())
      edits += ',';
    edits += "{\"handle\":" + json::write(*item.get("handle"));
    if (nodes)
      edits += ",\"target_kind\":\"node\",\"version\":"
          + json::write(*item.get("node_edit_version"))
          + ",\"structural_version\":"
          + json::write(*item.get("structural_version"))
          + ",\"parameter\":\"enabled\",\"value\":false}";
    else
      edits += ",\"version\":" + json::write(*item.get("version"))
          + ",\"parameter\":\"opacity\",\"value\":0.5}";
  }
  const auto submit = [&](std::uint64_t id, bool retry) {
    return f.tool(session,
        "vsr_edit",
        "{\"request_id\":" + std::to_string(id)
            + ",\"scene_id\":" + std::to_string(f.sceneId) + ",\"edits\":["
            + edits + "]" + (retry ? ",\"approval_request_id\":2}" : "}"));
  };
  submit(2, false);
  REQUIRE(f.await(session, 2).str("outcome") == "busy");
  auto proposal = f.imageActivity->approvals().snapshot();
  REQUIRE(proposal);
  REQUIRE(f.imageActivity->approvals().approve(
      proposal->identity, proposal->requiredBytes));
  submit(3, true);
  const auto committed = f.await(session, 3);
  REQUIRE(committed.flag("committed"));
  if (!nodes)
    CHECK(f.opacity0() == 0.5f);
  for (const auto &node : nodeRefs)
    CHECK_FALSE(node->value().isEnabled());
  CHECK(f.imageActivity->usage().restorableBatches == 1);
  CHECK(submit(3, true).flag("replayed"));
  f.restoreBatch(session, 4, *committed.result()->get("batch_ref"));
  REQUIRE(f.await(session, 4).flag("committed"));
  CHECK(f.opacity0() == 1.f);
  for (const auto &node : nodeRefs)
    CHECK(node->value().isEnabled());
  CHECK(f.imageActivity->limits().hardRetentionBytes == 10000);
}
