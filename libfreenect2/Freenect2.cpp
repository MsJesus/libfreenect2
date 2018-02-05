//
//  Freenect2.cpp
//  Freenect2
//
//  Created by Aheyeu on 04/02/2018.
//  Copyright © 2018 Aheyeu. All rights reserved.
//

#include "Freenect2.h"

#include <libfreenect2/logging.h>

#define WRITE_LIBUSB_ERROR(__RESULT) libusb_error_name(__RESULT) << " " << libusb_strerror((libusb_error)__RESULT)


namespace libfreenect2 {


    Freenect2::Freenect2(void *usb_context) :
        impl_(new Freenect2Impl(usb_context))
    {
    }
    
    Freenect2::~Freenect2()
    {
        delete impl_;
    }
    
    int Freenect2::enumerateDevices()
    {
        impl_->clearDeviceEnumeration();
        return impl_->getNumDevices();
    }
    
    std::string Freenect2::getDeviceSerialNumber(int idx)
    {
        if (!impl_->initialized)
            return std::string();
        if (idx >= impl_->getNumDevices() || idx < 0)
            return std::string();
        
        return impl_->enumerated_devices_[idx].serial;
    }
    
    std::string Freenect2::getDefaultDeviceSerialNumber()
    {
        return getDeviceSerialNumber(0);
    }
    
    Freenect2Device *Freenect2::openDevice(int idx)
    {
        return impl_->openDevice(idx, impl_->createDefaultPacketPipeline());
    }

    Freenect2Device *Freenect2::openDevice(int idx, const std::string &pipeline)
    {
        return impl_->openDevice(idx, impl_->createPacketPipelineByName(pipeline));
    }

    Freenect2Device *Freenect2::openDevice(const std::string &serial)
    {
        return impl_->openDevice(serial, impl_->createDefaultPacketPipeline());
    }
    
    Freenect2Device *Freenect2::openDevice(const std::string &serial, const std::string& pipeline)
    {
        return impl_->openDevice(serial, impl_->createPacketPipelineByName(pipeline));
    }

    Freenect2Device *Freenect2::openDefaultDevice()
    {
        return impl_->openDefaultDevice(impl_->createDefaultPacketPipeline());
    }
    
    
    
    Freenect2Impl::Freenect2Impl(void *usb_context) :
    managed_usb_context_(usb_context == 0),
    usb_context_(reinterpret_cast<libusb_context *>(usb_context)),
    has_device_enumeration_(false),
    initialized(false)
    {
#ifdef __linux__
        if (libusb_get_version()->nano < 10952)
        {
            LOG_ERROR << "Your libusb does not support large iso buffer!";
            return;
        }
#endif
        
        if(managed_usb_context_)
        {
            int r = libusb_init(&usb_context_);
            if(r != 0)
            {
                LOG_ERROR << "failed to create usb context: " << WRITE_LIBUSB_ERROR(r);
                return;
            }
        }
        
        usb_event_loop_.start(usb_context_);
        initialized = true;
    }
    
    Freenect2Impl::~Freenect2Impl()
    {
        if (!initialized)
            return;
        
        clearDevices();
        clearDeviceEnumeration();
        
        usb_event_loop_.stop();
        
        if(managed_usb_context_ && usb_context_ != 0)
        {
            libusb_exit(usb_context_);
            usb_context_ = 0;
        }
    }
    
    void Freenect2Impl::addDevice(Freenect2DeviceImpl *device)
    {
        if (!initialized)
            return;
        
        devices_.push_back(device);
    }
    
    void Freenect2Impl::removeDevice(Freenect2DeviceImpl *device)
    {
        if (!initialized)
            return;
        
        DeviceVector::iterator it = std::find(devices_.begin(), devices_.end(), device);
        
        if(it != devices_.end())
        {
            devices_.erase(it);
        }
        else
        {
            LOG_WARNING << "tried to remove device, which is not in the internal device list!";
        }
    }
    
    bool Freenect2Impl::tryGetDevice(libusb_device *usb_device, Freenect2DeviceImpl **device)
    {
        if (!initialized)
            return false;
        
        for (DeviceVector::iterator it = devices_.begin(); it != devices_.end(); ++it)
        {
            if((*it)->isSameUsbDevice(usb_device))
            {
                *device = *it;
                return true;
            }
        }
        
        return false;
    }
    
    std::string Freenect2Impl::getBusAndAddress(libusb_device *usb_device)
    {
        std::stringstream stream;
        stream << "@" << int(libusb_get_bus_number(usb_device)) << ":" << int(libusb_get_device_address(usb_device));
        return stream.str();
    }

    
    void Freenect2Impl::clearDevices()
    {
        if (!initialized)
            return;
        
        DeviceVector devices(devices_.begin(), devices_.end());
        
        for(DeviceVector::iterator it = devices.begin(); it != devices.end(); ++it)
        {
            delete (*it);
        }
        
        if(!devices_.empty())
        {
            LOG_WARNING << "after deleting all devices the internal device list should be empty!";
        }
    }
    
    void Freenect2Impl::clearDeviceEnumeration()
    {
        if (!initialized)
            return;
        
        // free enumerated device pointers, this should not affect opened devices
        for(UsbDeviceVector::iterator it = enumerated_devices_.begin(); it != enumerated_devices_.end(); ++it)
        {
            libusb_unref_device(it->dev);
        }
        
        enumerated_devices_.clear();
        has_device_enumeration_ = false;
    }
    
    void Freenect2Impl::enumerateDevices()
    {
        if (!initialized)
            return;
        
        LOG_INFO << "enumerating devices...";
        libusb_device **device_list;
        int num_devices = libusb_get_device_list(usb_context_, &device_list);
        
        LOG_INFO << num_devices << " usb devices connected";
        
        if(num_devices > 0)
        {
            for(int idx = 0; idx < num_devices; ++idx)
            {
                libusb_device *dev = device_list[idx];
                libusb_device_descriptor dev_desc;
                
                int r = libusb_get_device_descriptor(dev, &dev_desc); // this is always successful
                
                if(dev_desc.idVendor == Freenect2Device::VendorId && (dev_desc.idProduct == Freenect2Device::ProductId || dev_desc.idProduct == Freenect2Device::ProductIdPreview))
                {
                    Freenect2DeviceImpl *freenect2_dev;
                    
                    // prevent error if device is already open
                    if(tryGetDevice(dev, &freenect2_dev))
                    {
                        UsbDeviceWithSerial dev_with_serial;
                        dev_with_serial.dev = dev;
                        dev_with_serial.serial = freenect2_dev->getSerialNumber();
                        
                        enumerated_devices_.push_back(dev_with_serial);
                        continue;
                    }
                    else
                    {
                        libusb_device_handle *dev_handle;
                        r = libusb_open(dev, &dev_handle);
                        
                        if(r == LIBUSB_SUCCESS)
                        {
                            unsigned char buffer[1024];
                            r = libusb_get_string_descriptor_ascii(dev_handle, dev_desc.iSerialNumber, buffer, sizeof(buffer));
                            // keep the ref until determined not kinect
                            libusb_ref_device(dev);
                            libusb_close(dev_handle);
                            
                            if(r > LIBUSB_SUCCESS)
                            {
                                UsbDeviceWithSerial dev_with_serial;
                                dev_with_serial.dev = dev;
                                dev_with_serial.serial = std::string(reinterpret_cast<char *>(buffer), size_t(r));
                                
                                LOG_INFO << "found valid Kinect v2 " << getBusAndAddress(dev) << " with serial " << dev_with_serial.serial;
                                // valid Kinect v2
                                enumerated_devices_.push_back(dev_with_serial);
                                continue;
                            }
                            else
                            {
                                libusb_unref_device(dev);
                                LOG_ERROR << "failed to get serial number of Kinect v2: " << getBusAndAddress(dev) << " " << WRITE_LIBUSB_ERROR(r);
                            }
                        }
                        else
                        {
                            LOG_ERROR << "failed to open Kinect v2: " << getBusAndAddress(dev) << " " << WRITE_LIBUSB_ERROR(r);
                        }
                    }
                }
                libusb_unref_device(dev);
            }
        }
        
        libusb_free_device_list(device_list, 0);
        has_device_enumeration_ = true;
        
        LOG_INFO << "found " << enumerated_devices_.size() << " devices";
    }
    
    int Freenect2Impl::getNumDevices()
    {
        if (!initialized)
            return 0;
        
        if(!has_device_enumeration_)
        {
            enumerateDevices();
        }
        return enumerated_devices_.size();
    }
    
    
    PacketPipeline *Freenect2Impl::createPacketPipelineByName(std::string name)
    {
        if (name == "cpu")
            return new CpuPacketPipeline();
        if (name == "dump")
            return new DumpPacketPipeline();
        if (name == "cl")
            return new OpenCLPacketPipeline();
        return NULL;
    }
    
    PacketPipeline *Freenect2Impl::createDefaultPacketPipeline()
    {
        const char *pipeline_env = std::getenv("LIBFREENECT2_PIPELINE");
        if (pipeline_env)
        {
            PacketPipeline *pipeline = createPacketPipelineByName(pipeline_env);
            if (pipeline)
                return pipeline;
            else
                LOG_WARNING << "`" << pipeline_env << "' pipeline is not available.";
        }
        
        return new OpenCLPacketPipeline();
    }

    
    Freenect2Device *Freenect2Impl::openDefaultDevice(const PacketPipeline *pipeline)
    {
        return openDevice(0, createDefaultPacketPipeline());
    }

    Freenect2Device *Freenect2Impl::openDevice(const std::string &serial, const PacketPipeline *pipeline)
    {
        Freenect2DeviceImpl *device = 0;
        int num_devices = getNumDevices();
        
        for(int idx = 0; idx < num_devices; ++idx)
        {
            if(enumerated_devices_[idx].serial == serial)
            {
                return openDevice(idx, pipeline);
            }
        }
        
        delete pipeline;
        return device;
    }

    Freenect2Device *Freenect2Impl::openDevice(int idx, const PacketPipeline *pipeline)
    {
        return openDevice(idx, pipeline, true);
    }
    
    Freenect2Device *Freenect2Impl::openDevice(int idx, const PacketPipeline *pipeline, bool attempting_reset)
    {
        int num_devices = getNumDevices();
        Freenect2DeviceImpl *device = 0;
        
        if(idx >= num_devices)
        {
            LOG_ERROR << "requested device " << idx << " is not connected!";
            delete pipeline;
            
            return device;
        }
        
        Freenect2Impl::UsbDeviceWithSerial &dev = enumerated_devices_[idx];
        libusb_device_handle *dev_handle;
        
        if(tryGetDevice(dev.dev, &device))
        {
            LOG_WARNING << "device " << getBusAndAddress(dev.dev)
            << " is already be open!";
            delete pipeline;
            
            return device;
        }
        
        int r;
        for (int i = 0; i < 10; i++)
        {
            r = libusb_open(dev.dev, &dev_handle);
            if(r == LIBUSB_SUCCESS)
            {
                break;
            }
            LOG_INFO << "device unavailable right now, retrying";
            this_thread::sleep_for(chrono::milliseconds(100));
        }
        
        if(r != LIBUSB_SUCCESS)
        {
            LOG_ERROR << "failed to open Kinect v2: " << getBusAndAddress(dev.dev) << " " << WRITE_LIBUSB_ERROR(r);
            delete pipeline;
            
            return device;
        }
        
        if(attempting_reset)
        {
            r = libusb_reset_device(dev_handle);
            
            LOG_INFO << "attempt reset" << r;
            
            if(r == LIBUSB_ERROR_NOT_FOUND)
            {
                // From libusb documentation:
                // "If the reset fails, the descriptors change, or the previous state
                // cannot be restored, the device will appear to be disconnected and
                // reconnected. This means that the device handle is no longer valid (you
                // should close it) and rediscover the device. A return code of
                // LIBUSB_ERROR_NOT_FOUND indicates when this is the case."
                
                // be a good citizen
                libusb_close(dev_handle);
                
                // HACK: wait for the planets to align... (When the reset fails it may
                // take a short while for the device to show up on the bus again. In the
                // absence of hotplug support, we just wait a little. If this code path
                // is followed there will already be a delay opening the device fully so
                // adding a little more is tolerable.)
                libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(1000));
                
                // reenumerate devices
                LOG_INFO << "re-enumerating devices after reset";
                clearDeviceEnumeration();
                enumerateDevices();
                
                // re-open without reset
                return openDevice(idx, pipeline, false);
            }
            else if(r != LIBUSB_SUCCESS)
            {
                LOG_ERROR << "failed to reset Kinect v2: " << getBusAndAddress(dev.dev) << " " << WRITE_LIBUSB_ERROR(r);

                delete pipeline;
                
                return device;
            }
            LOG_INFO << "attempt reset complete";
        }
        
        device = new Freenect2DeviceImpl(this, pipeline, dev.dev, dev_handle, dev.serial);
        addDevice(device);
        
        if(!device->open())
        {
            delete device;
            device = 0;
            
            LOG_ERROR << "failed to open Kinect v2: " << getBusAndAddress(dev.dev);
        }
        
        return device;
    }
}
