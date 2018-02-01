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
    _enableSubmit(false),
    _enableThreads(false),
    _callback(nullptr),
    device_handle_(device_handle),
    device_endpoint_(device_endpoint),
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
    
    void TransferPool::allocate(size_t num_transfers, size_t transfer_size)
    {
        for (size_t i = 0; i < (10 * num_transfers); ++i)
        {
            _avalaibleBuffers.push_back_move(allocateBuffer());
        }
        
        for (size_t i = 0; i < num_transfers; ++i)
        {
            auto transfer = allocateTransfer();
            
            transfer->setStopped(false);
            transfer->transfer->dev_handle = device_handle_;
            transfer->transfer->endpoint = device_endpoint_;
            //    transfer->transfer->buffer = buffer->buffer;
            transfer->transfer->length = static_cast<int>(transfer_size);
            transfer->transfer->timeout = 1000;
            transfer->transfer->callback = (libusb_transfer_cb_fn) &TransferPool::onTransferCompleteStatic;
            transfer->transfer->user_data = transfer.get();
            
            _transfers.push_back(std::move(transfer));
        }
    }
    

    void TransferPool::deallocate()
    {
        for(TransferVector::iterator it = _transfers.begin(); it != _transfers.end(); ++it)
        {
            auto element = it->get();
            libusb_free_transfer(element->transfer);
        }
        _transfers.clear();
    }

    
    bool TransferPool::submit()
    {
        if (!_enableSubmit)
        {
            LOG_WARNING << "transfer submission disabled!";
            return false;
        }
        
        for (const auto& transfer : _transfers)
        {
            _submitTransfers.push_back(transfer.get());
        }
        
        if (!_enableThreads)
        {
            _enableThreads = true;
            if (_submit_thread == nullptr)
            {
                _submit_thread = new libfreenect2::thread(&TransferPool::submitThreadExecute, this);
            }
            if (_proccess_thread == nullptr)
            {
                _proccess_thread = new libfreenect2::thread(&TransferPool::proccessThreadExecute, this);
            }
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
        
        for (const auto& transfer : _transfers)
        {
            auto element = transfer.get();
            int r = libusb_cancel_transfer(element->transfer);
            if (r != LIBUSB_SUCCESS && r != LIBUSB_ERROR_NOT_FOUND)
            {
                LOG_ERROR << "failed to cancel transfer: " << WRITE_LIBUSB_ERROR(r);
            }
        }        
        
        for(;;)
        {
            libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(100));
            size_t stopped_transfers = 0;
            for (const auto& transfer : _transfers)
            {
                auto element = transfer.get();
                stopped_transfers += element->getStopped();
            }
            if (stopped_transfers == _transfers.size())
                break;
            LOG_INFO << "waiting for transfer cancellation";
            libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(1000));
        }

        _submitTransfers.clear();
        _proccessBuffers.clear();
        _avalaibleBuffers.clear();

        LOG_INFO << "complete transfer cancellation";
    }

    
    void TransferPool::setCallback(DataCallback *callback)
    {
        _callback = callback;
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
            LOG_ERROR << "usb transfer canceled";
            return;
        }
        
        processTransfer(t);
        _proccessBuffers.push_back_move(std::move(t->buffer));
        _submitTransfers.push_back(t);
     }
    
    void TransferPool::submitThreadExecute()
    {
        this_thread::set_name(poolName("SUBMIT"));
        size_t failcount = 0;
        size_t allTransfers = _transfers.size();
        while (_enableThreads)
        {
            auto pointer = _submitTransfers.pop_front_out();
            
            if (_enableSubmit)
            {
                if (_avalaibleBuffers.empty())
                {
                    _avalaibleBuffers.push_back_move(allocateBuffer());
                }
                
                {
                    auto pointerBuffer = _avalaibleBuffers.pop_front_out();
                    pointer->transfer->buffer = pointerBuffer->buffer;
                    pointer->buffer = std::move(pointerBuffer);
                }
                
                int r = libusb_submit_transfer(pointer->transfer);
                if (r != LIBUSB_SUCCESS)
                {
                    LOG_ERROR << "failed to submit transfer: " << WRITE_LIBUSB_ERROR(r);
                    pointer->setStopped(true);
                    failcount++;
                }
                
                if (failcount == allTransfers)
                {
                    LOG_ERROR << "all submissions failed. Try debugging with environment variable: LIBUSB_DEBUG=3.";
                }
            }
            else
            {
                pointer->setStopped(true);
                LOG_INFO << "stop transfer cancellation";
            }
        }
    }

    void TransferPool::proccessThreadExecute()
    {
        this_thread::set_name(poolName("EXECUTE"));
        while (_enableThreads)
        {
            auto pointer = _proccessBuffers.pop_front_out();

            proccessBuffer(pointer.get());
            _avalaibleBuffers.push_back_move(std::move(pointer));
        }
    }
    


    void BulkTransferPool::allocate(size_t num_transfers, size_t transfer_size)
    {
        transfer_size_ = transfer_size;
        
        TransferPool::allocate(num_transfers, transfer_size_);
    }

    
    std::unique_ptr<TransferPool::Transfer> BulkTransferPool::allocateTransfer()
    {
        libusb_transfer* transfer = libusb_alloc_transfer(0);
        transfer->type = LIBUSB_TRANSFER_TYPE_BULK;
        return std::unique_ptr<TransferPool::Transfer>(new Transfer(transfer, this));
    }

    
    std::unique_ptr<TransferPool::Buffer> BulkTransferPool::allocateBuffer()
    {
        return std::unique_ptr<TransferPool::Buffer>(new Buffer(1, transfer_size_));
    }

    
    void BulkTransferPool::processTransfer(Transfer* transfer)
    {
        const auto& buffer = transfer->buffer;
        buffer->actualStatusCompleted[0] = (transfer->transfer->status == LIBUSB_TRANSFER_COMPLETED);
        buffer->actualLength[0] = transfer->transfer->actual_length;
    }

    
    void BulkTransferPool::proccessBuffer(libfreenect2::usb::TransferPool::Buffer *buffer)
    {
        if (!buffer->actualStatusCompleted[0]) return;

        if (_callback)
            _callback->onDataReceived(buffer->buffer, buffer->actualLength[0]);
    }


    
    void IsoTransferPool::allocate(size_t num_transfers, size_t num_packets, size_t packet_size)
    {
        num_packets_ = num_packets;
        packet_size_ = packet_size;
        
        TransferPool::allocate(num_transfers, num_packets_ * packet_size_);
    }

    
    std::unique_ptr<TransferPool::Transfer> IsoTransferPool::allocateTransfer()
    {
        libusb_transfer* transfer = libusb_alloc_transfer(static_cast<int>(num_packets_));
        
        transfer->type = LIBUSB_TRANSFER_TYPE_ISOCHRONOUS;
        transfer->num_iso_packets = static_cast<int>(num_packets_);
        libusb_set_iso_packet_lengths(transfer, static_cast<int>(packet_size_));
        
        return std::unique_ptr<TransferPool::Transfer>(new Transfer(transfer, this));
    }

    
    std::unique_ptr<TransferPool::Buffer> IsoTransferPool::allocateBuffer()
    {
        return std::unique_ptr<TransferPool::Buffer>(new Buffer(num_packets_, packet_size_));
    }

    
    void IsoTransferPool::processTransfer(Transfer* transfer)
    {
        const auto& buffer = transfer->buffer;
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
            
            if (_callback)
                _callback->onDataReceived(ptr, buffer->actualLength[i]);
            
            ptr += packet_size_;
        }
    }
    
} /* namespace usb */
} /* namespace libfreenect2 */

