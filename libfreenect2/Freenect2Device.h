//
//  Freenect2Device.hpp
//  Freenect2
//
//  Created by Aheyeu on 04/02/2018.
//  Copyright © 2018 Aheyeu. All rights reserved.
//

#ifndef Freenect2Device_hpp
#define Freenect2Device_hpp

#include <algorithm>
#include <cmath>

#include <include/libfreenect2.h>

#include <libusb.h>

#include <libfreenect2/usb/EventLoop.h>
#include <libfreenect2/usb/TransferPool.h>
#include <libfreenect2/protocol/usb_control.h>
#include <libfreenect2/protocol/command.h>
#include <libfreenect2/protocol/response.h>
#include <libfreenect2/protocol/command_transaction.h>

#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/depth_packet_processor.h>
#include <libfreenect2/rgb_packet_processor.h>

#include <libfreenect2/logging.h>


namespace libfreenect2 {

    using namespace libfreenect2;
    using namespace libfreenect2::usb;
    using namespace libfreenect2::protocol;
    
    /*
     For detailed analysis see https://github.com/OpenKinect/libfreenect2/issues/144
     
     The following discussion is in no way authoritative. It is the current best
     explanation considering the hardcoded parameters and decompiled code.
     
     p0 tables are the "initial shift" of phase values, as in US8587771 B2.
     
     Three p0 tables are used for "disamgibuation" in the first half of stage 2
     processing.
     
     At the end of stage 2 processing:
     
     phase_final is the phase shift used to compute the travel distance.
     
     What is being measured is max_depth (d), the total travel distance of the
     reflected ray.
     
     But what we want is depth_fit (z), the distance from reflection to the XY
     plane. There are two issues: the distance before reflection is not needed;
     and the measured ray is not normal to the XY plane.
     
     Suppose L is the distance between the light source and the focal point (a
     fixed constant), and xu,yu is the undistorted and normalized coordinates for
     each measured pixel at unit depth.
     
     Through some derivation, we have
     
     z = (d*d - L*L)/(d*sqrt(xu*xu + yu*yu + 1) - xu*L)/2.
     
     The expression in stage 2 processing is a variant of this, with the term
     `-L*L` removed. Detailed derivation can be found in the above issue.
     
     Here, the two terms `sqrt(xu*xu + yu*yu + 1)` and `xu` requires undistorted
     coordinates, which is hard to compute in real-time because the inverse of
     radial and tangential distortion has no analytical solutions and requires
     numeric methods to solve. Thus these two terms are precomputed once and
     their variants are stored as ztable and xtable respectively.
     
     Even though x/ztable is derived with undistortion, they are only used to
     correct the effect of distortion on the z value. Image warping is needed for
     correcting distortion on x-y value, which happens in registration.cpp.
     */
    struct IrCameraTables: Freenect2Device::IrCameraParams
    {
        std::vector<float> xtable;
        std::vector<float> ztable;
        std::vector<short> lut;
        
        IrCameraTables(const Freenect2Device::IrCameraParams &parent):
        Freenect2Device::IrCameraParams(parent),
        xtable(DepthPacketProcessor::TABLE_SIZE),
        ztable(DepthPacketProcessor::TABLE_SIZE),
        lut(DepthPacketProcessor::LUT_SIZE)
        {
            const double scaling_factor = 8192;
            const double unambigious_dist = 6250.0/3;
            size_t divergence = 0;
            for (size_t i = 0; i < DepthPacketProcessor::TABLE_SIZE; i++)
            {
                size_t xi = i % 512;
                size_t yi = i / 512;
                double xd = (xi + 0.5 - cx)/fx;
                double yd = (yi + 0.5 - cy)/fy;
                double xu, yu;
                divergence += !undistort(xd, yd, xu, yu);
                xtable[i] = scaling_factor*xu;
                ztable[i] = unambigious_dist/sqrt(xu*xu + yu*yu + 1);
            }
            
            if (divergence > 0)
                LOG_ERROR << divergence << " pixels in x/ztable have incorrect undistortion.";
            
            short y = 0;
            for (int x = 0; x < 1024; x++)
            {
                unsigned inc = 1 << (x/128 - (x>=128));
                lut[x] = y;
                lut[1024 + x] = -y;
                y += inc;
            }
            lut[1024] = 32767;
        }
        
        //x,y: undistorted, normalized coordinates
        //xd,yd: distorted, normalized coordinates
        void distort(double x, double y, double &xd, double &yd) const
        {
            double x2 = x * x;
            double y2 = y * y;
            double r2 = x2 + y2;
            double xy = x * y;
            double kr = ((k3 * r2 + k2) * r2 + k1) * r2 + 1.0;
            xd = x*kr + p2*(r2 + 2*x2) + 2*p1*xy;
            yd = y*kr + p1*(r2 + 2*y2) + 2*p2*xy;
        }
        
        //The inverse of distort() using Newton's method
        //Return true if converged correctly
        //This function considers tangential distortion with double precision.
        bool undistort(double x, double y, double &xu, double &yu) const
        {
            double x0 = x;
            double y0 = y;
            
            double last_x = x;
            double last_y = y;
            const int max_iterations = 100;
            int iter;
            for (iter = 0; iter < max_iterations; iter++) {
                double x2 = x*x;
                double y2 = y*y;
                double x2y2 = x2 + y2;
                double x2y22 = x2y2*x2y2;
                double x2y23 = x2y2*x2y22;
                
                //Jacobian matrix
                double Ja = k3*x2y23 + (k2+6*k3*x2)*x2y22 + (k1+4*k2*x2)*x2y2 + 2*k1*x2 + 6*p2*x + 2*p1*y + 1;
                double Jb = 6*k3*x*y*x2y22 + 4*k2*x*y*x2y2 + 2*k1*x*y + 2*p1*x + 2*p2*y;
                double Jc = Jb;
                double Jd = k3*x2y23 + (k2+6*k3*y2)*x2y22 + (k1+4*k2*y2)*x2y2 + 2*k1*y2 + 2*p2*x + 6*p1*y + 1;
                
                //Inverse Jacobian
                double Jdet = 1/(Ja*Jd - Jb*Jc);
                double a = Jd*Jdet;
                double b = -Jb*Jdet;
                double c = -Jc*Jdet;
                double d = Ja*Jdet;
                
                double f, g;
                distort(x, y, f, g);
                f -= x0;
                g -= y0;
                
                x -= a*f + b*g;
                y -= c*f + d*g;
                const double eps = std::numeric_limits<double>::epsilon()*16;
                if (fabs(x - last_x) <= eps && fabs(y - last_y) <= eps)
                    break;
                last_x = x;
                last_y = y;
            }
            xu = x;
            yu = y;
            return iter < max_iterations;
        }
    };


    /** Freenect2 device implementation. */
    class Freenect2DeviceImpl : public virtual Freenect2Device
    {
    private:
        enum State
        {
            Created,
            Open,
            Streaming,
            Closed
        };
        
        State state_;
        bool has_usb_interfaces_;
        
        Freenect2Impl *context_;
        libusb_device *usb_device_;
        libusb_device_handle *usb_device_handle_;
        
        BulkTransferPool rgb_transfer_pool_;
        IsoTransferPool ir_transfer_pool_;
        
        UsbControl usb_control_;
        CommandTransaction command_tx_;
        int command_seq_;
        
        const PacketPipeline *pipeline_;
        std::string serial_, firmware_;
        Freenect2Device::IrCameraParams ir_camera_params_;
        Freenect2Device::ColorCameraParams rgb_camera_params_;
    public:
        Freenect2DeviceImpl(Freenect2Impl *context, const PacketPipeline *pipeline, libusb_device *usb_device, libusb_device_handle *usb_device_handle, const std::string &serial);
        virtual ~Freenect2DeviceImpl();
        
        bool isSameUsbDevice(libusb_device* other);
        
        virtual std::string getSerialNumber();
        virtual std::string getFirmwareVersion();
        
        virtual Freenect2Device::ColorCameraParams getColorCameraParams();
        virtual Freenect2Device::IrCameraParams getIrCameraParams();
        virtual void setColorCameraParams(const Freenect2Device::ColorCameraParams &params);
        virtual void setIrCameraParams(const Freenect2Device::IrCameraParams &params);
        virtual void setConfiguration(const Freenect2Device::Config &config);
        
        int nextCommandSeq();
        
        bool open();
        
        virtual void setColorFrameListener(libfreenect2::FrameListener* rgb_frame_listener);
        virtual void setIrAndDepthFrameListener(libfreenect2::FrameListener* ir_frame_listener);
        virtual bool start();
        virtual bool startStreams(bool rgb, bool depth);
        virtual bool stop();
        virtual bool close();
    };

}   /* namespace libfreenect2 */
#endif /* Freenect2Device_hpp */
