//
//  Object.h
//  CAVR
//
//  Created by Andrey on 19/04/2017.
//  Copyright © 2017 CAVR. All rights reserved.
//

#ifndef __cavr_synchronized_object_h__
#define __cavr_synchronized_object_h__

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <type_traits>


namespace libfreenect2 {
namespace usb {
    
    
    template <class T>
    class Object
    {
        using Mutex = std::mutex;
        using Condition = std::condition_variable;
        using Lock = std::unique_lock<Mutex>;
        
        
        T               _object;
        mutable Mutex   _mutex;
        Condition       _modifiedCondition;
        
        
    public:
        
        template <class ...Args>
        Object(Args ...args):
            _object(std::forward<Args>(args)...)
        {}
        
        
        Object(Object&& other):
            _object(std::move(other._object))
        {}
        
        
        template <class Handler>
        typename std::result_of<Handler(T&)>::type execute(Handler handler)
        {
            Lock lock(_mutex);
            
            _modifiedCondition.notify_all();
            
            return handler(_object);
        }
        
        
        template <class Handler>
        typename std::result_of<Handler(const T&)>::type execute(Handler handler) const
        {
            Lock lock(_mutex);
            
            return handler(_object);
        }
        
        
        template <class Predicate, class Handler>
        typename std::result_of<Handler(T&)>::type wait(Predicate predicate,
                                                        Handler handler)
        {
            Lock lock(_mutex);
            
            _modifiedCondition.wait(lock, [&predicate, this] () { return predicate(_object); });
            
            _modifiedCondition.notify_all();
                
            return handler(_object);
        }
        
        
        template <class Predicate, class Handler, class Rep, class Period>
        typename std::result_of<Handler(T&)>::type wait_for(const std::chrono::duration<Rep, Period>& duration,
                                                            Predicate predicate,
                                                            Handler handler)
        {
            Lock lock(_mutex);
            
            if (_modifiedCondition.wait_for(lock, duration, [&predicate, this] () { return predicate(_object); }))
            {
                _modifiedCondition.notify_all();
                
                return handler(_object);
            }
//            else
//            {
//                throw cavr::synchronized::TimeOut("");
//            }
        }
    };
    
    
}}


#endif /* __cavr_synchronized_object_h__ */
