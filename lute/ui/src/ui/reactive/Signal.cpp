#include "lute/ui/Signal.h"

#include <vector>

namespace lute::ui
{

ReactiveSource::~ReactiveSource()
{
    for (Effect* effect : observers)
        effect->dependencies.erase(this);
}

void ReactiveGraph::beginCollect(Effect* effect)
{
    currentEffect = effect;
}

void ReactiveGraph::endCollect(Effect* effect)
{
    if (currentEffect == effect)
        currentEffect = nullptr;
}

void ReactiveGraph::observe(ReactiveSource& source)
{
    if (!currentEffect || currentEffect->disposed)
        return;

    source.observers.insert(currentEffect);
    currentEffect->dependencies.insert(&source);
}

void ReactiveGraph::notify(ReactiveSource& source)
{
    std::vector<Effect*> observers(source.observers.begin(), source.observers.end());
    for (Effect* effect : observers)
    {
        if (!effect->disposed && !effect->isRunning())
            enqueue(effect);
    }
}

void ReactiveGraph::beginTransaction()
{
    transactionDepth++;
}

void ReactiveGraph::commitTransaction()
{
    if (transactionDepth == 0)
        return;

    transactionDepth--;
    if (transactionDepth != 0)
        return;

    while (!pendingEffects.empty())
    {
        std::vector<Effect*> pending(pendingEffects.begin(), pendingEffects.end());
        pendingEffects.clear();

        for (Effect* effect : pending)
        {
            if (!effect->disposed)
                effect->run();
        }
    }
}

void ReactiveGraph::detach(Effect* effect)
{
    pendingEffects.erase(effect);
}

void ReactiveGraph::enqueue(Effect* effect)
{
    if (transactionDepth > 0)
    {
        pendingEffects.insert(effect);
        return;
    }

    effect->run();
}

Effect::Effect(ReactiveGraph& graph, std::function<void()> callback)
    : graph(graph)
    , callback(std::move(callback))
{
    run();
}

Effect::~Effect()
{
    dispose();
}

void Effect::run()
{
    if (running || disposed)
        return;

    running = true;

    for (ReactiveSource* dependency : dependencies)
        dependency->observers.erase(this);
    dependencies.clear();

    graph.beginCollect(this);
    callback();
    graph.endCollect(this);

    running = false;
}

void Effect::dispose()
{
    if (disposed)
        return;

    disposed = true;
    graph.detach(this);
    for (ReactiveSource* dependency : dependencies)
        dependency->observers.erase(this);
    dependencies.clear();
}

bool Effect::isRunning() const
{
    return running;
}

Transaction::Transaction(ReactiveGraph& graph)
    : graph(graph)
{
    graph.beginTransaction();
}

Transaction::~Transaction()
{
    if (active)
        graph.commitTransaction();
}

void Transaction::commit()
{
    if (!active)
        return;

    active = false;
    graph.commitTransaction();
}

} // namespace lute::ui
