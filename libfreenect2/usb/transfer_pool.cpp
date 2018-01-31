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

/** @file transfer_pool.cpp Data transfer implementation. */

#include <libfreenect2/usb/transfer_pool.h>
#include <libfreenect2/logging.h>
#include <iostream>

#define WRITE_LIBUSB_ERROR(__RESULT) libusb_error_name(__RESULT) << " " << libusb_strerror((libusb_error)__RESULT)

namespace libfreenect2
{
namespace usb
{

TransferPool::TransferPool(libusb_device_handle* device_handle, unsigned char device_endpoint) :
    callback_(nullptr),
    device_handle_(device_handle),
    device_endpoint_(device_endpoint),
//    buffer_(0),
//    buffer_size_(0),
//    _transfer_size(0),
    _enableSubmit(false),
    _enableThreads(false),
    _proccess_thread(nullptr),
    _submit_thread(nullptr)
{
}

TransferPool::~TransferPool()
{
  deallocate();
}

void TransferPool::enableSubmission()
{
  _enableSubmit = true;
}

void TransferPool::disableSubmission()
{
  _enableSubmit = false;
}

bool TransferPool::enabled()
{
  return _enableSubmit;
}

void TransferPool::deallocate()
{
    for(TransferVector::iterator it = _transfers.begin(); it != _transfers.end(); ++it)
    {
        auto element = it->get();
        libusb_free_transfer(element->transfer);
    }
    _transfers.clear();
    
    _buffers.clear();
//  if(buffer_ != 0)
//  {
//    delete[] buffer_;
//    buffer_ = 0;
//    buffer_size_ = 0;
//  }
}

bool TransferPool::submit()
{
  if(!_enableSubmit)
  {
    LOG_WARNING << "transfer submission disabled!";
    return false;
  }
    
    size_t failcount = 0;
  for(size_t i = 0; i < _transfers.size(); ++i)
  {
      auto element = _transfers[i].get();
      element->setStopped(false);
    libusb_transfer *transfer = element->transfer;

      int r = libusb_submit_transfer(transfer);
      
      if (r != LIBUSB_SUCCESS)
      {
          LOG_ERROR << "failed to submit transfer: " << WRITE_LIBUSB_ERROR(r);
          element->setStopped(true);
          failcount++;
      }
  }
    
  if (failcount == _transfers.size())
    {
        LOG_ERROR << "all submissions failed. Try debugging with environment variable: LIBUSB_DEBUG=3.";
        return false;
    }

    if (!_enableThreads)
    {
        if (_submit_thread == nullptr)
        {
            _submit_thread = new libfreenect2::thread(&TransferPool::submitThreadExecute, this);
        }
        if (_proccess_thread == nullptr)
        {
            _proccess_thread = new libfreenect2::thread(&TransferPool::proccessThreadExecute, this);
        }
        _enableThreads = true;
    }
    
  return true;
}

void TransferPool::cancel()
{
    if (_enableThreads)
    {
        _enableThreads = false;
        
        if (_proccess_thread != nullptr)
        {
            _proccess_thread->join();
            delete _proccess_thread;
            _proccess_thread = nullptr;
        }
        if (_submit_thread != nullptr)
        {
            _submit_thread->join();
            delete _submit_thread;
            _submit_thread = nullptr;
        }
    }

  for(TransferVector::iterator it = _transfers.begin(); it != _transfers.end(); ++it)
  {
      auto element = it->get();
      int r = libusb_cancel_transfer(element->transfer);
      if(r != LIBUSB_SUCCESS && r != LIBUSB_ERROR_NOT_FOUND)
      {
          LOG_ERROR << "failed to cancel transfer: " << WRITE_LIBUSB_ERROR(r);
      }
  }

  for(;;)
  {
    libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(100));
    size_t stopped_transfers = 0;
    for(TransferVector::iterator it = _transfers.begin(); it != _transfers.end(); ++it)
    {
        auto element = it->get();
        stopped_transfers += element->getStopped();
    }
    if (stopped_transfers == _transfers.size())
      break;
    LOG_INFO << "waiting for transfer cancellation";
    libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(1000));
  }
}

void TransferPool::setCallback(DataCallback *callback)
{
  callback_ = callback;
}
    
    

void TransferPool::allocatePool(size_t num_transfers, size_t transfer_size)
{
//    _transfer_size = transfer_size;
//  buffer_size_ = num_transfers * transfer_size;
//  buffer_ = new unsigned char[buffer_size_];
//  transfers_.reserve(num_transfers);
//    num_submit_ = num_transfers / 3;
    
//  unsigned char *ptr = buffer_;

  for (size_t i = 0; i < num_transfers; ++i)
  {
      _buffers.emplace_back(allocateBuffer());
      auto buffer = _buffers.back().get();
      
    _transfers.emplace_back(allocateTransfer());
      auto transfer = _transfers.back().get();
      transfer->buffer = buffer;

    transfer->transfer->dev_handle = device_handle_;
    transfer->transfer->endpoint = device_endpoint_;
    transfer->transfer->buffer = buffer->buffer;
    transfer->transfer->length = static_cast<int>(transfer_size);
    transfer->transfer->timeout = 1000;
    transfer->transfer->callback = (libusb_transfer_cb_fn) &TransferPool::onTransferCompleteStatic;
    transfer->transfer->user_data = transfer;

//    ptr += transfer_size;
  }
    
    for (size_t i = 0; i < num_transfers; ++i)
    {
        _buffers.emplace_back(allocateBuffer());
        auto buffer = _buffers.back().get();
        _avalaibleBuffers.push_back(buffer);
    }
}

void TransferPool::onTransferCompleteStatic(libusb_transfer* transfer)
{
  TransferPool::Transfer *t = reinterpret_cast<TransferPool::Transfer*>(transfer->user_data);
  t->pool->onTransferComplete(t);
}
    
    void TransferPool::onTransferComplete(libfreenect2::usb::TransferPool::Transfer *t)
    {
        if(t->transfer->status == LIBUSB_TRANSFER_CANCELLED)
        {
            t->setStopped(true);
            return;
        }
        
        _submitTransfers.push_back(t);
        processTransfer(t);
        _proccessBuffers.push_back(t->buffer);
        
        if(!_enableSubmit)
        {
            t->setStopped(true);
            return;
        }
     }
    
    void TransferPool::submitThreadExecute()
    {
        this_thread::set_name(poolName("SUBMIT"));
        while (_enableThreads)
        {
            auto pointer = _submitTransfers.pop_front_out();
            
            if (_enableSubmit)
            {
                if (_avalaibleBuffers.empty())
                {
                    _buffers.emplace_back(allocateBuffer());
                }
                
                auto pointerBuffer = _avalaibleBuffers.pop_front_out();
                pointer->transfer->buffer = pointerBuffer->buffer;
                
                int r = libusb_submit_transfer(pointer->transfer);
                if (r != LIBUSB_SUCCESS)
                {
                    LOG_ERROR << "failed to submit transfer: " << WRITE_LIBUSB_ERROR(r);
                    pointer->setStopped(true);
                }
            }
        }
    }

    void TransferPool::proccessThreadExecute()
    {
        this_thread::set_name(poolName("EXECUTE"));
        while (_enableThreads)
        {
            auto pointer = _proccessBuffers.pop_front_out();

            proccessBuffer(pointer);
            _avalaibleBuffers.push_back(pointer);
        }
    }
    


void BulkTransferPool::allocate(size_t num_transfers, size_t transfer_size)
{
    transfer_size_ = transfer_size;
    
  allocatePool(num_transfers, transfer_size_);
}

    TransferPool::Transfer* BulkTransferPool::allocateTransfer()
{
    libusb_transfer* transfer = libusb_alloc_transfer(0);
    transfer->type = LIBUSB_TRANSFER_TYPE_BULK;
    return new Transfer(transfer, this);
}
    
    TransferPool::Buffer* BulkTransferPool::allocateBuffer()
    {
        return new Buffer(1, transfer_size_);
    }

    
    void BulkTransferPool::processTransfer(Transfer* transfer)
    {
        auto buffer = transfer->buffer;
        buffer->actualStatusCompleted[0] = (transfer->transfer->status == LIBUSB_TRANSFER_COMPLETED);
        buffer->actualLength[0] = transfer->transfer->actual_length;
    }

    
    void BulkTransferPool::proccessBuffer(libfreenect2::usb::TransferPool::Buffer *buffer)
    {
        if (!buffer->actualStatusCompleted[0]) return;

        if(callback_)
            callback_->onDataReceived(buffer->buffer, buffer->actualLength[0]);
    }


void IsoTransferPool::allocate(size_t num_transfers, size_t num_packets, size_t packet_size)
{
  num_packets_ = num_packets;
  packet_size_ = packet_size;

    allocatePool(num_transfers, num_packets_ * packet_size_);
}

    
TransferPool::Transfer* IsoTransferPool::allocateTransfer()
{
    libusb_transfer* transfer = libusb_alloc_transfer(num_packets_);
    
    transfer->type = LIBUSB_TRANSFER_TYPE_ISOCHRONOUS;
    transfer->num_iso_packets = num_packets_;
    libusb_set_iso_packet_lengths(transfer, packet_size_);
    
  return new Transfer(transfer, this);
}

    
    TransferPool::Buffer* IsoTransferPool::allocateBuffer()
    {
        return new Buffer(num_packets_, packet_size_);
    }

    void IsoTransferPool::processTransfer(Transfer* transfer)
    {
        auto buffer = transfer->buffer;
        for(size_t i = 0; i < num_packets_; ++i)
        {
            auto desc = transfer->transfer->iso_packet_desc[i];
            buffer->actualStatusCompleted[i] = (desc.status == LIBUSB_TRANSFER_COMPLETED);
            buffer->actualLength[i] = desc.actual_length;
        }
    }

    void IsoTransferPool::proccessBuffer(libfreenect2::usb::TransferPool::Buffer *buffer)
    {
        unsigned char *ptr = buffer->buffer;
        for(size_t i = 0; i < num_packets_; ++i)
        {
            if (!buffer->actualStatusCompleted[i]) continue;
            
            if(callback_)
                callback_->onDataReceived(ptr, buffer->actualLength[i]);
            
            ptr += packet_size_;
        }
    }
    
} /* namespace usb */
} /* namespace libfreenect2 */

