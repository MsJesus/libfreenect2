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
#include <libfreenect2/packet_pipeline.h>
#include <include/logger.h>

#define ENABLE_VIEWER 1

/// [headers]
#ifdef __APPLE__
#if ENABLE_VIEWER
#include "viewer.h"
#endif
#endif

#include <inttypes.h>

bool protonect_shutdown = false; ///< Whether the running application should shut down.

void sigint_handler(int s)
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
void sigusr1_handler(int s)
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

//The following demostrates how to create a custom logger
/// [logger]
#include <fstream>
#include <cstdlib>
class MyFileLogger: public libfreenect2::Logger
{
private:
    std::ofstream logfile_;
public:
    MyFileLogger(const char *filename)
    {
        if (filename)
            logfile_.open(filename);
        level_ = Debug;
    }
    bool good()
    {
        return logfile_.is_open() && logfile_.good();
    }
    virtual void log(Level level, const std::string &message)
    {
        logfile_ << "[" << libfreenect2::Logger::level2str(level) << "] " << message << std::endl;
    }
};
/// [logger]

/// [main]
/**
 * Main application entry point.
 *
 * Accepted argumemnts:
 * - cpu Perform depth processing with the CPU.
 * - gl  Perform depth processing with OpenGL.
 * - cl  Perform depth processing with OpenCL.
 * - <number> Serial number of the device to open.
 * - -noviewer Disable viewer window.
 */
int main(int argc, char *argv[])
/// [main]
{
    std::string program_path(argv[0]);
    std::cerr << "Version: " << LIBFREENECT2_VERSION << std::endl;
    std::cerr << "Environment variables: LOGFILE=<protonect.log>" << std::endl;
    std::cerr << "Usage: " << program_path << " [dump | cpu] [<device serial>]" << std::endl;
    std::cerr << "        [-norgb | -nodepth] [-help] [-version]" << std::endl;
    std::cerr << "        [-frames <number of frames to process>]" << std::endl;
    std::cerr << "To pause and unpause: pkill -USR1 protonect" << std::endl;
    size_t executable_name_idx = program_path.rfind("protonect");
    
    std::string binpath = "/";
    
    if(executable_name_idx != std::string::npos)
    {
        binpath = program_path.substr(0, executable_name_idx);
    }
    
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
    // avoid flooing the very slow Windows console with debug messages
    libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Info));
#else
    // create a console logger with debug level (default is console logger with info level)
    /// [logging]
    libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Debug));
    /// [logging]
#endif
    /// [file logging]
    MyFileLogger *filelogger = new MyFileLogger(getenv("LOGFILE"));
    if (filelogger->good())
        libfreenect2::setGlobalLogger(filelogger);
    else
        delete filelogger;
    /// [file logging]
    
    /// [context]
    libfreenect2::Freenect2 freenect2;
    libfreenect2::Freenect2Device *dev = 0;
    libfreenect2::PacketPipeline *pipeline = 0;
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
        else if(arg == "dump")
        {
            if (!pipeline)
            {
                /// [pipeline]
                pipeline = new libfreenect2::DumpPacketPipeline();
                enable_registration = false;
                /// [pipeline]
            }
        }
        else if(arg == "cpu")
        {
            if(!pipeline)
            {
                /// [pipeline]
                pipeline = new libfreenect2::CpuPacketPipeline();
                /// [pipeline]
            }
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
    
    /// [test]
    //     libfreenect2::Freenect2Device **freenect2Dev = new libfreenect2::Freenect2Device*[devicesCount];
    
    //     for (int i = 0; i < devicesCount; i++)
    //     {
    //         std::cout << "--PROTO OPEN START" << std::endl;
    //         freenect2Dev[i] = freenect2.openDevice(i);
    //         libfreenect2::Freenect2Device *dev = *(freenect2Dev + i);
    //         if (dev->start())
    //         {
    //             std::cout << "--PROTO device SUCCESS start" << std::endl;
    //         }
    //         else
    //         {
    //             std::cout << "--PROTO device ERROR start" << std::endl;
    // //            return -1;
    //         }
    //         std::cout << "--PROTO OPEN END" << std::endl;
    
    //         std::cout << "--Device serial: " << dev->getSerialNumber() << std::endl;
    //         std::cout << "--Device firmware: " << dev->getFirmwareVersion() << std::endl;
    
    //         auto colorParams = dev->getColorCameraParams();
    //         std::cout << "**Color camera calibration parameters" << std::endl
    //         << "**Kinect v2 includes factory preset values for these parameters" << std::endl
    //         << std::endl
    //         << "**Intrinsic parameters" << std::endl
    //         << "Focal length x (pixel) : " << colorParams.fx << std::endl
    //         << "Focal length y (pixel) : " << colorParams.fy << std::endl
    //         << "Principal point x (pixel) : " << colorParams.cx << std::endl
    //         << "Principal point y (pixel) : " << colorParams.cy << std::endl
    //         << "**Extrinsic parameters" << std::endl
    //         << "**These parameters are used in [a formula](https://github.com/OpenKinect/libfreenect2/issues/41#issuecomment-72022111) to map coordinates in the" << std::endl
    //         << "**depth camera to the color camera." << std::endl
    //         << "**They cannot be used for matrix transformation." << std::endl
    //         << "shift_d : " << colorParams.shift_d << std::endl
    //         << "shift_m : " << colorParams.shift_m << std::endl
    //         << "mx_x3y0 : " << colorParams.mx_x3y0 << std::endl
    //         << "mx_x0y3 : " << colorParams.mx_x0y3 << std::endl
    //         << "mx_x2y1 : " << colorParams.mx_x2y1 << std::endl
    //         << "mx_x1y2 : " << colorParams.mx_x1y2 << std::endl
    //         << "mx_x2y0 : " << colorParams.mx_x2y0 << std::endl
    //         << "mx_x0y2 : " << colorParams.mx_x0y2 << std::endl
    //         << "mx_x1y1 : " << colorParams.mx_x1y1 << std::endl
    //         << "mx_x1y0 : " << colorParams.mx_x1y0 << std::endl
    //         << "mx_x0y1 : " << colorParams.mx_x0y1 << std::endl
    //         << "mx_x0y0 : " << colorParams.mx_x0y0 << std::endl
    //         << "my_x3y0 : " << colorParams.my_x3y0 << std::endl
    //         << "my_x0y3 : " << colorParams.my_x0y3 << std::endl
    //         << "my_x2y1 : " << colorParams.my_x2y1 << std::endl
    //         << "my_x1y2 : " << colorParams.my_x1y2 << std::endl
    //         << "my_x2y0 : " << colorParams.my_x2y0 << std::endl
    //         << "my_x0y2 : " << colorParams.my_x0y2 << std::endl
    //         << "my_x1y1 : " << colorParams.mx_x1y1 << std::endl
    //         << "my_x1y0 : " << colorParams.my_x1y0 << std::endl
    //         << "my_x0y1 : " << colorParams.my_x0y1 << std::endl
    //         << "my_x0y0 : " << colorParams.my_x0y0 << std::endl;
    //         auto depthParams = dev->getIrCameraParams();
    //         std::cout << "**IR camera intrinsic calibration parameters" << std::endl
    //         << "**Kinect v2 includes factory preset values for these parameters." << std::endl
    //         << std::endl
    //         << "Focal length x (pixel) : " << depthParams.fx << std::endl
    //         << "Focal length y (pixel) : " << depthParams.fy << std::endl
    //         << "Principal point x (pixel) : " << depthParams.cx << std::endl
    //         << "Principal point y (pixel) : " << depthParams.cy << std::endl
    //         << "Radial distortion coefficient, 1st-order : " << depthParams.k1 << std::endl
    //         << "Radial distortion coefficient, 2nd-order : " << depthParams.k2 << std::endl
    //         << "Radial distortion coefficient, 3rd-order : " << depthParams.k3 << std::endl
    //         << "Tangential distortion coefficient 1 : " << depthParams.p1 << std::endl
    //         << "Tangential distortion coefficient 2 : " << depthParams.p2 << std::endl;
    //     }
    //     for (int i = 0; i < devicesCount; i++)
    //     {
    //         std::cout << "--PROTO CLOSE START" << std::endl;
    //         libfreenect2::Freenect2Device *dev = *(freenect2Dev + i);;
    //         std::cout << "--PROTO device serial: " << dev->getSerialNumber() << std::endl;
    //         std::cout << "--PROTO device firmware: " << dev->getFirmwareVersion() << std::endl;
    //         if (dev->stop())
    //         {
    //             std::cout << "--PROTO device SUCCES stop" << std::endl;
    //         }
    //         else
    //         {
    //             std::cout << "--PROTO device ERROR stop" << std::endl;
    // //            return -1;
    //         }
    //         if (dev->close())
    //         {
    //             std::cout << "--PROTO device SUCCESS close" << std::endl;
    //         }
    //         else
    //         {
    //             std::cout << "--PROTO device ERROR close" << std::endl;
    //             //            return -1;
    //         }
    //         std::cout << "--PROTO device CLOSE END" << std::endl;
    //     }
    //     return 0;
    /// [test]
    
    if(pipeline)
    {
        /// [open]
        dev = freenect2.openDevice(serial, pipeline);
        /// [open]
    }
    else
    {
        dev = freenect2.openDevice(serial);
    }
    
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
    libfreenect2::Frame undistorted(512, 424, 4), registered(512, 424, 4);
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
    while(!protonect_shutdown && (framemax == (size_t)-1 || framecount < framemax))
    {
        if (!listener.waitForNewFrame(frames, 10*1000)) // 10 sconds
        {
            std::cout << "timeout!" << std::endl;
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
            if (framecount % 100 == 0)
            {
                auto nowTime = std::chrono::high_resolution_clock::now();
                auto nowTimeMiliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime.time_since_epoch()).count();
                std::cout << "Time now in milliseconds " << nowTimeMiliseconds << std::endl;
                std::cout << "The viewer is turned off. Received " << framecount << " frames. Ctrl-C to stop." << std::endl;
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

