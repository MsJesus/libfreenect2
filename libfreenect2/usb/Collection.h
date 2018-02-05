//
//  Collection.h
//  CAVR
//
//  Created by Andrey on 19/04/2017.
//  Copyright © 2017 CAVR. All rights reserved.
//

#ifndef __cavr_synchronized_collection_h__
#define __cavr_synchronized_collection_h__

#include <libfreenect2/usb/object.h>


namespace libfreenect2 {
namespace usb {

    
    template <class T, class Handler>
    class MoveWrapper
    {
        T           _x;
        Handler     _h;
        
        
    public:
        
        MoveWrapper(T&& x, Handler&& h):
        _x(std::move(x)),
        _h(std::move(h))
        {}
        
        
        template <class Arg>
        auto operator () (Arg& c) -> decltype(_h(c, std::move(_x)))
        {
            return _h(c, std::move(_x));
        }
    };
    
    
    template <class T, class Handler>
    static MoveWrapper<T, Handler> moveWrapper(T&& t, Handler&& h)
    {
        return MoveWrapper<T, Handler>(std::move(t), std::move(h));
    }
    
    
    template<class TargetCollection>
    class Collection : private Object<TargetCollection>
    {
    public:
        
        
        template <class Handler>
        void forEach(Handler handler) const
        {
            this->execute([&handler] (const TargetCollection& c) {
                
                for (auto e : c)
                {
                    handler(e);
                }
            });
        }
        
        
        template <class Argument>
        void push_back(const Argument& argument)
        {
            this->execute([&argument] (TargetCollection& c) {
                c.push_back(argument);
            });
        }
        
        template <class Argument>
        void push_back_move(Argument&& argument)
        {
            auto wrapper = moveWrapper(std::move(argument),
                                       [] (TargetCollection& c, Argument&& arg) {
                                           
                                           c.push_back(std::move(arg));
                                       });
            this->execute(std::move(wrapper));
        }
        
        template <class Argument>
        void push_back_limited(Argument&& argument, size_t limit = 0)
        {
            auto wrapper = moveWrapper(std::move(argument),
                                       [] (TargetCollection& c, Argument&& arg) {
                                           
                                           c.push_back(std::move(arg));
                                       });
            this->wait([limit] (TargetCollection& c) { return c.size() <= limit; },
                       std::move(wrapper));
        }
        
        
        void clear()
        {
            this->execute([] (TargetCollection& c) {
                c.clear();
            });
        }
        
        
        bool empty() const
        {
            return this->execute([] (const TargetCollection& c) {
                return c.empty();
            });
        }
        
        
        typename TargetCollection::value_type pop_front_out()
        {
            return this->wait([] (TargetCollection& c) { return !c.empty(); },
                              [] (TargetCollection& c) {
                                  typename TargetCollection::value_type result = std::move(c.front());
                                  c.pop_front();
                                  return result;
                              });
        }
        
        
        typename TargetCollection::value_type pop_front_clear()
        {
            return this->wait([] (TargetCollection& c) { return !c.empty(); },
                              [] (TargetCollection& c) {
                                  typename TargetCollection::value_type result = std::move(c.front());
                                  c.clear();
                                  return result;
                              });
        }
        
        
        template <class Rep, class Period>
        typename TargetCollection::value_type pop_front_out(const std::chrono::duration<Rep, Period>& duration)
        {
            return this->wait_for(duration,
                                  [] (TargetCollection& c) { return !c.empty(); },
                                  [] (TargetCollection& c) {
                                      typename TargetCollection::value_type result = std::move(c.front());
                                      c.pop_front();
                                      return result;
                                  });
        }
    };
    
}}

#endif /* __cavr_base_synchronized_h__ */
