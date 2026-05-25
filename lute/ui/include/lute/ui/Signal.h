#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

namespace lute::ui
{

class Effect;
class ReactiveSource;

class ReactiveGraph
{
public:
    void beginCollect(Effect* effect);
    void endCollect(Effect* effect);
    void observe(ReactiveSource& source);
    void notify(ReactiveSource& source);

    void beginTransaction();
    void commitTransaction();

private:
    friend class Effect;
    friend class ReactiveSource;

    void detach(Effect* effect);
    void enqueue(Effect* effect);

    Effect* currentEffect = nullptr;
    int transactionDepth = 0;
    std::unordered_set<Effect*> pendingEffects;
};

class ReactiveSource
{
public:
    ReactiveSource() = default;
    ReactiveSource(const ReactiveSource&) = delete;
    ReactiveSource& operator=(const ReactiveSource&) = delete;
    ~ReactiveSource();

private:
    friend class ReactiveGraph;
    friend class Effect;

    std::unordered_set<Effect*> observers;
};

class Effect
{
public:
    Effect(ReactiveGraph& graph, std::function<void()> callback);
    Effect(const Effect&) = delete;
    Effect& operator=(const Effect&) = delete;
    ~Effect();

    void run();
    void dispose();
    bool isRunning() const;

private:
    friend class ReactiveGraph;
    friend class ReactiveSource;

    ReactiveGraph& graph;
    std::function<void()> callback;
    std::unordered_set<ReactiveSource*> dependencies;
    bool running = false;
    bool disposed = false;
};

class Transaction
{
public:
    explicit Transaction(ReactiveGraph& graph);
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    void commit();

    template<typename Fn>
    static void run(ReactiveGraph& graph, Fn&& fn)
    {
        Transaction transaction(graph);
        fn();
        transaction.commit();
    }

private:
    ReactiveGraph& graph;
    bool active = true;
};

template<class T>
class Signal
{
public:
    Signal(ReactiveGraph& graph, T value)
        : graph(graph)
        , value(std::move(value))
    {
    }

    const T& get()
    {
        graph.observe(source);
        return value;
    }

    const T& peek() const
    {
        return value;
    }

    void set(T next)
    {
        if (value == next)
            return;

        value = std::move(next);
        graph.notify(source);
    }

private:
    ReactiveGraph& graph;
    ReactiveSource source;
    T value;
};

template<class T>
class Computed
{
public:
    Computed(ReactiveGraph& graph, std::function<T()> compute)
        : signal(graph, compute())
        , effect(
              std::make_unique<Effect>(
                  graph,
                  [this, compute = std::move(compute)]()
                  {
                      signal.set(compute());
                  }
              )
          )
    {
    }

    const T& get()
    {
        return signal.get();
    }

    const T& peek() const
    {
        return signal.peek();
    }

private:
    Signal<T> signal;
    std::unique_ptr<Effect> effect;
};

} // namespace lute::ui
