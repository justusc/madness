#ifndef WORLDTASK_H
#define WORLDTASK_H

/// \file worldtask.h
/// \brief Defines TaskInterface and implements WorldTaskQueue and associated stuff.

// To do:
// a) Arrays/vectors of input futures for multiple dependencies
// b) 

namespace madness {

    class WorldTaskQueue;
    class TaskInterface;

    void task_ready_callback_function(WorldTaskQueue*, TaskInterface*);

    /// All tasks must be derived from this public interface
    class TaskInterface : public DependencyInterface{
        friend class WorldTaskQueue;
    private:
        bool hp;                //< True if high-priority (managed by taskq)

        class TaskReadyCallback : public CallbackInterface {
            WorldTaskQueue* taskq;
            TaskInterface* task;
        public:
            TaskReadyCallback() : taskq(0), task(0) {};
            void register_callback(WorldTaskQueue* taskq, TaskInterface* task) {
                this->taskq = taskq;
                this->task = task;
                static_cast<DependencyInterface*>(task)->register_callback(this);
            };
            void notify() {
                if (taskq) ::madness::task_ready_callback_function(taskq,task);
                taskq = 0;
            };
        } callback;   //< Embedded here for efficiency (managed by taskq)
        
    public:
        /// Create a new task with ndepend dependencies (default 0)
        TaskInterface(int ndepend=0) 
            : DependencyInterface(ndepend)
            , hp(false)
            {};


        /// Runs the task ... derived classes MUST implement this.
        virtual void run(World& world) = 0;

        /// Virtual base class destructor so that derived class destructor is called
        virtual ~TaskInterface() {};
    };

    class WorldTaskQueue;

    template <typename functionT> class TaskFunction;
    template <typename memfunT> class TaskMemfun;

    /// Queue to manage and run tasks.
    
    /// Note that even though the queue is presently single threaded,
    /// the invocation of poll() and/or fiber scheduling can still
    /// cause multiple entries through the task layer.  If modifying
    /// this class (indeed, any World class) be careful about keeping
    /// modifications to the data structure atomic w.r.t. AM
    /// operations and the execution of multiple fibers.
    ///
    /// The pending q for tasks that need probing to have their dependencies
    /// make progress has been removed.  It can be added back.  Similarly,
    /// rather than having a separate ready q for hp tasks, we now just add
    /// them at the front of the ready q.  Think KISS.
    class WorldTaskQueue : private NO_DEFAULTS {
        friend class TaskInterface;
    private:
        // Is there a more efficient choice than list?
        typedef std::list< TaskInterface* > listT;
        listT ready;       //< Tasks that are ready to run

        World& world;      //< The communication context
        const int maxfiber;//< Maximum no. of active fibers
        long nregistered;  //< Counts registered tasks with simple dependencies
        const ProcessID me;
        int suspended;      //< If non-zero, task processing is suspended

    public:
        WorldTaskQueue(World& world, int maxfiber=1) 
            : world(world)
            , maxfiber(maxfiber)
            , nregistered(0)
            , me(world.mpi.rank())
            , suspended(0) {};
        
        /// Add a new local task.  The task queue will eventually delete it.

        /// The task pointer (t) is assumed to have been created with
        /// \c new and when the task is eventually run the queue
        /// will call the task's destructor using \c delete.
        ///
        /// The optional argument specifies if the task should have
        /// high priority. The default is false.
        ///
        /// If the task has no outstanding dependencies it is immediately
        /// put into the ready queue (at the end for normal-priority tasks,
        /// at the beginning for high-priority tasks).
        ///
        /// If the task has outstanding dependencies then it is
        /// assumed that other activities will be calling task->dec()
        /// to decrement the dependency count.  When this count goes
        /// to zero the callback will be invoked automatically to
        /// insert the task into the ready queue.  All that will be
        /// done here is registering the callback
        inline void add(TaskInterface* t, bool hp = false) {
            t->hp = hp;
            nregistered++;
            if (t->probe()) add_ready_task(t);
            else t->callback.register_callback(this,t);
            World::poll_all(true); // Adding tasks is useful work so increase polling interval
        };


        /// Invoke "resultT (*function)(void)" as a local task

        /// A future is returned to eventually hold the result of the task.
        /// Future<void> is an empty class that may be ignored.
        template <typename functionT>
        Future<FUNCTION_RETURNT(functionT)> add(functionT function) {
            return add(me,function);
        }


        /// Invoke "resultT (*function)(void)" as a task, local or remote

        /// A future is returned to eventually hold the result of the task.
        /// Future<void> is an empty class that may be ignored.
        template <typename functionT>
        Future<FUNCTION_RETURNT(functionT)> add(ProcessID where, functionT function) {
            Future<FUNCTION_RETURNT(functionT)> result;
            if (where == me) 
                add(new TaskFunction<functionT>(result, function));
            else 
                TaskFunction<functionT>::sender(world, where, result, function);
            return result;
        }
        

        /// Invoke "resultT (*function)(arg1T)" as a local task
        template <typename functionT, typename arg1T>
        Future<FUNCTION_RETURNT(functionT)> add(functionT function, const arg1T& arg1) {
            return add(me, function, arg1);
        }


        /// Invoke "resultT (*function)(arg1T)" as a task, local or remote

        /// A future is returned to eventually hold the result of the
        /// task.  Future<void> is an empty class that may be ignored.
        ///
        /// If the task is remote, arguments must be serializable and
        /// make sense on the remote end (most pointers will not!).  A
        /// pointer to World is correctly (de)serialized and can be
        /// used to pass the world into a function locally or
        /// remotely.  An argument that is a future may be used to
        /// carry dependencies for local tasks.  An unready future cannot
        /// be used as an argument for a remote tasks --- i.e.,
        /// remote tasks must be ready to execute (you can work around this
        /// by making a local task to submit the remote task once everything
        /// is ready).
        template <typename functionT, typename arg1T>
        Future<FUNCTION_RETURNT(functionT)> add(ProcessID where, functionT function, 
                                                const arg1T& arg1) {
            Future<FUNCTION_RETURNT(functionT)> result;
            if (where == me) {
                add(new TaskFunction<functionT>(result, function, arg1));
            }
            else {
                MADNESS_ASSERT(future_probe(arg1));  // No dependencies allowed for remote tasks
                TaskFunction<functionT>::sender(world, where, result, function, arg1);
            }
            return result;
        }
        

        /// Invoke "resultT (*function)(arg1T,arg2T)" as a local task
        template <typename functionT, typename arg1T, typename arg2T>
        Future<FUNCTION_RETURNT(functionT)> add(functionT function, 
                                                const arg1T& arg1, const arg2T& arg2) {
            return add(me,function,arg1,arg2);
        }


        /// Invoke "resultT (*function)(arg1T,arg2T)" as a task, local or remote

        /// A future is returned to eventually hold the result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename functionT, typename arg1T, typename arg2T>
        Future<FUNCTION_RETURNT(functionT)> add(ProcessID where, functionT function, 
                                                const arg1T& arg1, const arg2T& arg2) {
            Future<FUNCTION_RETURNT(functionT)> result;
            if (where == me) {
                add(new TaskFunction<functionT>(result, function, arg1, arg2));
            }
            else {
                MADNESS_ASSERT(future_probe(arg1));  // No dependencies allowed for remote tasks
                MADNESS_ASSERT(future_probe(arg2));  // No dependencies allowed for remote tasks
                TaskFunction<functionT>::sender(world, where, result, function, arg1, arg2);
            }
            return result;
        }
        

        /// Invoke "resultT (*function)(arg1T,arg2T,arg3T)" as a local task
        template <typename functionT, typename arg1T, typename arg2T, typename arg3T>
        Future<FUNCTION_RETURNT(functionT)> add(functionT function, 
                                                const arg1T& arg1, const arg2T& arg2, 
                                                const arg3T& arg3) {
            return add(me,function,arg1,arg2,arg3);
        }


        /// Invoke "resultT (*function)(arg1T,arg2T,arg3T)" as a task, local or remote

        /// A future is returned to eventually hold the result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename functionT, typename arg1T, typename arg2T, typename arg3T>
        Future<FUNCTION_RETURNT(functionT)> add(ProcessID where, functionT function, 
                                                const arg1T& arg1, const arg2T& arg2,
                                                const arg3T& arg3) {
            Future<FUNCTION_RETURNT(functionT)> result;
            if (where == me) {
                add(new TaskFunction<functionT>(result, function, arg1, arg2, arg3));
            }
            else {
                MADNESS_ASSERT(future_probe(arg1));  // No dependencies allowed for remote tasks
                MADNESS_ASSERT(future_probe(arg2));  // No dependencies allowed for remote tasks
                MADNESS_ASSERT(future_probe(arg3));  // No dependencies allowed for remote tasks
                TaskFunction<functionT>::sender(world, where, result, function, arg1, arg2, arg3);
            }
            return result;
        }


        /// Invoke "resultT (obj.*memfun)(arg1T,arg2T,arg3)" as a local task

        /// DistributedContainer already provides remote method
        /// invocation for its contents.  The yet-to-be-cleaned-up
        /// WorldSharedPtr will provide remote method/task invocation.
        /// However, there is no deep reason why remote tasks are not
        /// directly supported here except we have not yet needed it,
        /// and what about safe management of remote pointers?
        /// Presently, only the pointer World* is automagically made
        /// globally valid by its (de)serialization.
        ///
        /// To provide remote member function invocation you can most
        /// easily call a static member function and pass the remote
        /// object pointer (which must be valid remotely) as an
        /// argument.  If you have not provided serialization for that
        /// pointer type (which enables you to translate local/remote
        /// pointers during (de)serialization), you can adopt a brute
        /// force approach of coercing the pointer to/from an unsigned
        /// long argument.
        template <typename memfunT, typename arg1T, typename arg2T, typename arg3T>
        Future<MEMFUN_RETURNT(memfunT)> add(MEMFUN_OBJT(memfunT)& obj, 
                                            memfunT memfun,
                                            const arg1T& arg1, const arg2T& arg2, const arg3T& arg3)
        {
            Future<MEMFUN_RETURNT(memfunT)> result;
            add(new TaskMemfun<memfunT>(result,obj,memfun,arg1,arg2,arg3));
            return result;
        }

        /// Invoke "resultT (obj.*memfun)(arg1T,arg2T)" as a local task
        template <typename memfunT, typename arg1T, typename arg2T>
        Future<MEMFUN_RETURNT(memfunT)> add(MEMFUN_OBJT(memfunT)& obj, 
                                            memfunT memfun,
                                            const arg1T& arg1, const arg2T& arg2)
        {
            Future<MEMFUN_RETURNT(memfunT)> result;
            add(new TaskMemfun<memfunT>(result,obj,memfun,arg1,arg2));
            return result;
        }


        /// Invoke "resultT (obj.*memfun)(arg1T)" as a local task
        template <typename memfunT, typename arg1T>
        Future<MEMFUN_RETURNT(memfunT)> add(MEMFUN_OBJT(memfunT)& obj, 
                                            memfunT memfun,
                                            const arg1T& arg1)
        {
            Future<MEMFUN_RETURNT(memfunT)> result;
            add(new TaskMemfun<memfunT>(result,obj,memfun,arg1));
            return result;
        }

        
        /// Invoke "resultT (obj.*memfun)()" as a local task
        template <typename memfunT>
        Future<MEMFUN_RETURNT(memfunT)> add(MEMFUN_OBJT(memfunT)& obj, memfunT memfun) 
        {
            Future<MEMFUN_RETURNT(memfunT)> result;
            add(new TaskMemfun<memfunT>(result,obj,memfun));
            return result;
        }


        struct TaskQFenceProbe {
            mutable WorldTaskQueue* q;
            TaskQFenceProbe(WorldTaskQueue* q) : q(q) {};
            bool operator()() const {
                return (q->nregistered==0 && q->ready.empty());
            };
        };

        /// Returns after all local tasks have completed AND am locally fenced
        void fence() {
            // Critical here is to ensure BOTH taskq is drained and
            // the AM locally fenced when we return.  Thus, drain the
            // queue, fence the AM, and if there are any new tasks we
            // have to repeat until all is quiet.  Do NOT poll
            // once this condition has been established.
            do {
                World::await(TaskQFenceProbe(this));
                world.am.fence();
            } while (nregistered || !ready.empty());
        };

        
        /// Private:  Call back for tasks that have satisfied their dependencies.
        inline void add_ready_task(TaskInterface* t) {
            if (t->hp) ready.push_front(t);
            else ready.push_back(t);
            nregistered--;
            MADNESS_ASSERT(nregistered>=0);
        };


        /// Suspend processing of tasks
        inline void suspend() {
            suspended++;
        };


        /// Resume processing of tasks
        inline void resume() {
            suspended--;
            if (suspended < 0) {
                print("TaskQueue: suspended was negative --- mismatched suspend/resume pairing");
                suspended = 0;
            };
        };


        /// Run the next ready task, returning true if one was run
        inline bool run_next_ready_task() {
            if (suspended || ready.empty()) {
                return false;
            }
            else {
                TaskInterface *p = ready.front();
                ready.pop_front();

                suspend();
                p->run(world);
                resume();

                delete p;
                return true;
            }
        };
   
    };

    // Internal: Convenience for serializing
    template <typename refT, typename functionT>
    struct TaskHandlerInfo {
        refT ref;
        functionT func;
        TaskHandlerInfo(const refT& ref, functionT func) 
            : ref(ref), func(func) {};
        TaskHandlerInfo() {};
        template <typename Archive> 
        void serialize(const Archive& ar) {
            ar & archive::wrap_opaque(*this);
        }
    };

    // Internal: Common functionality for TaskFunction and TaskMemfun classes
    class TaskFunctionBase : public TaskInterface {
    protected:
    public:
        // Register non-ready future as a dependency
        template <typename T>
        inline void check_dependency(Future<T>& fut) {
            if (!fut.probe()) {
                inc();
                fut.register_callback(this);
            }
        }
    };


    // Internal: This silliness since cannot use a void expression as a void argument
    template <typename resultT>
    struct TaskFunctionRun {
        template <typename functionT, typename a1T, typename a2T, typename a3T>
        static inline void run(Future<resultT>& result, functionT func, a1T& a1, a2T& a2, a3T& a3) {
            result.set(func(a1,a2,a3));
        }

        template <typename functionT, typename a1T, typename a2T>
        static inline void run(Future<resultT>& result, functionT func, a1T& a1, a2T& a2) {
            result.set(func(a1,a2));
        }

        template <typename functionT, typename a1T>
        static inline void run(Future<resultT>& result, functionT func, a1T& a1) {
            result.set(func(a1));
        }

        template <typename functionT>
        static inline void run(Future<resultT>& result, functionT func) {
            result.set(func());
        }
    };

    // Internal: This silliness since cannot use a void expression as a void argument
    template <>
    struct TaskFunctionRun<void> {
        template <typename functionT, typename a1T, typename a2T, typename a3T>
        static inline void run(Future<void>& result, functionT func, a1T& a1, a2T& a2, a3T& a3) {
            func(a1,a2,a3);
        }

        template <typename functionT, typename a1T, typename a2T>
        static inline void run(Future<void>& result, functionT func, a1T& a1, a2T& a2) {
            func(a1,a2);
        }

        template <typename functionT, typename a1T>
        static inline void run(Future<void>& result, functionT func, a1T& a1) {
            func(a1);
        }

        template <typename functionT>
        static inline void run(Future<void>& result, functionT func) {
            func();
        }
    };


    // This silliness since cannot use a void expression as a void argument
    template <typename resultT>
    struct TaskMemfunRun {
        template <typename memfunT, typename a1T, typename a2T, typename a3T>
        static inline void run(Future<resultT>& result, MEMFUN_OBJT(memfunT)& obj, 
                               memfunT func, a1T& a1, a2T& a2, a3T& a3) {
            result.set((obj.*func)(a1,a2,a3));
        }

        template <typename memfunT, typename a1T, typename a2T>
        static inline void run(Future<resultT>& result, MEMFUN_OBJT(memfunT)& obj, 
                               memfunT func, a1T& a1, a2T& a2) {
            result.set((obj.*func)(a1,a2));
        }

        template <typename memfunT, typename a1T>
        static inline void run(Future<resultT>& result, MEMFUN_OBJT(memfunT)& obj, 
                               memfunT func, a1T& a1) {
            result.set((obj.*func)(a1));
        }

        template <typename memfunT>
        static inline void run(Future<resultT>& result, MEMFUN_OBJT(memfunT)& obj, 
                               memfunT func) {
            result.set((obj.*func)());
        }
    };


    // This silliness since cannot use a void expression as a void argument
    template <>
    struct TaskMemfunRun<void> {
        template <typename memfunT, typename a1T, typename a2T, typename a3T>
        static inline void run(Future<void>& result, MEMFUN_OBJT(memfunT)& obj, 
                               memfunT func, a1T& a1, a2T& a2, a3T& a3) {
            (obj.*func)(a1,a2,a3);
        }

        template <typename memfunT, typename a1T, typename a2T>
        static inline void run(Future<void>& result, MEMFUN_OBJT(memfunT)& obj, 
                               memfunT func, a1T& a1, a2T& a2) {
            (obj.*func)(a1,a2);
        }

        template <typename memfunT, typename a1T>
        static inline void run(Future<void>& result, MEMFUN_OBJT(memfunT)& obj, 
                               memfunT func, a1T& a1) {
            (obj.*func)(a1);
        }

        template <typename memfunT>
        static inline void run(Future<void>& result, MEMFUN_OBJT(memfunT)& obj, 
                               memfunT func) {
            (obj.*func)();
        }
    };


    // Task wrapping "resultT (*function)()"
    template <typename resultT>
    struct TaskFunction<resultT (*)()> : public TaskFunctionBase {
        typedef resultT (*functionT)();
        typedef Future<resultT> futureT;
        typedef RemoteReference< FutureImpl<resultT> > refT;

        static void handler(World& world, ProcessID src, const AmArg& arg) {
            TaskHandlerInfo<refT,functionT> info = arg;
            world.taskq.add(new TaskFunction<functionT>(futureT(info.ref),info.func),true);
        };

        static void sender(World& world, ProcessID dest, Future<resultT>& result, functionT func) {
            world.am.send(dest, handler, TaskHandlerInfo<refT,functionT>(result.remote_ref(world), func));
        };

        Future<resultT> result; 
        const functionT func;
        TaskFunction(const futureT& result, functionT func) : result(result), func(func) {};

        void run(World& world) {TaskFunctionRun<resultT>::run(result,func);}
    };


    // Task wrapping "resultT (*function)(arg1)"
    template <typename resultT, typename arg1_type>
    struct TaskFunction<resultT (*)(arg1_type)> : public TaskFunctionBase {
        typedef resultT (*functionT)(arg1_type);
        typedef REMFUTURE(REMCONST(REMREF(arg1_type))) arg1T;
        typedef Future<resultT> futureT;
        typedef RemoteReference< FutureImpl<resultT> > refT;

        static void handler(World& world, ProcessID src, void* buf, size_t nbyte) {
            LongAmArg* arg = (LongAmArg*) buf;
            TaskHandlerInfo<refT,functionT> info;
            arg1T arg1;
            arg->unstuff(nbyte, info, arg1);
            world.taskq.add(new TaskFunction<functionT>(futureT(info.ref),info.func,arg1),true);
        };

        template <typename a1T>
        static void sender(World& world, ProcessID dest, const futureT& result, functionT func,
                           const a1T& arg1) {
            LongAmArg* arg = new LongAmArg();
            size_t nbyte = arg->stuff(TaskHandlerInfo<refT,functionT>(result.remote_ref(world), func), 
                                      static_cast<arg1T>(arg1));
            world.am.send_long_managed(dest, TaskFunction<functionT>::handler, arg, nbyte);
        }

        futureT result;
        const functionT func;
        Future<arg1T> arg1;

        template <typename a1T>
        TaskFunction(const futureT& result, functionT func, const a1T& a1) 
            : result(result), func(func), arg1(a1) {
            check_dependency(arg1);
        }

        void run(World& world) {
            TaskFunctionRun<resultT>::run(result,func,arg1);
        };
    };


    // Task wrapping "resultT (*function)(arg1,arg2)"
    template <typename resultT, typename arg1_type, typename arg2_type>
    struct TaskFunction<resultT (*)(arg1_type,arg2_type)> : public TaskFunctionBase {
        typedef resultT (*functionT)(arg1_type,arg2_type);
        typedef REMFUTURE(REMCONST(REMREF(arg1_type))) arg1T;
        typedef REMFUTURE(REMCONST(REMREF(arg2_type))) arg2T;
        typedef Future<resultT> futureT;
        typedef RemoteReference< FutureImpl<resultT> > refT;

        static void handler(World& world, ProcessID src, void* buf, size_t nbyte) {
            LongAmArg* arg = (LongAmArg*) buf;
            TaskHandlerInfo<refT,functionT> info;
            arg1T arg1;
            arg2T arg2;
            arg->unstuff(nbyte, info, arg1, arg2);
            world.taskq.add(new TaskFunction<functionT>(futureT(info.ref),info.func,arg1,arg2),true);
        };

        template <typename a1T, typename a2T>
        static void sender(World& world, ProcessID dest, const futureT& result, functionT func,
                           const a1T& arg1, const a2T& arg2) {
            LongAmArg* arg = new LongAmArg();
            size_t nbyte = arg->stuff(TaskHandlerInfo<refT,functionT>(result.remote_ref(world), func), 
                                      static_cast<arg1T>(arg1), static_cast<arg2T>(arg2));
            world.am.send_long_managed(dest, TaskFunction<functionT>::handler, arg, nbyte);
        }

        futureT result;
        const functionT func;
        Future<arg1T> arg1;
        Future<arg2T> arg2;

        template <typename a1T, typename a2T>
        TaskFunction(const futureT& result, functionT func, const a1T& a1, const a2T& a2) 
            : result(result), func(func), arg1(a1), arg2(a2) {
            check_dependency(arg1);
            check_dependency(arg2);
        }

        void run(World& world) {
            TaskFunctionRun<resultT>::run(result,func,arg1,arg2);
        };
    };


    // Task wrapping "resultT (*function)(arg1,arg2,arg3)"
    template <typename resultT, typename arg1_type, typename arg2_type, typename arg3_type>
    struct TaskFunction<resultT (*)(arg1_type,arg2_type,arg3_type)> : public TaskFunctionBase {
        typedef resultT (*functionT)(arg1_type,arg2_type,arg3_type);
        typedef REMFUTURE(REMCONST(REMREF(arg1_type))) arg1T;
        typedef REMFUTURE(REMCONST(REMREF(arg2_type))) arg2T;
        typedef REMFUTURE(REMCONST(REMREF(arg3_type))) arg3T;
        typedef Future<resultT> futureT;
        typedef RemoteReference< FutureImpl<resultT> > refT;

        static void handler(World& world, ProcessID src, void* buf, size_t nbyte) {
            LongAmArg* arg = (LongAmArg*) buf;
            TaskHandlerInfo<refT,functionT> info;
            arg1T arg1;
            arg2T arg2;
            arg3T arg3;
            arg->unstuff(nbyte, info, arg1, arg2, arg3);
            world.taskq.add(new TaskFunction<functionT>(futureT(info.ref),info.func,arg1,arg2,arg3),true);
        };

        template <typename a1T, typename a2T, typename a3T>
        static void sender(World& world, ProcessID dest, const futureT& result, functionT func,
                           const a1T& arg1, const a2T& arg2, const a3T& arg3) {
            LongAmArg* arg = new LongAmArg();
            size_t nbyte = arg->stuff(TaskHandlerInfo<refT,functionT>(result.remote_ref(world), func), 
                                      static_cast<arg1T>(arg1), static_cast<arg2T>(arg2), static_cast<arg3T>(arg3));
            world.am.send_long_managed(dest, TaskFunction<functionT>::handler, arg, nbyte);
        }

        futureT result;
        const functionT func;
        Future<arg1T> arg1;
        Future<arg2T> arg2;
        Future<arg3T> arg3;

        template <typename a1T, typename a2T, typename a3T>
        TaskFunction(const futureT& result, functionT func, const a1T& a1, const a2T& a2, const a3T& a3) 
            : result(result), func(func), arg1(a1), arg2(a2), arg3(a3) {
            check_dependency(arg1);
            check_dependency(arg2);
            check_dependency(arg3);
        }

        void run(World& world) {
            TaskFunctionRun<resultT>::run(result,func,arg1,arg2,arg3);
        };
    };


    // Task wrapping "resultT (obj.*function)(arg1,arg2,arg3)"
    template <typename resultT, typename objT, typename arg1_type, typename arg2_type, typename arg3_type>
    struct TaskMemfun<resultT (objT::*)(arg1_type,arg2_type,arg3_type)> : public TaskFunctionBase {
        typedef resultT (objT::*memfunT)(arg1_type,arg2_type,arg3_type);
        typedef REMFUTURE(REMCONST(REMREF(arg1_type))) arg1T;
        typedef REMFUTURE(REMCONST(REMREF(arg2_type))) arg2T;
        typedef REMFUTURE(REMCONST(REMREF(arg3_type))) arg3T;
        typedef Future<resultT> futureT;

        futureT result;
        objT& obj;
        const memfunT memfun;
        Future<arg1T> arg1;
        Future<arg2T> arg2;
        Future<arg3T> arg3;

        template <typename a1T, typename a2T, typename a3T>
            TaskMemfun(const futureT& result, objT& obj, memfunT memfun, 
                       const a1T& a1, const a2T& a2, const a3T& a3) 
            : result(result), obj(obj), memfun(memfun), arg1(a1), arg2(a2), arg3(a3) {
            check_dependency(arg1);
            check_dependency(arg2);
            check_dependency(arg3);
        }

        void run(World& world) {
            TaskMemfunRun<resultT>::run(result,obj,memfun,arg1,arg2,arg3);
        };
    };

    // Task wrapping "resultT (obj.*function)(arg1,arg2)"
    template <typename resultT, typename objT, typename arg1_type, typename arg2_type>
    struct TaskMemfun<resultT (objT::*)(arg1_type,arg2_type)> : public TaskFunctionBase {
        typedef resultT (objT::*memfunT)(arg1_type,arg2_type);
        typedef REMFUTURE(REMCONST(REMREF(arg1_type))) arg1T;
        typedef REMFUTURE(REMCONST(REMREF(arg2_type))) arg2T;
        typedef Future<resultT> futureT;

        futureT result;
        objT& obj;
        const memfunT memfun;
        Future<arg1T> arg1;
        Future<arg2T> arg2;

        template <typename a1T, typename a2T>
            TaskMemfun(const futureT& result, objT& obj, memfunT memfun, const a1T& a1, const a2T& a2) 
            : result(result), obj(obj), memfun(memfun), arg1(a1), arg2(a2) {
            check_dependency(arg1);
            check_dependency(arg2);
        }

        void run(World& world) {
            TaskMemfunRun<resultT>::run(result,obj,memfun,arg1,arg2);
        };
    };

    // Task wrapping "resultT (obj.*function)(arg1)"
    template <typename resultT, typename objT, typename arg1_type>
    struct TaskMemfun<resultT (objT::*)(arg1_type)> : public TaskFunctionBase {
        typedef resultT (objT::*memfunT)(arg1_type);
        typedef REMFUTURE(REMCONST(REMREF(arg1_type))) arg1T;
        typedef Future<resultT> futureT;

        futureT result;
        objT& obj;
        const memfunT memfun;
        Future<arg1T> arg1;

        template <typename a1T>
            TaskMemfun(const futureT& result, objT& obj, memfunT memfun, const a1T& a1) 
            : result(result), obj(obj), memfun(memfun), arg1(a1) {
            check_dependency(arg1);
        }

        void run(World& world) {
            TaskMemfunRun<resultT>::run(result,obj,memfun,arg1);
        };
    };

    // Task wrapping "resultT (obj.*function)()"
    template <typename resultT, typename objT>
    struct TaskMemfun<resultT (objT::*)()> : public TaskFunctionBase {
        typedef resultT (objT::*memfunT)();
        typedef Future<resultT> futureT;

        futureT result;
        objT& obj;
        const memfunT memfun;

        TaskMemfun(const futureT& result, objT& obj, memfunT memfun) 
            : result(result), obj(obj), memfun(memfun) {}

        void run(World& world) {
            TaskMemfunRun<resultT>::run(result,obj,memfun);
        };
    };
}


#endif
