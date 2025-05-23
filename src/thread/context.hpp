#pragma once
#include <fc/thread/thread.hpp>
#include <boost/context/all.hpp>
#include <fc/exception/exception.hpp>
#include <vector>

#include <boost/version.hpp>

#if BOOST_VERSION >= 105300
  #include <boost/coroutine/stack_allocator.hpp>
  namespace bc  = boost::context;
  namespace bco = boost::coroutines;
#elif BOOST_VERSION >= 105200
  namespace bc = boost::context;
#else
  namespace bc  = boost::ctx;
  namespace bco = boost::ctx;
#endif

namespace fc {
  class thread;
  class promise_base;
  class task_base;

  /**
   *  maintains information associated with each context such as
   *  where it is blocked, what time it should resume, priority,
   *  etc.
   */
  struct context  {
    typedef fc::context* ptr;


    context( void (*sf)(intptr_t), bco::stack_allocator& alloc, fc::thread* t )
    : caller_context(0),
      stack_alloc(&alloc),
      next_blocked(0), 
      next_blocked_mutex(0), 
      next(0), 
      ctx_thread(t),
      canceled(false),
      complete(false),
      cur_task(0)
    {
#if BOOST_VERSION >= 105300
     size_t stack_size =  bco::stack_allocator::default_stacksize();
     my_context = bc::make_fcontext(alloc.allocate(stack_size), stack_size, sf);
#else
     size_t stack_size = bc::default_stacksize();
     my_context.fc_stack.base = alloc.allocate( stack_size );
     my_context.fc_stack.limit = 
        static_cast<char*>( my_context.fc_stack.base) - stack_size;
     make_fcontext( &my_context, sf );
#endif
    }

    context( fc::thread* t) :
#if BOOST_VERSION >= 105300
     my_context(new bc::fcontext_t),
#endif
     caller_context(0),
     stack_alloc(0),
     next_blocked(0), 
     next_blocked_mutex(0), 
     next(0), 
     ctx_thread(t),
     canceled(false),
     complete(false),
     cur_task(0)
    {}

    ~context() {

#if BOOST_VERSION >= 105300
      if(stack_alloc)
        stack_alloc->deallocate( my_context->fc_stack.sp, bco::stack_allocator::default_stacksize() );
      else
        delete my_context;
#else
      if(stack_alloc)
        stack_alloc->deallocate( my_context.fc_stack.base, bc::default_stacksize() );
#endif
    }

    struct blocked_promise {
      blocked_promise( promise_base* p=0, bool r=true )
      :prom(p),required(r){}

      promise_base* prom;
      bool          required;
    };
    
    /**
     *  @todo Have a list of promises so that we can wait for
     *    P1 or P2 and either will unblock instead of requiring both
     *  @param req - require this promise to 'unblock', otherwise try_unblock
     *     will allow it to be one of many that could 'unblock'
     */
    void add_blocking_promise( promise_base* p, bool req = true ) {
      for( auto i = blocking_prom.begin(); i != blocking_prom.end(); ++i ) {
        if( i->prom == p ) {
          i->required = req;
          return;
        }
      }
      blocking_prom.push_back( blocked_promise(p,req) );
    }
    /**
     *  If all of the required promises and any optional promises then
     *  return true, else false.
     *  @todo check list
     */
    bool try_unblock( promise_base* p ) {
      if( blocking_prom.size() == 0 )  {
        return true;
     }
      bool req = false;
      for( uint32_t i = 0; i < blocking_prom.size(); ++i ) {
        if( blocking_prom[i].prom == p ) {
           blocking_prom[i].required = false;
           return true;
        }
        req = req || blocking_prom[i].required;
      }
      return !req;
    }

    void remove_blocking_promise( promise_base* p ) {
      for( auto i = blocking_prom.begin(); i != blocking_prom.end(); ++i ) {
        if( i->prom == p ) {
          blocking_prom.erase(i);
          return;
        }
      }
    }

    void timeout_blocking_promises() {
      for( auto i = blocking_prom.begin(); i != blocking_prom.end(); ++i ) {
        i->prom->set_exception( std::make_shared<timeout_exception>() );
      }
    }
    void except_blocking_promises( const exception_ptr& e ) {
      for( auto i = blocking_prom.begin(); i != blocking_prom.end(); ++i ) {
        i->prom->set_exception( e );
      }
    }
    void clear_blocking_promises() {
      blocking_prom.clear();
    }

    bool is_complete()const { return complete; }



#if BOOST_VERSION >= 105300
    bc::fcontext_t*              my_context;
#else
    bc::fcontext_t               my_context;
#endif
    fc::context*                caller_context;
    bco::stack_allocator*         stack_alloc;
    priority                     prio;
    //promise_base*              prom; 
    std::vector<blocked_promise> blocking_prom;
    time_point                   resume_time;
   // time_point                   ready_time; // time that this context was put on ready queue
    fc::context*                next_blocked;
    fc::context*                next_blocked_mutex;
    fc::context*                next;
    fc::thread*                 ctx_thread;
    bool                         canceled;
    bool                         complete;
    task_base*                   cur_task;
  };

} // naemspace fc 

