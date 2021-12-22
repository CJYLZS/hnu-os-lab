

# 练习0：填写已有实验

修改trap.c的 trap_dispatch

```C
static void
trap_dispatch(struct trapframe *tf) {
    char c;

    int ret=0;

    switch (tf->tf_trapno) {
    case T_PGFLT:  //page fault
        if ((ret = pgfault_handler(tf)) != 0) {
            print_trapframe(tf);
            if (current == NULL) {
                panic("handle pgfault failed. ret=%d\n", ret);
            }
            else {
                if (trap_in_kernel(tf)) {
                    panic("handle pgfault failed in kernel mode. ret=%d\n", ret);
                }
                cprintf("killed by kernel.\n");
                panic("handle user mode pgfault failed. ret=%d\n", ret); 
                do_exit(-E_KILLED);
            }
        }
        break;
    case T_SYSCALL:
        syscall();
        break;
    case IRQ_OFFSET + IRQ_TIMER:
#if 0
    LAB3 : If some page replacement algorithm(such as CLOCK PRA) need tick to change the priority of pages, 
    then you can add code here. 
#endif
        /* LAB1 YOUR CODE : STEP 3 */
        /* handle the timer interrupt */
        /* (1) After a timer interrupt, you should record this event using a global variable (increase it), such as ticks in kern/driver/clock.c
         * (2) Every TICK_NUM cycle, you can print some info using a funciton, such as print_ticks().
         * (3) Too Simple? Yes, I think so!
         */
        /* LAB5 YOUR CODE */
        /* you should upate you lab1 code (just add ONE or TWO lines of code):
         *    Every TICK_NUM cycle, you should set current process's current->need_resched = 1
         */
        /* LAB6 YOUR CODE */
        /* IMPORTANT FUNCTIONS:
	     * run_timer_list
	     *----------------------
	     * you should update your lab5 code (just add ONE or TWO lines of code):
         *    Every tick, you should update the system time, iterate the timers, and trigger the timers which are end to call scheduler.
         *    You can use one funcitons to finish all these things.
         */
        ticks ++;
        assert(current != NULL);
        run_timer_list();
        break;
    case IRQ_OFFSET + IRQ_COM1:
        c = cons_getc();
        cprintf("serial [%03d] %c\n", c, c);
        break;
    case IRQ_OFFSET + IRQ_KBD:
        c = cons_getc();
        cprintf("kbd [%03d] %c\n", c, c);
        break;
    //LAB1 CHALLENGE 1 : YOUR CODE you should modify below codes.
    case T_SWITCH_TOU:
    case T_SWITCH_TOK:
        panic("T_SWITCH_** ??\n");
        break;
    case IRQ_OFFSET + IRQ_IDE1:
    case IRQ_OFFSET + IRQ_IDE2:
        /* do nothing */
        break;
    default:
        print_trapframe(tf);
        if (current != NULL) {
            cprintf("unhandled trap.\n");
            do_exit(-E_KILLED);
        }
        // in kernel, it must be a mistake
        panic("unexpected trap in kernel.\n");

    }
}

```

增加使用 run_timer_list();

# 练习1: 理解内核级信号量的实现和基于内核级信号量的哲学家就餐问题（不需要编码）

哲学家就餐分为三个阶段

首先试图获得叉子

然后设置状态为进食 然后放下叉子

```c
void phi_take_forks_sema(int i) /* i：哲学家号码从0到N-1 */
{ 
        down(&mutex); /* 进入临界区 */
        state_sema[i]=HUNGRY; /* 记录下哲学家i饥饿的事实 */
        phi_test_sema(i); /* 试图得到两只叉子 */
        up(&mutex); /* 离开临界区 */
        down(&s[i]); /* 如果得不到叉子就阻塞 */
}
```

由于叉子是全局共享的变量 所以在获取叉子的时候 需要进入临界区



```c
void phi_put_forks_sema(int i) /* i：哲学家号码从0到N-1 */
{ 
        down(&mutex); /* 进入临界区 */
        state_sema[i]=THINKING; /* 哲学家进餐结束 */
        phi_test_sema(LEFT); /* 看一下左邻居现在是否能进餐 */
        phi_test_sema(RIGHT); /* 看一下右邻居现在是否能进餐 */
        up(&mutex); /* 离开临界区 */
}
```

在放下叉子的时候通知左右邻居可以进餐

```c
void phi_test_sema(i) /* i：哲学家号码从0到N-1 */
{ 
    if(state_sema[i]==HUNGRY&&state_sema[LEFT]!=EATING
            &&state_sema[RIGHT]!=EATING)
    {
        state_sema[i]=EATING;
        up(&s[i]);
    }
}
```

通过查看相邻的左右是否均不处于进餐状态 来唤醒对应的哲学家

## 请在实验报告中给出内核级信号量的设计描述，并说其大致执行流流程。

```c
typedef struct {
    int value;
    wait_queue_t wait_queue;
} semaphore_t;

```

调用down函数进入临界区

```c
static __noinline uint32_t __down(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag);
    if (sem->value > 0) {
        sem->value --;
        local_intr_restore(intr_flag);
        return 0;
    }
    wait_t __wait, *wait = &__wait;
    wait_current_set(&(sem->wait_queue), wait, wait_state);
    local_intr_restore(intr_flag);

    schedule();

    local_intr_save(intr_flag);
    wait_current_del(&(sem->wait_queue), wait);
    local_intr_restore(intr_flag);

    if (wait->wakeup_flags != wait_state) {
        return wait->wakeup_flags;
    }
    return 0;
}
```

在内部实现会尝试将value减1 如果值已经为0则调用schedule进行调度 当前进程放弃CPU



离开临界区时调用up函数

```c
static __noinline void __up(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        wait_t *wait;
        if ((wait = wait_queue_first(&(sem->wait_queue))) == NULL) {
            sem->value ++;
        }
        else {
            assert(wait->proc->wait_state == wait_state);
            wakeup_wait(&(sem->wait_queue), wait, wait_state, 1);
        }
    }
    local_intr_restore(intr_flag);
}

```

其内部实现尝试将value的值加1 如果有等待线程则采取唤醒线程 保持value的值不变

## 请在实验报告中给出给用户态进程/线程提供信号量机制的设计方案，并比较说明给内核级提供信号量机制的异同。



将内核信号量机制迁移到用户态的最大麻烦在于，用于保证操作原子性的禁用中断机制、以及 CPU 提供的 Test and Set 指令机制都只能在用户态下运行，而使用软件方法的同步互斥又相当复杂，这就使得没法在用户态下直接实现信号量机制；于是，为了方便起见，可以将信号量机制的实现放在 OS 中来提供，然后使用系统调用的方法统一提供出若干个管理信号量的系统调用，分别如下所示：

  - 申请创建一个信号量的系统调用，可以指定初始值，返回一个信号量描述符(类似文件描述符)；
  - 将指定信号量执行 P 操作；
  - 将指定信号量执行 V 操作；
  - 将指定信号量释放掉；

  给内核级线程提供信号量机制和给用户态进程/线程提供信号量机制的异同点在于：

  - 相同点：
    - 提供信号量机制的代码实现逻辑是相同的；
  - 不同点：
    - 由于实现原子操作的中断禁用、Test and Set 指令等均需要在内核态下运行，因此提供给用户态进程的信号量机制是通过系统调用来实现的，而内核级线程只需要直接调用相应的函数就可以了；

# 练习2: 完成内核级条件变量和基于内核级条件变量的哲学家就餐问题（需要编码）



```c
typedef struct condvar{
    semaphore_t sem;        // the sem semaphore  is used to down the waiting proc, and the signaling proc should up the waiting proc
    int count;              // the number of waiters on condvar
    monitor_t * owner;      // the owner(monitor) of this condvar
} condvar_t;

typedef struct monitor{
    semaphore_t mutex;      // the mutex lock for going into the routines in monitor, should be initialized to 1
    semaphore_t next;       // the next semaphore is used to down the signaling proc itself, and the other OR wakeuped waiting proc should wake up the sleeped signaling proc.
    int next_count;         // the number of of sleeped signaling proc
    condvar_t *cv;          // the condvars in monitor
} monitor_t;
```



修改phi_take_forks_condvar

```c
void phi_take_forks_condvar(int i) {
     down(&(mtp->mutex));
//--------into routine in monitor--------------
     // LAB7 EXERCISE1: YOUR CODE
     // I am hungry
     // try to get fork
      // I am hungry
      state_condvar[i]=HUNGRY; 
      // try to get fork
      phi_test_condvar(i); 
      if (state_condvar[i] != EATING) {
          cprintf("phi_take_forks_condvar: %d didn't get fork and will wait\n",i);
          cond_wait(&mtp->cv[i]);
      }
//--------leave routine in monitor--------------
      if(mtp->next_count>0)
         up(&(mtp->next));
      else
         up(&(mtp->mutex));
}

```

说明：

首先进入临界区mtp->mutex

- 设置当前哲学家的状态为HUNGRY
- 通过phi_test_condvar 尝试获取两个叉子
- 检查当前哲学家的状态 如果不为正在进食 说明未能获取到两个叉子 所以调用cond_wait进入等待中

- 进餐结束后判断队列中是否有等待的哲学家 如果有则唤醒下一个 否则离开临界区mtp->mutex

修改phi_put_forks_condvar

```c
void phi_put_forks_condvar(int i) {
     down(&(mtp->mutex));

//--------into routine in monitor--------------
     // LAB7 EXERCISE1: YOUR CODE
     // I ate over
     // test left and right neighbors
      // I ate over 
      state_condvar[i]=THINKING;
      // test left and right neighbors
      phi_test_condvar(LEFT);
      phi_test_condvar(RIGHT);
//--------leave routine in monitor--------------
     if(mtp->next_count>0)
        up(&(mtp->next));
     else
        up(&(mtp->mutex));
}

```

说明：

首先进入临界区mtp->mutex

- 设置当前哲学家的状态为THINKING
- 通过phi_test_condvar放下左侧和右侧的叉子
- 判断队列中是否有等待的哲学家 如果有则唤醒下一个 否则离开临界区mtp->mutex

## 请在实验报告中给出内核级条件变量的设计描述，并说其大致执行流程。

- 模仿信号量的实现，可以通过开关中断来完成cond_wait和cond_signal的原子性。下面给出伪代码：

  首先定义条件变量的结构体。其中需要一个计数器`count`来记录等待的进程数和一个等待队列`wait_queue`

  ```c
  typedef struct {
      int count;
      wait_queue_t wait_queue;
  } cond_t;
  ```

  接下来完成条件变量的wait操作。wait操作之前首先要关中断以保证其原子性。随后判断count是否为0，若为0则表明没有进程在占用该资源，直接使用即可；否则将自身挂起等待别的进程唤醒。

  ```c
  static __noinline uint32_t __wait(cond_t *cond, uint32_t wait_state) {
      bool intr_flag;
      local_intr_save(intr_flag);
      if (cond->count == 0) {
          cond->count ++;
          local_intr_restore(intr_flag);
          return 0;
      }
      wait_t __wait, *wait = &__wait;
      cond->count++;
      wait_current_set(&(cond->wait_queue), wait, wait_state);
      local_intr_restore(intr_flag);
  
      schedule();
  
      local_intr_save(intr_flag);
      wait_current_del(&(wait->wait_queue), wait);
      cond->count--;
      local_intr_restore(intr_flag);
  
      if (wait->wakeup_flags != wait_state) {
          return wait->wakeup_flags;
      }
      return 0;
  }
  void
  cond_wait(cond_t *cond) {
      uint32_t flags = __wait(cond, WT_KCOND);
      assert(flags == 0);
  }
  ```

  条件变量的signal操作同样需要先关中断，然后唤醒等待列表上的第一个进程。

  ```c
  static __noinline void __signal(cond_t *cond, uint32_t wait_state) {
      bool intr_flag;
      local_intr_save(intr_flag);
      {
          wait_t *wait;
          if ((wait = wait_queue_first(&(cond->wait_queue))) != NULL) {
              assert(wait->proc->wait_state == wait_state);
              wakeup_wait(&(cond->wait_queue), wait, wait_state, 1);
          }
      }
      local_intr_restore(intr_flag);
  }
  
  void
  cond_signal(semaphore_t *cond) {
      __signal(cond, WT_KCOND);
  }
  ```

以下是一个简单的流程：线程1执行wait被挂起，释放管程的mutex，之后线程2获取mutex并进入管程，然后执行了signal唤醒线程1，同时挂起自身。在这个过程中，管程中自始自终都只存在一个活跃线程（原先的线程1执行，线程2未进入，到线程1挂起，线程2进入，再到线程1被唤醒，线程2挂起）。而此时mutex在线程1被唤醒前就已被线程2所获取，**新线程无法进入管程**，因此被唤醒的线程1不需要再次获取mutex。由于管程锁已被获取（**不管是哪个线程获取**）、管程中只有一个活跃线程，因此我们可以**近似将管程锁视为是当前线程获取的**。

## 请在实验报告中给出给用户态进程/线程提供条件变量机制的设计方案，并比较说明给内核级提供条件变量机制的异同。

- 相同点：基本的实现逻辑相同； 
- 不同点：最终在用户态下实现管程和条件变量机制，需要使用到操作系统使用系统调用提供一定的支持; 而在内核态下实现条件变量是不需要的；

```shell
-> % make grade
badsegment:              (s)
  -check result:                             OK
  -check output:                             OK
divzero:                 (s)
  -check result:                             OK
  -check output:                             OK
softint:                 (s)
  -check result:                             OK
  -check output:                             OK
faultread:               (s)
  -check result:                             OK
  -check output:                             OK
faultreadkernel:         (s)
  -check result:                             OK
  -check output:                             OK
hello:                   (s)
  -check result:                             OK
  -check output:                             OK
testbss:                 (s)
  -check result:                             OK
  -check output:                             OK
pgdir:                   (s)
  -check result:                             OK
  -check output:                             OK
yield:                   (s)
  -check result:                             OK
  -check output:                             OK
badarg:                  (s)
  -check result:                             OK
  -check output:                             OK
exit:                    (s)
  -check result:                             OK
  -check output:                             OK
spin:                    (s)
  -check result:                             OK
  -check output:                             OK
waitkill:                (s)
  -check result:                             OK
  -check output:                             OK
forktest:                (s)
  -check result:                             OK
  -check output:                             OK
forktree:                (s)
  -check result:                             OK
  -check output:                             OK
priority:                (s)
  -check result:                             OK
  -check output:                             OK
sleep:                   (s)
  -check result:                             OK
  -check output:                             OK
sleepkill:               (s)
  -check result:                             OK
  -check output:                             OK
matrix:                  (s)
  -check result:                             OK
  -check output:                             OK
Total Score: 190/190
```

# 扩展练习 Challenge ：实现 Linux 的 RCU

没做