//
//  Freenect2.hpp
//  Freenect2
//
//  Created by Aheyeu on 04/02/2018.
//  Copyright © 2018 Aheyeu. All rights reserved.
//

#ifndef Freenect2_hpp
#define Freenect2_hpp

#include <include/libfreenect2.h>

#include <libusb.h>

#include <libfreenect2/usb/EventLoop.h>
#include <libfreenect2/Freenect2Device.h>


namespace libfreenect2
{
    
    using namespace libfreenect2::usb;

    /** Freenect2 device storage and control. */
    class Freenect2Impl
    {
    private:
        bool managed_usb_context_;
        libusb_context *usb_context_;
        EventLoop usb_event_loop_;
    public:
        struct UsbDeviceWithSerial
        {
            libusb_device *dev;
            std::string serial;
        };
        typedef std::vector<UsbDeviceWithSerial> UsbDeviceVector;
        typedef std::vector<Freenect2DeviceImpl *> DeviceVector;
        
        bool has_device_enumeration_;
        UsbDeviceVector enumerated_devices_;
        DeviceVector devices_;
        
        bool initialized;
        
        Freenect2Impl(void *usb_context);
        
        ~Freenect2Impl();
        
        void addDevice(Freenect2DeviceImpl *device);
        void removeDevice(Freenect2DeviceImpl *device);
        
        bool tryGetDevice(libusb_device *usb_device, Freenect2DeviceImpl **device);
        std::string getBusAndAddress(libusb_device *usb_device);
        
        void clearDevices();
        
        void clearDeviceEnumeration();
        void enumerateDevices();
        
        int getNumDevices();
        
        PacketPipeline *createPacketPipelineByName(std::string name);
        PacketPipeline *createDefaultPacketPipeline();
        
        Freenect2Device *openDefaultDevice(const PacketPipeline *factory);
        Freenect2Device *openDevice(const std::string &serial, const PacketPipeline *factory);
        Freenect2Device *openDevice(int idx, const PacketPipeline *factory);
        Freenect2Device *openDevice(int idx, const PacketPipeline *factory, bool attempting_reset);
    };
    
}   /* namespace libfreenect2 */

#endif /* Freenect2_hpp */
