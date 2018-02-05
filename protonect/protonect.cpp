/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2011 individual OpenKinect contributors. See the CONTRIB file
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

/** @file Protonect.cpp Main application file. */

#include <iostream>
#include <cstdlib>
#include <signal.h>
#include <chrono>

/// [headers]
#include <include/libfreenect2.h>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>

#define ENABLE_VIEWER 1

/// [headers]
#ifdef __APPLE__
#if ENABLE_VIEWER
#include "viewer.h"
#endif
#endif

#include <inttypes.h>

bool protonect_shutdown = false; ///< Whether the running application should shut down.

static void sigint_handler(int s)
{
    protonect_shutdown = true;
}

bool protonect_paused = false;
libfreenect2::Freenect2Device *devtopause;

//Doing non-trivial things in signal handler is bad. If you want to pause,
//do it in another thread.
//Though libusb operations are generally thread safe, I cannot guarantee
//everything above is thread safe when calling start()/stop() while
//waitForNewFrame().
static void sigusr1_handler(int s)
{
    if (devtopause == 0)
        return;
    /// [pause]
    if (protonect_paused)
        devtopause->start();
    else
        devtopause->stop();
    protonect_paused = !protonect_paused;
    /// [pause]
}


/// [main]
/**
 * Main application entry point.
 *
 * Accepted argumemnts:
 * - <number> Serial number of the device to open.
 * - -noviewer Disable viewer window.
 */
int main(int argc, char *argv[])
/// [main]
{
    std::string program_path(argv[0]);
    std::cerr << "Version: " << LIBFREENECT2_VERSION << std::endl;
    std::cerr << "Usage: " << program_path << " [<device serial>]" << std::endl;
    std::cerr << "        [-norgb | -nodepth] [-help] [-version]" << std::endl;
    std::cerr << "        [-frames <number of frames to process>]" << std::endl;
    std::cerr << "To pause and unpause: pkill -USR1 protonect" << std::endl;
    size_t executable_name_idx = program_path.rfind("protonect");

    std::string binpath = "/";
    
    if(executable_name_idx != std::string::npos)
    {
        binpath = program_path.substr(0, executable_name_idx);
    }
    
    // create a console logger with debug level (default is console logger with info level)
    /// [logging]
    libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Debug));
    /// [logging]
    
    /// [context]
    libfreenect2::Freenect2 freenect2;
    libfreenect2::Freenect2Device *dev = 0;
    /// [context]
    
    std::string serial = "";

    bool viewer_enabled = true;
    bool enable_rgb = true;
    bool enable_depth = true;
    bool enable_registration = true;
    size_t framemax = -1;
    
    for(int argI = 1; argI < argc; ++argI)
    {
        const std::string arg(argv[argI]);
        
        if(arg == "-help" || arg == "--help" || arg == "-h" || arg == "-v" || arg == "--version" || arg == "-version")
        {
            // Just let the initial lines display at the beginning of main
            return 0;
        }
        else if(arg.find_first_not_of("0123456789") == std::string::npos) //check if parameter could be a serial number
        {
            serial = arg;
        }
        else if(arg == "-noviewer" || arg == "--noviewer")
        {
            viewer_enabled = false;
        }
        else if(arg == "-norgb" || arg == "--norgb")
        {
            enable_rgb = false;
        }
        else if(arg == "-nodepth" || arg == "--nodepth")
        {
            enable_depth = false;
        }
        else if(arg == "-noreg" || arg == "--noreg")
        {
            enable_registration = false;
        }
        else if(arg == "-frames")
        {
            ++argI;
            framemax = strtol(argv[argI], NULL, 0);
            if (framemax == 0) {
                std::cerr << "invalid frame count '" << argv[argI] << "'" << std::endl;
                return -1;
            }
        }
        else
        {
            std::cout << "Unknown argument: " << arg << std::endl;
        }
    }
    
    if (!enable_rgb && !enable_depth)
    {
        std::cerr << "Disabling both streams is not allowed!" << std::endl;
        return -1;
    }
    
    /// [discovery]
    int devicesCount = freenect2.enumerateDevices();
    if(devicesCount == 0)
    {
        std::cout << "no device connected!" << std::endl;
        return -1;
    }
    
    if (serial == "")
    {
        serial = freenect2.getDefaultDeviceSerialNumber();
    }
    /// [discovery]
    
    
    /// [open]
    dev = freenect2.openDevice(serial);
    /// [open]
    
    if(dev == 0)
    {
        std::cout << "failure opening device!" << std::endl;
        return -1;
    }
    
    devtopause = dev;
    
    signal(SIGINT,sigint_handler);
#ifdef SIGUSR1
    signal(SIGUSR1, sigusr1_handler);
#endif
    protonect_shutdown = false;
    
    /// [listeners]
    int types = 0;
    if (enable_rgb)
        types |= libfreenect2::Frame::Color;
    if (enable_depth)
        types |= libfreenect2::Frame::Ir | libfreenect2::Frame::Depth;
    libfreenect2::SyncMultiFrameListener listener(types);
    libfreenect2::FrameMap frames;
    
    dev->setColorFrameListener(&listener);
    dev->setIrAndDepthFrameListener(&listener);
    /// [listeners]
    
    /// [start]
    if (enable_rgb && enable_depth)
    {
        if (!dev->start())
            return -1;
    }
    else
    {
        if (!dev->startStreams(enable_rgb, enable_depth))
            return -1;
    }
    
    std::cout << "device serial: " << dev->getSerialNumber() << std::endl;
    std::cout << "device firmware: " << dev->getFirmwareVersion() << std::endl;
    /// [start]
    
    /// [registration setup]
    libfreenect2::Registration* registration = new libfreenect2::Registration(dev->getIrCameraParams(), dev->getColorCameraParams());
    libfreenect2::Frame undistorted(512 * 424 * 4), registered(512 * 424 * 4);
    undistorted.format = libfreenect2::Frame::Float;
    undistorted.width = 512;
    undistorted.height = 424;
    undistorted.bytes_per_pixel = 4;
    registered.width = 512;
    registered.height = 424;
    registered.bytes_per_pixel = 4;
    registered.format = libfreenect2::Frame::RGBX;
    /// [registration setup]
    
    size_t framecount = 0;
    
#ifdef __APPLE__
#if ENABLE_VIEWER
    Viewer viewer;
    if (viewer_enabled)
        viewer.initialize();
#endif
#else
    viewer_enabled = false;
#endif

    /// [loop start]
    static auto lastNowTime = std::chrono::high_resolution_clock::now();
    static size_t numberFrames = 100;
    while(!protonect_shutdown && (framemax == (size_t)-1 || framecount < framemax))
    {
        if (!listener.waitForNewFrame(frames, 10*1000)) // 10 sconds
        {
            std::cout << "Protonect Timeout!" << std::endl;
            return -1;
        }
        libfreenect2::Frame *rgb = frames[libfreenect2::Frame::Color];
        libfreenect2::Frame *ir = frames[libfreenect2::Frame::Ir];
        libfreenect2::Frame *depth = frames[libfreenect2::Frame::Depth];
        /// [loop start]
        
        if (enable_rgb && enable_depth && enable_registration)
        {
            /// [registration]
            registration->apply(rgb, depth, &undistorted, &registered);
            /// [registration]
        }
        
        framecount++;
        if (!viewer_enabled)
        {
            if (framecount % numberFrames == 0)
            {
                auto nowTime = std::chrono::high_resolution_clock::now();
                auto lastNowTimeMiliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(lastNowTime.time_since_epoch()).count();
                auto nowTimeMiliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime.time_since_epoch()).count();
                auto lastMiliseconds = nowTimeMiliseconds - lastNowTimeMiliseconds;
                int fps = (numberFrames * 1000 / lastMiliseconds);
                std::cout << "Time last in milliseconds :: " << lastMiliseconds << std::endl;
                std::cout << "FPS :: " << fps << std::endl;
                std::cout << "The viewer is turned off. Received " << framecount << " frames. Ctrl-C to stop." << std::endl;
                lastNowTime = nowTime;
            }
        }
        else
        {
#ifdef __APPLE__
#if ENABLE_VIEWER
            if (enable_rgb)
            {
                viewer.addFrame("RGB", rgb);
            }
            if (enable_depth)
            {
                viewer.addFrame("ir", ir);
                viewer.addFrame("depth", depth);
            }
            if (enable_rgb && enable_depth && enable_registration)
            {
                viewer.addFrame("registered", &registered);
            }
            
            protonect_shutdown = protonect_shutdown || viewer.render();
#endif
#endif
        }
        
        /// [loop end]
        listener.release(frames);
        /** libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(100)); */
    }
    /// [loop end]
    
    // TODO: restarting ir stream doesn't work!
    // TODO: bad things will happen, if frame listeners are freed before dev->stop() :(
    /// [stop]
    dev->stop();
    dev->close();
    /// [stop]
    
    delete registration;
    
    return 0;
}

