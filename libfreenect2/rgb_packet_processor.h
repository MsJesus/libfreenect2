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

/** @file rgb_packet_processor.h JPEG decoder processors. */

#ifndef RGB_PACKET_PROCESSOR_H_
#define RGB_PACKET_PROCESSOR_H_

#include <stddef.h>
#include <stdint.h>

#include <include/config.h>
#include <include/frame_listener.h>
#include "packet_processor.h"

namespace libfreenect2
{

/** Packet with JPEG data. */
struct RgbPacket
{
  uint32_t sequence;

  uint32_t timestamp;
  unsigned char *jpeg_buffer; ///< JPEG data.
  size_t jpeg_buffer_length;  ///< Length of the JPEG data.
  float exposure;
  float gain;
  float gamma;

  Buffer *memory;
};

typedef PacketProcessor<RgbPacket> BaseRgbPacketProcessor;

/** JPEG processor. */
class RgbPacketProcessor : public BaseRgbPacketProcessor
{
public:
  RgbPacketProcessor();
  virtual ~RgbPacketProcessor();

  virtual void setFrameListener(libfreenect2::FrameListener *listener);
protected:
  libfreenect2::FrameListener *listener_;
};

/** Class for dumping the JPEG information, eg to file. */
class DumpRgbPacketProcessorImpl;
class DumpRgbPacketProcessor : public RgbPacketProcessor
{
public:
  DumpRgbPacketProcessor();
  virtual ~DumpRgbPacketProcessor();
  virtual void process(const libfreenect2::RgbPacket &packet);
private:
  DumpRgbPacketProcessorImpl *impl_;
};

#ifdef LIBFREENECT2_WITH_TURBOJPEG_SUPPORT
class TurboJpegRgbPacketProcessorImpl;

/** Processor to decode JPEG to image, using TurboJpeg. */
class TurboJpegRgbPacketProcessor : public RgbPacketProcessor
{
public:
  TurboJpegRgbPacketProcessor();
  virtual ~TurboJpegRgbPacketProcessor();
  virtual void process(const libfreenect2::RgbPacket &packet);
  virtual const char *name() { return "TurboJPEG"; }
private:
  TurboJpegRgbPacketProcessorImpl *impl_; ///< Decoder implementation.
};
#endif


} /* namespace libfreenect2 */
#endif /* RGB_PACKET_PROCESSOR_H_ */
