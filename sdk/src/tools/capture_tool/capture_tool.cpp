// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2016 Intel Corporation. All Rights Reserved.

#include <memory>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <librealsense/rs.hpp>
#include "rs_core.h"
#include "rs/core/context_interface.h"
#include "rs/playback/playback_context.h"
#include "rs/record/record_context.h"
#include "rs/playback/playback_device.h"
#include "rs/record/record_device.h"
#include "basic_cmd_util.h"
#include "viewer.h"
#include "rs/utils/librealsense_conversion_utils.h"
#include "rs_sdk_version.h"

using namespace std;
using namespace rs::utils;
using namespace rs::core;

basic_cmd_util g_cmd;
std::shared_ptr<viewer> g_renderer;
std::map<rs::stream,size_t> g_frame_count;
std::condition_variable g_streaming_cv;
std::mutex g_streaming_mutex;
bool g_user_quit = false;

void signal_handler(int s)
{
    if(s != 2)
        return;
    std::cout << std::endl << "streaming ended by the user" << std::endl;
    std::unique_lock<std::mutex> locker(g_streaming_mutex);
    g_user_quit = true;
    locker.unlock();

    g_streaming_cv.notify_one();
}

auto g_frame_callback = [](rs::frame frame)
{
    g_frame_count[frame.get_stream_type()]++;
    if(!g_cmd.is_rendering_enabled())return;
    auto image = rs::utils::get_shared_ptr_with_releaser(rs::core::image_interface::create_instance_from_librealsense_frame(frame, rs::core::image_interface::flag::any));
    g_renderer->show_image(image);
};

auto g_motion_callback = [](rs::motion_data motion){};

std::shared_ptr<context_interface> create_context(basic_cmd_util cl_util)
{
    switch(cl_util.get_streaming_mode())
    {
        case streaming_mode::live: return std::shared_ptr<context_interface>(new context());
        case streaming_mode::record: return std::shared_ptr<context_interface>(new rs::record::context(cl_util.get_file_path(streaming_mode::record).c_str()));
        case streaming_mode::playback: return std::shared_ptr<context_interface>(new rs::playback::context(cl_util.get_file_path(streaming_mode::playback).c_str()));
    }
    return nullptr;
}

std::string stream_type_to_string(rs::stream stream)
{
    switch(stream)
    {
        case rs::stream::depth: return "depth";
        case rs::stream::color: return "color";
        case rs::stream::infrared: return "infrared";
        case rs::stream::infrared2: return "infrared2";
        case rs::stream::fisheye: return "fisheye";
        default: return "";
    }
}

std::string pixel_format_to_string(rs::format format)
{
    switch(format)
    {
        case rs::format::rgb8: return "rgb8";
        case rs::format::rgba8: return "rgba8";
        case rs::format::bgr8: return "bgr8";
        case rs::format::bgra8: return "bgra8";
        case rs::format::yuyv: return "yuyv";
        case rs::format::raw8: return "raw8";
        case rs::format::raw10: return "raw10";
        case rs::format::raw16: return "raw16";
        case rs::format::y8: return "y8";
        case rs::format::y16: return "y16";
        case rs::format::z16: return "z16";
        case rs::format::any: return "any";
        default: return "";
    }
}

void configure_device(rs::device* device, basic_cmd_util cl_util, std::shared_ptr<viewer> &renderer)
{
    const int window_width = 640;
    const int window_height = 480;
    auto streams = cl_util.get_enabled_streams();
    auto is_playback = cl_util.get_streaming_mode() == streaming_mode::playback;
    auto is_record = cl_util.get_streaming_mode() == streaming_mode::record;
    std::cout << "enabled streams:" << std::endl;
    for(auto stream : streams)
    {
        auto lrs_stream = convert_stream_type(stream);

        bool is_stream_profile_available = cl_util.is_stream_profile_available(stream);
        bool is_stream_pixel_format_available = cl_util.is_stream_pixel_format_available(stream);

        device->set_frame_callback(lrs_stream, g_frame_callback);

        if(is_playback || !(is_stream_profile_available || is_stream_pixel_format_available))
        {
            device->enable_stream(lrs_stream, rs::preset::best_quality);
        }
        else
        {
            device->enable_stream(lrs_stream,
                                  cl_util.get_stream_width(stream),
                                  cl_util.get_stream_height(stream),
                                  convert_pixel_format(cl_util.get_stream_pixel_format(stream)),
                                  cl_util.get_stream_fps(stream));
        }

        if(is_record)
        {
            auto cl = cl_util.get_compression_level(stream);
            static_cast<rs::record::device*>(device)->set_compression(lrs_stream, cl);
        }

        std::cout << "\t" << stream_type_to_string(lrs_stream) <<
                     " - width:" << device->get_stream_width(lrs_stream) <<
                     ", height:" << device->get_stream_height(lrs_stream) <<
                     ", fps:" << device->get_stream_framerate(lrs_stream) <<
                     ", pixel format:" << pixel_format_to_string(device->get_stream_format(lrs_stream)) << std::endl;

    }

    if(is_playback)
    {
        static_cast<rs::playback::device*>(device)->set_real_time(g_cmd.is_real_time());
    }

    if(g_cmd.is_motion_enabled())
    {
        device->enable_motion_tracking(g_motion_callback);

        //set the camera to produce all streams timestamps from a single clock - the microcontroller's clock.
        //this option takes effect only if motion tracking is enabled and device->start() is called with rs::source::all_sources argument.
        device->set_option(rs::option::fisheye_strobe, 1);
    }

    if(cl_util.is_rendering_enabled())
    {
        renderer = std::make_shared<viewer>(streams.size(), window_width, window_height, [device]()
        {
            std::cout << std::endl << "streaming ended by the user" << std::endl;
            std::unique_lock<std::mutex> locker(g_streaming_mutex);
            g_user_quit = true;
            locker.unlock();

            g_streaming_cv.notify_one();
        });
    }
}

int main(int argc, char* argv[])
{
    try
    {
        signal(SIGINT, signal_handler);

        rs::utils::cmd_option opt;
        if(!g_cmd.parse(argc, argv))
        {
            g_cmd.get_cmd_option("-h --h -help --help -?", opt);
            return -1;
        }

        if(g_cmd.get_cmd_option("-h --h -help --help -?", opt))
        {
            std::cout << g_cmd.get_help();
            return 0;
        }

        std::cout << g_cmd.get_selection();

        if(g_cmd.is_print_file_info())
            std::cout << g_cmd.get_file_info() << std::endl;

        if(g_cmd.get_enabled_streams().size() == 0)
            return 0;

        std::shared_ptr<context_interface> context = create_context(g_cmd);

        if(context->get_device_count() == 0)
        {
            throw std::runtime_error("no device detected");
        }

        rs::device * device = context->get_device(0);

        configure_device(device, g_cmd, g_renderer);

        rs::source source = g_cmd.is_motion_enabled() ? rs::source::all_sources : rs::source::video;

        device->start(source);

        auto start_time = std::chrono::high_resolution_clock::now();

        auto capture_time = g_cmd.get_capture_time();
        auto frames = g_cmd.get_number_of_frames();

        cout << "start capturing ";
        if(frames)
            cout << frames << " frames ";

        if(capture_time)
            cout << "for " << to_string(g_cmd.get_capture_time()) << " second";

        std::cout << endl;

        auto pred =  [device, start_time, capture_time, frames]() -> bool
        {
            if(g_user_quit)
                return true;

            if(device->is_streaming() == false)
                return true;

            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            bool capture_time_done = capture_time > 0 && duration > capture_time;
            if(capture_time_done)
                return true;

            if(frames > 0 &&
                g_frame_count.size() == g_cmd.get_enabled_streams().size())//frame counter is initialized with all expected stream types
            {
                bool frames_done = true;
                for(auto fc : g_frame_count)
                {
                    if(fc.second < frames)
                    {
                        frames_done = false;
                        break;
                    }
                }
                if(frames_done)
                    return true;
            }

            return false;
        };

        bool running = true;

        while(running)
        {
            std::unique_lock<std::mutex> locker(g_streaming_mutex);
            if(g_streaming_cv.wait_for(locker, std::chrono::milliseconds(15), pred))
                running = false;
            locker.unlock();
        }

        if(device->is_streaming())
            device->stop(source);

        cout << "done capturing" << endl;

        return 0;
    }
    catch(rs::error e)
    {
        cout << e.what() << endl;
        return -1;
    }
    catch(string e)
    {
        cout << e << endl;
        return -1;
    }
}
