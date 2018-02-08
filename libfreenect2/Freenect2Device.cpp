//
//  Freenect2Device.cpp
//  Freenect2
//
//  Created by Aheyeu on 04/02/2018.
//  Copyright © 2018 Aheyeu. All rights reserved.
//

#include "Freenect2Device.h"

#include <libfreenect2/Freenect2.h>

namespace libfreenect2 {

    Freenect2Device::Config::Config() :
        MinDepth(0.5f),
        MaxDepth(4.5f), //set to > 8000 for best performance when using the kde pipeline
        EnableBilateralFilter(false),
        EnableEdgeAwareFilter(false)
    {}

    Freenect2Device::~Freenect2Device()
    {
    }
    
    Freenect2DeviceImpl::Freenect2DeviceImpl(Freenect2Impl *context, const PacketPipeline *pipeline, libusb_device *usb_device, libusb_device_handle *usb_device_handle, const std::string &serial) :
        state_(Created),
        has_usb_interfaces_(false),
        context_(context),
        usb_device_(usb_device),
        usb_device_handle_(usb_device_handle),
        rgb_transfer_pool_(usb_device_handle, 0x83),
        ir_transfer_pool_(usb_device_handle, 0x84),
        usb_control_(usb_device_handle_),
        command_tx_(usb_device_handle_, 0x81, 0x02),
        command_seq_(0),
        pipeline_(pipeline),
        serial_(serial),
        firmware_("<unknown>")
    {
        rgb_transfer_pool_.setCallback(pipeline_->getRgbPacketParser());
        ir_transfer_pool_.setCallback(pipeline_->getIrPacketParser());
    }
    
    Freenect2DeviceImpl::~Freenect2DeviceImpl()
    {
        close();
        context_->removeDevice(this);
        
        delete pipeline_;
    }
    
    int Freenect2DeviceImpl::nextCommandSeq()
    {
        return command_seq_++;
    }
    
    bool Freenect2DeviceImpl::isSameUsbDevice(libusb_device* other)
    {
        bool result = false;
        
        if(state_ != Closed && usb_device_ != 0)
        {
            unsigned char bus = libusb_get_bus_number(usb_device_);
            unsigned char address = libusb_get_device_address(usb_device_);
            
            unsigned char other_bus = libusb_get_bus_number(other);
            unsigned char other_address = libusb_get_device_address(other);
            
            result = (bus == other_bus) && (address == other_address);
        }
        
        return result;
    }
    
    std::string Freenect2DeviceImpl::getSerialNumber()
    {
        return serial_;
    }
    
    std::string Freenect2DeviceImpl::getFirmwareVersion()
    {
        return firmware_;
    }
    
    Freenect2Device::ColorCameraParams Freenect2DeviceImpl::getColorCameraParams()
    {
        return rgb_camera_params_;
    }
    
    
    Freenect2Device::IrCameraParams Freenect2DeviceImpl::getIrCameraParams()
    {
        return ir_camera_params_;
    }
    
    void Freenect2DeviceImpl::setColorCameraParams(const Freenect2Device::ColorCameraParams &params)
    {
        rgb_camera_params_ = params;
    }
    
    void Freenect2DeviceImpl::setIrCameraParams(const Freenect2Device::IrCameraParams &params)
    {
        ir_camera_params_ = params;
        DepthPacketProcessor *proc = pipeline_->getDepthPacketProcessor();
        if (proc != 0)
        {
            IrCameraTables tables(params);
            proc->loadXZTables(&tables.xtable[0], &tables.ztable[0]);
            proc->loadLookupTable(&tables.lut[0]);
        }
    }
    
    
    void Freenect2DeviceImpl::setConfiguration(const Freenect2Device::Config &config)
    {
        DepthPacketProcessor *proc = pipeline_->getDepthPacketProcessor();
        if (proc != 0)
            proc->setConfiguration(config);
    }
    
    void Freenect2DeviceImpl::setColorFrameListener(libfreenect2::FrameListener* rgb_frame_listener)
    {
        // TODO: should only be possible, if not started
        if(pipeline_->getRgbPacketProcessor() != 0)
            pipeline_->getRgbPacketProcessor()->setFrameListener(rgb_frame_listener);
    }
    
    void Freenect2DeviceImpl::setIrAndDepthFrameListener(libfreenect2::FrameListener* ir_frame_listener)
    {
        // TODO: should only be possible, if not started
        if(pipeline_->getDepthPacketProcessor() != 0)
            pipeline_->getDepthPacketProcessor()->setFrameListener(ir_frame_listener);
    }
    
    bool Freenect2DeviceImpl::open()
    {
        LOG_INFO << "opening...";
        
        if(state_ != Created) return false;
        
        if(usb_control_.setConfiguration() != UsbControl::Success) return false;
        if(!has_usb_interfaces_ && usb_control_.claimInterfaces() != UsbControl::Success) return false;
        has_usb_interfaces_ = true;
        
        if(usb_control_.setIsochronousDelay() != UsbControl::Success) return false;
        // TODO: always fails right now with error 6 - TRANSFER_OVERFLOW!
        //if(usb_control_.setPowerStateLatencies() != UsbControl::Success) return false;
        if(usb_control_.setIrInterfaceState(UsbControl::Disabled) != UsbControl::Success) return false;
        if(usb_control_.enablePowerStates() != UsbControl::Success) return false;
        if(usb_control_.setVideoTransferFunctionState(UsbControl::Disabled) != UsbControl::Success) return false;
        
        int max_iso_packet_size;
        if(usb_control_.getIrMaxIsoPacketSize(max_iso_packet_size) != UsbControl::Success) return false;
        
        if(max_iso_packet_size < 0x8400)
        {
            LOG_ERROR << "max iso packet size for endpoint 0x84 too small! (expected: " << 0x8400 << " got: " << max_iso_packet_size << ")";
            return false;
        }
        
        unsigned rgb_xfer_size = 0x4000;
        unsigned rgb_num_xfers = 20;
        unsigned ir_pkts_per_xfer = 8;
        unsigned ir_num_xfers = 60;
        
#if defined(__APPLE__)
        ir_pkts_per_xfer = 128;
        ir_num_xfers = 8;
#elif defined(_WIN32) || defined(__WIN32__) || defined(__WINDOWS__)
        // For multi-Kinect setup, there is a 64 fd limit on poll().
        rgb_xfer_size = 1048576;
        rgb_num_xfers = 3;
        ir_pkts_per_xfer = 64;
        ir_num_xfers = 8;
#elif defined(__linux__)
        rgb_num_xfers = 8;
        ir_pkts_per_xfer = 64;
        ir_num_xfers = 5;
#endif
        
        const char *xfer_str;
        xfer_str = std::getenv("LIBFREENECT2_RGB_TRANSFER_SIZE");
        if(xfer_str) rgb_xfer_size = std::atoi(xfer_str);
        xfer_str = std::getenv("LIBFREENECT2_RGB_TRANSFERS");
        if(xfer_str) rgb_num_xfers = std::atoi(xfer_str);
        xfer_str = std::getenv("LIBFREENECT2_IR_PACKETS");
        if(xfer_str) ir_pkts_per_xfer = std::atoi(xfer_str);
        xfer_str = std::getenv("LIBFREENECT2_IR_TRANSFERS");
        if(xfer_str) ir_num_xfers = std::atoi(xfer_str);
        
        LOG_INFO << "transfer pool sizes"
        << " rgb: " << rgb_num_xfers << "*" << rgb_xfer_size
        << " ir: " << ir_num_xfers << "*" << ir_pkts_per_xfer << "*" << max_iso_packet_size;
        rgb_transfer_pool_.allocate(rgb_num_xfers, rgb_xfer_size);
        ir_transfer_pool_.allocate(ir_num_xfers, ir_pkts_per_xfer, max_iso_packet_size);
        
        state_ = Open;
        
        LOG_INFO << "opened";
        
        return true;
    }
    
    bool Freenect2DeviceImpl::start()
    {
        return startStreams(true, true);
    }
    
    bool Freenect2DeviceImpl::startStreams(bool enable_rgb, bool enable_depth)
    {
        LOG_INFO << "starting...";
        if(state_ != Open) return false;
        
        CommandTransaction::Result serial_result, firmware_result, result;
        
        if (usb_control_.setVideoTransferFunctionState(UsbControl::Enabled) != UsbControl::Success) return false;
        
        if (!command_tx_.execute(ReadFirmwareVersionsCommand(nextCommandSeq()), firmware_result)) return false;
        firmware_ = FirmwareVersionResponse(firmware_result).toString();
        
        if (!command_tx_.execute(ReadHardwareInfoCommand(nextCommandSeq()), result)) return false;
        //The hardware version is currently useless.  It is only used to select the
        //IR normalization table, but we don't have that.
        
        if (!command_tx_.execute(ReadSerialNumberCommand(nextCommandSeq()), serial_result)) return false;
        std::string new_serial = SerialNumberResponse(serial_result).toString();
        
        if(serial_ != new_serial)
        {
            LOG_WARNING << "serial number reported by libusb " << serial_ << " differs from serial number " << new_serial << " in device protocol! ";
        }
        
        if (!command_tx_.execute(ReadDepthCameraParametersCommand(nextCommandSeq()), result)) return false;
        setIrCameraParams(DepthCameraParamsResponse(result).toIrCameraParams());
        
        if (!command_tx_.execute(ReadP0TablesCommand(nextCommandSeq()), result)) return false;
        if(pipeline_->getDepthPacketProcessor() != 0)
            pipeline_->getDepthPacketProcessor()->loadP0TablesFromCommandResponse(&result[0], result.size());
        
        if (!command_tx_.execute(ReadRgbCameraParametersCommand(nextCommandSeq()), result)) return false;
        setColorCameraParams(RgbCameraParamsResponse(result).toColorCameraParams());
        
        if (!command_tx_.execute(SetModeEnabledWith0x00640064Command(nextCommandSeq()), result)) return false;
        if (!command_tx_.execute(SetModeDisabledCommand(nextCommandSeq()), result)) return false;
        
        int timeout = 50; // about 5 seconds (100ms x 50)
        for (uint32_t status = 0, last = 0; (status & 1) == 0 && 0 < timeout; last = status, timeout--)
        {
            if (!command_tx_.execute(ReadStatus0x090000Command(nextCommandSeq()), result)) return false;
            status = Status0x090000Response(result).toNumber();
            if (status != last)
                LOG_DEBUG << "status 0x090000: " << status;
            if ((status & 1) == 0)
                this_thread::sleep_for(chrono::milliseconds(100));
        }
        if (timeout == 0) {
            LOG_DEBUG << "status 0x090000: timeout";
        }
        
        if (!command_tx_.execute(InitStreamsCommand(nextCommandSeq()), result)) return false;
        
        if (usb_control_.setIrInterfaceState(UsbControl::Enabled) != UsbControl::Success) return false;
        
        if (!command_tx_.execute(ReadStatus0x090000Command(nextCommandSeq()), result)) return false;
        LOG_DEBUG << "status 0x090000: " << Status0x090000Response(result).toNumber();
        
        if (!command_tx_.execute(SetStreamEnabledCommand(nextCommandSeq()), result)) return false;
        
        //command_tx_.execute(Unknown0x47Command(nextCommandSeq()), result);
        //command_tx_.execute(Unknown0x46Command(nextCommandSeq()), result);
        /*
         command_tx_.execute(SetModeEnabledCommand(nextCommandSeq()), result);
         command_tx_.execute(SetModeDisabledCommand(nextCommandSeq()), result);
         
         usb_control_.setIrInterfaceState(UsbControl::Enabled);
         
         command_tx_.execute(SetModeEnabledWith0x00640064Command(nextCommandSeq()), result);
         command_tx_.execute(ReadData0x26Command(nextCommandSeq()), result);
         command_tx_.execute(ReadStatus0x100007Command(nextCommandSeq()), result);
         command_tx_.execute(SetModeEnabledWith0x00500050Command(nextCommandSeq()), result);
         command_tx_.execute(ReadData0x26Command(nextCommandSeq()), result);
         command_tx_.execute(ReadStatus0x100007Command(nextCommandSeq()), result);
         command_tx_.execute(ReadData0x26Command(nextCommandSeq()), result);
         command_tx_.execute(ReadData0x26Command(nextCommandSeq()), result);
         */
        if (enable_rgb)
        {
            LOG_INFO << "submitting rgb transfers...";
            rgb_transfer_pool_.enableSubmission();
            if (!rgb_transfer_pool_.submit()) return false;
        }
        
        if (enable_depth)
        {
            LOG_INFO << "submitting depth transfers...";
            ir_transfer_pool_.enableSubmission();
            if (!ir_transfer_pool_.submit()) return false;
        }
        
        state_ = Streaming;
        LOG_INFO << "started";
        return true;
    }
    
    bool Freenect2DeviceImpl::stop()
    {
        LOG_INFO << "stopping...";
        
        if(state_ != Streaming)
        {
            LOG_INFO << "already stopped, doing nothing";
            return false;
        }
        
        if (rgb_transfer_pool_.enabled())
        {
            LOG_INFO << "canceling rgb transfers...";
            rgb_transfer_pool_.disableSubmission();
            rgb_transfer_pool_.cancel();
        }
        
        if (ir_transfer_pool_.enabled())
        {
            LOG_INFO << "canceling depth transfers...";
            ir_transfer_pool_.disableSubmission();
            ir_transfer_pool_.cancel();
        }
        
        if (usb_control_.setIrInterfaceState(UsbControl::Disabled) != UsbControl::Success) return false;
        
        CommandTransaction::Result result;
        if (!command_tx_.execute(SetModeEnabledWith0x00640064Command(nextCommandSeq()), result)) return false;
        if (!command_tx_.execute(SetModeDisabledCommand(nextCommandSeq()), result)) return false;
        if (!command_tx_.execute(StopCommand(nextCommandSeq()), result)) return false;
        if (!command_tx_.execute(SetStreamDisabledCommand(nextCommandSeq()), result)) return false;
        if (!command_tx_.execute(SetModeEnabledCommand(nextCommandSeq()), result)) return false;
        if (!command_tx_.execute(SetModeDisabledCommand(nextCommandSeq()), result)) return false;
        if (!command_tx_.execute(SetModeEnabledCommand(nextCommandSeq()), result)) return false;
        if (!command_tx_.execute(SetModeDisabledCommand(nextCommandSeq()), result)) return false;
        
        if (usb_control_.setVideoTransferFunctionState(UsbControl::Disabled) != UsbControl::Success) return false;
        
        state_ = Open;
        LOG_INFO << "stopped";
        return true;
    }
    
    bool Freenect2DeviceImpl::close()
    {
        LOG_INFO << "closing...";
        
        if(state_ == Closed)
        {
            LOG_INFO << "already closed, doing nothing";
            return true;
        }
        
        if(state_ == Streaming)
        {
            stop();
        }
        
        CommandTransaction::Result result;
        command_tx_.execute(SetModeEnabledWith0x00640064Command(nextCommandSeq()), result);
        command_tx_.execute(SetModeDisabledCommand(nextCommandSeq()), result);
        /* This command actually reboots the device and makes it disappear for 3 seconds.
         * Protonect can restart instantly without it.
         */
#ifdef __APPLE__
        /* Kinect will disappear on Mac OS X regardless during close().
         * Painstaking effort could not determine the root cause.
         * See https://github.com/OpenKinect/libfreenect2/issues/539
         *
         * Shut down Kinect explicitly on Mac and wait a fixed time.
         */
        command_tx_.execute(ShutdownCommand(nextCommandSeq()), result);
        libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(4*1000));
#endif
        
        if(pipeline_->getRgbPacketProcessor() != 0)
            pipeline_->getRgbPacketProcessor()->setFrameListener(0);
        
        if(pipeline_->getDepthPacketProcessor() != 0)
            pipeline_->getDepthPacketProcessor()->setFrameListener(0);
        
        if(has_usb_interfaces_)
        {
            LOG_INFO << "releasing usb interfaces...";
            
            usb_control_.releaseInterfaces();
            has_usb_interfaces_ = false;
        }
        
        LOG_INFO << "deallocating usb transfer pools...";
        rgb_transfer_pool_.deallocate();
        ir_transfer_pool_.deallocate();
        
        LOG_INFO << "closing usb device...";
        
        libusb_close(usb_device_handle_);
        usb_device_handle_ = 0;
        usb_device_ = 0;
        
        state_ = Closed;
        LOG_INFO << "closed";
        return true;
    }
    
}
