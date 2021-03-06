#include "metrics/registry.hpp"

#include <boost/range/algorithm/transform.hpp>

#include "metrics/counter.hpp"
#include "metrics/gauge.hpp"
#include "metrics/meter.hpp"
#include "metrics/metric.hpp"

#include "registry.hpp"

namespace metrics {

namespace {

const auto query_all = [](const tagged_t&) -> bool {
    return true;
};

struct transformer_t {
    template<typename T>
    auto
    operator()(const std::pair<const tags_t, shared_metric<T>>& pair) const ->
        std::shared_ptr<tagged_t>
    {
        return std::make_shared<shared_metric<T>>(pair.second);
    }
};

template<typename R, typename T, typename M>
auto
instances(const query_t& query, M& map) -> registry_t::metric_set<R> {
    typedef shared_metric<R> value_type;

    registry_t::metric_set<R> result;

    std::lock_guard<std::mutex> lock(map.mutex);
    const auto& instances = map.template get<T>();

    for (const auto& it : instances) {
        if (auto metric = std::get<1>(it).lock()) {
            const auto& tags = std::get<0>(it);
            auto shared = value_type(tags, metric);
            if (query(shared) && metric) {
                result.insert(std::make_pair(tags, std::move(shared)));
            }
        }
    }

    return result;
}

template<typename T>
struct type_traits;

template<typename T>
struct type_traits<gauge<T>> {
    static auto type_name() noexcept -> const char* {
        return "gauge";
    }
};

template<typename T>
struct type_traits<std::atomic<T>> {
    static auto type_name() noexcept -> const char* {
        return "counter";
    }
};

template<>
struct type_traits<meter_t> {
    static auto type_name() noexcept -> const char* {
        return "meter";
    }
};

template<typename T>
struct type_traits<timer<T>> {
    static auto type_name() noexcept -> const char* {
        return "timer";
    }
};

} // namespace

registry_t::registry_t():
    inner(new inner_t)
{}

registry_t::~registry_t() = default;

template<typename R>
auto
registry_t::register_gauge(std::string name, tags_t::container_type other, std::function<R()> fn) ->
    shared_metric<metrics::gauge<R>>
{
    other["type"] = type_traits<metrics::gauge<R>>::type_name();
    tags_t tags(std::move(name), std::move(other));

    std::lock_guard<std::mutex> lock(inner->gauges.mutex);
    auto& instances = inner->gauges.template get<R>();

    std::shared_ptr<metrics::gauge<R>> instance;
    if((instance = instances[tags].lock()) == nullptr) {
        instance = std::make_shared<metrics::gauge<R>>(std::move(fn));
        instances[tags] = instance;
    }
    return {tags, instance};
}

template<typename T>
auto
registry_t::gauge(std::string name, tags_t::container_type other) const ->
    shared_metric<metrics::gauge<T>>
{
    other["type"] = type_traits<metrics::gauge<T>>::type_name();
    tags_t tags(std::move(name), std::move(other));

    std::lock_guard<std::mutex> lock(inner->gauges.mutex);
    const auto& instances = inner->gauges.template get<T>();
    std::shared_ptr<metrics::gauge<T>> instance;
    if((instance = instances.at(tags).lock()) == nullptr) {
        throw std::invalid_argument(name);
    }
    return {tags, instance};
}

template<typename T>
auto
registry_t::gauges() const -> metric_set<metrics::gauge<T>> {
    return gauges<T>(query_all);
}

template<typename T>
auto
registry_t::gauges(const query_t& query) const -> metric_set<metrics::gauge<T>> {
    return instances<metrics::gauge<T>, T>(query, inner->gauges);
}

template<typename T>
auto
registry_t::counter(std::string name, tags_t::container_type other) const ->
    shared_metric<std::atomic<T>>
{
    other["type"] = type_traits<std::atomic<T>>::type_name();
    tags_t tags(std::move(name), std::move(other));

    std::lock_guard<std::mutex> lock(inner->counters.mutex);
    auto& instances = inner->counters.template get<T>();

    std::shared_ptr<std::atomic<T>> instance;
    if((instance = instances[tags].lock()) == nullptr) {
        instance = std::make_shared<std::atomic<T>>();
        instances[tags] = instance;
    }

    return {std::move(tags), std::move(instance)};
}

template<typename T>
auto
registry_t::counters() const -> metric_set<std::atomic<T>> {
    return counters<T>(query_all);
}

template<typename T>
auto
registry_t::counters(const query_t& query) const -> metric_set<std::atomic<T>> {
    return instances<std::atomic<T>, T>(query, inner->counters);
}

auto
registry_t::meter(std::string name, tags_t::container_type other) const ->
    shared_metric<meter_t>
{
    other["type"] = type_traits<meter_t>::type_name();
    tags_t tags(std::move(name), std::move(other));

    std::lock_guard<std::mutex> lock(inner->meters.mutex);
    auto& instances = inner->meters.get<detail::meter_t>();

    std::shared_ptr<detail::meter_t> instance;
    if((instance = instances[tags].lock()) == nullptr) {
        instance = std::make_shared<detail::meter_t>();
        instances[tags] = instance;
    }

    return {std::move(tags), std::move(instance)};
}

auto
registry_t::meters() const -> metric_set<meter_t> {
    return meters(query_all);
}

auto
registry_t::meters(const query_t& query) const -> metric_set<meter_t> {
    return instances<meter_t, detail::meter_t>(query, inner->meters);
}

template<class Accumulate>
auto registry_t::timer(std::string name, tags_t::container_type other) const ->
    shared_metric<metrics::timer<Accumulate>>
{
    typedef std::chrono::high_resolution_clock clock_type;
    typedef detail::timer<
        clock_type,
        detail::meter<clock_type>,
        detail::histogram<Accumulate>
    > result_type;

    other["type"] = type_traits<metrics::timer<Accumulate>>::type_name();
    tags_t tags(std::move(name), std::move(other));

    std::lock_guard<std::mutex> lock(inner->timers.mutex);
    auto& instances = inner->timers.template get<Accumulate>();

    std::shared_ptr<result_type> instance;
    if((instance = instances[tags].lock()) == nullptr) {
        instance = std::make_shared<result_type>();
        instances[tags] = instance;
    }

    return {std::move(tags), std::move(instance)};
}

template<class Accumulate>
auto
registry_t::timers() const -> metric_set<metrics::timer<Accumulate>> {
    return timers<Accumulate>(query_all);
}

template<class Accumulate>
auto
registry_t::timers(const query_t& query) const -> metric_set<metrics::timer<Accumulate>> {
    return instances<metrics::timer<Accumulate>, Accumulate>(query, inner->timers);
}

auto
registry_t::select() const -> std::vector<std::shared_ptr<tagged_t>> {
    return select(query_all);
}

auto
registry_t::select(const query_t& query) const ->
    std::vector<std::shared_ptr<tagged_t>>
{
    std::vector<std::shared_ptr<tagged_t>> result;
    auto fn = transformer_t();
    auto out = std::back_inserter(result);

    boost::transform(gauges<std::int64_t>(query), out, fn);
    boost::transform(gauges<std::uint64_t>(query), out, fn);
    boost::transform(gauges<std::double_t>(query), out, fn);
    boost::transform(gauges<std::string>(query), out, fn);

    boost::transform(counters<std::int64_t>(query), out, fn);
    boost::transform(counters<std::uint64_t>(query), out, fn);

    boost::transform(meters(query), out, fn);

    boost::transform(timers<accumulator::sliding::window_t>(query), out, fn);
    boost::transform(timers<accumulator::decaying::exponentially_t>(query), out, fn);

    return result;
}

template<typename T>
struct remove_metric {
    template<typename D>
    static auto apply(D& d, const std::string& name, const tags_t::container_type& tags) -> bool;
};

template<typename T>
struct remove_metric<metrics::gauge<T>> {
    template<typename D>
    static auto apply(D& d, const std::string& name, tags_t::container_type other) -> bool {
        other["type"] = type_traits<metrics::gauge<T>>::type_name();
        tags_t tags(std::move(name), std::move(other));

        std::lock_guard<std::mutex> lock(d->gauges.mutex);
        return d->gauges.template get<T>().erase(tags);
    }
};

template<typename T>
struct remove_metric<std::atomic<T>> {
    template<typename D>
    static auto apply(D& d, const std::string& name, tags_t::container_type other) -> bool {
        other["type"] = type_traits<std::atomic<T>>::type_name();
        tags_t tags(std::move(name), std::move(other));

        std::lock_guard<std::mutex> lock(d->counters.mutex);
        return d->counters.template get<T>().erase(tags);
    }
};

template<>
struct remove_metric<meter_t> {
    template<typename D>
    static auto apply(D& d, const std::string& name, tags_t::container_type other) -> bool {
        other["type"] = type_traits<meter_t>::type_name();
        tags_t tags(std::move(name), std::move(other));

        std::lock_guard<std::mutex> lock(d->meters.mutex);
        return d->meters.template get<detail::meter_t>().erase(tags);
    }
};

template<typename Accumulate>
struct remove_metric<timer<Accumulate>> {
    template<typename D>
    static auto apply(D& d, const std::string& name, tags_t::container_type other) -> bool {
        other["type"] = type_traits<metrics::timer<Accumulate>>::type_name();
        tags_t tags(std::move(name), std::move(other));

        std::lock_guard<std::mutex> lock(d->timers.mutex);
        return d->timers.template get<Accumulate>().erase(tags);
    }
};

template<typename T>
auto
registry_t::remove(const std::string& name, const tags_t::container_type& tags) -> bool {
    return remove_metric<T>::apply(inner, name, tags);
}

/// Instantiations.

template
auto registry_t::register_gauge<std::int64_t>(std::string, tags_t::container_type, std::function<std::int64_t()>) ->
    shared_metric<metrics::gauge<std::int64_t>>;

template
auto registry_t::register_gauge<std::uint64_t>(std::string, tags_t::container_type, std::function<std::uint64_t()>) ->
    shared_metric<metrics::gauge<std::uint64_t>>;

template
auto registry_t::register_gauge<double>(std::string, tags_t::container_type, std::function<double()>) ->
    shared_metric<metrics::gauge<double>>;

template
auto registry_t::register_gauge<std::string>(std::string, tags_t::container_type, std::function<std::string()>) ->
    shared_metric<metrics::gauge<std::string>>;

template
auto registry_t::gauge<std::int64_t>(std::string, tags_t::container_type) const ->
    shared_metric<metrics::gauge<std::int64_t>>;

template
auto registry_t::gauge<std::uint64_t>(std::string, tags_t::container_type) const ->
    shared_metric<metrics::gauge<std::uint64_t>>;

template
auto registry_t::gauge<double>(std::string, tags_t::container_type) const ->
    shared_metric<metrics::gauge<double>>;

template
auto registry_t::gauge<std::string>(std::string, tags_t::container_type) const ->
    shared_metric<metrics::gauge<std::string>>;

template
auto registry_t::gauges<std::int64_t>() const ->
    std::map<tags_t, shared_metric<metrics::gauge<std::int64_t>>>;

template
auto registry_t::gauges<std::uint64_t>() const ->
    std::map<tags_t, shared_metric<metrics::gauge<std::uint64_t>>>;

template
auto registry_t::gauges<double>() const ->
    std::map<tags_t, shared_metric<metrics::gauge<double>>>;

template
auto registry_t::gauges<std::string>() const ->
    std::map<tags_t, shared_metric<metrics::gauge<std::string>>>;

template
auto registry_t::counter<std::int64_t>(std::string, tags_t::container_type) const ->
    shared_metric<std::atomic<std::int64_t>>;

template
auto registry_t::counter<std::uint64_t>(std::string, tags_t::container_type) const ->
    shared_metric<std::atomic<std::uint64_t>>;

template
auto registry_t::counters<std::int64_t>() const ->
    std::map<tags_t, shared_metric<std::atomic<std::int64_t>>>;

template
auto registry_t::timer<accumulator::sliding::window_t>(std::string, tags_t::container_type tags) const ->
    shared_metric<metrics::timer<accumulator::sliding::window_t>>;

template
auto registry_t::timer<accumulator::decaying::exponentially_t>(std::string, tags_t::container_type tags) const ->
    shared_metric<metrics::timer<accumulator::decaying::exponentially_t>>;

template
auto registry_t::timers<accumulator::sliding::window_t>() const ->
    std::map<tags_t, shared_metric<metrics::timer<accumulator::sliding::window_t>>>;

template
auto registry_t::timers<accumulator::decaying::exponentially_t>() const ->
    std::map<tags_t, shared_metric<metrics::timer<accumulator::decaying::exponentially_t>>>;

template auto registry_t::remove<gauge<std::int64_t>>(const std::string& name, const tags_t::container_type& tags) -> bool;
template auto registry_t::remove<gauge<std::uint64_t>>(const std::string& name, const tags_t::container_type& tags) -> bool;
template auto registry_t::remove<gauge<std::double_t>>(const std::string& name, const tags_t::container_type& tags) -> bool;
template auto registry_t::remove<std::atomic<std::int64_t>>(const std::string& name, const tags_t::container_type& tags) -> bool;
template auto registry_t::remove<std::atomic<std::uint64_t>>(const std::string& name, const tags_t::container_type& tags) -> bool;
template auto registry_t::remove<meter_t>(const std::string& name, const tags_t::container_type& tags) -> bool;
template auto registry_t::remove<timer<accumulator::sliding::window_t>>(const std::string& name, const tags_t::container_type& tags) -> bool;
template auto registry_t::remove<timer<accumulator::decaying::exponentially_t>>(const std::string& name, const tags_t::container_type& tags) -> bool;

}  // namespace metrics
