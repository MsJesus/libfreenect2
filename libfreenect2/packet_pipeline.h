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

/** @file packet_pipeline.h Packet pipe line definitions. */

#ifndef PACKET_PIPELINE_H_
#define PACKET_PIPELINE_H_

#include <stdlib.h>

#include <include/libfreenect2.h>
#include <libfreenect2/usb/DataCallback.h>

namespace libfreenect2
{

class RgbPacketProcessor;
class DepthPacketProcessor;
class PacketPipelineComponents;

/** @defgroup pipeline Packet Pipelines
 * Implement various methods to decode color and depth images with different performance and platform support
 *
 * You can construct a specific PacketPipeline object and provide it to Freenect2::openDevice().
 */
///@{

/** Base class for other pipeline classes.
 * Methods in this class are reserved for internal use.
 */
class LIBFREENECT2_API PacketPipeline
{
public:
  typedef usb::DataCallback PacketParser;

  PacketPipeline();
  virtual ~PacketPipeline();

  virtual PacketParser *getRgbPacketParser() const;
  virtual PacketParser *getIrPacketParser() const;

  virtual RgbPacketProcessor *getRgbPacketProcessor() const;
  virtual DepthPacketProcessor *getDepthPacketProcessor() const;
protected:
  PacketPipelineComponents *comp_;
};

 class LIBFREENECT2_API DumpPacketPipeline: public PacketPipeline
 {
 public:
   DumpPacketPipeline();
   virtual ~DumpPacketPipeline();
 };

/** Pipeline with CPU depth processing. */
class LIBFREENECT2_API CpuPacketPipeline : public PacketPipeline
{
public:
  CpuPacketPipeline();
  virtual ~CpuPacketPipeline();
};

    
    /** Pipeline with OpenCL depth processing. */
    class LIBFREENECT2_API OpenCLPacketPipeline : public PacketPipeline
    {
    public:
        OpenCLPacketPipeline();
        virtual ~OpenCLPacketPipeline();
    };

///@}
} /* namespace libfreenect2 */
#endif /* PACKET_PIPELINE_H_ */
