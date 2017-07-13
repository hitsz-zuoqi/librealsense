// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2015 Intel Corporation. All Rights Reserved.

#include "source.h"
#include "option.h"

namespace librealsense
{
    class frame_queue_size : public option_base
    {
    public:
        frame_queue_size(std::atomic<uint32_t>* ptr, const option_range& opt_range)
            : option_base(opt_range),
              _ptr(ptr)
        {}

        void set(float value) override
        {
            if (!is_valid(value))
                throw invalid_value_exception(to_string() << "set(frame_queue_size) failed! Given value " << value << " is out of range.");

            *_ptr = static_cast<uint32_t>(value);
        }

        float query() const override { return static_cast<float>(_ptr->load()); }

        bool is_enabled() const override { return true; }

        const char* get_description() const override
        {
            return "Max number of frames you can hold at a given time. Increasing this number will reduce frame drops but increase lattency, and vice versa";
        }
    private:
        std::atomic<uint32_t>* _ptr;
    };

    std::shared_ptr<option> frame_source::get_published_size_option()
    {
        return std::make_shared<frame_queue_size>(&_max_publish_list_size, option_range{ 0, 32, 1, 16 });
    }

    frame_source::frame_source(std::shared_ptr<uvc::time_service> ts)
            : _callback(nullptr, [](rs2_frame_callback*) {}),
              _max_publish_list_size(16),
              _ts(ts)
    {}

    void frame_source::init(rs2_extension_type type, std::shared_ptr<metadata_parser_map> metadata_parsers, std::weak_ptr<device_interface> owner)
    {
        std::lock_guard<std::mutex> lock(_callback_mutex);
        _archive = make_archive(type, &_max_publish_list_size, _ts, metadata_parsers, owner);
    }

    callback_invocation_holder frame_source::begin_callback() { return _archive->begin_callback(); }

    void frame_source::reset()
    {
        std::lock_guard<std::mutex> lock(_callback_mutex);
        _callback.reset();
        _archive.reset();
    }

    rs2_frame* frame_source::alloc_frame(size_t size, frame_additional_data additional_data, bool requires_memory) const
    {
        return _archive->alloc_and_track(size, additional_data, requires_memory);
    }

    void frame_source::set_callback(frame_callback_ptr callback)
    {
        std::lock_guard<std::mutex> lock(_callback_mutex);
        _callback = callback;
    }

    void frame_source::invoke_callback(frame_holder frame) const
    {
        if (frame)
        {
            auto callback = _archive->begin_callback();
            try
            {
                frame->log_callback_start(_ts?_ts->get_time():0);
                if (_callback)
                {
                    rs2_frame* ref = nullptr;
                    std::swap(frame.frame, ref);
                    _callback->on_frame(ref);
                }
            }
            catch(...)
            {
                LOG_ERROR("Exception was thrown during user callback!");
            }
        }
    }

    void frame_source::flush() const
    {
        if (_archive.get())
            _archive->flush();
    }
}

