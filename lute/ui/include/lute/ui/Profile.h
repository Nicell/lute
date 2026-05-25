#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lute::ui
{

using ProfileFrameId = uint64_t;

enum class ProfilePhase : uint8_t
{
    Flush,
    Input,
    Layout,
    Text,
    Scene,
    Semantics,
    Render,
    PipelineSetup,
    SurfaceAcquire,
    RenderPrepare,
    Upload,
    RenderEncode,
    Submit,
    Present,
    Count,
};

struct FrameCounters
{
    uint32_t dirtyMarks = 0;
    uint32_t layoutNodesMeasured = 0;
    uint32_t layoutNodesPlaced = 0;
    uint32_t textRunsMeasured = 0;
    uint32_t textRunsRendered = 0;
    uint32_t glyphsRendered = 0;
    uint32_t sceneNodesUpdated = 0;
    uint32_t sceneItemsEmitted = 0;
    uint32_t semanticNodesUpdated = 0;
    uint32_t displayItemsRendered = 0;
    uint32_t renderCalls = 0;
    uint32_t drawCalls = 0;
    uint32_t bufferUploads = 0;
    uint64_t bytesUploaded = 0;
};

struct FrameProfile
{
    ProfileFrameId id = 0;
    std::string label;
    uint64_t startNs = 0;
    uint64_t endNs = 0;
    std::array<uint64_t, static_cast<size_t>(ProfilePhase::Count)> phaseNs = {};
    FrameCounters counters;
    std::string renderBackend;
    size_t displayItemCount = 0;
    uint64_t sceneGeneration = 0;

    uint64_t durationNs() const;
    uint64_t phaseDurationNs(ProfilePhase phase) const;
};

class ProfileStore
{
public:
    explicit ProfileStore(size_t capacity = 512);

    void push(FrameProfile profile);
    std::optional<FrameProfile> latest() const;
    std::vector<FrameProfile> recent(size_t limit) const;
    std::string dump(size_t limit = 8) const;
    size_t size() const;

private:
    std::vector<FrameProfile> frames;
    size_t capacity = 0;
    size_t nextIndex = 0;
    bool wrapped = false;
};

class ProfileFrameScope
{
public:
    ProfileFrameScope(ProfileStore& store, ProfileFrameId id, std::string label);
    ProfileFrameScope(const ProfileFrameScope&) = delete;
    ProfileFrameScope& operator=(const ProfileFrameScope&) = delete;
    ~ProfileFrameScope();

private:
    ProfileStore& store;
    FrameProfile profile;
    FrameProfile* previous = nullptr;
};

class ProfileZone
{
public:
    explicit ProfileZone(ProfilePhase phase);
    ProfileZone(const ProfileZone&) = delete;
    ProfileZone& operator=(const ProfileZone&) = delete;
    ~ProfileZone();

private:
    ProfilePhase phase;
    uint64_t startNs = 0;
    bool active = false;
};

class UiProfiler
{
public:
    static bool isFrameActive();
    static void addPhaseTime(ProfilePhase phase, uint64_t durationNs);
    static void addDirtyMark();
    static void addLayoutNodeMeasured();
    static void addLayoutNodePlaced();
    static void addTextRunMeasured();
    static void addTextRunRendered();
    static void addGlyphsRendered(uint32_t count);
    static void addSceneNodeUpdated(uint32_t emittedItems);
    static void addSemanticNodeUpdated();
    static void addDisplayItemsRendered(size_t count);
    static void addRenderCall();
    static void addDrawCall();
    static void addBufferUpload(uint64_t bytes);
    static void setRenderStats(std::string backend, size_t displayItemCount, uint64_t sceneGeneration);
};

} // namespace lute::ui
