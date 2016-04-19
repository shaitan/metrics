#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "metrics/forwards.hpp"
#include "metrics/metric.hpp"
#include "metrics/tags.hpp"

namespace metrics {

using detail::processor_t;

class registry_t {
    std::unique_ptr<processor_t> processor;

public:
    /// Constructs a new metric registry.
    registry_t();

    /// Destroys the current metric registry, freeing all its allocated resources and joining the
    /// processor thread.
    ~registry_t();

    /// Returns a counter shared metric that is mapped to a given tags, performing a creation with
    /// registering if such metric does not already exist.
    ///
    /// \param name counter name.
    /// \param tags optional additional tags.
    /// \tparam `T` must be either `std::uint64_t` or `std::int64_t`.
    template<typename T>
    auto counter(std::string name, tags_t::container_type tags = tags_t::container_type()) const ->
        metric<std::atomic<T>>;

    /// Returns a meter shared metric that is mapped to a given tags, performing a creation with
    /// registering if such metric does not already exist.
    ///
    /// \param name counter name.
    /// \param tags optional additional tags.
    auto meter(std::string name, tags_t::container_type tags = tags_t::container_type()) const ->
        metric<meter_t>;

    // TODO: Change default accumulator to `exponentially_decaying_t`.
    template<class Accumulate = accumulator::sliding::window_t>
    auto timer(std::string name, tags_t::container_type tags = tags_t::container_type()) const ->
        metric<timer<Accumulate>>;
};

}  // namespace metrics
