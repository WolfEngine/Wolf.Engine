#pragma once

#include "media/gst/core/w_element.hpp"

namespace wolf::media::gst {

/**
 * @brief wrappper of videoscale gstreamer element.
 */
class w_element_videoscale : public w_element
{
    constexpr static const char* factory_name = "videoscale";

public:
    [[nodiscard]] static auto make() -> boost::leaf::result<w_element_videoscale>
    {
        BOOST_LEAF_AUTO(base_element, w_element::make(factory_name));
        return w_element_videoscale(std::move(base_element));
    }

private:
    explicit w_element_videoscale(w_element&& p_base) noexcept
        : w_element(std::move(p_base))
    {}
};

}  // namespace wolf::media::gst
