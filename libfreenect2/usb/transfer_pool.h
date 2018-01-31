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

#ifndef TRANSFER_POOL_H_
#define TRANSFER_POOL_H_

#include <atomic>
#include <vector>
#include <libusb.h>

#include <libfreenect2/data_callback.h>
#include <libfreenect2/threading.h>

namespace libfreenect2
{

namespace usb
{

class TransferPool
{
public:
  TransferPool(libusb_device_handle *device_handle, unsigned char device_endpoint);
  virtual ~TransferPool();

  void deallocate();

  void enableSubmission();

  void disableSubmission();

  bool enabled();

  bool submit();

  void cancel();

  void setCallback(DataCallback *callback);
protected:

    size_t counter = 0;
    std::atomic_ulong submittedCount;
    std::atomic_bool enable_submit_;
    std::atomic_bool submiting_;
    std::atomic_bool proccess_thread_shutdown_;

    libfreenect2::mutex stopped_mutex;
  struct Transfer
  {
      Transfer(libusb_transfer *transfer, TransferPool *pool):
      transfer(transfer),
      pool(pool),
      stopped(true),
      submited(false),
      proccessing(0)
      {}

      libusb_transfer *transfer;
      TransferPool *pool;
      std::atomic_bool stopped;
      std::atomic_bool submited;
      std::atomic_ulong proccessing;
      
      void setStopped(bool value)
      {
//          libfreenect2::lock_guard guard(pool->stopped_mutex);
          stopped = value;
      }
      bool getStopped()
      {
//          libfreenect2::lock_guard guard(pool->stopped_mutex);
          return stopped;
      }
      
      void setSubmited(bool value)
      {
//          libfreenect2::lock_guard guard(pool->stopped_mutex);
          submited = value;
      }
      bool getSubmited()
      {
//          libfreenect2::lock_guard guard(pool->stopped_mutex);
          return submited;
      }

      void setProccessing(size_t value)
      {
//          libfreenect2::lock_guard guard(pool->stopped_mutex);
          proccessing = value;
      }
      size_t getProccessing()
      {
//          libfreenect2::lock_guard guard(pool->stopped_mutex);
          return proccessing;
      }
  };

    typedef std::vector<std::unique_ptr<Transfer>> TransferQueue;
    TransferQueue transfers_;

  void allocateTransfers(size_t num_transfers, size_t transfer_size);

  virtual libusb_transfer *allocateTransfer() = 0;
  virtual void fillTransfer(libusb_transfer *transfer) = 0;

  virtual void processTransfer(libusb_transfer *transfer) = 0;
    
  virtual const char *poolName() = 0;

  DataCallback *callback_;
private:
//  typedef std::vector<Transfer> TransferQueue;

  libusb_device_handle *device_handle_;
  unsigned char device_endpoint_;

  unsigned char *buffer_;
  size_t buffer_size_;
    size_t num_submit_;


  static void onTransferCompleteStatic(libusb_transfer *transfer);
    void onTransferComplete(Transfer *transfer);

    libfreenect2::thread *proccess_thread_;
    void proccessExecute();
    libfreenect2::thread *submit_thread_;
    void submitExecute();
};

class BulkTransferPool : public TransferPool
{
public:
  BulkTransferPool(libusb_device_handle *device_handle, unsigned char device_endpoint);
  virtual ~BulkTransferPool();

  void allocate(size_t num_transfers, size_t transfer_size);

protected:
  virtual libusb_transfer *allocateTransfer();
  virtual void fillTransfer(libusb_transfer *transfer);
  virtual void processTransfer(libusb_transfer *transfer);
    
    virtual const char *poolName() { return "BULK USB"; };
};

class IsoTransferPool : public TransferPool
{
public:
  IsoTransferPool(libusb_device_handle *device_handle, unsigned char device_endpoint);
  virtual ~IsoTransferPool();

  void allocate(size_t num_transfers, size_t num_packets, size_t packet_size);

protected:
  virtual libusb_transfer *allocateTransfer();
  virtual void fillTransfer(libusb_transfer *transfer);
  virtual void processTransfer(libusb_transfer *transfer);

    virtual const char *poolName() { return "ISO USB"; };

private:
  size_t num_packets_;
  size_t packet_size_;
};

} /* namespace usb */
} /* namespace libfreenect2 */
#endif /* TRANSFER_POOL_H_ */
