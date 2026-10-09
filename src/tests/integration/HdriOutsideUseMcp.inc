// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

SCENARIO(
    "Public HDRI Restore refuses ordinary ignored-count outside references",
    "[integration][content][mcp][public-hdri-outside-use]")
{
  const auto referenceForm = GENERATE(0, 1, 2, 3, 4);
  HdriFixture f;
  f.scene.defaultLayer();
  const auto session = f.attach();
  {
    std::ofstream file(f.tmp.path + "/outside.hdr", std::ios::binary);
    file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
    const unsigned char pixel[] = {128, 64, 32, 132};
    file.write(reinterpret_cast<const char *>(pixel), sizeof(pixel));
  }
  REQUIRE(f.tool(session,
               "vsr_create_hdri",
               "{\"request_id\":1,\"scene_id\":" + std::to_string(f.sceneId)
                   + ",\"asset_root\":0,\"file_name\":\"outside.hdr\"}")
              .str("kind")
      == "create_hdri");
  const auto created = f.await(session, 1);
  REQUIRE(created.str("outcome") == "ok");
  REQUIRE(created.flag("committed"));
  const auto *primary = created.result()->get("primary");
  const auto lightHandle =
      mcp::tools::detail::handleArg(primary->get("light"), f.ids.launch());
  const auto arrayHandle =
      mcp::tools::detail::handleArg(primary->get("radiance"), f.ids.launch());
  REQUIRE(lightHandle);
  REQUIRE(arrayHandle);
  auto *light = f.sceneIds->resolve(*lightHandle);
  auto *array = f.sceneIds->resolve(*arrayHandle);
  REQUIRE(light);
  REQUIRE(array);
  auto outside =
      f.scene.insertChildNode(f.scene.defaultLayer()->root(), "outside user");
  vsr::scene::ArrayRef objectArray;
  if (referenceForm == 0)
    outside->value().setInstanceParameter(
        "radiance", vsr::core::Any(ANARI_ARRAY2D, array->index()));
  else if (referenceForm == 1)
    outside->value().setValueRaw(
        vsr::core::Any(ANARI_LIGHT, light->index()), true);
  else if (referenceForm == 2) {
    objectArray = f.scene.createArray(ANARI_LIGHT, 1);
    array = f.sceneIds->resolve(*arrayHandle); // Pool growth relocates values.
    const auto index = light->index();
    objectArray->setData(&index, 1);
    outside->value().setInstanceParameter(
        "lights", vsr::core::Any(ANARI_ARRAY1D, objectArray.index()));
  } else {
    auto material = f.scene.getObject<vsr::scene::Material>(0);
    auto &parameter = material->addParameter("unusual bound");
    if (referenceForm == 3)
      parameter.setMin(vsr::core::Any(ANARI_LIGHT, light->index()));
    else
      parameter.setMax(vsr::core::Any(ANARI_ARRAY2D, array->index()));
    parameter.setEnabled(false);
  }
  // Both ordinary APIs leave the original counted closure unchanged. The
  // authoritative scan, not a tautological count assertion, must refuse Undo.
  CHECK(light->totalUseCount() == 1);
  CHECK(array->totalUseCount() == 1);
  REQUIRE(f.restoreBatch(session, 2, *created.result()->get("batch_ref"))
              .str("kind")
      == "restore");
  const auto removed = f.await(session, 2);
  CHECK(removed.str("outcome") == "conflict");
  CHECK_FALSE(removed.flag("committed"));
  CHECK(f.sceneIds->resolve(*lightHandle) == light);
  CHECK(f.sceneIds->resolve(*arrayHandle) == array);
  CHECK(outside);
  // Clean the ignored binding with the same supported API so fixture teardown
  // does not decrement a count which this caller deliberately never added.
  outside->value().setValueRaw(vsr::core::Any{}, true);
  outside->value().clearInstanceParameters();
}
