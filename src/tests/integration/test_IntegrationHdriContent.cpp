// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_integration
#include "vsr/integration/content/AssetRoots.hpp"
#include "vsr/integration/content/HdriDecode.hpp"
#include "vsr/integration/content/PreparedHdriPublication.hpp"
#include "vsr/integration/identity/Identity.hpp"
#include "vsr/integration/operations/ResultOwnership.hpp"
// vsr_rendering
#include "vsr/rendering/view/FrameObservation.h"
// vsr_scene
#include "vsr/scene/detail/PreparedHdriContent.hpp"
#include "vsr/scene/detail/PreparedMeshBinding.hpp"
// catch
#include "catch.hpp"
// std
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <type_traits>
// posix
#ifdef __linux__
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace vsr;

TEST_CASE("Prepared parameter maps refuse intervening metadata change-back",
    "[integration][content][meta-param]")
{
  scene::Scene scene;
  auto geometry = scene.createObject<scene::Geometry>("triangle");
  auto *parameter = geometry->setParameter("custom", 1.f);
  parameter->setDescription("original");
  scene::detail::PreparedMeshBinding prepared(*geometry);
  auto copy = *parameter;
  copy.setDescription("detached preparation");
  prepared.set("custom", copy);
  CHECK(prepared.isCurrent(*geometry));
  CHECK(parameter->description() == "original");
  const auto parameters = geometry->lastParameterChange();
  parameter->setDescription("intervening edit");
  parameter->setDescription("original");
  CHECK(geometry->lastParameterChange() == parameters);
  CHECK_FALSE(prepared.isCurrent(*geometry));
  CHECK(parameter->description() == "original");
}

TEST_CASE("Ordinary instance parameter change-back preserves metadata history",
    "[integration][content][metadata]")
{
  scene::Scene scene;
  auto node = scene.insertChildTransformNode(scene.defaultLayer()->root());
  const auto before = node->value().metadataSequence();
  const auto edit = node->value().editVersion();
  const auto transform = node->value().transformVersion();
  const auto &reader = node->value().getInstanceParameters();
  node->value().setInstanceParameter("user", core::Any(1.f));
  node->value().clearInstanceParameters();
  CHECK(reader.empty());
  CHECK(node->value().metadataSequence() > before);
  CHECK(node->value().editVersion() == edit);
  CHECK(node->value().transformVersion() == transform);
}

static_assert(
    std::is_same_v<decltype(std::declval<scene::LayerNodeData &>().name()),
        const std::string &>);
static_assert(std::is_same_v<decltype(std::declval<scene::Layer &>().name()),
    const std::string &>);
static_assert(std::is_same_v<
    decltype(std::declval<scene::LayerNodeData &>().getInstanceParameters()),
    const scene::InstanceParameterMap &>);

TEST_CASE("Metadata guards track owned labels without notifications",
    "[integration][content][metadata]")
{
  scene::Scene scene;
  auto *borrowedLayer = scene.defaultLayer();
  auto parent = scene.insertChildTransformNode(borrowedLayer->root());
  auto *ownedLayer = scene.addLayer("owned");
  auto root = scene.insertChildTransformNode(
      ownedLayer->root(), core::math::IDENTITY_MAT4, "Content Root");
  auto camera = scene.createObject<scene::Camera>("perspective");
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  const auto handle = identity.handleOf(root);
  // Readers/buffers can predate capture, but none expose writable backing.
  const auto &reader = root->value().name();
  std::string editor = reader;
  const auto nodeToken = identity.nodeMetadataVersion(handle);
  const auto layerToken =
      identity.layerMetadataVersion(identity.handleOf(ownedLayer));
  const auto editToken = identity.nodeEditVersion(handle);
  REQUIRE(nodeToken);
  REQUIRE(layerToken);
  REQUIRE(editToken);
  editor = "new label";
  CHECK(identity.isCurrent(*nodeToken));
  borrowedLayer->setName("unrelated borrowed label");
  parent->value().setName("unrelated parent label");
  camera->setParameter("position", core::math::float3(1.f));
  CHECK(identity.isCurrent(*nodeToken));
  CHECK(identity.isCurrent(*layerToken));
  root->value().setName(editor);
  CHECK(reader == "new label");
  root->value().setName("Content Root");
  CHECK_FALSE(identity.isCurrent(*nodeToken));
  CHECK(identity.isCurrent(*layerToken));
  CHECK(identity.isCurrent(*editToken));
  std::string layerEditor = ownedLayer->name();
  layerEditor = "new layer label";
  ownedLayer->setName(layerEditor);
  ownedLayer->setName("owned");
  CHECK_FALSE(identity.isCurrent(*layerToken));
  CHECK(identity.isCurrent(*editToken));
}

TEST_CASE("Baseline writes have metadata history separate from transforms",
    "[integration][content][metadata]")
{
  scene::LayerNodeData value(nullptr, core::math::IDENTITY_MAT4, "transform");
  const auto before = value.metadataSequence();
  const auto edit = value.editVersion();
  const auto transform = value.transformVersion();
  value.setCurrentValueAsDefault();
  CHECK(value.metadataSequence() > before);
  CHECK(value.editVersion() == edit);
  CHECK(value.transformVersion() == transform);
  const auto baseline = value.metadataSequence();
  auto moved = core::math::IDENTITY_MAT4;
  moved[3].x = 2.f;
  value.setAsTransform(moved, moved);
  value.setAsTransform(core::math::IDENTITY_MAT4, core::math::IDENTITY_MAT4);
  CHECK(value.metadataSequence() > baseline);
  CHECK(value.isDefaultValue());
}

TEST_CASE("Payload assignment and move-out never rewind metadata history",
    "[integration][content][metadata]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  auto source = scene.insertChildTransformNode(layer->root());
  auto destination = scene.insertChildTransformNode(layer->root());
  for (int i = 0; i < 16; ++i)
    destination->value().setName("edited");
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  const auto srcHandle = identity.handleOf(source);
  const auto dstHandle = identity.handleOf(destination);
  const auto srcToken = identity.nodeMetadataVersion(srcHandle);
  const auto dstToken = identity.nodeMetadataVersion(dstHandle);
  REQUIRE(srcToken);
  REQUIRE(dstToken);
  destination->value() = source->value();
  CHECK(destination->value().metadataSequence() > dstToken->sequence);
  CHECK_FALSE(identity.isCurrent(*dstToken));
  CHECK(identity.isCurrent(*srcToken));
  const auto copiedToken = identity.nodeMetadataVersion(dstHandle);
  destination->value() = std::move(source->value());
  CHECK_FALSE(identity.isCurrent(*copiedToken));
  CHECK_FALSE(identity.isCurrent(*srcToken));
  const auto beforeMoveOut = identity.nodeMetadataVersion(dstHandle);
  scene::LayerNodeData moved(std::move(destination->value()));
  CHECK_FALSE(identity.isCurrent(*beforeMoveOut));
  CHECK(moved.metadataSequence() == beforeMoveOut->sequence);
  const auto beforeSelf = moved.metadataSequence();
  moved = moved;
  CHECK(moved.metadataSequence() > beforeSelf);
}

TEST_CASE(
    "Forest relocation preserves all histories while reuse revokes identity",
    "[integration][content][metadata]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  auto node = scene.insertChildTransformNode(layer->root());
  node->value().setName("retained");
  node->value().setInstanceParameter("user", core::Any(2.f));
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  const auto handle = identity.handleOf(node);
  const auto metadata = identity.nodeMetadataVersion(handle);
  const auto edit = identity.nodeEditVersion(handle);
  const auto transform = node->value().transformVersion();
  const auto lifetime = node->lifetime();
  const auto topology = node->topologyVersion();
  const auto capacity = layer->capacity();
  for (int i = 0; i < 256; ++i)
    scene.insertChildTransformNode(layer->root());
  REQUIRE(layer->capacity() > capacity);
  CHECK(identity.isCurrent(*metadata));
  CHECK(identity.isCurrent(*edit));
  CHECK(node->value().transformVersion() == transform);
  CHECK(node->lifetime() == lifetime);
  CHECK(node->topologyVersion() == topology);
  const auto index = node.index();
  layer->erase(node);
  auto replacement = layer->root()->insert_last_child_at(index,
      scene::LayerNodeData(layer, core::math::IDENTITY_MAT4, "retained"));
  REQUIRE(replacement);
  CHECK(replacement.index() == index);
  CHECK(replacement->lifetime() != lifetime);
  CHECK(identity.handleOf(replacement) != handle);
  CHECK_FALSE(identity.isCurrent(*metadata));
  const auto layerHandle = identity.handleOf(layer);
  const auto layerMetadata = identity.layerMetadataVersion(layerHandle);
  REQUIRE(layerMetadata);
  scene.removeLayer(layer);
  auto *newLayer = scene.defaultLayer();
  CHECK(identity.handleOf(newLayer) != layerHandle);
  CHECK_FALSE(identity.isCurrent(*layerMetadata));
}

TEST_CASE("Detached metadata histories outlive Scene owners without revival",
    "[integration][content][metadata]")
{
  auto sceneOwner = std::make_unique<scene::Scene>();
  auto *layer = sceneOwner->defaultLayer();
  auto node = sceneOwner->insertChildTransformNode(layer->root());
  node->value().setName("before death");
  scene::LayerNodeData copy(node->value());
  const auto before = copy.metadataSequence();
  auto retainedLayer =
      std::make_unique<scene::Layer>(sceneOwner.get(), "retained");
  const auto layerBeforeDeath = retainedLayer->metadataSequence();
  sceneOwner.reset();
  REQUIRE(retainedLayer->scene() == nullptr);
  retainedLayer->setName("after Scene death");
  CHECK(retainedLayer->metadataSequence() > layerBeforeDeath);
  CHECK(copy.layer() == nullptr);
  copy.setName("after death");
  copy.setInstanceParameter("user", core::Any(3.f));
  copy.clearInstanceParameters();
  copy.setCurrentValueAsDefault();
  CHECK(copy.metadataSequence() > before);
  const auto beforeMove = copy.metadataSequence();
  scene::LayerNodeData moved(std::move(copy));
  CHECK(copy.metadataSequence() > beforeMove);
  CHECK(moved.metadataSequence() == beforeMove);
  const auto beforeAssign = copy.metadataSequence();
  const auto sourceBeforeAssign = moved.metadataSequence();
  copy = std::move(moved);
  CHECK(copy.metadataSequence() > beforeAssign);
  CHECK(moved.metadataSequence() > sourceBeforeAssign);
  scene::Scene nextScene;
  const auto after = copy.metadataSequence();
  nextScene.defaultLayer()->root()->value() = copy;
  CHECK(copy.metadataSequence() == after);
  CHECK(copy.layer() == nullptr);
  scene::Layer detached(nullptr, "detached");
  const auto layerBefore = detached.metadataSequence();
  detached.setName("new name");
  detached.setName("detached");
  CHECK(detached.metadataSequence() > layerBefore);
}

namespace {
struct IdentityPublicationObserver : scene::EmptyUpdateDelegate
{
  IdentityPublicationObserver(integration::identity::SceneIdentity *identity,
      const integration::content::HdriHandles *handles)
      : identity(identity), handles(handles)
  {}
  void signalObjectAdded(const scene::Object *) override;
  integration::identity::SceneIdentity *identity{nullptr};
  const integration::content::HdriHandles *handles{nullptr};
  bool coherent{false};
  bool fail{false};
};

void IdentityPublicationObserver::signalObjectAdded(const scene::Object *)
{
  coherent = identity->resolve(handles->radiance)
      && identity->resolve(handles->light) && identity->resolve(handles->layer)
      && identity->resolve(handles->contentRoot)
      && identity->resolve(handles->lightNode);
  if (fail)
    throw std::runtime_error("injected identity consumer failure");
}

struct PublicationObserver : scene::EmptyUpdateDelegate
{
  explicit PublicationObserver(scene::Scene *scene) : scene(scene) {}
  void signalObjectAdded(const scene::Object *) override;
  scene::Scene *scene{nullptr};
  bool coherent{false};
  bool fail{false};
};

void PublicationObserver::signalObjectAdded(const scene::Object *)
{
  coherent = scene->numberOfObjects(ANARI_LIGHT) == 1
      && scene->numberOfObjects(ANARI_ARRAY) == 1
      && scene->defaultLayer()->size() == 3;
  if (fail)
    throw std::runtime_error("injected notification failure");
}

std::vector<std::uint8_t> hdrFixture()
{
  const std::string header =
      "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
  std::vector<std::uint8_t> bytes(header.begin(), header.end());
  const std::uint8_t pixels[] = {
      128, 64, 32, 132, 64, 128, 32, 132, 32, 64, 128, 130, 64, 32, 128, 130};
  bytes.insert(bytes.end(), std::begin(pixels), std::end(pixels));
  return bytes;
}

#ifdef __linux__
struct Assets
{
  Assets();
  ~Assets();
  void write(const std::string &name, const std::vector<std::uint8_t> &bytes);
  std::string parent;
  std::string root;
};

Assets::Assets()
{
  char pattern[] = "/tmp/vsr-hdri-test-XXXXXX";
  const auto *path = mkdtemp(pattern);
  REQUIRE(path);
  parent = path;
  root = parent + "/shared-assets";
  REQUIRE(mkdir(root.c_str(), 0755) == 0);
}

Assets::~Assets()
{
  std::filesystem::remove_all(parent);
}

void Assets::write(
    const std::string &name, const std::vector<std::uint8_t> &bytes)
{
  std::ofstream file(root + "/" + name, std::ios::binary);
  file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  REQUIRE(file.good());
}
#endif
} // namespace

TEST_CASE("Real Radiance HDR preserves linear precision and Light row order",
    "[integration][content]")
{
  const auto bytes = hdrFixture();
  auto decoded = integration::content::decodeHdri(bytes);
  REQUIRE(decoded.error == integration::content::HdriDecodeError::NONE);
  REQUIRE(decoded.image.width == 2);
  REQUIRE(decoded.image.height == 2);
  CHECK(decoded.image.radiance[0].x == .5f);
  CHECK(decoded.image.radiance[0].y == 1.f);
  CHECK(decoded.image.radiance[0].z == 2.f);
  CHECK(decoded.image.radiance[2].x == 8.f);
  CHECK(decoded.image.radiance[2].y == 4.f);
  CHECK(decoded.image.radiance[2].z == 2.f);
  scene::Scene scene;
  scene::detail::PreparedHdriContent prepared(
      scene, {}, "HDR source", 2, 2, decoded.image.radiance.data());
  CHECK(scene.numberOfLayers() == 0);
  REQUIRE(prepared.commit());
  CHECK(scene.numberOfLayers() == 1);
  auto radiance = scene.getObject<scene::Array>(prepared.arrayIndex());
  REQUIRE(radiance);
  CHECK(radiance->elementType() == ANARI_FLOAT32_VEC3);
  CHECK(radiance->dataAs<core::math::float3>()[2].x == 8.f);
}

TEST_CASE("Radiance scanline RLE has exact bounds and known HDR values",
    "[integration][content]")
{
  using namespace integration::content;
  const std::string header =
      "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n";
  std::vector<std::uint8_t> bytes(header.begin(), header.end());
  const std::uint8_t scanline[] = {
      2, 2, 0, 8, 136, 128, 136, 64, 136, 32, 136, 132};
  bytes.insert(bytes.end(), std::begin(scanline), std::end(scanline));
  auto decoded = decodeHdri(bytes);
  REQUIRE(decoded.error == HdriDecodeError::NONE);
  REQUIRE(decoded.image.radiance.size() == 8);
  for (const auto &pixel : decoded.image.radiance) {
    CHECK(pixel.x == 8.f);
    CHECK(pixel.y == 4.f);
    CHECK(pixel.z == 2.f);
  }
  bytes[header.size() + 4] = 137; // a nine-pixel run in an eight-pixel row
  CHECK(decodeHdri(bytes).error == HdriDecodeError::MALFORMED);
  bytes[header.size() + 4] = 0; // non-progressing run
  CHECK(decodeHdri(bytes).error == HdriDecodeError::MALFORMED);
}

TEST_CASE("HDR rejects malformed, unsupported, over-budget and cancelled input",
    "[integration][content]")
{
  using namespace integration::content;
  auto bytes = hdrFixture();
  bytes.pop_back();
  CHECK(decodeHdri(bytes).error == HdriDecodeError::MALFORMED);
  bytes = hdrFixture();
  bytes.push_back(0);
  CHECK(decodeHdri(bytes).error == HdriDecodeError::MALFORMED);
  HdriDecodeLimits limits;
  limits.pixels = 3;
  CHECK(decodeHdri(hdrFixture(), limits).error == HdriDecodeError::LIMIT);
  limits = {};
  limits.workingBytes = hdrFixture().capacity();
  CHECK(decodeHdri(hdrFixture(), limits).error == HdriDecodeError::LIMIT);
  CHECK(decodeHdri(hdrFixture(), {}, [] { return true; }).error
      == HdriDecodeError::CANCELLED);
  const std::string enormous =
      "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 65536 +X 65536\n";
  CHECK(decodeHdri(std::vector<std::uint8_t>(enormous.begin(), enormous.end()))
            .error
      == HdriDecodeError::LIMIT);
}

#ifdef __linux__
TEST_CASE(
    "Approved shared asset roots confine immutable input and check currency",
    "[integration][content]")
{
  using namespace integration::content;
  Assets assets;
  assets.write("sky.hdr", hdrFixture());
  AssetRoots roots;
  REQUIRE(roots.approve(assets.root) == 0);
  CHECK_FALSE(roots.approve(assets.root));
  CHECK_FALSE(roots.approve("relative"));
  auto snapshot = roots.read(0, "sky.hdr");
  REQUIRE(snapshot);
  REQUIRE(snapshot->isCurrent());
  CHECK_FALSE(roots.read(1, "sky.hdr"));
  CHECK_FALSE(roots.read(0, "../shared-assets/sky.hdr"));
  CHECK_FALSE(roots.read(0, "./sky.hdr"));
  CHECK_FALSE(roots.read(0, "/sky.hdr"));
  CHECK_FALSE(roots.read(0, "sky.hdr", 8));
  CHECK_FALSE(roots.read(
      0, "sky.hdr", AssetRoots::MAX_INPUT_BYTES, [] { return true; }));
  REQUIRE(symlink("sky.hdr", (assets.root + "/link.hdr").c_str()) == 0);
  CHECK_FALSE(roots.read(0, "link.hdr"));
  REQUIRE(symlink(".", (assets.root + "/directory-link").c_str()) == 0);
  CHECK_FALSE(roots.read(0, "directory-link/sky.hdr"));
  const auto decoded = decodeHdri(snapshot->bytes());
  REQUIRE(decoded.error == HdriDecodeError::NONE);
  auto changed = hdrFixture();
  changed.back() = 128;
  assets.write("sky.hdr", changed);
  CHECK_FALSE(snapshot->isCurrent());
  const auto expected = hdrFixture();
  CHECK(std::equal(snapshot->bytes().begin(),
      snapshot->bytes().end(),
      expected.begin(),
      expected.end()));
  auto latest = roots.read(0, "sky.hdr");
  REQUIRE(latest);
  REQUIRE(roots.revoke(0));
  CHECK_FALSE(latest->isCurrent());
  CHECK_FALSE(roots.read(0, "sky.hdr"));
}

TEST_CASE("Source snapshot backing remains charged through last weak owner",
    "[integration][content][capacity-retained-source]")
{
  using namespace integration::content;
  Assets assets;
  assets.write("sky.hdr", hdrFixture());
  std::atomic<size_t> live{0};
  bool deny = false;
  auto arena = std::make_shared<CapacityArena>(8192, [&](size_t bytes) {
    if (deny)
      return std::shared_ptr<const void>{};
    live += bytes;
    return std::shared_ptr<const void>(
        &live, [&, bytes](const void *) { live -= bytes; });
  });
  auto roots = std::make_unique<AssetRoots>();
  REQUIRE(roots->approve(assets.root));
  auto snapshot = roots->read(0, "sky.hdr", 4096, {}, arena);
  REQUIRE(snapshot);
  CHECK(live > snapshot->bytes().capacity() + sizeof(AssetRoots::Snapshot));
  const auto held = live.load();
  deny = true;
  CHECK_THROWS_AS(roots->read(0, "sky.hdr", 4096, {}, arena), std::bad_alloc);
  deny = false;
  CHECK(live == held);
  auto copy = snapshot;
  std::weak_ptr<AssetRoots::Snapshot> weak = snapshot;
  snapshot.reset();
  roots.reset();
  CHECK_FALSE(copy->isCurrent());
  CHECK(live == held);
  std::thread release([copy = std::move(copy)]() mutable { copy.reset(); });
  release.join();
  CHECK(weak.expired());
  CHECK(live > 0); // The coallocated control block still physically exists.
  CHECK(live < held);
  weak.reset();
  CHECK(live == 0);
  CHECK(arena->backingBytes() == 0);
}

TEST_CASE("Root pins survive renaming but not authority-owner teardown",
    "[integration][content]")
{
  using namespace integration::content;
  Assets assets;
  assets.write("sky.hdr", hdrFixture());
  auto roots = std::make_unique<AssetRoots>();
  REQUIRE(roots->approve(assets.root) == 0);
  auto snapshot = roots->read(0, "sky.hdr");
  REQUIRE(snapshot);
  REQUIRE(rename(assets.root.c_str(), (assets.parent + "/moved").c_str()) == 0);
  REQUIRE(mkdir(assets.root.c_str(), 0755) == 0);
  assets.write("sky.hdr", {1, 2, 3, 4});
  CHECK(snapshot->isCurrent());
  const auto pinnedRead = roots->read(0, "sky.hdr");
  REQUIRE(pinnedRead);
  const auto expected = hdrFixture();
  CHECK(std::equal(pinnedRead->bytes().begin(),
      pinnedRead->bytes().end(),
      expected.begin(),
      expected.end()));
  roots.reset();
  CHECK_FALSE(snapshot->isCurrent());
}
#endif

TEST_CASE("Backing capacity charges follow copies growth and physical release",
    "[integration][content][capacity]")
{
  using namespace integration::content;
  struct Accounting
  {
    std::atomic<size_t> live{0};
    std::atomic<size_t> peak{0};
  };
  auto account = std::make_shared<Accounting>();
  auto arena = std::make_shared<CapacityArena>(8192, [account](size_t bytes) {
    const auto live = account->live.fetch_add(bytes) + bytes;
    account->peak.store(std::max(account->peak.load(), live));
    return std::shared_ptr<const void>(account.get(),
        [account, bytes](const void *) { account->live.fetch_sub(bytes); });
  });
  AssetRoots::Bytes bytes(256, 42, CapacityAllocator<std::uint8_t>(arena));
  const auto original = arena->backingBytes();
  REQUIRE(original >= 256);
  REQUIRE(original < 512); // Small bounded in-allocation owner, not 128 MiB.
  REQUIRE(account->live == original);
  auto copied = bytes;
  CHECK(arena->backingBytes() == original * 2);
  CHECK(copied.get_allocator().arena() == arena);
  bytes.reserve(1024);
  CHECK(bytes.size() == 256);
  CHECK(bytes.front() == 42);
  CHECK(arena->allocatorPeak() > arena->backingBytes());
  CHECK(arena->allocatorPeak() == account->peak);
  const auto held = arena->backingBytes();
  CHECK_THROWS_AS(bytes.reserve(8192), std::bad_alloc);
  CHECK(arena->backingBytes() == held);
  CHECK(bytes.size() == 256);
  CHECK(bytes.front() == 42);
  bytes.clear();
  CHECK(arena->backingBytes() == held); // Logical clear keeps capacity alive.
  AssetRoots::Bytes().swap(bytes);
  CHECK(arena->backingBytes() == original);
  std::weak_ptr<CapacityArena> weakArena = arena;
  arena.reset();
  REQUIRE_FALSE(weakArena.expired()); // A physical copy outlives its initiator.
  std::thread release(
      [copy = std::move(copied)]() mutable { AssetRoots::Bytes().swap(copy); });
  release.join();
  CHECK(weakArena.expired());
  CHECK(account->live == 0);
}

TEST_CASE("Bounded Layer label interning reserves before registry adoption",
    "[integration][content][capacity-retained-token]")
{
  const std::string label = "capacity-retained-layer-intern-control";
  size_t admitted = 0;
  CHECK_FALSE(core::Token::internBounded(label, [&](size_t bytes) {
    admitted = bytes;
    return false;
  }));
  REQUIRE(admitted > label.size());
  const auto token =
      core::Token::internBounded(label, [](size_t) { return true; });
  REQUIRE(token);
  CHECK(token->str() == label);
  // An existing intern requires neither a copy nor another registry lease.
  CHECK(core::Token::internBounded(label, [](size_t) {
    FAIL("existing intern unexpectedly allocates");
    return false;
  }) == token);
  CHECK_FALSE(core::Token::internBounded(std::string(257, 'x'), [](size_t) {
    FAIL("unsupported label allocated before limit refusal");
    return true;
  }));
}

TEST_CASE("HDR radiance writes have authoritative change-back history",
    "[integration][content]")
{
  scene::Scene scene;
  auto sequence = std::make_shared<scene::ArrayWriteSequence>();
  REQUIRE(scene.trackImageWrites(sequence));
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  auto radiance = scene.createArray(ANARI_FLOAT32_VEC3, 2, 2);
  const auto handle = identity.handleOf(radiance.data());
  REQUIRE(radiance->supportsHostWriteTracking());
  const auto before = identity.arrayDataVersion(handle);
  REQUIRE(before);
  radiance->mapAs<core::math::float3>()[0] = {8.f, 4.f, 2.f};
  radiance->unmap();
  const auto after = identity.arrayDataVersion(handle);
  REQUIRE(after);
  CHECK(after != before);
  radiance->mapAs<core::math::float3>()[0] = {0.f, 0.f, 0.f};
  radiance->unmap();
  CHECK(identity.arrayDataVersion(handle) != before);
  CHECK(identity.arrayDataVersion(handle) != after);
  scene.removeObject(radiance.data());
  CHECK_FALSE(identity.arrayDataVersion(handle));
}

TEST_CASE("Detached HDRI preparation is invisible until coherent publication",
    "[integration][content]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  const auto before = identity.sceneRevision();
  const auto arrays = scene.numberOfObjects(ANARI_ARRAY);
  const auto lights = scene.numberOfObjects(ANARI_LIGHT);
  const auto nodes = layer->size();
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}, {2.f, 1.f, .5f}};
  {
    scene::detail::PreparedHdriContent abandoned(
        scene, layer->root(), "abandoned", 2, 1, data);
    CHECK(scene.numberOfObjects(ANARI_ARRAY) == arrays);
    CHECK(scene.numberOfObjects(ANARI_LIGHT) == lights);
    CHECK(layer->size() == nodes);
    CHECK(identity.sceneRevision() == before);
  }
  CHECK(identity.sceneRevision() == before);
  scene::detail::PreparedHdriContent prepared(
      scene, layer->root(), "Content Root", 2, 1, data);
  CHECK_FALSE(scene.getObject<scene::Array>(prepared.arrayIndex()));
  CHECK_FALSE(scene.getObject<scene::Light>(prepared.lightIndex()));
  REQUIRE(prepared.isCurrent());
  REQUIRE(prepared.commit());
  CHECK_FALSE(prepared.commit());
  CHECK(scene.numberOfObjects(ANARI_ARRAY) == arrays + 1);
  CHECK(scene.numberOfObjects(ANARI_LIGHT) == lights + 1);
  CHECK(layer->size() == nodes + 2);
  auto array = scene.getObject<scene::Array>(prepared.arrayIndex());
  auto light = scene.getObject<scene::Light>(prepared.lightIndex());
  REQUIRE(array);
  REQUIRE(light);
  CHECK(array->dataAs<core::math::float3>()[0].x == 8.f);
  CHECK(light->parameterValueAs<float>("scale") == 1.f);
  CHECK(light->parameterValueAsObject("radiance") == array.data());
  CHECK(array->useCount(scene::Object::UseKind::PARAMETER) == 1);
  CHECK(light->useCount(scene::Object::UseKind::LAYER) == 1);
  prepared.notify();
  CHECK(identity.handleOf(array.data()).valid());
  CHECK(identity.handleOf(light.data()).valid());
  CHECK(prepared.root()->value().name() == "Content Root");
  CHECK(prepared.lightNode()->parent() == prepared.root());
}

TEST_CASE("HDRI post-publication callback failure is committed and coherent",
    "[integration][content]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  auto *observer = scene.updateDelegate().emplace<PublicationObserver>(&scene);
  observer->fail = true;
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  scene::detail::PreparedHdriContent prepared(
      scene, layer->root(), "root", 1, 1, data);
  REQUIRE(prepared.commit());
  CHECK_THROWS(prepared.notify());
  CHECK(prepared.committed());
  CHECK(observer->coherent);
  CHECK(identity
          .handleOf(scene.getObject<scene::Light>(prepared.lightIndex()).data())
          .valid());
  CHECK(identity
          .handleOf(scene.getObject<scene::Array>(prepared.arrayIndex()).data())
          .valid());
  // Terminal notification failure is not retried.
  CHECK_NOTHROW(prepared.notify());
}

TEST_CASE("Abandoned HDRI pool preparation preserves existing write histories",
    "[integration][content]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  auto light = scene.createObject<scene::Light>(scene::tokens::light::point);
  light->setName("keep this label");
  light->setParameter("intensity", 3.f);
  const auto metadata = light->lastMetadataChange();
  const auto parameters = light->lastParameterChange();
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  {
    scene::detail::PreparedHdriContent abandoned(
        scene, layer->root(), "root", 1, 1, data);
    CHECK(light->lastMetadataChange() == metadata);
    CHECK(light->lastParameterChange() == parameters);
    CHECK(light->name() == "keep this label");
  }
  CHECK(light->lastMetadataChange() == metadata);
}

TEST_CASE("Prepared HDRI destination evidence is scoped and historical",
    "[integration][content]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  auto parent = scene.insertChildNode(layer->root(), "parent");
  auto camera =
      scene.createObject<scene::Camera>(scene::tokens::camera::perspective);
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  scene::detail::PreparedHdriContent prepared(
      scene, parent, "root", 1, 1, data);
  camera->setParameter("position", core::math::float3(1.f, 2.f, 3.f));
  REQUIRE(prepared.isCurrent());
  parent->value().setEnabled(false);
  parent->value().setEnabled(true);
  CHECK_FALSE(prepared.isCurrent());
  CHECK_FALSE(prepared.commit());
  CHECK(scene.numberOfObjects(ANARI_LIGHT) == 0);
}

TEST_CASE("New HDRI Layer remains private and does not replace a namesake",
    "[integration][content]")
{
  scene::Scene scene;
  auto *baseline = scene.defaultLayer();
  const auto count = scene.numberOfLayers();
  const core::math::float3 data[] = {{16.f, 8.f, 4.f}};
  scene::detail::PreparedHdriContent prepared(
      scene, {}, "Environment", 1, 1, data, {}, "new HDRI Layer");
  CHECK(scene.numberOfLayers() == count);
  CHECK_FALSE(scene.layer("new HDRI Layer"));
  REQUIRE(prepared.commit());
  CHECK(scene.numberOfLayers() == count + 1);
  CHECK(scene.defaultLayer() == baseline);
  CHECK(scene.layer("new HDRI Layer") == prepared.ownedLayer());
  CHECK_THROWS(scene::detail::PreparedHdriContent(
      scene, {}, "other", 1, 1, data, {}, "new HDRI Layer"));
}

TEST_CASE(
    "Prepared HDRI identities remain private and stable through callbacks",
    "[integration][content]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  integration::content::HdriHandles handles;
  auto *observer = scene.updateDelegate().emplace<IdentityPublicationObserver>(
      &identity, &handles);
  const auto revision = identity.sceneRevision();
  const auto inspection = identity.inspectionRevision();
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  integration::content::PreparedHdriPublication prepared(
      identity, layer->root(), "root", 1, 1, data);
  handles = prepared.handles();
  CHECK_FALSE(identity.resolve(handles.radiance));
  CHECK_FALSE(identity.resolve(handles.light));
  CHECK_FALSE(identity.resolve(handles.contentRoot));
  CHECK_FALSE(identity.resolve(handles.lightNode));
  CHECK(identity.resolve(handles.layer) == layer);
  CHECK(identity.sceneRevision() == revision);
  CHECK(identity.inspectionRevision() == inspection);
  const auto receipt = prepared.inventoryReceipt();
  REQUIRE(prepared.commit());
  CHECK(identity.resolve(handles.radiance));
  CHECK(identity.resolve(handles.light));
  CHECK(identity.handleOf(identity.resolve(handles.contentRoot))
      == handles.contentRoot);
  CHECK(identity.handleOf(identity.resolve(handles.lightNode))
      == handles.lightNode);
  const auto lightVersion = identity.parameterVersion(handles.light);
  REQUIRE(lightVersion);
  prepared.notify();
  CHECK(observer->coherent);
  CHECK(identity.parameterVersion(handles.light) == lightVersion);
  CHECK(prepared.inventoryReceipt() == receipt);
  CHECK_FALSE(prepared.commit());
}

TEST_CASE(
    "Abandoned HDRI identity preparation does not publish or reuse handles",
    "[integration][content]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  auto camera =
      scene.createObject<scene::Camera>(scene::tokens::camera::perspective);
  const auto cameraHandle = identity.handleOf(camera.data());
  const auto cameraVersion = identity.parameterVersion(cameraHandle);
  const auto revision = identity.sceneRevision();
  const auto inspection = identity.inspectionRevision();
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  integration::content::HdriHandles abandoned;
  {
    integration::content::PreparedHdriPublication prepared(
        identity, {}, "root", 1, 1, data, {}, "owned Layer");
    abandoned = prepared.handles();
    CHECK_FALSE(identity.resolve(abandoned.layer));
    CHECK_FALSE(identity.resolve(abandoned.light));
  }
  CHECK(identity.sceneRevision() == revision);
  CHECK(identity.inspectionRevision() == inspection);
  CHECK(identity.parameterVersion(cameraHandle) == cameraVersion);
  CHECK(scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(scene.numberOfLayers() == 1);
  integration::content::PreparedHdriPublication replacement(
      identity, {}, "root", 1, 1, data, {}, "owned Layer");
  REQUIRE(replacement.commit());
  replacement.notify();
  CHECK(replacement.handles().layer != abandoned.layer);
  CHECK(replacement.handles().light != abandoned.light);
  CHECK(replacement.handles().contentRoot != abandoned.contentRoot);
  CHECK_FALSE(identity.resolve(abandoned.layer));
  CHECK_FALSE(identity.resolve(abandoned.light));
  CHECK_FALSE(identity.resolve(abandoned.contentRoot));
}

TEST_CASE(
    "Prepared HDRI handles survive owned-Layer notification failure and defrag",
    "[integration][content]")
{
  scene::Scene scene;
  scene.defaultLayer();
  auto gap = scene.createObject<scene::Light>(scene::tokens::light::point);
  scene.createObject<scene::Light>(scene::tokens::light::point);
  scene.removeObject(gap.data());
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  integration::content::HdriHandles handles;
  auto *observer = scene.updateDelegate().emplace<IdentityPublicationObserver>(
      &identity, &handles);
  observer->fail = true;
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  integration::content::PreparedHdriPublication prepared(
      identity, {}, "root", 1, 1, data, {}, "owned Layer");
  handles = prepared.handles();
  REQUIRE(prepared.commit());
  CHECK_THROWS(prepared.notify());
  CHECK(prepared.committed());
  CHECK(observer->coherent);
  CHECK_NOTHROW(prepared.notify());
  scene.defragmentObjectStorage();
  REQUIRE(identity.resolve(handles.light));
  REQUIRE(identity.resolve(handles.radiance));
  CHECK(identity.handleOf(identity.resolve(handles.light)) == handles.light);
  CHECK(identity.handleOf(identity.resolve(handles.contentRoot))
      == handles.contentRoot);
  CHECK(identity.resolve(handles.light)->parameterValueAsObject("radiance")
      == identity.resolve(handles.radiance));
}

TEST_CASE("Prepared HDRI node predictions refuse intervening membership churn",
    "[integration][content]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  auto gap = scene.insertChildNode(layer->root(), "gap");
  scene.removeNode(gap);
  integration::identity::IdentityAuthority authority;
  integration::identity::SceneIdentity identity(authority, scene);
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  integration::content::PreparedHdriPublication prepared(
      identity, layer->root(), "root", 1, 1, data);
  const auto before = layer->capacity();
  auto reused = scene.insertChildNode(layer->root(), "reused");
  scene.removeNode(reused);
  CHECK(layer->capacity() == before);
  CHECK_FALSE(prepared.isCurrent());
  CHECK_FALSE(prepared.commit());
  CHECK_FALSE(identity.resolve(prepared.handles().light));
  CHECK(scene.numberOfObjects(ANARI_LIGHT) == 0);
}

TEST_CASE("Prepared HDRI identity owner teardown cannot publish staging",
    "[integration][content]")
{
  scene::Scene scene;
  auto *layer = scene.defaultLayer();
  integration::identity::IdentityAuthority authority;
  auto identity =
      std::make_unique<integration::identity::SceneIdentity>(authority, scene);
  const core::math::float3 data[] = {{8.f, 4.f, 2.f}};
  integration::content::PreparedHdriPublication prepared(
      *identity, layer->root(), "root", 1, 1, data);
  identity.reset();
  CHECK_FALSE(prepared.isCurrent());
  CHECK_FALSE(prepared.commit());
  CHECK(scene.numberOfObjects(ANARI_LIGHT) == 0);
  CHECK(scene.numberOfObjects(ANARI_ARRAY) == 0);
}

TEST_CASE("Prepared HDRI content refuses retired destination and dead Scene",
    "[integration][content]")
{
  const core::math::float3 data[] = {{2.f, 3.f, 4.f}};
  auto scene = std::make_unique<scene::Scene>();
  auto *layer = scene->addLayer("retired");
  auto prepared = std::make_unique<scene::detail::PreparedHdriContent>(
      *scene, layer->root(), "root", 1, 1, data);
  scene->removeLayer(layer);
  CHECK_FALSE(prepared->isCurrent());
  CHECK_FALSE(prepared->commit());
  scene.reset();
  CHECK_FALSE(prepared->isCurrent());
  CHECK_FALSE(prepared->commit());
}

namespace {
struct RetainedCallbackProbe
{
  std::shared_ptr<size_t> copies;
  std::vector<unsigned char> capture;
  explicit RetainedCallbackProbe(std::shared_ptr<size_t> count);
  RetainedCallbackProbe(const RetainedCallbackProbe &other);
  RetainedCallbackProbe(RetainedCallbackProbe &&) noexcept = default;
  std::shared_ptr<const void> operator()() const;
};

RetainedCallbackProbe::RetainedCallbackProbe(std::shared_ptr<size_t> count)
    : copies(std::move(count)), capture(1u << 20, 7)
{}

RetainedCallbackProbe::RetainedCallbackProbe(const RetainedCallbackProbe &other)
    : copies(other.copies), capture(other.capture)
{
  ++*copies;
}

std::shared_ptr<const void> RetainedCallbackProbe::operator()() const
{
  return std::make_shared<int>(1);
}

struct ResultTestAuthority
{
  std::shared_ptr<const void> reserve(size_t) const;
};

std::shared_ptr<const void> ResultTestAuthority::reserve(size_t) const
{
  return std::make_shared<int>(0);
}
} // namespace

TEST_CASE("Result owner copies never clone an opaque reservation capture",
    "[integration][content][capacity-owner-final-callback]")
{
  using integration::operations::ResultOwnership;
  auto copies = std::make_shared<size_t>(0);
  ResultOwnership original{RetainedCallbackProbe(copies)};
  const auto installedCopies = *copies;
  ResultOwnership::Reserve factory = original.reserve;
  ResultOwnership copy(original);
  ResultOwnership assigned;
  assigned = copy;
  CHECK(*copies == installedCopies);
  CHECK(original.lease);
  CHECK(copy.lease);
  CHECK(assigned.lease);
  CHECK(original.lease != copy.lease);
  CHECK(copy.lease != assigned.lease);
  std::thread release([&] {
    original = {};
    copy = {};
  });
  release.join();
  CHECK(factory());
  CHECK(assigned.lease);
  CHECK(*copies == installedCopies);
}

TEST_CASE(
    "Bounded result factories retain their original authority until last owner",
    "[integration][content][capacity-owner-final-callback]")
{
  using integration::operations::Reservation;
  using integration::operations::ResultOwnership;
  auto authority = std::make_shared<ResultTestAuthority>();
  std::weak_ptr<ResultTestAuthority> life = authority;
  ResultOwnership first{
      ResultOwnership::Reserve(Reservation::from(authority), 31)};
  auto copiedFactory = first.reserve;
  ResultOwnership second(first);
  authority.reset();
  first = {};
  CHECK_FALSE(life.expired());
  std::thread release([&] { second = {}; });
  release.join();
  CHECK_FALSE(life.expired());
  CHECK(copiedFactory());
  copiedFactory = {};
  CHECK(life.expired());
}

TEST_CASE(
    "Root capacity controls remain charged through last weak off-thread owner",
    "[integration][content][capacity-owner-final-root]")
{
  using integration::content::CapacityAllocator;
  using integration::content::CapacityArena;
  using integration::operations::Reservation;
  auto budget = std::make_shared<rendering::FrameBudget>(8192);
  const auto baseline = budget->used();
  auto arena = CapacityArena::create(8192, Reservation::from(budget));
  std::weak_ptr<CapacityArena> weak = arena;
  const auto rootBacking = budget->used();
  CHECK(rootBacking > baseline + sizeof(CapacityArena));
  {
    std::vector<int, CapacityAllocator<int>> values{
        CapacityAllocator<int>(arena)};
    values.reserve(32);
    CHECK(arena->backingBytes() >= 32 * sizeof(int));
    CHECK(budget->used() > rootBacking);
  }
  CHECK(budget->used() == rootBacking);
  arena.reset();
  CHECK(weak.expired());
  CHECK(budget->used() == rootBacking); // The control allocation still exists.
  std::thread release([&] { weak.reset(); });
  release.join();
  CHECK(budget->used() == baseline);

  auto refused = std::make_shared<rendering::FrameBudget>(
      sizeof(rendering::FrameBudget) + 256);
  const auto refusedBaseline = refused->used();
  CHECK_THROWS_AS(
      CapacityArena::create(8192, Reservation::from(refused)), std::bad_alloc);
  CHECK(refused->used() == refusedBaseline);
}
