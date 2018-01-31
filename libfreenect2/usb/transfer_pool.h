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
#include <queue>
#include <string>
#include <libusb.h>

#include <libfreenect2/data_callback.h>
#include <libfreenect2/threading.h>
#include <libfreenect2/usb/collection.h>

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

    std::atomic_bool _enableSubmit;
    std::atomic_bool _enableThreads;

    
    struct Buffer
    {
        unsigned char *buffer;
        size_t bufferSize;
        
        unsigned int *actualLength;
        bool *actualStatusCompleted;
        
        Buffer(size_t numberPackets, size_t sizePackets):
        buffer(nullptr),
        bufferSize(numberPackets * sizePackets),
        actualLength(nullptr),
        actualStatusCompleted(nullptr)
        {
            buffer = new unsigned char[bufferSize];
            
            actualLength = new unsigned int[numberPackets];
            actualStatusCompleted = new bool[numberPackets];
        };
        
        ~Buffer()
        {
            if (buffer != nullptr)
            {
                delete[] buffer;
                buffer = nullptr;
            }
            if (actualLength != nullptr)
            {
                delete[] actualLength;
                actualLength = nullptr;
            }
            if (actualStatusCompleted != nullptr)
            {
                delete[] actualStatusCompleted;
                actualStatusCompleted = nullptr;
            }
            bufferSize = 0;
        }
    };

  struct Transfer
  {
      Transfer(libusb_transfer *transfer, TransferPool *pool):
      transfer(transfer),
      pool(pool),
      buffer(nullptr),
      stopped(true)
      {}

      libusb_transfer *transfer;
      TransferPool *pool;
      std::unique_ptr<Buffer> buffer;
      std::atomic_bool stopped;
      
      void setStopped(bool value)
      {
          stopped = value;
      }
      bool getStopped()
      {
          return stopped;
      }
  };
    

    typedef std::vector<std::unique_ptr<Transfer>> TransferVector;
    TransferVector _transfers;
//
//    typedef std::vector<std::unique_ptr<Buffer>> BufferVector;
//    BufferVector _buffers;

    typedef Collection<std::deque<Transfer *>> SyncTransferQueue;
    SyncTransferQueue _submitTransfers;

    typedef Collection<std::deque<std::unique_ptr<Buffer>>> SyncBufferQueue;
    SyncBufferQueue _proccessBuffers;
    SyncBufferQueue _avalaibleBuffers;

  void allocatePool(size_t num_transfers, size_t transfer_size);

    virtual std::unique_ptr<Transfer> allocateTransfer() = 0;
    virtual std::unique_ptr<Buffer> allocateBuffer() = 0;
    virtual void processTransfer(Transfer *transfer) = 0;
    virtual void proccessBuffer(Buffer* buffer) = 0;

  virtual const char *poolName(std::string suffix) = 0;

  DataCallback *callback_;
    
private:

  libusb_device_handle *device_handle_;
  unsigned char device_endpoint_;

//  unsigned char *buffer_;
//  size_t buffer_size_;
//    size_t _transfer_size;


  static void onTransferCompleteStatic(libusb_transfer *transfer);
    void onTransferComplete(Transfer *transfer);

    libfreenect2::thread *_proccess_thread;
    void proccessThreadExecute();
    libfreenect2::thread *_submit_thread;
    void submitThreadExecute();
};

class BulkTransferPool : public TransferPool
{
public:
    BulkTransferPool(libusb_device_handle *device_handle, unsigned char device_endpoint):
    TransferPool(device_handle, device_endpoint)
    {};
    
    virtual ~BulkTransferPool() {};

  void allocate(size_t num_transfers, size_t transfer_size);

protected:
    virtual std::unique_ptr<Transfer> allocateTransfer();
    virtual std::unique_ptr<Buffer> allocateBuffer();

    virtual void processTransfer(Transfer *transfer);
    virtual void proccessBuffer(Buffer* buffer);

    
    virtual const char *poolName(std::string suffix) { return (std::string("BULK USB ") + suffix).c_str(); };
    
private:
    size_t transfer_size_;
};

class IsoTransferPool : public TransferPool
{
public:
    IsoTransferPool(libusb_device_handle *device_handle, unsigned char device_endpoint):
    TransferPool(device_handle, device_endpoint),
    num_packets_(0),
    packet_size_(0)
    {};

    virtual ~IsoTransferPool() {};

    void allocate(size_t num_transfers, size_t num_packets, size_t packet_size);

protected:
    virtual std::unique_ptr<Transfer> allocateTransfer();
    virtual std::unique_ptr<Buffer> allocateBuffer();

    virtual void processTransfer(Transfer *transfer);
    virtual void proccessBuffer(Buffer* buffer);

    virtual const char *poolName(std::string suffix) { return (std::string("ISO USB ") + suffix).c_str(); };

private:
  size_t num_packets_;
  size_t packet_size_;
};

} /* namespace usb */
} /* namespace libfreenect2 */
#endif /* TRANSFER_POOL_H_ */
