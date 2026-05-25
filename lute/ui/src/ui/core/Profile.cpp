#include "lute/ui/Profile.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace lute::ui
{

namespace
{

thread_local FrameProfile* activeFrame = nullptr;

uint64_t nowNs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string_view phaseName(ProfilePhase phase)
{
    switch (phase)
    {
    case ProfilePhase::Flush:
        return "flush";
    case ProfilePhase::Input:
        return "input";
    case ProfilePhase::Layout:
        return "layout";
    case ProfilePhase::Text:
        return "text";
    case ProfilePhase::Scene:
        return "scene";
    case ProfilePhase::Semantics:
        return "semantics";
    case ProfilePhase::Render:
        return "render";
    case ProfilePhase::PipelineSetup:
        return "pipeline_setup";
    case ProfilePhase::SurfaceAcquire:
        return "surface_acquire";
    case ProfilePhase::RenderPrepare:
        return "render_prepare";
    case ProfilePhase::Upload:
        return "upload";
    case ProfilePhase::RenderEncode:
        return "render_encode";
    case ProfilePhase::Submit:
        return "submit";
    case ProfilePhase::Present:
        return "present";
    case ProfilePhase::Count:
        break;
    }

    return "unknown";
}

double nsToMs(uint64_t ns)
{
    return static_cast<double>(ns) / 1000000.0;
}

void dumpPhase(std::ostringstream& out, const FrameProfile& frame, ProfilePhase phase)
{
    uint64_t duration = frame.phaseDurationNs(phase);
    if (duration == 0)
        return;

    out << " " << phaseName(phase) << "=" << std::fixed << std::setprecision(3) << nsToMs(duration) << "ms";
}

FrameCounters* activeCounters()
{
    return activeFrame ? &activeFrame->counters : nullptr;
}

} // namespace

uint64_t FrameProfile::durationNs() const
{
    return endNs > startNs ? endNs - startNs : 0;
}

uint64_t FrameProfile::phaseDurationNs(ProfilePhase phase) const
{
    return phaseNs[static_cast<size_t>(phase)];
}

ProfileStore::ProfileStore(size_t capacity)
    : capacity(std::max<size_t>(1, capacity))
{
}

void ProfileStore::push(FrameProfile profile)
{
    if (frames.size() < capacity)
    {
        frames.push_back(std::move(profile));
        nextIndex = frames.size() % capacity;
        return;
    }

    frames[nextIndex] = std::move(profile);
    nextIndex = (nextIndex + 1) % capacity;
    wrapped = true;
}

std::optional<FrameProfile> ProfileStore::latest() const
{
    if (frames.empty())
        return std::nullopt;

    size_t index = nextIndex == 0 ? frames.size() - 1 : nextIndex - 1;
    if (wrapped)
        index = (nextIndex + capacity - 1) % capacity;

    return frames[index];
}

std::vector<FrameProfile> ProfileStore::recent(size_t limit) const
{
    std::vector<FrameProfile> result;
    if (frames.empty() || limit == 0)
        return result;

    size_t count = std::min(limit, frames.size());
    result.reserve(count);

    for (size_t i = 0; i < count; i++)
    {
        size_t age = count - 1 - i;
        size_t index = nextIndex >= age + 1 ? nextIndex - age - 1 : frames.size() + nextIndex - age - 1;
        result.push_back(frames[index]);
    }

    return result;
}

std::string ProfileStore::dump(size_t limit) const
{
    std::ostringstream out;
    out << "UI Profile frames=" << frames.size() << "\n";

    for (const FrameProfile& frame : recent(limit))
    {
        out << "Frame " << frame.id << " label=\"" << frame.label << "\" cpu=" << std::fixed << std::setprecision(3) << nsToMs(frame.durationNs())
            << "ms";
        dumpPhase(out, frame, ProfilePhase::Flush);
        dumpPhase(out, frame, ProfilePhase::Input);
        dumpPhase(out, frame, ProfilePhase::Layout);
        dumpPhase(out, frame, ProfilePhase::Text);
        dumpPhase(out, frame, ProfilePhase::Scene);
        dumpPhase(out, frame, ProfilePhase::Semantics);
        dumpPhase(out, frame, ProfilePhase::Render);
        dumpPhase(out, frame, ProfilePhase::PipelineSetup);
        dumpPhase(out, frame, ProfilePhase::SurfaceAcquire);
        dumpPhase(out, frame, ProfilePhase::RenderPrepare);
        dumpPhase(out, frame, ProfilePhase::Upload);
        dumpPhase(out, frame, ProfilePhase::RenderEncode);
        dumpPhase(out, frame, ProfilePhase::Submit);
        dumpPhase(out, frame, ProfilePhase::Present);

        if (!frame.renderBackend.empty())
            out << " backend=" << frame.renderBackend;
        if (frame.sceneGeneration != 0)
            out << " sceneGen=" << frame.sceneGeneration;

        out << "\n";
        out << "  counters"
            << " dirty=" << frame.counters.dirtyMarks
            << " layoutMeasured=" << frame.counters.layoutNodesMeasured
            << " layoutPlaced=" << frame.counters.layoutNodesPlaced
            << " textMeasured=" << frame.counters.textRunsMeasured
            << " textRendered=" << frame.counters.textRunsRendered
            << " glyphs=" << frame.counters.glyphsRendered
            << " sceneNodes=" << frame.counters.sceneNodesUpdated
            << " sceneItems=" << frame.counters.sceneItemsEmitted
            << " semanticNodes=" << frame.counters.semanticNodesUpdated
            << " displayItems=" << frame.counters.displayItemsRendered
            << " renderCalls=" << frame.counters.renderCalls
            << " drawCalls=" << frame.counters.drawCalls
            << " uploads=" << frame.counters.bufferUploads
            << " uploadBytes=" << frame.counters.bytesUploaded << "\n";
    }

    return out.str();
}

size_t ProfileStore::size() const
{
    return frames.size();
}

ProfileFrameScope::ProfileFrameScope(ProfileStore& store, ProfileFrameId id, std::string label)
    : store(store)
{
    profile.id = id;
    profile.label = std::move(label);
    profile.startNs = nowNs();
    previous = activeFrame;
    activeFrame = &profile;
}

ProfileFrameScope::~ProfileFrameScope()
{
    profile.endNs = nowNs();
    activeFrame = previous;
    store.push(std::move(profile));
}

ProfileZone::ProfileZone(ProfilePhase phase)
    : phase(phase)
{
    active = activeFrame != nullptr;
    if (active)
        startNs = nowNs();
}

ProfileZone::~ProfileZone()
{
    if (!active)
        return;

    UiProfiler::addPhaseTime(phase, nowNs() - startNs);
}

bool UiProfiler::isFrameActive()
{
    return activeFrame != nullptr;
}

void UiProfiler::addPhaseTime(ProfilePhase phase, uint64_t durationNs)
{
    if (!activeFrame)
        return;

    activeFrame->phaseNs[static_cast<size_t>(phase)] += durationNs;
}

void UiProfiler::addDirtyMark()
{
    if (FrameCounters* counters = activeCounters())
        counters->dirtyMarks++;
}

void UiProfiler::addLayoutNodeMeasured()
{
    if (FrameCounters* counters = activeCounters())
        counters->layoutNodesMeasured++;
}

void UiProfiler::addLayoutNodePlaced()
{
    if (FrameCounters* counters = activeCounters())
        counters->layoutNodesPlaced++;
}

void UiProfiler::addTextRunMeasured()
{
    if (FrameCounters* counters = activeCounters())
        counters->textRunsMeasured++;
}

void UiProfiler::addTextRunRendered()
{
    if (FrameCounters* counters = activeCounters())
        counters->textRunsRendered++;
}

void UiProfiler::addGlyphsRendered(uint32_t count)
{
    if (FrameCounters* counters = activeCounters())
        counters->glyphsRendered += count;
}

void UiProfiler::addSceneNodeUpdated(uint32_t emittedItems)
{
    if (FrameCounters* counters = activeCounters())
    {
        counters->sceneNodesUpdated++;
        counters->sceneItemsEmitted += emittedItems;
    }
}

void UiProfiler::addSemanticNodeUpdated()
{
    if (FrameCounters* counters = activeCounters())
        counters->semanticNodesUpdated++;
}

void UiProfiler::addDisplayItemsRendered(size_t count)
{
    if (FrameCounters* counters = activeCounters())
        counters->displayItemsRendered += static_cast<uint32_t>(std::min<size_t>(count, UINT32_MAX));
}

void UiProfiler::addRenderCall()
{
    if (FrameCounters* counters = activeCounters())
        counters->renderCalls++;
}

void UiProfiler::addDrawCall()
{
    if (FrameCounters* counters = activeCounters())
        counters->drawCalls++;
}

void UiProfiler::addBufferUpload(uint64_t bytes)
{
    if (FrameCounters* counters = activeCounters())
    {
        counters->bufferUploads++;
        counters->bytesUploaded += bytes;
    }
}

void UiProfiler::setRenderStats(std::string backend, size_t displayItemCount, uint64_t sceneGeneration)
{
    if (!activeFrame)
        return;

    activeFrame->renderBackend = std::move(backend);
    activeFrame->displayItemCount = displayItemCount;
    activeFrame->sceneGeneration = sceneGeneration;
    activeFrame->counters.displayItemsRendered = static_cast<uint32_t>(std::min<size_t>(displayItemCount, UINT32_MAX));
}

} // namespace lute::ui
