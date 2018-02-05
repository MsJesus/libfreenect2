/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2014 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */

/** @file event_loop.cpp Event handling. */

#include <libfreenect2/usb/EventLoop.h>

#include <libusb.h>
#ifdef _WIN32
#include <winsock.h>
#else
#include <sys/time.h>
#endif

namespace libfreenect2 {
namespace usb {

    
    EventLoop::EventLoop() :
        _shutdown(false),
        _thread(0),
        _usbContext(0)
    {}
    
    
    EventLoop::~EventLoop()
    {
        stop();
    }
    
    /**
     * Start the event-loop thread.
     * @param usb_context Context.
     */
    void EventLoop::start(void *usbContext)
    {
        if (_thread == 0)
        {
            _shutdown = false;
            _usbContext = usbContext;
            _thread = new libfreenect2::thread(&EventLoop::staticExecute, this);
        }
    }
    
    /** Stop the thread. */
    void EventLoop::stop()
    {
        if (_thread != 0)
        {
            _shutdown = true;
            _thread->join();
            delete _thread;
            _thread = 0;
            _usbContext = 0;
        }
    }

    
    /**
     * Execute the given event loop.
     * @param cookie Event loop to run.
     */
    void EventLoop::staticExecute(void *cookie)
    {
        static_cast<EventLoop *>(cookie)->execute();
    }
    

    /** Execute the job, until shut down. */
    void EventLoop::execute()
    {
        this_thread::set_name("USB");
        timeval t;
        t.tv_sec = 0;
        t.tv_usec = 10000;
        
        while (!_shutdown)
        {
            libusb_handle_events_timeout_completed(reinterpret_cast<libusb_context *>(_usbContext), &t, 0);
            
            
        }
    }

} /* namespace usb */
} /* namespace libfreenect2 */
