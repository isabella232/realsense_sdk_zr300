// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2016 Intel Corporation. All Rights Reserved.

#include "custom_image.h"

namespace rs
{
    namespace core
    {
        custom_image::custom_image(image_info * info,
                                   const void * data,
                                   stream_type stream,
                                   image_interface::flag flags,
                                   double time_stamp,
                                   rs::core::timestamp_domain time_stamp_domain,
                                   uint64_t frame_number,
                                   rs::utils::unique_ptr<release_interface> data_releaser)
            : image_base(), m_info(*info), m_data(data), m_stream(stream), m_flags(flags),
              m_frame_number(frame_number), m_time_stamp(time_stamp),m_time_stamp_domain(time_stamp_domain), m_data_releaser(std::move(data_releaser))
        {

        }

        image_info custom_image::query_info() const
        {
            return m_info;
        }

        double custom_image::query_time_stamp() const
        {
            return m_time_stamp;
        }

        timestamp_domain custom_image::query_time_stamp_domain() const
        {
            return m_time_stamp_domain;
        }

        image_interface::flag custom_image::query_flags() const
        {
            return m_flags;
        }

        const void * custom_image::query_data() const
        {
            return m_data;
        }

        stream_type custom_image::query_stream_type() const
        {
            return m_stream;
        }

        uint64_t custom_image::query_frame_number() const
        {
            return m_frame_number;
        }

        custom_image::~custom_image() {}

        image_interface * image_interface::create_instance_from_raw_data(image_info * info,
                                                                         const image_data_with_data_releaser & data_container,
                                                                         stream_type stream,
                                                                         image_interface::flag flags,
                                                                         double time_stamp,
                                                                         uint64_t frame_number,
                                                                         timestamp_domain time_stamp_domain)
        {
            return new custom_image(info,
                                    data_container.data,
                                    stream,
                                    flags,
                                    time_stamp,
                                    time_stamp_domain,
                                    frame_number,
                                    std::move(rs::utils::get_unique_ptr_with_releaser(data_container.data_releaser)));
        }
    }
}


